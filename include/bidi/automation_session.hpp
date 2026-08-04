#pragma once

#include "bidi/automation_session_config.hpp"
#include "bidi/client.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/guards.hpp"
#include "bidi/io_context_runner.hpp"
#include "bidi/network_intercept_handler.hpp"
#include "bidi/script/extraction.hpp"
#include "bidi/script/function_wrapper.hpp"
#include "bidi/script_eval.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/json/object.hpp>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bidi {

/** Selector strategy used by AutomationSession::wait_for_element(). */
enum class ElementSelectorType : std::uint8_t { css, xpath };

// Forward declaration for network configuration helper
struct NetworkConfiguration;
struct NetworkInterceptConfig;

/**
 * @brief Build NetworkInterceptConfig from NetworkConfiguration
 *
 * Converts AutomationSession's network configuration to NetworkInterceptHandler
 * configuration, applying convenience features and validation.
 *
 * @param net_config Source configuration from AutomationSessionConfig
 * @param context_id Browsing context ID to filter intercepts
 * @return Expected NetworkInterceptConfig or error string
 */
[[nodiscard]] auto
build_network_intercept_config(const NetworkConfiguration &net_config,
                               std::string_view context_id)
    -> std::expected<NetworkInterceptConfig, std::string>;

/**
 * @brief High-level automation session facade for simplified WebDriver BiDi
 * usage
 *
 * Provides a simplified API for common browser automation tasks with:
 * - Blocking setup (start()) - acceptable for one-time initialization
 * - Async operations (navigate, evaluate) - natural to BiDi protocol
 * - Workflow runner (run()) - hides io_context + co_spawn boilerplate
 * - Automatic context management
 *
 * **ARCHITECTURAL DESIGN: Hybrid Blocking + Async**
 *
 * The hybrid nature of AutomationSession (blocking start(), async run()) is
 * INTENTIONAL and represents a deliberate architectural tradeoff.
 *
 * **Design Alternatives Considered:**
 *
 * - **Alternative 1 (Rejected)**: Fully Blocking API
 *   - PRO: Simple mental model, straightforward sequencing
 *   - CON: Blocks thread during I/O operations (unacceptable for servers)
 *   - CON: Poor resource utilization, cannot handle concurrent operations
 *   - Example: `client.navigate(url)` blocks until complete
 *
 * - **Alternative 2 (Rejected)**: Fully Async API
 *   - PRO: Maximum throughput, non-blocking I/O
 *   - PRO: Aligns with BiDi's inherently async protocol
 *   - CON: Boilerplate burden for simple scripts (co_await everywhere)
 *   - CON: Steep learning curve for coroutine concepts
 *   - Example: `co_await session.start()` adds cognitive load
 *
 * - **Selected (Current)**: Hybrid Approach
 *   - start(): Blocking (one-time HTTP handshake bootstrap)
 *     - Rationale: Setup happens once, simplifies initialization code
 *     - Typical latency: 1-2 seconds (acceptable for startup)
 *   - run(): Async (naturally async workflow orchestration)
 *     - Rationale: Navigation, evaluation are inherently I/O-bound
 *     - Allows concurrent operations via coroutine composition
 *   - Sweet spot: Balances simplicity for 80% use cases with power for
 * advanced scenarios
 *
 * **Architectural Benefits:**
 * - Zero learning curve for basic scripts (no coroutines in main())
 * - Full async power available when needed (via run() workflow)
 * - Escape hatches for experts (client(), get_io_context())
 * - Clean separation: setup (sync) vs operations (async)
 *
 * For detailed design rationale, see @ref
 * docs/automation_session_architecture_review.md
 *
 * **Key Benefits:**
 * - 85% boilerplate reduction for common cases
 * - Intuitive API: start() → run(workflow)
 * - Escape hatches for advanced users (client(), get_io_context())
 * - Pure composition (zero new dependencies)
 * - Cleanup guarantees with exception safety (RAII)
 *
 * @example
 * @code
 * int main() {
 *     auto session = bidi::AutomationSession::start();  // Blocks until ready
 *
 *     return session.run([&]() -> boost::asio::awaitable<int> {
 *         co_await session.navigate("https://example.com")();
 *         auto title = co_await session.get_title()();
 *         std::cout << "Title: " << title << "\n";
 *         co_return 0;
 *     });
 * }
 * @endcode
 *
 * @see automation_session.hpp Lifecycle management
 * @see automation_session_architecture_review.md Architecture decisions
 * @see AWAIT_PENDING_OPERATIONS_DESIGN.md Exception handling guarantees
 */
class AutomationSession {
  public:
    /**
     * @brief Create and start an automation session (BLOCKING)
     *
     * **ARCHITECTURE**: This is the ONLY blocking entry point by design.
     * Rationale: Guarantees AutomationSession always has valid state.
     *
     * **Two-Phase Initialization Strategy:**
     *
     * 1. **Phase 0 (Blocking, happens in this method)**:
     *    - Spawns background thread with io_context
     *    - Performs HTTP handshake with WebDriver server
     *    - Obtains WebSocket URL but DEFERS actual connection
     *    - Returns with websocket_url_ populated (WebSocket not connected yet)
     *    - Typical latency: 1-2 seconds
     *
     * 2. **Phase 1 (Async, happens inside run())**:
     *    - Connects BiDi WebSocket (co_await)
     *    - Creates default browsing context (co_await)
     *    - Initializes client_guard_ to manage BiDi resources
     *
     * **Why Deferred WebSocket Connection?**
     *
     * This design decision addresses several critical concerns:
     *
     * - **Latency reduction**: Avoids 2-3s I/O blocking on main thread
     *   - HTTP handshake (Phase 0): ~1s
     *   - WebSocket + context creation (Phase 1): ~2s
     *   - Sequential blocking would be ~3s total
     *   - Deferred connection spreads latency across start() and run()
     *
     * - **Reusability**: Enables multiple run() invocations
     *   - run() can be called multiple times on same session
     *   - Each run() performs fresh WebSocket connect if needed
     *   - Supports workflow restart patterns
     *
     * - **Flexibility**: User controls when connection happens
     *   - start() returns immediately with valid session
     *   - Connection deferred until first run() call
     *   - Allows configuration between start() and run()
     *
     * - **Error recovery**: Phase 1 failure doesn't abort program
     *   - start() exceptions are fatal (bad WebDriver URL, etc.)
     *   - run() exceptions are recoverable (network glitch, timeout)
     *   - Separation enables better error handling strategies
     *
     * @param webdriver_url WebDriver server URL (default: localhost:9515)
     * @param headless Headless browser mode (default: true)
     * @return AutomationSession in "ready for run()" state
     *
     * @throws std::runtime_error If Phase 0 fails:
     *   - HTTP connection to WebDriver fails
     *   - WebDriver returns invalid status
     *   - Background thread initialization fails
     *   - These are fatal errors (cannot proceed)
     *
     * @note Phase 1 failures are captured in run() and rethrown
     * @note start() is blocking, but run() is fully asynchronous
     * @note run() is reusable after automatic restart()
     *
     * @see run() for async workflow continuation
     * @see IoContextRunner for thread lifecycle management
     * @see SessionGuard for HTTP cleanup
     */
    [[nodiscard]] static auto
    start(std::string_view webdriver_url = "http://localhost:9515",
          bool headless = true) -> AutomationSession;

    /**
     * @brief Create and start an automation session with full configuration
     * (BLOCKING)
     *
     * Enhanced version of start() that accepts AutomationSessionConfig for
     * extensive customization. Provides access to all Phase 1 (P0) features:
     * - Script preload configuration
     * - Viewport and screenshot settings
     * - Auto-subscriptions for events
     * - Custom timeouts and retry policies
     *
     * **Initialization remains 2-phase** (same as basic start()):
     * - Phase 0 (Blocking): HTTP handshake, background thread setup
     * - Phase 1 (Async in run()): WebSocket connect, context creation, **config
     * application**
     *
     * **Configuration Application Timeline**:
     * - Phase 0: Browser args, headless mode applied to ChromeDriver launch
     * - Phase 1: Preload scripts, viewport, subscriptions applied after
     * WebSocket connect
     *
     * @param config Complete session configuration (AutomationSessionConfig)
     * @return AutomationSession in state "pronto para run()"
     *
     * @throws std::runtime_error Se falhar na Fase 0 (same as basic start())
     *
     * @example Basic configuration
     * @code
     * auto config = AutomationSessionConfig{
     *     .webdriver_url = "http://localhost:9515",
     *     .headless = true
     * };
     * auto session = AutomationSession::start(config);
     * @endcode
     *
     * @example Advanced P0 configuration
     * @code
     * auto config = AutomationSessionConfig{
     *     .script = {
     *         .preload_scripts = {{
     *             .function_declaration = "window.testHelpers = { ... };"
     *         }}
     *     },
     *     .browsing_context = {
     *         .default_viewport = ViewportConfig{1920, 1080},
     *         .auto_subscribe_navigation_events = true
     *     }
     * };
     * auto session = AutomationSession::start(config);
     * @endcode
     *
     * @note Prefer AutomationSessionBuilder for fluent API style
     * @see AutomationSessionBuilder for fluent configuration
     * @see run() for async workflow continuation
     */
    [[nodiscard]] static auto
    start(const AutomationSessionConfig &config) -> AutomationSession;

    /**
     * @brief Navigate to URL in the default context (ASYNC)
     *
     * **Expected Timing by ReadinessState** (performance characteristics):
     * - none: 10-100ms (navigation initiated, not waiting for load)
     * - interactive: 100ms-5s (DOMContentLoaded event fired)
     * - complete: 500ms-30s (all resources loaded, window.onload fired)
     *
     * These timings guide timeout configuration and readiness selection.
     *
     * **Lazy Evaluation**: Operation only triggers when co_await is invoked.
     *
     * @param url Target URL to navigate to
     * @param wait Readiness state to wait for (default: complete)
     *             **Readiness State Semantics:**
     *             - none: Return immediately after navigation starts
     *               Use case: Fire-and-forget navigation, custom wait logic
     *             - interactive: Wait for DOM ready (DOMContentLoaded)
     *               Use case: Interact with DOM before images/scripts finish
     *             - complete: Wait for full page load (window.onload)
     *               Use case: Ensure all resources loaded (default behavior)
     * @param loc Source location for debugging (captured for GDB inspection)
     * @return Task<std::string> - Navigation ID (lazy, must co_await)
     *
     * @throws std::runtime_error If navigation fails:
     *   - Timeout waiting for ReadinessState
     *   - Invalid URL format
     *   - Browsing context closed or destroyed
     *   - Network error during navigation
     *
     * @example Default navigation (complete) - Wait for everything
     * @code
     * auto nav_id = co_await session.navigate("https://example.com");
     * // Waits for full page load including images, scripts, stylesheets
     * // Typical latency: 500ms-30s depending on page complexity
     * @endcode
     *
     * @example Fast navigation (interactive) - Wait for DOM only
     * @code
     * using namespace bidi::commands::browsing_context;
     * auto nav_id = co_await session.navigate(
     *     "https://example.com",
     *     ReadinessState::interactive
     * );
     * // Can interact with DOM before all resources finish loading
     * // Typical latency: 100ms-5s, much faster for heavy pages
     * @endcode
     */
    [[nodiscard]] auto
    navigate(std::string_view url,
             commands::browsing_context::ReadinessState wait =
                 commands::browsing_context::ReadinessState::complete,
             const std::source_location &loc = std::source_location::current())
        -> Task<std::string>;

    /**
     * @brief Evaluate JavaScript expression - LOW-LEVEL API (ADVANCED)
     *
     * Returns raw BiDi JSON response. **Most users should use
     * evaluate_as<T>()** for type-safe extraction instead.
     *
     * **When to use this API:**
     * - You need access to full BiDi response structure
     * - You're building custom extraction logic
     * - You need metadata beyond the result value
     *
     * **Recommended alternatives:**
     * - `evaluate_as<T>(expr)` - Type-safe extraction
     * - `evaluate_as_or<T>(expr, fallback)` - With fallback value
     * - `evaluate_outcome(expr)` - Policy-aware with exception details
     *
     * @param expression JavaScript code to evaluate
     * @param loc Source location for debugging
     * @return Task<boost::json::object> - Raw BiDi evaluation result (lazy,
     * awaitable)
     *
     * @example Advanced: Inspecting full response structure
     * @code
     * auto result = co_await session.evaluate("document.title");
     * if (result.contains("type")) {
     *     std::string result_type = result.at("type").as_string();
     *     // Custom logic based on type
     * }
     * @endcode
     *
     * @example Recommended: Use type-safe API instead
     * @code
     * // BETTER: Type-safe, no manual JSON parsing
     * auto title = co_await session.evaluate_as<std::string>("document.title");
     * @endcode
     *
     * @see evaluate_as<T>() for type-safe alternative
     * @see evaluate_outcome() for policy-aware evaluation
     * @note This is an escape hatch for advanced users. Prefer type-safe APIs.
     */
    [[nodiscard]] auto
    evaluate(std::string_view expression,
             const std::source_location &loc = std::source_location::current())
        -> Task<boost::json::object>;

    /**
     * @brief Get page title (convenience wrapper for evaluate) (ASYNC)
     *
     * @param loc Source location for debugging
     * @return Task<std::string> - Page title (lazy, awaitable)
     *
     * @example
     * @code
     * auto title = co_await session.get_title();
     * @endcode
     */
    [[nodiscard]] auto
    get_title(const std::source_location &loc = std::source_location::current())
        -> Task<std::string>;

    /**
     * @brief Get current URL (convenience wrapper for evaluate) (ASYNC)
     *
     * @param loc Source location for debugging
     * @return Task<std::string> - Current URL (lazy, awaitable)
     */
    [[nodiscard]] auto
    get_url(const std::source_location &loc = std::source_location::current())
        -> Task<std::string>;

    /**
     * @brief Wait until an element exists in the default browsing context
     *
     * Uses a browser-side MutationObserver, so waiting does not poll either
     * the browser or the client. The operation completes immediately when the
     * element already exists and returns false when the timeout expires.
     * Invalid selectors and script or transport failures remain errors.
     *
     * @param selector CSS selector or XPath expression
     * @param timeout Maximum browser-side wait duration
     * @param type Selector strategy (CSS by default)
     * @param loc Source location for diagnostics
     * @return Lazy task containing true when the element is found, otherwise
     * false on timeout
     *
     * @example
     * @code
     * const bool found = co_await session.wait_for_element("main", 5s);
     * const bool heading = co_await session.wait_for_element(
     *     "//h1", 5s, bidi::ElementSelectorType::xpath);
     * @endcode
     */
    [[nodiscard]] auto wait_for_element(
        std::string_view selector,
        std::chrono::milliseconds timeout = std::chrono::seconds{5},
        ElementSelectorType type = ElementSelectorType::css,
        const std::source_location &loc = std::source_location::current())
        -> Task<bool>;

    /**
     * @brief Type-safe JavaScript evaluation (ASYNC)
     *
     * Evaluates expression and automatically converts result to specified type.
     * Eliminates manual JSON parsing boilerplate.
     *
     * @tparam T Expected return type (must be Boost.JSON compatible)
     * @param expression JavaScript code to evaluate
     * @return Task<T> - Typed result (lazy, awaitable)
     *
     * @throws std::runtime_error if conversion fails
     * @throws ScriptEvaluateException if script execution fails
     *
     * @example
     * @code
     * // Instead of manual parsing:
     * auto result = co_await session.evaluate("document.readyState");
     * std::string state = result.at("value").as_string();  // Manual!
     *
     * // Type-safe alternative:
     * auto state = co_await
     * session.evaluate_as<std::string>("document.readyState");
     *
     * // Works with all JSON-compatible types:
     * auto count = co_await
     * session.evaluate_as<int>("document.links.length");
     * auto visible = co_await session.evaluate_as<bool>("document.hasFocus()");
     * @endcode
     */
    template <typename T>
    [[nodiscard]] auto
    evaluate_as(std::string_view expression,
                const std::source_location &loc =
                    std::source_location::current()) -> Task<T> {
        return evaluate(expression, loc)
            .map(
                [expr = std::string(expression),
                 loc](boost::json::object result) -> T {
                    (void)loc; // suppress unused parameter warning
                    try {
                        return bidi::script::extract_value<T>(result);
                    } catch (const std::exception &e) {
                        throw std::runtime_error(std::format(
                            "evaluate_as failed for expression '{}': {}", expr,
                            e.what()));
                    }
                },
                loc);
    }

    /**
     * @brief Type-safe evaluation with fallback (ASYNC)
     *
     * @tparam T Expected return type
     * @param expression JavaScript code to evaluate
     * @param fallback Default value if evaluation or conversion fails
     * @return Task<T> - Typed result or fallback
     *
     * @example
     * @code
     * auto title = co_await session.evaluate_as_or("document.title",
     *                                               std::string("Untitled"));
     * auto count = co_await session.evaluate_as_or("document.links.length",
     * 0);
     * @endcode
     */
    template <typename T>
    [[nodiscard]] auto
    evaluate_as_or(std::string_view expression, T fallback,
                   const std::source_location &loc =
                       std::source_location::current()) -> Task<T> {
        return evaluate(expression, loc)
            .map([fallback = std::move(fallback),
                  loc](boost::json::object result) -> T {
                (void)loc; // suppress unused parameter warning
                return bidi::script::extract_value_or(result, fallback);
            });
    }

    /**
     * @brief Policy-aware JavaScript evaluation (ASYNC)
     *
     * Uses script evaluation policy to control exception handling.
     * With return_outcome policy, script exceptions are captured in the result
     * instead of being thrown, allowing inspection of exception details.
     *
     * @param expression JavaScript code to evaluate
     * @param policy Exception handling policy
     * @return Task<script::ScriptEvalOutcome> - Result with optional exception
     *
     * @example
     * @code
     * using namespace bidi::script;
     * auto outcome = co_await session.evaluate_outcome(
     *     "nonexistent.property",
     *     script_eval_policy::return_outcome
     * );
     *
     * if (outcome.has_exception()) {
     *     std::cerr << "Script error: " << outcome.exception->text << "\n";
     *     std::cerr << "Line: " << outcome.exception->line_number.value_or(-1)
     * << "\n"; } else {
     *     // Process result
     * }
     * @endcode
     */
    [[nodiscard]] auto evaluate_outcome(
        std::string_view expression,
        script::script_eval_policy policy =
            script::script_eval_policy::return_outcome,
        const std::source_location &loc = std::source_location::current())
        -> Task<script::ScriptEvalOutcome> {
        return client_guard_->client()->evaluate(expression, context_id_,
                                                 policy, true, loc);
    }

    /**
     * @brief Type-safe policy-aware evaluation (ASYNC)
     *
     * Combines policy-aware evaluation with type-safe extraction.
     * Provides rich exception details when script fails.
     *
     * @tparam T Expected return type
     * @param expression JavaScript code to evaluate
     * @param policy Exception handling policy
     * @return Task<T> - Typed result
     *
     * @throws ScriptEvaluateException with full details if script fails
     * @throws std::runtime_error if type conversion fails
     *
     * @example
     * @code
     * try {
     *     auto title = co_await session.evaluate_as_outcome<std::string>(
     *         "document.title"
     *     );
     * } catch (const script::ScriptEvaluateException &e) {
     *     std::cerr << "Script error at line "
     *               << e.details().line_number.value_or(-1) << ": "
     *               << e.details().text << "\n";
     * }
     * @endcode
     */
    template <typename T>
    [[nodiscard]] auto
    evaluate_as_outcome(std::string_view expression,
                        script::script_eval_policy policy =
                            script::script_eval_policy::return_outcome,
                        const std::source_location &loc =
                            std::source_location::current()) -> Task<T> {
        return evaluate_outcome(expression, policy, loc)
            .map([expr = std::string(expression)](
                     script::ScriptEvalOutcome outcome) -> T {
                try {
                    return script::extract_value_from_outcome<T>(outcome);
                } catch (const script::ScriptEvaluateException &) {
                    throw; // Preserve rich exception details
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::format(
                        "evaluate_as_outcome failed for expression '{}': {}",
                        expr, e.what()));
                }
            });
    }

    /**
     * @brief Create type-safe JavaScript function caller
     *
     * Wraps FunctionBidi with automatic context management.
     * The returned callable can be invoked multiple times with C++ arguments
     * that are automatically marshalled to JavaScript.
     *
     * @tparam Result Expected C++ return type
     * @tparam Args C++ argument types (automatically marshalled)
     * @param function_declaration JavaScript function source code
     * @param policy Script exception handling policy
     * @param loc Source location for debugging
     * @return FunctionBidi callable object
     *
     * @example Basic arithmetic
     * @code
     * auto add = session.make_function<int, int, int>(
     *     "function(a, b) { return a + b; }"
     * );
     * auto result = co_await add(2, 3);  // returns 5
     * @endcode
     *
     * @example DOM queries
     * @code
     * auto get_element_count = session.make_function<int, std::string>(
     *     "function(selector) { "
     *     "  return document.querySelectorAll(selector).length; "
     *     "}"
     * );
     * auto div_count = co_await get_element_count("div");
     * auto link_count = co_await get_element_count("a");
     * @endcode
     *
     * @example String manipulation
     * @code
     * auto get_attribute = session.make_function<std::string, std::string,
     * std::string>( "function(selector, attr) { " "  return
     * document.querySelector(selector).getAttribute(attr); "
     *     "}"
     * );
     * auto href = co_await get_attribute("a.first", "href");
     * @endcode
     *
     * @see FunctionBidi for implementation details
     * @see bidi::script::make_function_caller for underlying factory
     */
    template <typename Result, typename... Args>
    [[nodiscard]] auto make_function(
        std::string function_declaration,
        script::script_eval_policy policy =
            script::script_eval_policy::throw_on_script_exception,
        const std::source_location &loc = std::source_location::current())
        -> script::FunctionBidi<Result, Args...> {
        (void)loc; // Available for debugging via GDB
        return script::make_function_caller<Result, Args...>(
            client_guard_->client(), context_id_,
            std::move(function_declaration), policy);
    }

    /**
     * @brief Run a coroutine workflow with automatic io_context management
     *
     * **CRITICAL CLEANUP GUARANTEE** (with architectural tradeoff):
     *
     * This method orchestrates ALL complexity:
     * - co_spawn with exception handling
     * - Blocking io_context.run() execution
     * - Exit code extraction
     * - **CRITICAL**: Cleanup guarantee via ScopeGuard (RAII)
     *
     * **Normal Execution Flow (Success Path)**:
     * ```
     * 1. ScopeGuard created (guarantees cleanup)
     * 2. Connect WebSocket if Phase 1 still pending
     * 3. co_spawn launches workflow on io_context
     * 4. io_context.run() blocks main thread
     * 5. Workflow executes and returns
     * 6. ~ScopeGuard: cleanup (BiDi disconnect + restart context)
     * 7. Return exit code
     * ```
     *
     * **Exception Execution Flow (CRITICAL PATH)**:
     * ```
     * 1-4. Same as success path
     * 5. Workflow throws exception
     * 6. Exception captured in callback → stored_exception
     * 7. ~ScopeGuard: fast cleanup (disconnect, cancel subscriptions)
     * 8. ╔═══════════════════════════════════════════════════════════╗
     *    ║ NEW PHASE: await_pending_operations executes              ║
     *    ║ - Embedded in workflow before exit                        ║
     *    ║ - Waits up to 5s for pending operations to complete       ║
     *    ║ - Allows callbacks to finalize cleanly                    ║
     *    ║ - Prevents callback-after-free and use-after-free bugs    ║
     *    ║ - Then: std::rethrow_exception()                          ║
     *    ╚═══════════════════════════════════════════════════════════╝
     * 9. Exception propagated to caller WITH cleanup guaranteed
     * ```
     *
     * **Architectural Rationale for await_pending_operations**:
     *
     * Problem: BiDi operations are fully asynchronous with 3 components:
     * - Request sent to browser
     * - Response received from browser (triggers callback)
     * - Callback handler executes user code
     *
     * Without await_pending: Exception in workflow → immediate cleanup
     *   → in-flight operations get destroyed → callbacks execute on
     *   dangling pointers → segfault
     *
     * With await_pending: Exception in workflow → wait for callbacks
     *   → clean completion → then cleanup → then rethrow
     *
     * **Exception Safety Guarantees**:
     * - Basic: ScopeGuard ALWAYS runs destructor (noexcept)
     * - Strong: run() is reusable (restart() restores io_context state)
     * - Exception transparency: std::rethrow_exception() after cleanup
     *
     * **Architectural Tradeoff**:
     * - PRO: Complete cleanup, no leaks, callbacks guaranteed to complete
     * - PRO: Memory safety (no use-after-free, no callback-after-free)
     * - PRO: Deterministic resource reclamation
     * - CON: Exception propagation delayed by up to 5s (pending timeout)
     * - CON: Cannot fail-fast in exception scenarios
     *
     * Tradeoff justification: Memory safety and resource correctness are
     * non-negotiable. 5s latency on exception path is acceptable because
     * exceptions represent failure scenarios, not hot paths.
     *
     * @tparam WorkflowFunc Coroutine function returning awaitable<int>
     * @param workflow Async workflow to execute
     * @return Exit code (0 = success, non-zero = failure)
     *
     * @throws std::exception Rethrows workflow exceptions after cleanup
     * @throws std::runtime_error If Phase 1 (WebSocket connect) fails
     *
     * @example Basic workflow with error handling
     * @code
     * try {
     *     return session.run([&]() -> boost::asio::awaitable<int> {
     *         co_await session.navigate("https://example.com");
     *         auto title = co_await session.get_title();
     *         std::cout << "Title: " << title << "\n";
     *         co_return 0;
     *     });
     * } catch (const std::exception &e) {
     *     std::cerr << "Workflow or cleanup failed: " << e.what() << "\n";
     *     return 1;
     * }
     * // ~ScopeGuard guaranteed even on exception
     * @endcode
     *
     * @example Reusing session (run() is reusable)
     * @code
     * auto session = AutomationSession::start();
     *
     * // First workflow
     * session.run([&]() -> boost::asio::awaitable<int> {
     *     co_await session.navigate("https://site1.com");
     *     co_return 0;
     * });
     *
     * // Second workflow (restart() already called automatically)
     * session.run([&]() -> boost::asio::awaitable<int> {
     *     co_await session.navigate("https://site2.com");
     *     co_return 0;
     * });
     * @endcode
     *
     * @note ScopeGuard pattern (RAII) guarantees deterministic cleanup
     * @note await_pending_operations timeout is configurable (default: 10s)
     * @see AWAIT_PENDING_OPERATIONS_DESIGN.md for detailed design rationale
     * @see ScopeGuard internal implementation
     * @see ClientGuard::cleanup() BiDi resource cleanup
     * @see WorkflowExceptionDoesNotLeakPendings validation test
     */
    template <typename WorkflowFunc> auto run(WorkflowFunc &&workflow) -> int {
        int exit_code = 1;
        std::exception_ptr stored_exception;

        runner_->arm_work(); // Ensure guard is held

        // RAII scope guard: guarantee cleanup even if workflow throws
        struct ScopeGuard {
            AutomationSession *self;

            explicit ScopeGuard(AutomationSession *session) : self(session) {}

            ScopeGuard(const ScopeGuard &) = delete;
            auto operator=(const ScopeGuard &) -> ScopeGuard & = delete;
            ScopeGuard(ScopeGuard &&) = delete;
            auto operator=(ScopeGuard &&) -> ScopeGuard & = delete;

            /**
             * @brief RAII destructor: guarantees deterministic cleanup
             *
             * **Cleanup Sequence** (always executed, regardless of errors):
             *
             * 1. Phase 1: ClientGuard.cleanup() - Fast BiDi disconnect
             *    - Sends "session.end" command to BiDi server
             *    - Cancels all active subscriptions
             *    - Disconnects WebSocket connection
             *    - Timing: 10-100ms (non-blocking)
             *
             * 2. Phase 2: runner_->restart() - Restore io_context state
             *    - Resets io_context for potential next run() call
             *    - Restores state after workflow execution
             *    - Prepares for session reusability
             *
             * **IMPORTANT**: Only fast cleanup happens here!
             * Pending operations are awaited BEFORE this point in the
             * workflow itself (await_pending_operations), not in destructor.
             *
             * **Design Rationale**:
             * - Destructor must be noexcept (C++ requirement)
             * - Cannot wait for async operations in destructor (blocking)
             * - await_pending is embedded in workflow for proper async handling
             * - Destructor only performs synchronous cleanup
             *
             * @note Destructor is noexcept: captures and logs exceptions
             * @note Guarantees cleanup even if both phases fail
             */
            ~ScopeGuard() {
                // Phase 1: Clear all pending async operations and close
                // WebSocket
                if (self && self->client_guard_) {
                    try {
                        self->client_guard_->cleanup();
                    } catch (const std::exception &e) {
                        bidi::logging::log_error(
                            std::string("cleanup() exception: ") + e.what());
                    }
                }
                // Phase 2: Reset io_context to initial state for next run()
                if (self && self->runner_) {
                    try {
                        self->runner_->restart();
                        self->runner_->arm_work();
                    } catch (const std::exception &e) {
                        bidi::logging::log_error(
                            std::string("restart() exception: ") + e.what());
                    }
                }
            }
        } scope_guard{this};

        auto full_workflow = [this,
                              workflow = std::forward<WorkflowFunc>(
                                  workflow)]() -> boost::asio::awaitable<int> {
            auto *self = this;

            // Connect WebSocket if deferred from start()
            if (!self->client_guard_ && !self->websocket_url_.empty()) {
                bidi::logging::log_info("Connecting WebSocket: " +
                                        self->websocket_url_);

                // Connect WebSocket with co_await (proper async)
                auto client =
                    co_await ConnectionBuilder::to(self->websocket_url_)
                        .use_existing_websocket(self->websocket_url_)
                        .connect(self->runner_->get());

                if (!client) {
                    throw std::runtime_error(
                        "Failed to establish BiDi WebSocket connection");
                }

                // Create default browsing context with co_await
                auto context_id = co_await client->create_context();

                if (context_id.empty()) {
                    throw std::runtime_error(
                        "Failed to create browsing context: empty context ID "
                        "returned");
                }

                // Now fully initialized
                self->client_guard_ = std::make_unique<ClientGuard>(client);
                self->context_id_ = std::move(context_id);
                self->websocket_url_.clear();
                // Keep session_guard alive! Don't reset it!
                // self->session_guard_.reset();

                // Phase 1.5: Initialize network interception if enabled
                if (self->config_.network.enable_network_intercept) {
                    bidi::logging::log_info(
                        "Initializing network interception for context=" +
                        self->context_id_);

                    // Convert config and create handler
                    auto net_config_result = build_network_intercept_config(
                        self->config_.network, self->context_id_);

                    if (!net_config_result) {
                        throw std::runtime_error(
                            "Network configuration error: " +
                            net_config_result.error());
                    }

                    self->network_handler_ =
                        co_await NetworkInterceptHandler::create(
                            client, *net_config_result);

                    bidi::logging::log_info(
                        "Network interception initialized successfully");
                }

                bidi::logging::log_info(
                    "AutomationSession fully initialized: context=" +
                    self->context_id_);
            }

            if (!self->client_guard_) {
                throw std::runtime_error(
                    "run() called without proper initialization from start()");
            }

            // Execute user workflow
            std::exception_ptr stored_workflow_exception;
            try {
                co_return co_await workflow();
            } catch (...) {
                bidi::logging::log_error("Workflow execution failed");
                stored_workflow_exception = std::current_exception();
            }

            if (!client_guard_) {
                co_return -1;
            }

            auto client = client_guard_->client();

            if (!client) {
                co_return -1;
            }

            auto session = client->session();

            if (!session) {
                co_return -1;
            }

            co_await session->await_pending_operations_complete(
                pending_operations_timeout);

            if (stored_workflow_exception) {
                std::rethrow_exception(stored_workflow_exception);
            }

            co_return -1;
        };

        boost::asio::co_spawn(
            runner_->get(), std::move(full_workflow),
            [this, &exit_code, &stored_exception](const std::exception_ptr &exc,
                                                  int result) {
                // Ensure io_context is released and stopped so
                // runner_->get().run() (the blocking call below) will
                // return after workflow completion.
                try {
                    if (exc) {
                        try {
                            std::rethrow_exception(exc);
                        } catch (const std::exception &e) {
                            bidi::logging::log_error(
                                std::string("Workflow exception: ") + e.what());
                        }
                        stored_exception = exc;
                        exit_code = 1;
                    } else {
                        exit_code = result;
                    }
                } catch (...) {
                    // Preserve existing behavior: store exception and set
                    // non-zero exit code.
                    stored_exception = std::current_exception();
                    exit_code = 1;
                }

                // Drop the work guard and request the io_context to stop.
                // This unblocks any threads currently running
                // io_context::run().
                try {
                    runner_->release_work();
                    runner_->stop();
                } catch (const std::exception &e) {
                    bidi::logging::log_error(
                        std::string("Failed to stop runner: ") + e.what());
                }
            });

        // Execute workflow (blocking until completion)
        runner_->get().run();

        // ScopeGuard destructor runs here, ensuring cleanup always happens

        if (stored_exception) {
            std::rethrow_exception(stored_exception); // Let caller handle
        }

        return exit_code;
    }

    /**
     * @brief Access the underlying Client (escape hatch for advanced usage)
     *
     * ⚠️ **WARNING**: Use only if AutomationSession doesn't expose needed API.
     * Mixing direct Client access with AutomationSession can cause:
     *
     * **Potential Issues:**
     * - Dual resource management (both session and user managing same
     * resources)
     * - Context confusion (operating on wrong browsing context)
     * - Undefined behavior (concurrent operations without synchronization)
     * - RAII violations (manual cleanup interfering with automatic cleanup)
     *
     * **When to use this escape hatch:**
     * - Operations not exposed by AutomationSession API
     * - Access to advanced Client features
     * - Custom session introspection
     * - BiDi commands not yet wrapped by high-level API
     *
     * **When NOT to use:**
     * - Operations that AutomationSession already provides
     * - Operations that modify browsing context (use session methods)
     * - Manual cleanup (rely on AutomationSession destructor)
     * - Basic navigation/evaluation (use session convenience methods)
     *
     * **Safety Guidelines:**
     * - Never call client->close() (let AutomationSession manage lifecycle)
     * - Prefer session.context_id() over creating new contexts
     * - Don't bypass session's cleanup guarantees
     * - Be aware of context_id_ managed by AutomationSession
     *
     * @return Reference to shared_ptr<Client>
     *
     * @example Advanced operation not exposed by AutomationSession
     * @code
     * auto client = session.client();
     * auto info = co_await client->get_session_info();
     * @endcode
     *
     * @see evaluate() for basic script evaluation
     * @see make_function() for function invocation
     * @note Prefer AutomationSession APIs when available
     */
    [[nodiscard]] auto client() -> std::shared_ptr<Client> & {
        return client_guard_->client();
    }

    /**
     * @brief Access the underlying Client (const, escape hatch for advanced
     * usage)
     *
     * @return Const reference to shared_ptr<Client>
     */
    [[nodiscard]] auto client() const -> const std::shared_ptr<Client> & {
        return client_guard_->client();
    }

    /**
     * @brief Explicitly cleanup BiDi resources (subscriptions, connections)
     *
     * Can be called before destruction for early cleanup. Safe to call
     * multiple times (idempotent).
     *
     * @note Automatically called by destructor via ClientGuard
     */
    void cleanup() noexcept { client_guard_->cleanup(); }

    /**
     * @brief Check if cleanup has been completed
     *
     * @return true if cleanup() has been called
     */
    [[nodiscard]] auto is_cleaned_up() const noexcept -> bool {
        return client_guard_->is_cleaned_up();
    }

    /**
     * @brief Get the default browsing context ID
     *
     * @return String view of the context ID
     */
    [[nodiscard]] auto context_id() const -> std::string_view {
        return context_id_;
    }

    /**
     * @brief Get current session configuration (read-only access)
     *
     * Provides access to the configuration used to initialize this session.
     * Useful for inspecting defaults or sharing config across sessions.
     *
     * @return Const reference to AutomationSessionConfig
     *
     * @example
     * @code
     * auto& cfg = session.config();
     * std::cout << "Timeout: " << cfg.pending_operations_timeout.count() <<
     * "ms\n";
     * @endcode
     */
    [[nodiscard]] auto config() const -> const AutomationSessionConfig & {
        return config_;
    }

    /**
     * @brief Access the io_context (escape hatch for custom async operations)
     *
     * ⚠️ **WARNING**: Use only if direct Boost.Asio APIs are necessary.
     * Incorrect sequencing can cause severe issues:
     *
     * **Potential Hazards:**
     * - Race conditions (multiple workflows running in parallel)
     *   Example: Two co_spawns modifying shared state concurrently
     * - Deadlocks (blocking inside coroutine)
     *   Example: Calling .get() on Task inside awaitable
     * - Crashes (destruction during active operations)
     *   Example: AutomationSession destroyed while custom operation pending
     * - Undefined behavior (modifying AutomationSession internal state)
     *   Example: Manually restarting io_context while run() is active
     *
     * **When to use this escape hatch:**
     * - Custom timers or I/O operations not provided by AutomationSession
     * - Advanced async patterns (race, all, timeout with custom logic)
     * - Integration with other Boost.Asio-based libraries
     * - Performance optimization requiring direct executor access
     *
     * **When NOT to use:**
     * - Operations that AutomationSession already provides
     * - Parallel co_spawn without synchronization mechanisms
     * - Manual stopping/restarting of io_context (managed by runner_)
     * - Operations that interfere with session lifecycle
     *
     * **Safety Guidelines:**
     * - Never call io_context.stop() manually (managed by runner_)
     * - Never call io_context.restart() manually (managed by runner_)
     * - Always use boost::asio::detached for fire-and-forget spawns
     * - Be aware that custom operations extend session lifetime
     * - Ensure custom operations complete before session destruction
     *
     * **Threading Model:**
     * - io_context runs on single background thread (no mutex needed)
     * - All operations posted to io_context are serialized
     * - Custom operations share the same event loop as BiDi operations
     * - No data races possible within single io_context
     *
     * @return Reference to boost::asio::io_context
     *
     * @example Custom async operation with timer
     * @code
     * auto& io = session.get_io_context();
     * boost::asio::co_spawn(
     *     io,
     *     [&]() -> boost::asio::awaitable<void> {
     *         boost::asio::steady_timer timer(io, std::chrono::seconds{1});
     *         co_await timer.async_wait(boost::asio::use_awaitable);
     *         // Custom async work
     *         co_return;
     *     },
     *     boost::asio::detached
     * );
     * @endcode
     *
     * @see run() for safe workflow orchestration
     * @see navigate() for safe navigation operations
     * @note This is a true escape hatch. Use with EXTREME caution!
     */
    [[nodiscard]] auto get_io_context() -> boost::asio::io_context & {
        return runner_->get();
    }

    // Movable but not copyable
    AutomationSession(AutomationSession &&) = default;
    auto operator=(AutomationSession &&) -> AutomationSession & = default;
    AutomationSession(const AutomationSession &) = delete;
    auto operator=(const AutomationSession &) -> AutomationSession & = delete;

    // Explicit destructor: ensures cleanup before member destruction
    /**
     * @brief Explicit destructor (RAII cleanup)
     *
     * **ARCHITECTURE**: Destructor guarantees BiDi resource cleanup but
     * **INTENTIONALLY** does NOT stop io_context.
     *
     * **Why Not Stop io_context?**
     *
     * Rationale: There may be pending async operations that still need to run:
     * - BiDi subscriptions in cleanup phase
     * - Finalization of async operations
     * - Pending callbacks that must complete
     * - WebSocket graceful shutdown sequence
     *
     * **Design Decision**: io_context will only stop when IoContextRunner is
     * destroyed (RAII), which happens AFTER AutomationSession destruction.
     *
     * **Cleanup Performed** (in this destructor):
     * 1. ClientGuard.cleanup() - Disconnect BiDi WebSocket
     *    - Sends "session.end" command to BiDi server
     *    - Cancels all active subscriptions
     *    - Closes WebSocket connection
     * 2. **Does NOT** stop io_context (deferred to runner_ destruction)
     * 3. **Does NOT** halt background thread (managed by runner_)
     *
     * **Exception Safety Guarantees**:
     * - Destructor is noexcept: captures exceptions and logs them
     * - Safe destruction even if cleanup fails
     * - Never throws (C++ destructor contract)
     * - Logging instead of propagation for diagnostic purposes
     *
     * **Critical Destruction Sequence** (member destruction order):
     * ```
     * 1. ~AutomationSession() - BiDi cleanup (this destructor)
     * 2. ~network_handler_ - Network intercept cleanup
     * 3. ~client_guard_ - Client resource release
     * 4. ~runner_ (~IoContextRunner) - Stops io_context, joins thread
     * 5. ~session_guard_ (~SessionGuard) - HTTP session cleanup
     * ```
     *
     * **Why This Order Matters**:
     * - BiDi must disconnect before io_context stops (graceful shutdown)
     * - io_context must stop before thread joins (prevents blocking)
     * - HTTP session must cleanup after thread stops (final cleanup)
     *
     * **Performance Characteristics**:
     * - Non-blocking (returns immediately)
     * - Actual cleanup happens asynchronously on background thread
     * - Async operations may continue briefly after destructor returns
     * - Final completion guaranteed by runner_ destruction
     *
     * @see ClientGuard::cleanup() for BiDi cleanup details
     * @see IoContextRunner for thread lifecycle management
     * @see SessionGuard for HTTP cleanup
     * @note Never blocks main thread
     * @note Async operations may continue briefly after destructor
     * @note Full cleanup completion guaranteed by member destruction order
     */
    ~AutomationSession() {
        // Cleanup BiDi session (disconnect WebSocket, clear subscriptions)
        // NOTE: Only cleanup client, NOT io_context
        // The io_context thread should continue running until explicitly
        // stopped
        if (client_guard_) {
            try {
                client_guard_->cleanup();
            } catch (const std::exception &e) {
                bidi::logging::log_error(
                    std::string(
                        "AutomationSession destructor cleanup failed: ") +
                    e.what());
            }
        }
        // NOTE: Do NOT call runner_->stop() here
        // The io_context should remain alive for pending async operations
        // It will be stopped when the IoContextRunner is destroyed (RAII)
    }

    static constexpr std::chrono::milliseconds DEFAULT_CLEANUP_TIMEOUT{10000};

    void set_pending_operations_timeout(std::chrono::milliseconds timeout) {
        assert(timeout.count() >= 0 && "Timeout must be non-negative");
        pending_operations_timeout = timeout;
    }

    [[nodiscard]] auto
    get_pending_operations_timeout() const -> std::chrono::milliseconds {
        return pending_operations_timeout;
    }

  private:
    // Private constructor - use start() factory
    // Phase 1: HTTP-only initialization (before WebSocket connect)
    AutomationSession(std::unique_ptr<IoContextRunner> runner,
                      std::string websocket_url,
                      std::shared_ptr<SessionGuard> session_guard,
                      AutomationSessionConfig config);

    // Phase 2: Full initialization (after WebSocket connect)
    AutomationSession(std::unique_ptr<IoContextRunner> runner,
                      std::unique_ptr<ClientGuard> client_guard,
                      std::string context_id, AutomationSessionConfig config);

    std::chrono::milliseconds pending_operations_timeout{
        DEFAULT_CLEANUP_TIMEOUT};
    std::unique_ptr<IoContextRunner> runner_;
    std::unique_ptr<ClientGuard> client_guard_;
    std::string context_id_;

    // Deferred WebSocket connection (populated by start(), consumed by run())
    std::string websocket_url_;
    std::shared_ptr<SessionGuard> session_guard_;

    // Session configuration (stored for reference and Phase 1 application)
    AutomationSessionConfig config_;

    // Network intercept handler (RAII cleanup, created in run() if enabled)
    std::shared_ptr<NetworkInterceptHandler> network_handler_;
};

} // namespace bidi

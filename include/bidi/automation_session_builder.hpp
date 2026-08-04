#pragma once

#include "bidi/automation_session_config.hpp"
#include "bidi/commands/browsing_context.hpp"
#include "bidi/resilience.hpp"
#include "bidi/script_eval.hpp"
#include "bidi/types/browsing_context.hpp"
#include "bidi/types/script.hpp"
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

// Forward declaration to avoid circular dependency
namespace bidi {
class AutomationSession;
}

namespace bidi {

/**
 * @brief Fluent builder for AutomationSession with progressive disclosure
 *
 * Provides zero-cost abstraction for constructing AutomationSession with
 * extensive configuration options. Uses value semantics with both copy (const&)
 * and move (&&) overloads for optimal performance.
 *
 * **Design Philosophy**:
 * - Simple by default: AutomationSession::start() still works
 * - Powerful when needed: Builder exposes all configuration
 * - Zero-cost: Inlined methods, move semantics, no virtual dispatch
 * - Type-safe: Compile-time validation of configuration
 * - Progressive disclosure: Common options first, advanced options later
 *
 * **Performance Characteristics**:
 * - Builder methods are inline and [[nodiscard]]
 * - Rvalue overloads (&&) enable zero-copy chaining
 * - Lvalue overloads (const&) allow reusing builders
 * - No heap allocations until start()
 *
 * **C++ Core Guidelines Compliance**:
 * - C.45: Value semantics for builders
 * - F.15: Const-correct methods
 * - F.16: Dual overloads for pass-by-value parameters
 * - I.11: Reference wrapper for io_context (no raw pointers)
 *
 * @example Basic usage (fluent chaining with moves)
 * @code
 * auto session = AutomationSessionBuilder::create()
 *     .webdriver_url("http://localhost:9515")
 *     .headless()
 *     .with_timeout(std::chrono::seconds{10})
 *     .start();
 * @endcode
 *
 * @example Advanced configuration (P0 features)
 * @code
 * auto session = AutomationSessionBuilder::create()
 *     .with_preload_scripts({{
 *         .function_declaration = "window.testHelpers = { ... };"
 *     }})
 *     .with_default_serialization({
 *         .maxDomDepth = 5,
 *         .maxObjectDepth = 5,
 *         .includeShadowTree = false
 *     })
 *     .with_viewport(1366, 768)
 *     .auto_subscribe_to_navigation()
 *     .with_screenshot_format("image/webp", 0.8)
 *     .start();
 * @endcode
 *
 * @example Reusing builder (lvalue semantics)
 * @code
 * auto base_builder = AutomationSessionBuilder::create()
 *     .headless()
 *     .with_timeout(std::chrono::seconds{30});
 *
 * auto session1 = base_builder.webdriver_url("http://localhost:9515").start();
 * auto session2 = base_builder.webdriver_url("http://localhost:4444").start();
 * @endcode
 */
class AutomationSessionBuilder {
  public:
    /**
     * @brief Factory method to create builder (fluent API entry point)
     *
     * Preferred over constructor for discoverability and IDE auto-completion.
     *
     * @return New AutomationSessionBuilder with default configuration
     */
    [[nodiscard]] static auto create() -> AutomationSessionBuilder {
        return AutomationSessionBuilder{};
    }

    // ========== Core Settings (Backward Compatible) ==========

    /**
     * @brief Set WebDriver server URL
     *
     * @param url WebDriver endpoint (default: "http://localhost:9515")
     * @return Builder for chaining
     */
    [[nodiscard]] auto
    webdriver_url(std::string url) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    webdriver_url(std::string url) && -> AutomationSessionBuilder;

    /**
     * @brief Enable headless mode (shorthand)
     *
     * Equivalent to setting headless flag to true.
     *
     * @param enable Whether to enable headless mode (default: true)
     * @return Builder for chaining
     */
    [[nodiscard]] auto
    headless(bool enable = true) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    headless(bool enable = true) && -> AutomationSessionBuilder;

    /**
     * @brief Set custom browser command-line arguments
     *
     * Replaces any previously set arguments. Use append_browser_args()
     * to add to existing arguments.
     *
     * @param args Vector of command-line arguments
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.with_browser_args({
     *     "--disable-gpu",
     *     "--window-size=1920,1080",
     *     "--user-agent=CustomBot"
     * });
     * @endcode
     */
    [[nodiscard]] auto with_browser_args(const std::vector<std::string> &args)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto with_browser_args(
        std::vector<std::string> &&args) && -> AutomationSessionBuilder;

    /**
     * @brief Append browser arguments to existing list
     *
     * @param args Arguments to append
     * @return Builder for chaining
     */
    [[nodiscard]] auto append_browser_args(const std::vector<std::string> &args)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto append_browser_args(
        std::vector<std::string> &&args) && -> AutomationSessionBuilder;

    /**
     * @brief Common preset: Disable sandbox
     *
     * Equivalent to append_browser_args({"--no-sandbox"}).
     * Required in some Docker/container environments.
     */
    [[nodiscard]] auto no_sandbox() const & -> AutomationSessionBuilder;
    [[nodiscard]] auto no_sandbox() && -> AutomationSessionBuilder;

    /**
     * @brief Common preset: Disable GPU acceleration
     *
     * Equivalent to append_browser_args({"--disable-gpu"}).
     * Useful for headless environments without GPU support.
     */
    [[nodiscard]] auto disable_gpu() const & -> AutomationSessionBuilder;
    [[nodiscard]] auto disable_gpu() && -> AutomationSessionBuilder;

    /**
     * @brief Common preset: Set window size
     *
     * Equivalent to append_browser_args({"--window-size=<width>,<height>"}).
     *
     * @param width Window width in pixels
     * @param height Window height in pixels
     */
    [[nodiscard]] auto
    window_size(int width, int height) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto window_size(int width,
                                   int height) && -> AutomationSessionBuilder;

    /**
     * @brief Common preset: Set custom user agent
     *
     * Equivalent to append_browser_args({"--user-agent=<agent>"}).
     *
     * @param agent User agent string
     */
    [[nodiscard]] auto
    user_agent(std::string_view agent) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    user_agent(std::string_view agent) && -> AutomationSessionBuilder;

    // ========== Operation Defaults ==========

    /**
     * @brief Set default ReadinessState for navigate() calls
     *
     * Individual navigate() calls can still override this.
     *
     * @param state Readiness state (none/interactive/complete)
     * @return Builder for chaining
     */
    [[nodiscard]] auto with_default_navigation_wait(
        commands::browsing_context::ReadinessState state)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    with_default_navigation_wait(commands::browsing_context::ReadinessState
                                     state) && -> AutomationSessionBuilder;

    /**
     * @brief Set default script evaluation policy
     *
     * @param policy Exception handling policy
     * @return Builder for chaining
     */
    [[nodiscard]] auto with_default_script_policy(
        script::script_eval_policy policy) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto with_default_script_policy(
        script::script_eval_policy policy) && -> AutomationSessionBuilder;

    /**
     * @brief Set timeout for pending operations during cleanup
     *
     * @param timeout Duration to wait for pending operations
     * @return Builder for chaining
     */
    [[nodiscard]] auto with_timeout(
        std::chrono::milliseconds timeout) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto with_timeout(
        std::chrono::milliseconds timeout) && -> AutomationSessionBuilder;

    /**
     * @brief Set retry policy for failed operations
     *
     * @param policy Retry policy (max attempts, backoff strategy)
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.with_retry_policy(resilience::RetryPolicy::exponential(3));
     * @endcode
     */
    [[nodiscard]] auto with_retry_policy(const resilience::RetryPolicy &policy)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto with_retry_policy(
        resilience::RetryPolicy &&policy) && -> AutomationSessionBuilder;

    // ========== P0: Script Configuration ==========

    /**
     * @brief Set preload scripts to inject on realm creation
     *
     * Preload scripts run automatically when a new JavaScript execution
     * context is created (e.g., new page, iframe, or worker).
     *
     * @param scripts Vector of preload script configurations
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.with_preload_scripts({{
     *     .function_declaration = "window.testHelpers = { ... };",
     *     .sandbox = "isolated"
     * }});
     * @endcode
     */
    [[nodiscard]] auto
    with_preload_scripts(const std::vector<PreloadScriptConfig> &scripts)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    with_preload_scripts(std::vector<PreloadScriptConfig> &&scripts)
        && -> AutomationSessionBuilder;

    /**
     * @brief Set default serialization options for RemoteValue results
     *
     * Controls how JavaScript objects are serialized when returned from
     * evaluate() calls.
     *
     * @param options Serialization options (max depths, shadow tree)
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.with_default_serialization({
     *     .maxDomDepth = 5,
     *     .maxObjectDepth = 5,
     *     .includeShadowTree = false
     * });
     * @endcode
     */
    [[nodiscard]] auto with_default_serialization(
        const types::script::SerializationOptions &options)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    with_default_serialization(types::script::SerializationOptions &&options)
        && -> AutomationSessionBuilder;

    /**
     * @brief Set default ownership mode for script evaluation results
     *
     * @param ownership ResultOwnership::Root or ResultOwnership::None
     * @return Builder for chaining
     */
    [[nodiscard]] auto
    with_default_ownership(types::script::ResultOwnership ownership)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    with_default_ownership(types::script::ResultOwnership ownership)
        && -> AutomationSessionBuilder;

    // ========== P0: Browsing Context Configuration ==========

    /**
     * @brief Set default viewport dimensions
     *
     * @param width Viewport width in pixels
     * @param height Viewport height in pixels
     * @return Builder for chaining
     */
    [[nodiscard]] auto
    with_viewport(int width, int height) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto with_viewport(int width,
                                     int height) && -> AutomationSessionBuilder;

    /**
     * @brief Auto-subscribe to navigation lifecycle events
     *
     * When enabled, automatically subscribes to:
     * - browsingContext.navigationStarted
     * - browsingContext.domContentLoaded
     * - browsingContext.load
     * - browsingContext.fragmentNavigated
     */
    [[nodiscard]] auto
    auto_subscribe_to_navigation() const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    auto_subscribe_to_navigation() && -> AutomationSessionBuilder;

    /**
     * @brief Auto-subscribe to user prompt events
     *
     * When enabled, automatically subscribes to:
     * - browsingContext.userPromptOpened
     * - browsingContext.userPromptClosed
     */
    [[nodiscard]] auto
    auto_subscribe_to_user_prompts() const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    auto_subscribe_to_user_prompts() && -> AutomationSessionBuilder;

    /**
     * @brief Set default screenshot format and quality
     *
     * @param type Image format ("image/png", "image/jpeg", "image/webp")
     * @param quality Quality factor (0.0 to 1.0, only for lossy formats)
     * @return Builder for chaining
     */
    [[nodiscard]] auto with_screenshot_format(
        std::string_view type,
        double quality = 1.0) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    with_screenshot_format(std::string_view type,
                           double quality = 1.0) && -> AutomationSessionBuilder;

    // ========== P1: Network Configuration ==========

    /**
     * @brief Enable network interception with policy
     *
     * Automatically creates NetworkInterceptHandler during session
     * initialization. Handler is cleaned up via RAII when session is destroyed.
     *
     * @param policy Intercept policy (ContinueAll/FailAll/Custom)
     * @param phases Phases to intercept (default: BeforeRequestSent)
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.enable_network_intercept(
     *     NetworkInterceptPolicy::ContinueAll,
     *     {InterceptPhase::BeforeRequestSent}
     * );
     * @endcode
     */
    [[nodiscard]] auto enable_network_intercept(
        NetworkInterceptPolicy policy = NetworkInterceptPolicy::ContinueAll,
        std::vector<types::network::InterceptPhase> phases =
            {types::network::InterceptPhase::BeforeRequestSent})
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto enable_network_intercept(
        NetworkInterceptPolicy policy = NetworkInterceptPolicy::ContinueAll,
        std::vector<types::network::InterceptPhase> phases =
            {types::network::InterceptPhase::BeforeRequestSent})
        && -> AutomationSessionBuilder;

    /**
     * @brief Set URL patterns to intercept
     *
     * If empty (default), all URLs are intercepted.
     *
     * @param patterns Vector of UrlPattern (string or pattern-based)
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.with_intercept_patterns({
     *     types::network::UrlPatternString{.pattern =
     * "https://api.example.com/v1"}
     * });
     * @endcode
     */
    [[nodiscard]] auto with_intercept_patterns(
        const std::vector<types::network::UrlPattern> &patterns)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    with_intercept_patterns(std::vector<types::network::UrlPattern> &&patterns)
        && -> AutomationSessionBuilder;

    /**
     * @brief Set custom beforeRequestSent callback
     *
     * Callback is invoked for each intercepted request in BeforeRequestSent
     * phase. Return RequestResolution to control request
     * (Continue/Fail/Modify).
     *
     * **Automatically sets intercept_policy to Custom**.
     *
     * @tparam F Callable matching BeforeRequestCallable concept
     * @param handler Callback function
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.with_before_request_handler([](const auto& params) {
     *     if (params.base.request.url.contains("tracking")) {
     *         return RequestResolution{.action = InterceptAction::Fail};
     *     }
     *     return RequestResolution{.action = InterceptAction::Continue};
     * });
     * @endcode
     */
    template <BeforeRequestCallable F>
    [[nodiscard]] auto
    with_before_request_handler(F handler) const & -> AutomationSessionBuilder {
        AutomationSessionBuilder copy = *this;
        copy.config_.network.intercept_policy = NetworkInterceptPolicy::Custom;
        copy.config_.network.before_request_handler = std::move(handler);
        return copy;
    }

    template <BeforeRequestCallable F>
    [[nodiscard]] auto
    with_before_request_handler(F handler) && -> AutomationSessionBuilder {
        config_.network.intercept_policy = NetworkInterceptPolicy::Custom;
        config_.network.before_request_handler = std::move(handler);
        return std::move(*this);
    }

    /**
     * @brief Set custom responseStarted callback
     *
     * Callback is invoked for each intercepted response in ResponseStarted
     * phase. Return ResponseResolution to control response
     * (Continue/Fail/Provide).
     *
     * **Automatically sets intercept_policy to Custom**.
     *
     * @tparam F Callable matching ResponseStartedCallable concept
     * @param handler Callback function
     * @return Builder for chaining
     */
    template <ResponseStartedCallable F>
    [[nodiscard]] auto with_response_started_handler(
        F handler) const & -> AutomationSessionBuilder {
        AutomationSessionBuilder copy = *this;
        copy.config_.network.intercept_policy = NetworkInterceptPolicy::Custom;
        copy.config_.network.response_started_handler = std::move(handler);
        return copy;
    }

    template <ResponseStartedCallable F>
    [[nodiscard]] auto
    with_response_started_handler(F handler) && -> AutomationSessionBuilder {
        config_.network.intercept_policy = NetworkInterceptPolicy::Custom;
        config_.network.response_started_handler = std::move(handler);
        return std::move(*this);
    }

    /**
     * @brief Set custom authRequired callback
     *
     * Callback is invoked when authentication challenge is received.
     * Return AuthResolution to provide credentials or cancel.
     *
     * **Automatically sets intercept_policy to Custom**.
     *
     * @tparam F Callable matching AuthRequiredCallable concept
     * @param handler Callback function
     * @return Builder for chaining
     */
    template <AuthRequiredCallable F>
    [[nodiscard]] auto
    with_auth_required_handler(F handler) const & -> AutomationSessionBuilder {
        AutomationSessionBuilder copy = *this;
        copy.config_.network.intercept_policy = NetworkInterceptPolicy::Custom;
        copy.config_.network.auth_required_handler = std::move(handler);
        return copy;
    }

    template <AuthRequiredCallable F>
    [[nodiscard]] auto
    with_auth_required_handler(F handler) && -> AutomationSessionBuilder {
        config_.network.intercept_policy = NetworkInterceptPolicy::Custom;
        config_.network.auth_required_handler = std::move(handler);
        return std::move(*this);
    }

    /**
     * @brief Inject extra headers into all requests
     *
     * Headers are automatically added to every intercepted request.
     * **Automatically enables network interception**.
     *
     * @param headers Map of header name → value
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.with_extra_headers({
     *     {"Authorization", "Bearer token123"},
     *     {"X-Test-Mode", "true"}
     * });
     * @endcode
     */
    [[nodiscard]] auto
    with_extra_headers(const std::map<std::string, std::string> &headers)
        const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    with_extra_headers(std::map<std::string, std::string> &&headers)
        && -> AutomationSessionBuilder;

    /**
     * @brief Bypass HTTP cache
     *
     * Forces fresh fetches for all requests (useful for testing).
     * **Automatically enables network interception**.
     *
     * @return Builder for chaining
     */
    [[nodiscard]] auto bypass_cache() const & -> AutomationSessionBuilder;
    [[nodiscard]] auto bypass_cache() && -> AutomationSessionBuilder;

    /**
     * @brief Set default authentication credentials
     *
     * Credentials are automatically provided when auth challenges are
     * encountered.
     * **Automatically enables network interception and adds AuthRequired
     * phase**.
     *
     * @param username Username for authentication
     * @param password Password for authentication
     * @return Builder for chaining
     *
     * @example
     * @code
     * builder.with_default_auth("testuser", "testpass");
     * @endcode
     */
    [[nodiscard]] auto with_default_auth(
        std::string_view username,
        std::string_view password) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto
    with_default_auth(std::string_view username,
                      std::string_view password) && -> AutomationSessionBuilder;

    // ========== Infrastructure ==========

    /**
     * @brief Inject custom io_context (advanced users)
     *
     * Allows sharing io_context across multiple sessions or with other
     * Boost.Asio operations. Session will NOT own the io_context.
     *
     * **WARNING**: User must ensure io_context outlives the session.
     *
     * @param ioc Reference to external io_context
     * @return Builder for chaining
     */
    [[nodiscard]] auto with_io_context(
        boost::asio::io_context &ioc) const & -> AutomationSessionBuilder;
    [[nodiscard]] auto with_io_context(
        boost::asio::io_context &ioc) && -> AutomationSessionBuilder;

    // ========== Terminal Operation ==========

    /**
     * @brief Terminal operation: Create and start AutomationSession (BLOCKING)
     *
     * Performs HTTP handshake and returns session ready for run().
     * This is a terminal operation that consumes the builder.
     *
     * **Phase 0 (Blocking - happens here)**:
     * - Starts background thread with io_context
     * - Performs HTTP handshake with WebDriver
     * - Returns with WebSocket URL (deferred connection)
     *
     * **Phase 1 (Async - happens in run())**:
     * - Connects WebSocket BiDi
     * - Creates default browsing context
     * - Applies preload scripts and configuration
     *
     * @return AutomationSession ready for run()
     * @throws std::runtime_error if Phase 0 fails
     *
     * @example
     * @code
     * auto session = AutomationSessionBuilder::create()
     *     .headless()
     *     .with_timeout(std::chrono::seconds{10})
     *     .start();  // Blocks until Phase 0 complete
     *
     * return session.run([&]() -> boost::asio::awaitable<int> {
     *     // Phase 1 happens here (async)
     *     co_await session.navigate("https://example.com");
     *     co_return 0;
     * });
     * @endcode
     */
    [[nodiscard]] auto start() && -> AutomationSession;

  private:
    AutomationSessionBuilder() = default;

    AutomationSessionConfig config_;
};

/**
 * @brief Convenience function for fluent API entry point
 *
 * Equivalent to AutomationSessionBuilder::create() but more concise.
 *
 * @return New AutomationSessionBuilder with default configuration
 *
 * @example
 * @code
 * auto session = bidi::create_session()
 *     .headless()
 *     .start();
 * @endcode
 */
inline auto create_session() -> AutomationSessionBuilder {
    return AutomationSessionBuilder::create();
}

} // namespace bidi

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
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bidi {

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
 * **DESIGN ARQUITETURAL: Híbrido Bloqueante + Assíncrono**
 *
 * A razão desta ser `AutomationSession` é híbrida (bloqueante em start(),
 * assíncrono em run()) é INTENCIONAL:
 *
 * - **Alternativa 1 (Rejeitada)**: Totalmente bloqueante
 *   - PRO: Simples de entender
 *   - CON: Bloqueia thread em operações I/O (inaceitável para servidores)
 *
 * - **Alternativa 2 (Rejeitada)**: Totalmente assíncrona
 *   - PRO: Maximiza throughput
 *   - CON: Boilerplate de co_await mesmo para scripts simples
 *
 * - **Selecionada (Atual)**: Híbrida
 *   - start(): Bloqueante (bootstrap HTTP one-time)
 *   - run(): Assíncrona (operações naturalmente async)
 *   - Razão: Sweet spot para maioria dos usuários
 *
 * Para mais detalhes, ver @ref docs/automation_session_architecture_review.md
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
     * **ARQUITETURA**: Este é o ÚNICO ponto de entrada bloqueante.
     * A razão: garantir que AutomationSession sempre tenha estado válido.
     *
     * Inicialização em 2 fases:
     * 1. **Fase 0 (Bloqueante neste método)**:
     *    - Inicia thread de background com io_context
     *    - Realiza HTTP handshake com WebDriver
     *    - Retorna com websocket_url_ deferred (não conecta WebSocket ainda)
     *    - Timing típico: 1-2 segundos
     *
     * 2. **Fase 1 (Assíncrona, dentro de run())**:
     *    - Conecta WebSocket BiDi (co_await)
     *    - Cria contexto de navegação padrão (co_await)
     *    - Inicializa client_guard_ para gerenciar recursos BiDi
     *
     * **Por que deferred WebSocket connection?**
     * - Evita latência de 2-3s de I/O na thread principal
     * - Permite reusabilidade: run() pode ser chamado múltiplas vezes
     * - Flexibilidade: usuário escolhe quando conectar
     * - Error recovery: Falha na Fase 1 não mata o programa
     *
     * @param webdriver_url URL do servidor WebDriver (padrão: localhost:9515)
     * @param headless Modo headless (padrão: true)
     * @return AutomationSession em estado "pronto para run()"
     *
     * @throws std::runtime_error Se falhar na Fase 0:
     *   - Conexão HTTP falhar
     *   - WebDriver retornar status inválido
     *   - Thread de background falhar
     *
     * @note Falhas na Fase 1 serão capturadas em run() com exceções relançadas
     * @note start() é bloqueante, mas run() é totalmente assíncrono
     * @note run() é reutilizável após restart() automático
     *
     * @see run() para continuação assíncrona
     * @see IoContextRunner para gerenciamento de thread
     * @see SessionGuard para limpeza HTTP
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
     * @see run() para continuação assíncrona
     */
    [[nodiscard]] static auto
    start(const AutomationSessionConfig &config) -> AutomationSession;

    /**
     * @brief Navigate to URL in the default context (ASYNC)
     *
     * **Timing esperado por ReadinessState**:
     * - none: 10-100ms (início de navegação)
     * - interactive: 100ms-5s (DOMContentLoaded)
     * - complete: 500ms-30s (todas as resources carregadas)
     *
     * Operação lazy: apenas co_await dispara navegação real.
     *
     * @param url Target URL to navigate to
     * @param wait Readiness state to wait for (default: complete)
     *             - none: Return immediately after navigation starts
     *             - interactive: Wait for DOM ready (DOMContentLoaded)
     *             - complete: Wait for full page load (window.onload)
     * @param loc Source location for debugging (via GDB)
     * @return Task<std::string> - Navigation ID (lazy, deve ser co_await)
     *
     * @throws std::runtime_error Se navegação falhar:
     *   - Timeout aguardando ReadinessState
     *   - URL inválida
     *   - Contexto de navegação fechado
     *
     * @example Default navigation (complete) - Aguarda tudo
     * @code
     * auto nav_id = co_await session.navigate("https://example.com");
     * // Waits for full page load including images, scripts, etc.
     * @endcode
     *
     * @example Fast navigation (interactive) - Aguarda apenas DOM
     * @code
     * using namespace bidi::commands::browsing_context;
     * auto nav_id = co_await session.navigate(
     *     "https://example.com",
     *     ReadinessState::interactive
     * );
     * // Can interact with DOM before all resources load
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
     * **GARANTIA DE LIMPEZA CRÍTICA** (com trade-off arquitetural):
     *
     * Este método orquestra TODA a complexidade:
     * - co_spawn com exception handling
     * - io_context.run() bloqueante
     * - Extração de exit code
     * - **CRÍTICO**: Garantia de limpeza via ScopeGuard (RAII)
     *
     * **Fluxo de Execução Normal (Sucesso)**:
     * ```
     * 1. ScopeGuard criado (garante limpeza)
     * 2. Conecta WebSocket se Fase 1 ainda pendente
     * 3. co_spawn dispara workflow no io_context
     * 4. io_context.run() bloqueia thread principal
     * 5. Workflow executa e retorna
     * 6. ~ScopeGuard: cleanup (BiDi disconnect + restart context)
     * 7. Return exit code
     * ```
     *
     * **Fluxo de Execução com Exceção (CRÍTICO)**:
     * ```
     * 1-4. Idem acima
     * 5. Workflow joga exceção
     * 6. Exception capturada em callback → stored_exception
     * 7. ~ScopeGuard: cleanup rápido
     * 8. ╔═══════════════════════════════════════════════════╗
     *    ║ NOVA FASE: awaitPendingOperations roda          ║
     *    ║ - runner_->restart() (restaura io_context)      ║
     *    ║ - runner_->get().run() bloqueia NOVAMENTE       ║
     *    ║ - Aguarda até 5s por operações pendentes        ║
     *    ║ - Permite callbacks finalizarem                 ║
     *    ║ - Depois: std::rethrow_exception()              ║
     *    ╚═══════════════════════════════════════════════════╝
     * 9. Exception propagada ao caller COM GARANTIA de cleanup
     * ```
     *
     * **Exception Safety Guarantees**:
     * - Basic: ScopeGuard SEMPRE roda destrutor
     * - Strong: run() reutilizável (restart() restaura estado)
     * - Exception propagation: std::rethrow_exception() após cleanup
     *
     * **Trade-off Arquitetural**:
     * - PRO: Cleanup completo, nenhum leak, callbacks garantidos
     * - CON: Exceção tem latência de até 5s (await_pending_operations)
     *
     * @tparam WorkflowFunc Coroutine function returning awaitable<int>
     * @param workflow Async workflow a executar
     * @return Exit code (0 = sucesso, non-zero = falha)
     *
     * @throws std::exception Rethrows exceções do workflow
     * @throws std::runtime_error Se Fase 1 (WebSocket) falhar
     *
     * @example Workflow básico com error handling
     * @code
     * try {
     *     return session.run([&]() -> boost::asio::awaitable<int> {
     *         co_await session.navigate("https://example.com");
     *         auto title = co_await session.get_title();
     *         std::cout << "Title: " << title << "\n";
     *         co_return 0;
     *     });
     * } catch (const std::exception &e) {
     *     std::cerr << "Workflow ou limpeza falharam: " << e.what() << "\n";
     *     return 1;
     * }
     * // ~ScopeGuard garantido mesmo em exception
     * @endcode
     *
     * @example Reutilizando session (run() é reutilizável)
     * @code
     * auto session = AutomationSession::start();
     *
     * // Primeiro workflow
     * session.run([&]() -> boost::asio::awaitable<int> {
     *     co_await session.navigate("https://site1.com");
     *     co_return 0;
     * });
     *
     * // Segundo workflow (restart() já foi chamado automaticamente)
     * session.run([&]() -> boost::asio::awaitable<int> {
     *     co_await session.navigate("https://site2.com");
     *     co_return 0;
     * });
     * @endcode
     *
     * @note ScopeGuard pattern (RAII) garante limpeza determinística
     * @note Timeout de awaitPendingOperations é 5s (hardcoded)
     * @see AWAIT_PENDING_OPERATIONS_DESIGN.md para design detalhado
     * @see ScopeGuard implementação interna
     * @see ClientGuard::cleanup() limpeza de recursos BiDi
     * @see WorkflowExceptionDoesNotLeakPendings teste de validação
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
             * @brief RAII destrutor: garante limpeza determinística
             *
             * **Sequência de cleanup** (sempre executado, independente de
             * erro):
             * 1. Phase 1: ClientGuard.cleanup() - Desconecta BiDi rápido
             *    - Envia "session.end" ao BiDi
             *    - Cancela subscriptions
             *    - Desconecta WebSocket
             * 2. Phase 2: runner_->restart() - Restaura io_context
             *    - Readmite novo run() se necessário
             *    - Restaura estado após workflow
             *
             * **IMPORTANTE**: Apenas cleanup rápido aqui!
             * Operações pendentes são aguardadas DEPOIS em
             * awaitPendingOperations (se workflow jogou exceção)
             *
             * @note Destrutor noexcept: captura e loga exceções
             * @note Garante cleanup mesmo se ambas as fases falharem
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

            std::cout << "Awaiting pending operations before finalizing..."
                      << std::endl;
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
                std::chrono::milliseconds(5000));

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
     * ⚠️ **AVISO**: Use apenas se AutomationSession não expor API necessária.
     * Misturar Client direto com AutomationSession pode causar:
     * - Gerenciamento duplo de recursos
     * - Exceções de contexto (usar contexto errado)
     * - Comportamento indefinido (operações concorrentes)
     *
     * **Quando usar**:
     * - Operações não expostas por AutomationSession
     * - Acesso a APIs avançadas do Client
     * - Custom session introspection
     *
     * **Quando NÃO usar**:
     * - Operações que AutomationSession já oferece
     * - Operações que modificam contexto de navegação
     * - Cleanup manual (use AutomationSession destrutor)
     *
     * @return Referência a shared_ptr<Client>
     *
     * @example Operação avançada não exposta
     * @code
     * auto client = session.client();
     * auto info = co_await client->get_session_info();
     * @endcode
     *
     * @see evaluate() para operações básicas
     * @see make_function() para chamadas de função
     * @note Prefer APIs de AutomationSession quando possível
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
     * ⚠️ **AVISO**: Use apenas se Boost.Asio APIs diretas forem necessárias.
     * Sequenciamento incorreto pode causar:
     * - Race conditions (outros workflows rodando em paralelo)
     * - Deadlocks (se bloquear dentro da coroutine)
     * - Crashes (se destruir durante operações)
     * - Undefined behavior (se modificar estado de AutomationSession)
     *
     * **Quando usar**:
     * - Custom timers ou I/O operations
     * - Advanced async patterns não suportadas por AutomationSession
     * - Integration com outro código Boost.Asio
     *
     * **Quando NÃO usar**:
     * - Operations que AutomationSession já oferece
     * - co_spawn paralelo sem sincronização
     * - Stopping/restarting io_context manualmente
     *
     * @return Referência a boost::asio::io_context
     *
     * @example Custom async operation com timer
     * @code
     * auto& io = session.get_io_context();
     * boost::asio::co_spawn(
     *     io,
     *     [&]() -> boost::asio::awaitable<void> {
     *         co_await asyncx::net::steady_timer(io,
     * 1s).async_wait(asyncx::net::use_awaitable);
     *         // Custom async work
     *         co_return;
     *     },
     *     boost::asio::detached
     * );
     * @endcode
     *
     * @see run() para orchestração segura
     * @see navigate() para operações seguras
     * @note Este é verdadeiro escape hatch. Use com EXTREMO cuidado!
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
     * @brief Destrutor explícito (RAII cleanup)
     *
     * **ARQUITETURA**: Destrutor garante limpeza de recursos BiDi, mas
     * **PROPOSITALMENTE** NÃO para io_context.
     *
     * **Por que não para io_context?**
     * Razão: Pode haver operações assíncronas pendentes que ainda precisam
     * rodar:
     * - Subscriptions BiDi em limpeza
     * - Operações finalizadas async
     * - Callbacks pendentes
     *
     * io_context será parado apenas quando IoContextRunner é destruído (RAII).
     *
     * **Limpeza realizada**:
     * 1. ClientGuard.cleanup() - Desconecta WebSocket BiDi
     * 2. Cancela todas as subscriptions
     * 3. Envia comando "session.end" ao BiDi
     * 4. **NÃO** para io_context
     * 5. **NÃO** paralisa thread de background
     *
     * **Exception Safety**:
     * - Destrutor noexcept: captura exceções e loga
     * - Destruição segura mesmo se cleanup falhar
     * - Nunca joga (Contracts do destrutor)
     *
     * **Sequência de destruição importante**:
     * ```
     * 1. ~AutomationSession() - cleanup BiDi
     * 2. ~IoContextRunner() - para io_context
     * 3. ~SessionGuard() - cleanup HTTP
     * ```
     *
     * @see ClientGuard::cleanup() para detalhes BiDi
     * @see IoContextRunner para ciclo de vida de thread
     * @see SessionGuard para limpeza HTTP
     * @note Nunca bloqueia thread principal
     * @note Operações async podem continuar brevemente após destrutor
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

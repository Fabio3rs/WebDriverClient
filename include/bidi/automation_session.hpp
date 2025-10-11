#pragma once

#include "bidi/client.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/guards.hpp"
#include "bidi/io_context_runner.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/json/object.hpp>
#include <memory>
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
 * **Key Benefits:**
 * - 85% boilerplate reduction for common cases
 * - Intuitive API: start() → run(workflow)
 * - Escape hatches for advanced users (client(), get_io_context())
 * - Pure composition (zero new dependencies)
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
 */
class AutomationSession {
  public:
    /**
     * @brief Create and start an automation session (BLOCKING)
     *
     * Performs blocking initialization:
     * 1. Starts background io_context thread
     * 2. Connects to WebDriver via HTTP handshake
     * 3. Establishes BiDi WebSocket connection
     * 4. Creates default browsing context
     *
     * @param webdriver_url WebDriver server URL (default: localhost:9515)
     * @param headless Launch browser in headless mode (default: true)
     * @return Initialized AutomationSession ready for operations
     *
     * @throws std::runtime_error if connection fails
     *
     * @note This is the ONLY blocking operation in the API. All subsequent
     *       operations are async and return Task<T>.
     */
    [[nodiscard]] static auto
    start(std::string_view webdriver_url = "http://localhost:9515",
          bool headless = true) -> AutomationSession;

    /**
     * @brief Navigate to URL in the default context (ASYNC)
     *
     * @param url Target URL to navigate to
     * @return Task<std::string> - Navigation ID (lazy, call () to execute)
     *
     * @example
     * @code
     * auto nav_id = co_await session.navigate("https://example.com")();
     * @endcode
     */
    [[nodiscard]] auto navigate(std::string_view url) -> Task<std::string>;

    /**
     * @brief Evaluate JavaScript expression in the default context (ASYNC)
     *
     * @param expression JavaScript code to evaluate
     * @return Task<boost::json::object> - Evaluation result (lazy, call () to
     * execute)
     *
     * @example
     * @code
     * auto result = co_await session.evaluate("document.title")();
     * auto title = result.at("value").as_string();
     * @endcode
     */
    [[nodiscard]] auto
    evaluate(std::string_view expression) -> Task<boost::json::object>;

    /**
     * @brief Get page title (convenience wrapper for evaluate) (ASYNC)
     *
     * @return Task<std::string> - Page title (lazy, call () to execute)
     *
     * @example
     * @code
     * auto title = co_await session.get_title()();
     * @endcode
     */
    [[nodiscard]] auto get_title() -> Task<std::string>;

    /**
     * @brief Get current URL (convenience wrapper for evaluate) (ASYNC)
     *
     * @return Task<std::string> - Current URL (lazy, call () to execute)
     */
    [[nodiscard]] auto get_url() -> Task<std::string>;

    /**
     * @brief Run a coroutine workflow with automatic io_context management
     *
     * Hides boilerplate:
     * - co_spawn with exception handling
     * - io_context.run()
     * - Exit code extraction
     *
     * @tparam WorkflowFunc Coroutine function returning awaitable<int>
     * @param workflow Async workflow to execute
     * @return Exit code (0 = success, non-zero = failure)
     *
     * @example
     * @code
     * return session.run([&]() -> boost::asio::awaitable<int> {
     *     co_await session.navigate("https://example.com")();
     *     co_return 0;
     * });
     * @endcode
     */
    template <typename WorkflowFunc> auto run(WorkflowFunc &&workflow) -> int {
        int exit_code = 1;

        boost::asio::co_spawn(
            runner_->get(), std::forward<WorkflowFunc>(workflow),
            [&exit_code](const std::exception_ptr &exc, int result) {
                if (exc) {
                    try {
                        std::rethrow_exception(exc);
                    } catch (const std::exception &e) {
                        bidi::logging::log_error(
                            std::string("Workflow exception: ") + e.what());
                        exit_code = 1;
                    }
                } else {
                    exit_code = result;
                }
            });

        runner_->get().run();
        return exit_code;
    }

    /**
     * @brief Access the underlying Client (escape hatch for advanced usage)
     *
     * Use this when you need direct Client API access for features not
     * exposed by AutomationSession.
     *
     * @return Reference to shared_ptr<Client>
     */
    [[nodiscard]] auto client() -> std::shared_ptr<Client> & { return client_; }

    /**
     * @brief Get the default browsing context ID
     *
     * @return String view of the context ID
     */
    [[nodiscard]] auto context_id() const -> std::string_view {
        return context_id_;
    }

    /**
     * @brief Access the io_context (escape hatch for custom async operations)
     *
     * @return Reference to the io_context
     */
    [[nodiscard]] auto get_io_context() -> boost::asio::io_context & {
        return runner_->get();
    }

    // Movable but not copyable
    AutomationSession(AutomationSession &&) = default;
    auto operator=(AutomationSession &&) -> AutomationSession & = default;
    AutomationSession(const AutomationSession &) = delete;
    auto operator=(const AutomationSession &) -> AutomationSession & = delete;

    ~AutomationSession() = default;

  private:
    // Private constructor - use start() factory
    AutomationSession(std::unique_ptr<IoContextRunner> runner,
                      std::unique_ptr<SessionGuard> session_guard,
                      std::shared_ptr<Client> client, std::string context_id);

    std::unique_ptr<IoContextRunner> runner_;
    std::unique_ptr<SessionGuard> session_guard_;
    std::shared_ptr<Client> client_;
    std::string context_id_;
};

} // namespace bidi

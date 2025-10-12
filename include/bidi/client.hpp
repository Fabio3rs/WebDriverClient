#pragma once
/**
 * @file client.hpp
 * @brief High-level BiDi Client with lazy evaluation and RAII subscriptions.
 *
 * Architectural rationale:
 * - Lazy evaluation model: The public `Task<T>` API (alias for
 * `asyncx::Async<T>`) is intentionally lazy. Operations are only materialized
 * when a terminal is invoked (`.finally()` or `co_await`). This design
 * minimizes unnecessary allocations and side-effects on the hot path.
 * - [[nodiscard]] enforcement: Prevents accidental ignoring of lazy operations
 *   which would otherwise silently do nothing. Compile-time safety for API
 * misuse.
 * - RAII subscription management: Event subscriptions return RAII handles that
 *   auto-unsubscribe on destruction, preventing subscription leaks.
 * - Integration with BiDiSession: Client wraps core::BiDiSession and provides
 *   high-level convenience methods while preserving all architectural
 * guarantees (strand serialization, timer racing, pool-based allocation).
 * - Pool-based allocation: Underlying BiDiSession uses pending_entry pools and
 *   buffer pools to minimize allocation overhead under high throughput.
 * - Zero busy-wait: All async operations use native kernel suspension via
 *   Boost.Asio primitives (epoll/kqueue/IOCP).
 *
 * Threading expectations:
 * - Client methods are non-blocking and will post work to the session's
 *   strand when necessary.
 * - Long blocking waits must not be performed on the strand to avoid deadlocks.
 * - Terminal operations (.finally, co_await) trigger actual async work.
 */

#include "asyncx.hpp"
#include "bidi/commands.hpp"
#include "bidi/core.hpp"
#include "bidi/ids.hpp"
#include "bidi/script_eval.hpp"
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <memory>
#include <string>
#include <utility>

namespace bidi {

// Type aliases for convenience
template <typename T> using Task = asyncx::Async<T>;

// High-level BiDi client with monadic/async API
class Client : public std::enable_shared_from_this<Client> {
  public:
    using Ptr = std::shared_ptr<Client>;

    // Create client with existing BiDi session
    explicit Client(std::shared_ptr<core::BiDiSession> session);

    // Factory: connect to BiDi WebSocket directly
    static auto connect(boost::asio::io_context &ioc,
                        std::string_view websocket_url) -> Task<Ptr>;

    // Generic zero-overhead async_send (CompletionToken based)
    template <class CompletionToken>
    auto async_send(std::string_view method, boost::json::object params,
                    CompletionToken &&token);

    // ======================== BrowsingContext API ========================

    // Create new browsing context (tab/window)
    [[nodiscard]] auto
    create_context(commands::browsing_context::CreateType type =
                       commands::browsing_context::CreateType::window)
        -> Task<std::string>;

    // Navigate to URL
    [[nodiscard]] auto
    navigate(std::string_view context, std::string_view url,
             commands::browsing_context::ReadinessState wait =
                 commands::browsing_context::ReadinessState::complete)
        -> Task<std::string>;

    // Close browsing context
    [[nodiscard]] auto close_context(std::string_view context) -> Task<bool>;

    // Get browsing context tree
    [[nodiscard]] auto get_context_tree(std::string_view root = {})
        -> Task<boost::json::object>;

    // Handle user prompt
    [[nodiscard]] auto handle_user_prompt(
        std::string_view context, std::optional<bool> accept = std::nullopt,
        std::optional<std::string_view> user_text = std::nullopt) -> Task<void>;

    // ======================== Script API ========================

    // Evaluate JavaScript expression
    [[nodiscard]] auto evaluate(std::string_view expression,
                                std::string_view context,
                                bool await_promise = true)
        -> Task<boost::json::object>;

    // Evaluate JavaScript expression with script evaluation policy
    [[nodiscard]] auto
    evaluate(std::string_view expression, std::string_view context,
             script::script_eval_policy policy, bool await_promise = true)
        -> Task<script::ScriptEvalOutcome>;

    // Call JavaScript function
    [[nodiscard]] auto call_function(std::string_view function_declaration,
                                     std::string_view context,
                                     const boost::json::array &arguments = {},
                                     bool await_promise = true)
        -> Task<boost::json::object>;

    // Call JavaScript function
    [[nodiscard]] auto
    call_function(std::string_view function_declaration,
                  std::string_view context,
                  const boost::json::array &arguments = {},
                  script::script_eval_policy policy =
                      script::script_eval_policy::return_outcome,
                  bool await_promise = true) -> Task<script::ScriptEvalOutcome>;

    // ======================== Session API ========================

    // Subscribe to events (returns RAII subscription handle)
    class Subscription {
      public:
        Subscription() = default;
        ~Subscription() noexcept;

        // Move-only
        Subscription(Subscription &&other) noexcept;
        auto operator=(Subscription &&other) noexcept -> Subscription &;
        Subscription(const Subscription &) = delete;
        auto operator=(const Subscription &) -> Subscription & = delete;

        // Check if subscription is active
        [[nodiscard]] auto is_active() const -> bool {
            return !events_.empty();
        }

      private:
        friend class Client;
        Subscription(std::weak_ptr<Client> client,
                     std::vector<std::string> events);

        std::weak_ptr<Client> client_;
        std::vector<std::string> events_;
    };

    // Subscribe to events with RAII cleanup
    [[nodiscard]] auto subscribe(const std::vector<std::string> &events,
                                 const std::vector<std::string> &contexts = {})
        -> Task<Subscription>;

    // Set event handler for specific method
    auto set_event_handler(std::string method,
                           std::function<void(boost::json::object)> handler)
        -> boost::asio::awaitable<void>;

    // ======================== Utility ========================

    // Get underlying session (for advanced usage)
    auto session() const -> std::shared_ptr<core::BiDiSession> {
        return session_;
    }

    // Get executor for async operations
    auto get_executor() const -> boost::asio::any_io_executor;

    // Graceful disconnect (releases pending responses and closes websocket)
    void disconnect() {
        if (session_) {
            session_->disconnect();
        }
    }

  private:
    std::shared_ptr<core::BiDiSession> session_;

    // Internal: unsubscribe events
    void unsubscribe_events(const std::vector<std::string> &events);
};

// Inline template implementation
template <class CompletionToken>
auto Client::async_send(std::string_view method, boost::json::object params,
                        CompletionToken &&token) {
    using handler_sig = void(boost::system::error_code, boost::json::object);
    return boost::asio::async_initiate<CompletionToken, handler_sig>(
        [self = shared_from_this(), method,
         params = std::move(params)](auto completion_handler) mutable {
            // Wrap completion_handler in shared_ptr to ensure copyable functor
            auto handler_wrapper =
                std::make_shared<decltype(completion_handler)>(
                    std::move(completion_handler));
            self->session_->send_command(
                std::string(method), params,
                [handler_wrapper](
                    const core::ParsedResponse &response) mutable {
                    auto &completion_ref = *handler_wrapper;
                    if (!response.is_success) {
                        boost::system::error_code ec = make_error_code(
                            boost::system::errc::operation_canceled);
                        completion_ref(ec, boost::json::object{});
                    } else {
                        completion_ref({}, response.result);
                    }
                });
        },
        token);
}

} // namespace bidi

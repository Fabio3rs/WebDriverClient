// bidi/client.hpp — High-level BiDi Client with Async/Monadic API
#pragma once

#include "asyncx.hpp"
#include "bidi/commands.hpp"
#include "bidi/core.hpp"
#include "bidi/ids.hpp"
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
    static Task<Ptr> connect(boost::asio::io_context &ioc,
                             std::string_view websocket_url);

    // Generic zero-overhead async_send (CompletionToken based)
    template <class CompletionToken>
    auto async_send(std::string_view method, boost::json::object params,
                    CompletionToken &&token);

    // ======================== BrowsingContext API ========================

    // Create new browsing context (tab/window)
    Task<std::string>
    create_context(commands::browsing_context::CreateType type =
                       commands::browsing_context::CreateType::window);

    // Navigate to URL
    Task<std::string>
    navigate(std::string_view context, std::string_view url,
             commands::browsing_context::ReadinessState wait =
                 commands::browsing_context::ReadinessState::complete);

    // Close browsing context
    Task<bool> close_context(std::string_view context);

    // Get browsing context tree
    Task<boost::json::object> get_context_tree(std::string_view root = {});

    // ======================== Script API ========================

    // Evaluate JavaScript expression
    Task<boost::json::object> evaluate(std::string_view expression,
                                       std::string_view context,
                                       bool await_promise = true);

    // Call JavaScript function
    Task<boost::json::object> call_function(
        std::string_view function_declaration, std::string_view context,
        const boost::json::array &arguments = {}, bool await_promise = true);

    // ======================== Session API ========================

    // Subscribe to events (returns RAII subscription handle)
    class Subscription {
      public:
        Subscription() = default;
        ~Subscription() noexcept;

        // Move-only
        Subscription(Subscription &&other) noexcept;
        Subscription &operator=(Subscription &&other) noexcept;
        Subscription(const Subscription &) = delete;
        Subscription &operator=(const Subscription &) = delete;

        // Check if subscription is active
        bool is_active() const { return !events_.empty(); }

      private:
        friend class Client;
        Subscription(std::weak_ptr<Client> client,
                     std::vector<std::string> events);

        std::weak_ptr<Client> client_;
        std::vector<std::string> events_;
    };

    // Subscribe to events with RAII cleanup
    Task<Subscription> subscribe(const std::vector<std::string> &events,
                                 const std::vector<std::string> &contexts = {});

    // Set event handler for specific method
    void set_event_handler(const std::string &method,
                           std::function<void(boost::json::object)> handler);

    // ======================== Utility ========================

    // Get underlying session (for advanced usage)
    std::shared_ptr<core::BiDiSession> session() const { return session_; }

    // Get executor for async operations
    boost::asio::any_io_executor get_executor() const;

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
         params = std::move(params)](auto handler) mutable {
            // Reusa infraestrutura existente de BiDiSession
            self->session_->send_command(
                std::string(method), params,
                [handler = std::move(handler)](
                    const core::ParsedResponse &response) mutable {
                    if (!response.is_success) {
                        // Mapear para error_code genérico (extensível
                        // futuramente)
                        boost::system::error_code ec = make_error_code(
                            boost::system::errc::
                                operation_canceled); // placeholder
                                                     // domain
                        handler(ec, {});
                    } else {
                        handler({}, response.result);
                    }
                });
        },
        token);
}

} // namespace bidi

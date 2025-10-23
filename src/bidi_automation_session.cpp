#include "bidi/automation_session.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/logging.hpp"
#include <boost/json/object.hpp>
#include <boost/json/string.hpp>
#include <stdexcept>

namespace bidi {

// Phase 1 constructor: HTTP-only initialization (before WebSocket connect)
AutomationSession::AutomationSession(
    std::unique_ptr<IoContextRunner> runner, std::string websocket_url,
    std::shared_ptr<SessionGuard> session_guard)
    : runner_(std::move(runner)), client_guard_(nullptr), context_id_(""),
      websocket_url_(std::move(websocket_url)),
      session_guard_(std::move(session_guard)) {}

// Phase 2 constructor: Full initialization (after WebSocket connect)
AutomationSession::AutomationSession(std::unique_ptr<IoContextRunner> runner,
                                     std::unique_ptr<ClientGuard> client_guard,
                                     std::string context_id)
    : runner_(std::move(runner)), client_guard_(std::move(client_guard)),
      context_id_(std::move(context_id)), websocket_url_(""),
      session_guard_(nullptr) {}

// Static factory (blocking HTTP handshake only, WebSocket deferred to run())
auto AutomationSession::start(std::string_view webdriver_url, bool headless)
    -> AutomationSession {

    // Phase 1: Start background io_context thread
    auto runner = std::make_unique<IoContextRunner>();

    // Phase 2: HTTP handshake only (blocking, synchronous, NO WebSocket yet)
    auto builder = connect_to(webdriver_url);
    if (headless) {
        builder = builder.headless();
    }
    builder = builder.no_sandbox();

    // Get WebSocket URL via HTTP handshake (blocking)
    auto [ws_url, session_guard] = std::move(builder).get_websocket_url();

    bidi::logging::log_info("AutomationSession HTTP handshake complete: ws=" +
                            ws_url);

    // Return with deferred WebSocket connection
    // Phase 2 (WebSocket connect + context creation) will happen in run()
    return {std::move(runner), std::move(ws_url), std::move(session_guard)};
}

// Navigate (async, returns lazy Task)
auto AutomationSession::navigate(
    std::string_view url, commands::browsing_context::ReadinessState wait,
    const std::source_location &loc) -> Task<std::string> {
    if (!client_guard_) {
        throw std::runtime_error(
            "Navigate called before run() - WebSocket not connected");
    }
    return client_guard_->client()->navigate(context_id_, url, wait, loc);
}

// Evaluate (async, returns lazy Task)
auto AutomationSession::evaluate(std::string_view expression,
                                 const std::source_location &loc)
    -> Task<boost::json::object> {
    if (!client_guard_) {
        throw std::runtime_error(
            "Evaluate called before run() - WebSocket not connected");
    }
    return client_guard_->client()->evaluate(expression, context_id_, true,
                                             loc);
}

// Get page title (convenience wrapper)
auto AutomationSession::get_title(const std::source_location &loc)
    -> Task<std::string> {
    return evaluate_as_or("document.title", std::string(""), loc);
}

// Get current URL (convenience wrapper)
auto AutomationSession::get_url(const std::source_location &loc)
    -> Task<std::string> {
    return evaluate_as_or("document.location.href", std::string(""), loc);
}

} // namespace bidi

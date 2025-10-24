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
    std::shared_ptr<SessionGuard> session_guard, AutomationSessionConfig config)
    : runner_(std::move(runner)), client_guard_(nullptr), context_id_(""),
      websocket_url_(std::move(websocket_url)),
      session_guard_(std::move(session_guard)), config_(std::move(config)) {}

// Phase 2 constructor: Full initialization (after WebSocket connect)
AutomationSession::AutomationSession(std::unique_ptr<IoContextRunner> runner,
                                     std::unique_ptr<ClientGuard> client_guard,
                                     std::string context_id,
                                     AutomationSessionConfig config)
    : runner_(std::move(runner)), client_guard_(std::move(client_guard)),
      context_id_(std::move(context_id)), websocket_url_(""),
      session_guard_(nullptr), config_(std::move(config)) {}

// Static factory (blocking HTTP handshake only, WebSocket deferred to run())
// Backward-compatible version - delegates to config-based start()
auto AutomationSession::start(std::string_view webdriver_url,
                              bool headless) -> AutomationSession {
    // Create default config from parameters
    AutomationSessionConfig config{};
    config.webdriver_url = std::string(webdriver_url);
    config.headless = headless;

    // Delegate to config-based start()
    return start(config);
}

// Config-based static factory (blocking HTTP handshake only, WebSocket deferred
// to run())
auto AutomationSession::start(const AutomationSessionConfig &config)
    -> AutomationSession {

    // Phase 1: Start background io_context thread
    auto runner = std::make_unique<IoContextRunner>();

    // Phase 2: HTTP handshake only (blocking, synchronous, NO WebSocket yet)
    auto builder = connect_to(config.webdriver_url);

    // Apply basic configuration to connection builder
    if (config.headless) {
        builder = builder.headless();
    }

    // Apply browser args (including no-sandbox if not already present)
    bool has_no_sandbox = false;
    for (const auto &arg : config.browser_args) {
        if (arg == "--no-sandbox") {
            has_no_sandbox = true;
            break;
        }
    }

    if (!has_no_sandbox) {
        builder = builder.no_sandbox();
    }

    if (!config.browser_args.empty()) {
        builder = builder.with_args(config.browser_args);
    }

    // Get WebSocket URL via HTTP handshake (blocking)
    auto [ws_url, session_guard] = std::move(builder).get_websocket_url();

    bidi::logging::log_info("AutomationSession HTTP handshake complete: ws=" +
                            ws_url);

    // Return with deferred WebSocket connection
    // Phase 2 (WebSocket connect + context creation + config application) will
    // happen in run()
    return {std::move(runner), std::move(ws_url), std::move(session_guard),
            config};
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

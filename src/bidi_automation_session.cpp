#include "bidi/automation_session.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/logging.hpp"
#include <boost/json/object.hpp>
#include <boost/json/string.hpp>
#include <stdexcept>

namespace bidi {

// Private constructor
AutomationSession::AutomationSession(
    std::unique_ptr<IoContextRunner> runner,
    std::unique_ptr<SessionGuard> session_guard, std::shared_ptr<Client> client,
    std::string context_id)
    : runner_(std::move(runner)), session_guard_(std::move(session_guard)),
      client_(std::move(client)), context_id_(std::move(context_id)) {}

// Static factory (blocking)
auto AutomationSession::start(std::string_view webdriver_url, bool headless)
    -> AutomationSession {

    // Phase 1: Start background io_context thread
    auto runner = std::make_unique<IoContextRunner>();

    // Phase 2: Connect via HTTP handshake and BiDi WebSocket (blocking)
    auto builder = connect_to(webdriver_url);
    if (headless) {
        builder = builder.headless();
    }
    builder = builder.no_sandbox();

    // Execute the connection (blocking)
    auto client_task = std::move(builder).connect(runner->get());
    auto client = client_task.get(); // Blocking call via Task<T>::get()

    if (!client) {
        throw std::runtime_error(
            "Failed to establish BiDi connection to WebDriver server");
    }

    // Phase 3: Create default browsing context (blocking)
    auto context_task = client->create_context();
    auto context_id = context_task.get(); // Blocking call via Task<T>::get()

    // Validate context creation
    if (context_id.empty()) {
        throw std::runtime_error(
            "Failed to create browsing context: empty context ID returned");
    }

    // Phase 4: Extract SessionGuard from ConnectionBuilder's capture
    // Note: We rely on SessionGuard being kept alive by ConnectionBuilder's map
    // continuation.
    // For now, we create a new SessionGuard to manage the session lifecycle.
    auto session_guard =
        std::make_unique<SessionGuard>(std::string(webdriver_url));

    bidi::logging::log_info("AutomationSession started: context=" + context_id);

    return {std::move(runner), std::move(session_guard), client,
            std::move(context_id)};
}

// Navigate (async, returns lazy Task)
auto AutomationSession::navigate(
    std::string_view url, commands::browsing_context::ReadinessState wait,
    const std::source_location &loc) -> Task<std::string> {
    return client_->navigate(context_id_, url, wait, loc);
}

// Evaluate (async, returns lazy Task)
auto AutomationSession::evaluate(std::string_view expression,
                                 const std::source_location &loc)
    -> Task<boost::json::object> {
    return client_->evaluate(expression, context_id_, true, loc);
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

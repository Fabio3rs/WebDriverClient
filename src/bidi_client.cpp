// src/bidi_client.cpp — High-level BiDi Client implementation
#include <memory>

#include "bidi/client.hpp"
#include "bidi/ids.hpp"
#include "bidi/logging.hpp"
#include "bidi/script_eval.hpp"
#include "bidi_methods.hpp"

// script_eval.hpp já fornece declarações necessárias

namespace bidi {

// ======================== Client Implementation ========================

Client::Client(std::shared_ptr<core::BiDiSession> session)
    : session_(std::move(session)) {}

auto Client::connect(boost::asio::io_context &ioc,
                     std::string_view websocket_url) -> Task<Client::Ptr> {
    auto ex = ioc.get_executor();
    auto result = Task<Ptr>::make(ex);
    auto ws_client = std::make_shared<core::WebSocketClient>(ioc);
    auto session = std::make_shared<core::BiDiSession>(ws_client);
    session->async_start(
        websocket_url,
        [result, session](boost::system::error_code error_code) mutable {
            if (error_code) {
                result.fail(std::make_exception_ptr(
                    std::runtime_error("Failed to connect to BiDi WebSocket: " +
                                       error_code.message())));
                return;
            }
            auto client = std::make_shared<Client>(session);
            result.fulfill(client);
        });
    return result;
}

auto Client::get_executor() const -> boost::asio::any_io_executor {
    return session_->get_executor();
}

// ======================== BrowsingContext API ========================

auto Client::create_context(commands::browsing_context::CreateType type)
    -> Task<std::string> {
    auto ex = get_executor();
    auto result = Task<std::string>::make(ex);
    auto params = commands::browsing_context::create(type);
    session_->send_command(
        std::string(bidi::ids::methods::bc_create), params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.create failed: " + response.error_code +
                    " - " + response.error_message)));
                return;
            }
            const auto *context_it = response.result.find("context");
            if (context_it != response.result.end() &&
                context_it->value().is_string()) {
                result.fulfill(context_it->value().as_string().c_str());
            } else {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "Invalid browsingContext.create response")));
            }
        });
    return result;
}

auto Client::navigate(std::string_view context, std::string_view url,
                      commands::browsing_context::ReadinessState wait)
    -> Task<std::string> {
    auto ex = get_executor();
    auto result = Task<std::string>::make(ex);
    auto params = commands::browsing_context::navigate(context, url, wait);
    session_->send_command(
        std::string(bidi::ids::methods::bc_navigate), params,
        [result, url](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.navigate failed: " + response.error_code +
                    " - " + response.error_message)));
                return;
            }
            const auto *url_it = response.result.find("url");
            if (url_it != response.result.end() &&
                url_it->value().is_string()) {
                result.fulfill(url_it->value().as_string().c_str());
            } else {
                // convert string_view to std::string for Task fulfillment
                result.fulfill(std::string(url));
            }
        });
    return result;
}

auto Client::close_context(std::string_view context) -> Task<bool> {
    auto ex = get_executor();
    auto result = Task<bool>::make(ex);
    auto params = commands::browsing_context::close(context);
    session_->send_command(
        std::string(bidi::ids::methods::bc_close), params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.close failed: " + response.error_code +
                    " - " + response.error_message)));
                return;
            }
            result.fulfill(true);
        });
    return result;
}

auto Client::get_context_tree(std::string_view root)
    -> Task<boost::json::object> {
    auto ex = get_executor();
    auto result = Task<boost::json::object>::make(ex);
    auto params = commands::browsing_context::get_tree(root);
    session_->send_command(
        std::string(bidi::ids::methods::bc_getTree), params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.getTree failed: " + response.error_code +
                    " - " + response.error_message)));
                return;
            }
            result.fulfill(response.result);
        });
    return result;
}

// ======================== Script API ========================

auto Client::evaluate(std::string_view expression, std::string_view context,
                      bool await_promise) -> Task<boost::json::object> {
    auto ex = get_executor();
    auto result = Task<boost::json::object>::make(ex);
    commands::script::Target target{.context = context};
    auto params = commands::script::evaluate(expression, target, await_promise);
    session_->send_command(
        std::string(bidi::ids::methods::script_evaluate), params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "script.evaluate failed: " + response.error_code + " - " +
                    response.error_message)));
                return;
            }
            result.fulfill(response.result);
        });
    return result;
}

auto Client::evaluate(std::string_view expression, std::string_view context,
                      script::script_eval_policy policy,
                      bool await_promise) -> Task<script::ScriptEvalOutcome> {
    auto ex = get_executor();
    auto task = Task<script::ScriptEvalOutcome>::make(ex);
    commands::script::Target target{.context = context};
    auto params = commands::script::evaluate(expression, target, await_promise);
    session_->send_command(
        std::string(bidi::ids::methods::script_evaluate), params,
        [task, policy](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                task.fail(std::make_exception_ptr(std::runtime_error(
                    std::string("script.evaluate failed: ") +
                    response.error_code + " - " + response.error_message)));
                return;
            }
            auto decision = script::apply_policy(response, policy);
            if (decision.action ==
                script::PolicyApplicationResult::Action::throw_exception) {
                task.fail(
                    std::make_exception_ptr(script::ScriptEvaluateException(
                        std::move(decision.exception))));
                return;
            }
            task.fulfill(std::move(decision.outcome));
        });
    return task;
}

auto Client::call_function(std::string_view function_declaration,
                           std::string_view context,
                           const boost::json::array &arguments,
                           bool await_promise) -> Task<boost::json::object> {
    auto ex = get_executor();
    auto result = Task<boost::json::object>::make(ex);
    commands::script::Target target{.context = context};
    auto params = commands::script::call_function(function_declaration, target,
                                                  arguments, await_promise);
    session_->send_command(
        std::string(bidi::ids::methods::script_callFunction), params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "script.callFunction failed: " + response.error_code +
                    " - " + response.error_message)));
                return;
            }
            result.fulfill(response.result);
        });
    return result;
}

// ======================== Session API ========================

auto Client::subscribe(const std::vector<std::string> &events,
                       const std::vector<std::string> &contexts)
    -> Task<Client::Subscription> {
    auto ex = get_executor();
    auto result = Task<Subscription>::make(ex);
    auto params = commands::session::subscribe(events, contexts);
    session_->send_command(
        std::string(bidi::ids::methods::session_subscribe), params,
        [result, events, self = weak_from_this()](
            const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "session.subscribe failed: " + response.error_code + " - " +
                    response.error_message)));
                return;
            }
            if (auto client = self.lock()) {
                result.fulfill(Subscription(client, events));
            } else {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "Client was destroyed during subscription")));
            }
        });
    return result;
}

auto Client::set_event_handler(std::string method,
                               std::function<void(boost::json::object)> handler)
    -> boost::asio::awaitable<void> {
    auto sub_async = session_->subscribe_event(
        method, [handler = std::move(handler)](const core::ParsedEvent &event) {
            handler(event.params);
        });
    // Convert asyncx::Async to awaitable and co_await
    co_await asyncx::as_awaitable(std::move(sub_async));
    co_return;
}

void Client::unsubscribe_events(const std::vector<std::string> &events) {
    auto params = commands::session::unsubscribe(events);
    session_->send_command(
        std::string(bidi::ids::methods::session_unsubscribe), params,
        [](const core::ParsedResponse &response) {
            if (!response.is_success) {
                bidi::logging::log_error(
                    std::string("session.unsubscribe failed: ") +
                    response.error_code + " - " + response.error_message);
            }
        });
}

// ======================== Subscription Implementation ========================

Client::Subscription::Subscription(std::weak_ptr<Client> client,
                                   std::vector<std::string> events)
    : client_(std::move(client)), events_(std::move(events)) {}

Client::Subscription::~Subscription() noexcept {
    try {
        if (!events_.empty()) {
            if (auto client = client_.lock()) {
                client->unsubscribe_events(events_);
            }
        }
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::string("~Client::Subscription exception: ") + e.what());
    } catch (...) {
        bidi::logging::log_error("~Client::Subscription unknown exception");
    }
}

Client::Subscription::Subscription(Subscription &&other) noexcept
    : client_(std::move(other.client_)), events_(std::move(other.events_)) {
    other.events_.clear();
}

auto Client::Subscription::operator=(Subscription &&other) noexcept
    -> Client::Subscription & {
    if (this != &other) {
        if (!events_.empty()) {
            if (auto client = client_.lock()) {
                client->unsubscribe_events(events_);
            }
        }
        client_ = std::move(other.client_);
        events_ = std::move(other.events_);
        other.events_.clear();
    }
    return *this;
}

} // namespace bidi

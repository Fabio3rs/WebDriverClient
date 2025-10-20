// src/bidi_network_intercept_handler.cpp
#include "bidi/client.hpp"
#include "bidi/ids.hpp"
#include "bidi/logging.hpp"
#include "bidi/network_intercept_handler.hpp"
#include "bidi_methods.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <format>
#include <utility>

namespace bidi {

// ==================== Factory Method ====================

auto NetworkInterceptHandler::create(std::shared_ptr<Client> client,
                                     NetworkInterceptConfig config,
                                     const std::source_location &loc)
    -> Task<std::shared_ptr<NetworkInterceptHandler>> {

    auto ex = client->get_executor();
    auto result = Task<std::shared_ptr<NetworkInterceptHandler>>::make(ex, loc);

    // Spawn coroutine to perform async initialization
    boost::asio::co_spawn(
        ex,
        [client, config = std::move(config), result,
         loc]() mutable -> boost::asio::awaitable<void> {
            try {
                // Call network.addIntercept
                auto intercept_id = co_await client->add_intercept(
                    config.phases, config.contexts, config.url_patterns, loc);

                // Create handler instance
                auto handler = NetworkInterceptHandler(
                    client, std::move(config), std::move(intercept_id));

                // Create shared_ptr ONCE to avoid use-after-move
                auto handler_ptr = std::make_shared<NetworkInterceptHandler>(
                    std::move(handler));

                // Subscribe to events based on phases
                for (const auto &phase : handler_ptr->config_.phases) {
                    using enum types::network::InterceptPhase;
                    switch (phase) {
                    case BeforeRequestSent:
                        // Subscribe and set handler (fire-and-forget)
                        (void)client->set_event_handler(
                            bidi::ids::events::net_beforeRequestSent,
                            [handler_ptr](boost::json::object params) {
                                try {
                                    auto event_params = boost::json::value_to<
                                        types::network::
                                            BeforeRequestSentParameters>(
                                        boost::json::value(params));
                                    handler_ptr->handle_before_request(
                                        event_params);
                                } catch (const std::exception &e) {
                                    logging::log_error(
                                        std::format("Failed to handle "
                                                    "beforeRequestSent: {}",
                                                    e.what()));
                                }
                            },
                            loc);
                        break;

                    case ResponseStarted:
                        (void)client->set_event_handler(
                            bidi::ids::events::net_responseStarted,
                            [handler_ptr](boost::json::object params) {
                                try {
                                    auto event_params = boost::json::value_to<
                                        types::network::
                                            ResponseStartedParameters>(
                                        boost::json::value(params));
                                    handler_ptr->handle_response_started(
                                        event_params);
                                } catch (const std::exception &e) {
                                    logging::log_error(std::format(
                                        "Failed to handle responseStarted: {}",
                                        e.what()));
                                }
                            },
                            loc);
                        break;

                    case AuthRequired:
                        (void)client->set_event_handler(
                            bidi::ids::events::net_authRequired,
                            [handler_ptr](boost::json::object params) {
                                try {
                                    auto event_params = boost::json::value_to<
                                        types::network::AuthRequiredParameters>(
                                        boost::json::value(params));
                                    handler_ptr->handle_auth_required(
                                        event_params);
                                } catch (const std::exception &e) {
                                    logging::log_error(std::format(
                                        "Failed to handle authRequired: {}",
                                        e.what()));
                                }
                            },
                            loc);
                        break;
                    }
                }

                result.fulfill(handler_ptr);

            } catch (const std::exception &) {
                result.fail(std::current_exception());
            }
        },
        boost::asio::detached);

    return result;
}

// ==================== Destructor ====================

NetworkInterceptHandler::~NetworkInterceptHandler() noexcept {
    try {
        if (!intercept_id_.empty() && client_) {
            // Fire-and-forget: remove intercept on destruction
            auto remove_task = client_->remove_intercept(intercept_id_);
            remove_task.finally([](auto &&...) {
                // Cleanup complete (or failed - either way, handler is
                // destroyed)
            });
        }
    } catch (...) {
        // Swallow all exceptions in destructor (C.31)
        try {
            logging::log_error(
                "NetworkInterceptHandler destructor: exception during cleanup");
        } catch (...) {
            // Even logging failed - give up silently
        }
    }
}

// ==================== Event Handlers ====================

void NetworkInterceptHandler::handle_before_request(
    const types::network::BeforeRequestSentParameters &params) {

    // Apply policy to determine action
    auto resolution = [this, &params]() -> std::optional<RequestResolution> {
        using enum NetworkInterceptPolicy;
        switch (config_.policy) {
        case ContinueAll:
            return RequestResolution{.action = InterceptAction::Continue,
                                     .body = std::nullopt,
                                     .cookies = std::nullopt,
                                     .headers = std::nullopt,
                                     .method = std::nullopt,
                                     .url = std::nullopt};
        case FailAll:
            return RequestResolution{.action = InterceptAction::Fail,
                                     .body = std::nullopt,
                                     .cookies = std::nullopt,
                                     .headers = std::nullopt,
                                     .method = std::nullopt,
                                     .url = std::nullopt};
        case Custom:
            if (config_.before_request_handler) {
                return (*config_.before_request_handler)(params);
            }
            return std::nullopt;
        }
        return std::nullopt;
    }();

    if (!resolution) {
        return; // No action - manual handling
    }

    // Execute appropriate command based on action
    using enum InterceptAction;
    switch (resolution->action) {
    case Continue: {
        auto task = client_->continue_request(
            params.base.request, resolution->body, resolution->cookies,
            resolution->headers, resolution->method, resolution->url);
        task.finally([](auto &&...) {});
        break;
    }
    case Fail: {
        auto task = client_->fail_request(params.base.request);
        task.finally([](auto &&...) {});
        break;
    }
    case Provide:
        // ProvideResponse not applicable for beforeRequestSent phase
        logging::log_warning(
            "InterceptAction::Provide not valid for beforeRequestSent phase");
        break;
    case Ignore:
        // Do nothing
        break;
    }
}

void NetworkInterceptHandler::handle_response_started(
    const types::network::ResponseStartedParameters &params) {

    auto resolution = [this, &params]() -> std::optional<ResponseResolution> {
        using enum NetworkInterceptPolicy;
        switch (config_.policy) {
        case ContinueAll:
            return ResponseResolution{.action = InterceptAction::Continue,
                                      .body = std::nullopt,
                                      .cookies = std::nullopt,
                                      .credentials = std::nullopt,
                                      .headers = std::nullopt,
                                      .reason_phrase = std::nullopt,
                                      .status_code = std::nullopt};
        case FailAll:
            return ResponseResolution{.action = InterceptAction::Fail,
                                      .body = std::nullopt,
                                      .cookies = std::nullopt,
                                      .credentials = std::nullopt,
                                      .headers = std::nullopt,
                                      .reason_phrase = std::nullopt,
                                      .status_code = std::nullopt};
        case Custom:
            if (config_.response_started_handler) {
                return (*config_.response_started_handler)(params);
            }
            return std::nullopt;
        }
        return std::nullopt;
    }();

    if (!resolution) {
        return;
    }

    using enum InterceptAction;
    switch (resolution->action) {
    case Continue: {
        auto task = client_->continue_response(
            params.base.request, resolution->cookies, resolution->credentials,
            resolution->headers, resolution->reason_phrase,
            resolution->status_code);
        task.finally([](auto &&...) {});
        break;
    }
    case Provide: {
        auto task = client_->provide_response(
            params.base.request, resolution->body, resolution->cookies,
            resolution->headers, resolution->reason_phrase,
            resolution->status_code);
        task.finally([](auto &&...) {});
        break;
    }
    case Fail: {
        auto task = client_->fail_request(params.base.request);
        task.finally([](auto &&...) {});
        break;
    }
    case Ignore:
        break;
    }
}

void NetworkInterceptHandler::handle_auth_required(
    const types::network::AuthRequiredParameters &params) {

    auto resolution = [this, &params]() -> std::optional<AuthResolution> {
        using enum NetworkInterceptPolicy;
        switch (config_.policy) {
        case ContinueAll:
            return AuthResolution{.action = InterceptAction::Continue,
                                  .auth_action =
                                      types::network::AuthAction::Default,
                                  .credentials = std::nullopt};
        case FailAll:
            return AuthResolution{.action = InterceptAction::Fail,
                                  .auth_action =
                                      types::network::AuthAction::Cancel,
                                  .credentials = std::nullopt};
        case Custom:
            if (config_.auth_required_handler) {
                return (*config_.auth_required_handler)(params);
            }
            return std::nullopt;
        }
        return std::nullopt;
    }();

    if (!resolution) {
        return;
    }

    using enum InterceptAction;
    switch (resolution->action) {
    case Continue: {
        auto task = client_->continue_with_auth(params.base.request,
                                                resolution->auth_action,
                                                resolution->credentials);
        task.finally([](auto &&...) {});
        break;
    }
    case Fail: {
        auto task = client_->fail_request(params.base.request);
        task.finally([](auto &&...) {});
        break;
    }
    case Provide:
    case Ignore:
        // Not applicable for auth
        break;
    }
}

} // namespace bidi

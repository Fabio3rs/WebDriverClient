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

auto NetworkInterceptHandler::cleanup(const std::source_location &loc)
    -> Task<void> {
    cleanup_started_.store(true, std::memory_order_relaxed);

    auto exec =
        client_ ? client_->get_executor() : asyncx::net::system_executor();
    auto result = Task<void>::make(exec, loc);

    if (!client_ || intercept_id_.empty()) {
        cleanup_succeeded_.store(true, std::memory_order_relaxed);
        result.fulfill();
        return result;
    }

    auto intercept = intercept_id_;
    auto remove_task = client_->remove_intercept(intercept, loc);

    remove_task.finally([this, result,
                         intercept](std::optional<asyncx::EC> ec,
                                    std::exception_ptr ep) mutable {
        if (ec && *ec) {
            cleanup_succeeded_.store(false, std::memory_order_relaxed);
            logging::log_error(std::format(
                "NetworkInterceptHandler cleanup failed for intercept {}: {}",
                intercept, ec->message()));
            result.fail(*ec);
            return;
        }
        if (ep) {
            cleanup_succeeded_.store(false, std::memory_order_relaxed);
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception &e) {
                logging::log_error(
                    std::format("NetworkInterceptHandler cleanup failed for "
                                "intercept {}: {}",
                                intercept, e.what()));
            } catch (...) {
                logging::log_error(
                    std::format("NetworkInterceptHandler cleanup failed for "
                                "intercept {} with "
                                "unknown exception",
                                intercept));
            }
            result.fail(ep);
            return;
        }

        intercept_id_.clear();
        cleanup_succeeded_.store(true, std::memory_order_relaxed);
        result.fulfill();
    });

    return result;
}

NetworkInterceptHandler::NetworkInterceptHandler(
    NetworkInterceptHandler &&other) noexcept
    : client_(std::move(other.client_)), config_(std::move(other.config_)),
      intercept_id_(std::move(other.intercept_id_)) {
    cleanup_started_.store(
        other.cleanup_started_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    cleanup_succeeded_.store(
        other.cleanup_succeeded_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    other.cleanup_started_.store(false, std::memory_order_relaxed);
    other.cleanup_succeeded_.store(false, std::memory_order_relaxed);
}

auto NetworkInterceptHandler::operator=(
    NetworkInterceptHandler &&other) noexcept -> NetworkInterceptHandler & {
    if (this == &other) {
        return *this;
    }

    client_ = std::move(other.client_);
    config_ = std::move(other.config_);
    intercept_id_ = std::move(other.intercept_id_);

    cleanup_started_.store(
        other.cleanup_started_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    cleanup_succeeded_.store(
        other.cleanup_succeeded_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    other.cleanup_started_.store(false, std::memory_order_relaxed);
    other.cleanup_succeeded_.store(false, std::memory_order_relaxed);

    return *this;
}

// ==================== Destructor ====================

NetworkInterceptHandler::~NetworkInterceptHandler() noexcept {
    try {
        if (!client_ || intercept_id_.empty()) {
            return;
        }

        const auto intercept = intercept_id_;
        const auto started = cleanup_started_.load(std::memory_order_relaxed);
        const auto succeeded =
            cleanup_succeeded_.load(std::memory_order_relaxed);

        if (!started) {
            logging::log_warning(std::format(
                "NetworkInterceptHandler destroyed without explicit cleanup; "
                "scheduling network.removeIntercept for {}",
                intercept));
        } else if (!succeeded) {
            logging::log_warning(std::format(
                "NetworkInterceptHandler cleanup previously failed for {}; "
                "retrying during destruction",
                intercept));
        }

        cleanup_started_.store(true, std::memory_order_relaxed);

        auto remove_task = client_->remove_intercept(intercept);
        remove_task.finally([id = intercept](std::optional<asyncx::EC> ec,
                                             std::exception_ptr ep) {
            if (ec && *ec) {
                logging::log_error(std::format(
                    "network.removeIntercept (destructor) failed for {}: {}",
                    id, ec->message()));
                return;
            }
            if (ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (const std::exception &e) {
                    logging::log_error(std::format(
                        "network.removeIntercept (destructor) threw for {}: {}",
                        id, e.what()));
                } catch (...) {
                    logging::log_error(std::format(
                        "network.removeIntercept (destructor) threw unknown "
                        "exception for {}",
                        id));
                }
            }
        });
    } catch (...) {
        // Swallow all exceptions in destructor (C.31)
        try {
            logging::log_error(std::format(
                "NetworkInterceptHandler destructor: exception scheduling "
                "cleanup for {}",
                intercept_id_));
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

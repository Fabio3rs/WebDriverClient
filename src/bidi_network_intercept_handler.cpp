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

// ==================== Helper Functions ====================

namespace {
/**
 * @brief Create callback for beforeRequestSent phase using weak_ptr to break
 * cycles
 *
 * @param handler_weak Weak pointer to handler (allows handler to be destroyed)
 * @return Callback function
 */
auto make_before_request_callback(
    const std::weak_ptr<NetworkInterceptHandler> &handler_weak)
    -> std::function<void(const boost::json::object &)> {
    return [handler_weak](const boost::json::object &params) {
        try {
            auto handler_ptr = handler_weak.lock();
            if (!handler_ptr) {
                return; // Handler destroyed, callback is dead
            }
            auto event_params = boost::json::value_to<
                types::network::BeforeRequestSentParameters>(
                boost::json::value(params));
            handler_ptr->handle_before_request(event_params);
        } catch (const std::exception &e) {
            logging::log_error(std::format(
                "Failed to handle beforeRequestSent: {}", e.what()));
        }
    };
}

/**
 * @brief Create callback for responseStarted phase using weak_ptr to break
 * cycles
 *
 * @param handler_weak Weak pointer to handler (allows handler to be destroyed)
 * @return Callback function
 */
auto make_response_started_callback(
    const std::weak_ptr<NetworkInterceptHandler> &handler_weak)
    -> std::function<void(const boost::json::object &)> {
    return [handler_weak](const boost::json::object &params) {
        try {
            auto handler_ptr = handler_weak.lock();
            if (!handler_ptr) {
                return; // Handler destroyed, callback is dead
            }
            auto event_params = boost::json::value_to<
                types::network::ResponseStartedParameters>(
                boost::json::value(params));
            handler_ptr->handle_response_started(event_params);
        } catch (const std::exception &e) {
            logging::log_error(
                std::format("Failed to handle responseStarted: {}", e.what()));
        }
    };
}

/**
 * @brief Create callback for authRequired phase using weak_ptr to break cycles
 *
 * @param handler_weak Weak pointer to handler (allows handler to be destroyed)
 * @return Callback function
 */
auto make_auth_required_callback(
    const std::weak_ptr<NetworkInterceptHandler> &handler_weak)
    -> std::function<void(const boost::json::object &)> {
    return [handler_weak](const boost::json::object &params) {
        try {
            auto handler_ptr = handler_weak.lock();
            if (!handler_ptr) {
                return; // Handler destroyed, callback is dead
            }
            auto event_params =
                boost::json::value_to<types::network::AuthRequiredParameters>(
                    boost::json::value(params));
            handler_ptr->handle_auth_required(event_params);
        } catch (const std::exception &e) {
            logging::log_error(
                std::format("Failed to handle authRequired: {}", e.what()));
        }
    };
}
} // namespace

// ==================== Factory Method ====================

auto NetworkInterceptHandler::create(std::shared_ptr<Client> client,
                                     NetworkInterceptConfig config,
                                     const std::source_location &loc)
    -> Task<std::shared_ptr<NetworkInterceptHandler>> {

    auto ex = client->get_executor();
    auto result = Task<std::shared_ptr<NetworkInterceptHandler>>::make(ex, loc);

    // Create a shared_ptr to hold the result task to avoid use-after-free
    auto result_holder = std::make_shared<decltype(result)>(result);

    // Spawn coroutine to perform async initialization
    boost::asio::co_spawn(
        ex,
        [client, config = std::move(config), result_holder,
         loc]() mutable -> boost::asio::awaitable<void> {
            std::shared_ptr<NetworkInterceptHandler> handler_ptr;

            try {
                // Step 1: Call network.addIntercept
                auto intercept_id = co_await client->add_intercept(
                    config.phases, config.contexts, config.url_patterns, loc);

                // Step 2: Create handler instance
                auto handler = NetworkInterceptHandler(
                    client, std::move(config), std::move(intercept_id));

                // Step 3: Create shared_ptr ONCE to avoid use-after-move
                handler_ptr = std::make_shared<NetworkInterceptHandler>(
                    std::move(handler));

                // Create weak_ptr to break reference cycles in callbacks
                std::weak_ptr<NetworkInterceptHandler> handler_weak =
                    handler_ptr;

                // Step 4: Subscribe to events based on phases
                // IMPORTANT: Must await each subscription before continuing
                // to ensure handlers are registered before events arrive
                for (const auto &phase : handler_ptr->config_.phases) {
                    using enum types::network::InterceptPhase;

                    std::string event_name;
                    std::function<void(const boost::json::object &)> callback;

                    switch (phase) {
                    case BeforeRequestSent:
                        event_name = bidi::ids::events::net_beforeRequestSent;
                        callback = make_before_request_callback(handler_weak);
                        break;

                    case ResponseStarted:
                        event_name = bidi::ids::events::net_responseStarted;
                        callback = make_response_started_callback(handler_weak);
                        break;

                    case AuthRequired:
                        event_name = bidi::ids::events::net_authRequired;
                        callback = make_auth_required_callback(handler_weak);
                        break;
                    }

                    // CRITICAL: Await subscription completion before continuing
                    // This ensures the handler is registered before we move
                    // forward
                    try {
                        auto subscription =
                            co_await client->set_event_handler_subscription(
                                event_name, callback, loc);
                        if (!subscription) {
                            throw std::runtime_error(
                                "Failed to subscribe to event: " +
                                std::string(event_name));
                        }

                        // Store subscription for RAII cleanup
                        switch (phase) {
                        case BeforeRequestSent:
                            handler_ptr->before_request_sub_ = subscription;
                            break;
                        case ResponseStarted:
                            handler_ptr->response_started_sub_ = subscription;
                            break;
                        case AuthRequired:
                            handler_ptr->auth_required_sub_ = subscription;
                            break;
                        }
                    } catch (const std::exception &e) {
                        logging::log_error(std::format(
                            "Failed to register event handler for {}: {}",
                            event_name, e.what()));
                        result_holder->fail(std::current_exception());
                        co_return;
                    }
                }

                // Step 5: All handlers registered successfully - return handler
                result_holder->fulfill(handler_ptr);

            } catch (const std::exception &) {
                result_holder->fail(std::current_exception());
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
        logging::log_info(
            "NetworkInterceptHandler cleanup: no intercept to remove");
        cleanup_succeeded_.store(true, std::memory_order_relaxed);
        result.fulfill();
        return result;
    }

    logging::log_info(
        std::format("NetworkInterceptHandler cleanup: removing intercept {}",
                    intercept_id_));

    auto intercept = intercept_id_;
    auto remove_task = client_->remove_intercept(intercept, loc);

    return remove_task.map([this, intercept]() mutable {
        logging::log_info(
            std::format("network.removeIntercept succeeded for {}", intercept));
        intercept_id_.clear();
        cleanup_succeeded_.store(true, std::memory_order_relaxed);
    });
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
        // RAII cleanup: subscriptions are automatically cleaned up as
        // shared_ptr members are destroyed. This ensures event handlers are
        // unsubscribed.

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
                                             const std::exception_ptr &ep) {
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

    // First, invoke user callback if provided (for monitoring/custom logic)
    if (config_.before_request_handler) {
        auto custom_resolution = (*config_.before_request_handler)(params);
        if (custom_resolution) {
            // User provided a custom resolution, use it
            const auto &resolution = custom_resolution.value();
            using enum InterceptAction;
            switch (resolution.action) {
            case InterceptAction::Continue: {
                auto task = client_->continue_request(
                    params.base.request, resolution.body, resolution.cookies,
                    resolution.headers, resolution.method, resolution.url);
                task.finally([](auto &&...) {});
                break;
            }
            case InterceptAction::Fail: {
                auto task = client_->fail_request(params.base.request);
                task.finally([](auto &&...) {});
                break;
            }
            case InterceptAction::Provide:
                // ProvideResponse not applicable for beforeRequestSent phase
                logging::log_warning("InterceptAction::Provide not valid for "
                                     "beforeRequestSent phase");
                break;
            case InterceptAction::Ignore:
                // User wants to handle manually
                break;
            }
            return;
        }
    }

    // Apply policy to determine default action
    auto resolution = [this]() -> std::optional<RequestResolution> {
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
            // Custom policy with no handler = ignore (manual handling)
            return std::nullopt;
        }
        return std::nullopt;
    }();

    if (!resolution) {
        return; // No action - manual handling
    }

    // Execute appropriate command based on action
    // Note: For beforeRequestSent, params.base.request contains the RequestId
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

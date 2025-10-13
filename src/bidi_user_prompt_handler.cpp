// src/bidi_user_prompt_handler.cpp - User prompt handler JSON converters and
// RAII implementation
#include "bidi/client.hpp"
#include "bidi/logging.hpp"
#include "bidi/user_prompt_handler.hpp"
#include "bidi_methods.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/json.hpp>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

namespace bidi {

/**
 * @brief Convert JSON object to UserPromptOpenedParameters
 *
 * Extracts fields from browsingContext.userPromptOpened event params.
 * Follows defensive parsing: validates all required fields, gracefully handles
 * optional fields.
 *
 * @param obj JSON object from BiDi event
 * @return Strongly-typed parameters struct
 * @throws std::runtime_error if required fields are missing or invalid
 */
auto from_json_prompt_opened(const boost::json::object &obj)
    -> types::browsing_context::UserPromptOpenedParameters {
    types::browsing_context::UserPromptOpenedParameters params;

    // Required: context (BrowsingContextId)
    const auto *context_ptr = obj.if_contains("context");
    if (!context_ptr || !context_ptr->is_string()) {
        throw std::runtime_error(
            "UserPromptOpenedParameters: missing or invalid 'context' field");
    }
    params.context = std::string(context_ptr->as_string().c_str());

    // Required: handler (UserPromptAction)
    const auto *handler_ptr = obj.if_contains("handler");
    if (!handler_ptr || !handler_ptr->is_string()) {
        throw std::runtime_error(
            "UserPromptOpenedParameters: missing or invalid 'handler' field");
    }
    params.handler =
        boost::json::value_to<types::session::UserPromptAction>(*handler_ptr);

    // Required: message
    const auto *message_ptr = obj.if_contains("message");
    if (!message_ptr || !message_ptr->is_string()) {
        throw std::runtime_error(
            "UserPromptOpenedParameters: missing or invalid 'message' field");
    }
    params.message = std::string(message_ptr->as_string().c_str());

    // Required: type (UserPromptType)
    const auto *type_ptr = obj.if_contains("type");
    if (!type_ptr || !type_ptr->is_string()) {
        throw std::runtime_error(
            "UserPromptOpenedParameters: missing or invalid 'type' field");
    }
    params.type =
        boost::json::value_to<types::browsing_context::UserPromptType>(
            *type_ptr);

    // Optional: defaultValue (only for "prompt" type)
    const auto *default_value_ptr = obj.if_contains("defaultValue");
    if (default_value_ptr != nullptr && default_value_ptr->is_string()) {
        params.default_value =
            std::string(default_value_ptr->as_string().c_str());
    }

    return params;
}

/**
 * @brief Convert JSON object to UserPromptClosedParameters
 *
 * Extracts fields from browsingContext.userPromptClosed event params.
 * Follows defensive parsing: validates all required fields, gracefully handles
 * optional fields.
 *
 * @param obj JSON object from BiDi event
 * @return Strongly-typed parameters struct
 * @throws std::runtime_error if required fields are missing or invalid
 */
auto from_json_prompt_closed(const boost::json::object &obj)
    -> types::browsing_context::UserPromptClosedParameters {
    types::browsing_context::UserPromptClosedParameters params;

    // Required: context (BrowsingContextId)
    const auto *context_ptr = obj.if_contains("context");
    if (!context_ptr || !context_ptr->is_string()) {
        throw std::runtime_error(
            "UserPromptClosedParameters: missing or invalid 'context' field");
    }
    params.context = std::string(context_ptr->as_string().c_str());

    // Required: accepted (bool)
    const auto *accepted_ptr = obj.if_contains("accepted");
    if (!accepted_ptr || !accepted_ptr->is_bool()) {
        throw std::runtime_error(
            "UserPromptClosedParameters: missing or invalid 'accepted' field");
    }
    params.accepted = accepted_ptr->as_bool();

    // Required: type (UserPromptType)
    const auto *type_ptr = obj.if_contains("type");
    if (!type_ptr || !type_ptr->is_string()) {
        throw std::runtime_error(
            "UserPromptClosedParameters: missing or invalid 'type' field");
    }
    params.type =
        boost::json::value_to<types::browsing_context::UserPromptType>(
            *type_ptr);

    // Optional: userText
    const auto *user_text_ptr = obj.if_contains("userText");
    if (user_text_ptr != nullptr && user_text_ptr->is_string()) {
        params.user_text = std::string(user_text_ptr->as_string().c_str());
    }

    return params;
}

// ======================== UserPromptHandler Implementation
// ========================

/**
 * @brief Private constructor for UserPromptHandler
 *
 * Constructs handler with client, config, and subscription.
 * C.45: Use factory method (create) for public construction.
 *
 * @param client Shared pointer to Client (must outlive handler)
 * @param config Policy configuration
 * @param subscription RAII subscription handle (type-erased)
 */
UserPromptHandler::UserPromptHandler(std::shared_ptr<Client> client,
                                     UserPromptHandlerConfig config,
                                     Client::Subscription subscription)
    : client_(std::move(client)), config_(std::move(config)),
      subscription_(std::move(subscription)) {}

/**
 * @brief Factory method to create UserPromptHandler with async subscription
 *
 * TODO: Full implementation requires:
 * - Event ID constants in bidi/ids.hpp or bidi_methods.hpp
 * - Proper asyncx integration for lazy Task creation
 * - Complete Client::subscribe implementation
 *
 * This is a simplified placeholder that compiles.
 *
 * @param client Shared pointer to Client
 * @param config Policy configuration (default: accept_all)
 * @return asyncx::Async<UserPromptHandler> Lazy task that creates handler when
 * materialized
 */
auto UserPromptHandler::create(std::shared_ptr<Client> client,
                               UserPromptHandlerConfig config)
    -> Task<std::shared_ptr<UserPromptHandler>> {
    // Events to subscribe (use constants from bidi::ids::events)
    std::vector<std::string> events{
        std::string(bidi::ids::events::bc_userPromptOpened),
        std::string(bidi::ids::events::bc_userPromptClosed)};

    auto result =
        Task<std::shared_ptr<UserPromptHandler>>::make(client->get_executor());

    // Chain: subscribe → create handler → register opened → register closed
    client->subscribe(events).finally(
        [client, config,
         result](std::optional<Client::Subscription> sub_opt,
                 const std::optional<boost::system::error_code> &ec,
                 const std::exception_ptr &eptr) mutable {
            // Handle subscription errors
            if (eptr) {
                result.fail(eptr);
                return;
            }
            if (ec && *ec) {
                result.fail(std::make_exception_ptr(std::system_error(*ec)));
                return;
            }
            if (!sub_opt) {
                result.fail(std::make_exception_ptr(
                    std::runtime_error("subscribe returned no subscription")));
                return;
            }

            // Create handler with subscription (move-only type)
            auto handler_ptr = std::shared_ptr<UserPromptHandler>(
                new UserPromptHandler(client, config, std::move(*sub_opt)));
            auto weak_handler = std::weak_ptr<UserPromptHandler>(handler_ptr);

            // Register userPromptOpened handler (required) →  register closed
            // handler (optional) → return handler
            auto opened_callback = make_opened_event_callback(weak_handler);

            client
                ->set_event_handler_subscription(
                    bidi::ids::events::bc_userPromptOpened, opened_callback)
                .and_then(
                    [client, weak_handler, handler_ptr](
                        std::shared_ptr<bidi::core::BiDiSession::Subscription>
                            subs) {
                        handler_ptr->opened_subscription_ = std::move(subs);
                        auto closed_callback =
                            make_closed_event_callback(weak_handler);

                        return client->set_event_handler_subscription(
                            bidi::ids::events::bc_userPromptClosed,
                            closed_callback);
                    })
                .finally([handler_ptr, result](
                             std::optional<std::shared_ptr<
                                 bidi::core::BiDiSession::Subscription>>
                                 subs,
                             const std::optional<boost::system::error_code> &ec,
                             const std::exception_ptr &eptr) mutable {
                    if (subs) {
                        handler_ptr->closed_subscription_ = std::move(*subs);
                    }
                    if (eptr) {
                        result.fail(eptr);
                        return;
                    }
                    if (ec && *ec) {
                        result.fail(
                            std::make_exception_ptr(std::system_error(*ec)));
                        return;
                    }
                    // Successfully registered both handlers
                    result.fulfill(handler_ptr);
                });
        });

    return result;
}

/**
 * @brief Check if subscription is active
 *
 * Queries the type-erased subscription for active status.
 *
 * @return true if handler is subscribed to events, false otherwise
 */
auto UserPromptHandler::is_active() const -> bool {
    return subscription_.is_active();
}

/**
 * @brief Internal: apply policy to opened prompt
 *
 * Zero-cost policy dispatch via switch statement (no virtual calls).
 * Uses fire-and-forget pattern via .finally() for async prompt handling.
 *
 * @param params Strongly-typed prompt opened parameters
 *
 * Policy behavior:
 * - AcceptAll: Sends handleUserPrompt with accept=true
 * - DismissAll: Sends handleUserPrompt with accept=false
 * - IgnoreAll: No-op (manual handling required)
 * - Custom: Invokes callback, sends resolution if provided
 *
 * @see ES.28 - Use switch for enums (exhaustive handling)
 */
void UserPromptHandler::handle_prompt(
    const types::browsing_context::UserPromptOpenedParameters &params) {
    using types::browsing_context::UserPromptResolution;
    logging::log_debug(
        "UserPromptHandler: Handling prompt in context '" + params.context +
        "' of type " +
        std::string(types::browsing_context::to_string(params.type)) +
        " with policy " +
        std::string(
            (config_.policy == UserPromptPolicy::AcceptAll    ? "AcceptAll"
             : config_.policy == UserPromptPolicy::DismissAll ? "DismissAll"
             : config_.policy == UserPromptPolicy::IgnoreAll  ? "IgnoreAll"
                                                              : "Custom")));

    // Zero-cost policy dispatch (switch statement, no virtual calls)
    auto resolution = [&]() -> std::optional<UserPromptResolution> {
        switch (config_.policy) {
        case UserPromptPolicy::AcceptAll:
            return UserPromptResolution{.accept = true,
                                        .user_text = std::nullopt};
        case UserPromptPolicy::DismissAll:
            return UserPromptResolution{.accept = false,
                                        .user_text = std::nullopt};
        case UserPromptPolicy::IgnoreAll:
            return std::nullopt;
        case UserPromptPolicy::Custom:
            if (!config_.custom_handler) {
                return std::nullopt;
            }
            return (*config_.custom_handler)(params);
        }
        return std::nullopt; // Unreachable, but satisfies compiler
    }();

    // Send handleUserPrompt command if resolution provided
    if (resolution && client_) {
        auto task = client_->handle_user_prompt(
            params.context, resolution->accept, resolution->user_text);
        // Fire-and-forget: materialize task with .finally() terminal
        task.finally([](const std::optional<boost::system::error_code> &ec,
                        const std::exception_ptr &eptr) {
            if (ec) {
                logging::log_error(std::format(
                    "UserPromptHandler: Failed to send handleUserPrompt: {}",
                    ec->message()));
            }
            if (eptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception &e) {
                    logging::log_error(std::format(
                        "UserPromptHandler: Exception in handleUserPrompt: {}",
                        e.what()));
                } catch (...) {
                    logging::log_error("UserPromptHandler: Unknown exception "
                                       "in handleUserPrompt");
                }
            }
        });
    }
}

/**
 * @brief Handle userPromptClosed event
 *
 * Logs prompt closure for observability. Can be extended for metrics.
 *
 * @param params Strongly-typed prompt closed parameters
 */
void UserPromptHandler::on_prompt_closed(
    const types::browsing_context::UserPromptClosedParameters &params) {

    // Log prompt closure for observability
    logging::log_info(std::format(
        "UserPromptHandler: Prompt closed in context '{}', type: {}, accepted: "
        "{}{}",
        params.context, types::browsing_context::to_string(params.type),
        params.accepted,
        params.user_text ? std::format(", userText: '{}'", *params.user_text)
                         : ""));
}

/**
 * @brief Register event handler as Task (wraps awaitable)
 *
 * Converts Client::set_event_handler (which returns awaitable<void>) to
 * Task<void> for use with asyncx composition operators like .and_then() and
 * .map().
 *
 * @param client Client instance
 * @param event_name Event identifier (from bidi::ids::events)
 * @param callback Event handler callback
 * @return Task<void> that completes when handler is registered
 */
auto UserPromptHandler::register_event_as_task(
    std::shared_ptr<Client> client, std::string_view event_name,
    std::function<void(const boost::json::object &)> callback) -> Task<void> {

    auto result = Task<void>::make(client->get_executor());

    boost::asio::co_spawn(
        client->get_executor(),
        [client, event_name,
         callback = std::move(callback)]() -> boost::asio::awaitable<void> {
            co_await client->set_event_handler(event_name, callback);
        }(),
        [result](std::exception_ptr eptr) mutable {
            if (eptr) {
                result.fail(eptr);
            } else {
                result.fulfill();
            }
        });

    return result;
}

/**
 * @brief Create event handler callback for userPromptOpened
 *
 * Extracts lambda logic to reduce cognitive complexity in create() method.
 * Handles JSON parsing and delegates to handle_prompt().
 *
 * @param weak_handler Weak pointer to handler instance (prevents circular ref)
 * @return Event handler callback function
 */
auto UserPromptHandler::make_opened_event_callback(
    std::weak_ptr<UserPromptHandler> weak_handler)
    -> std::function<void(const boost::json::object &)> {

    return [weak_handler =
                std::move(weak_handler)](const boost::json::object &params) {
        logging::log_debug(
            "UserPromptHandler: Received userPromptOpened event");
        if (auto handler = weak_handler.lock()) {
            try {
                auto parsed = from_json_prompt_opened(params);
                // Post to client's executor to call member safely
                boost::asio::post(handler->client_->get_executor(),
                                  [handler, parsed]() mutable {
                                      handler->handle_prompt(parsed);
                                  });
            } catch (const std::exception &e) {
                logging::log_error(std::format(
                    "UserPromptHandler: Failed to parse opened event: {}",
                    e.what()));
            } catch (...) {
                logging::log_error(
                    "UserPromptHandler: Unknown error parsing opened event");
            }
        }
    };
}

/**
 * @brief Create event handler callback for userPromptClosed
 *
 * Extracts lambda logic to reduce cognitive complexity in create() method.
 * Handles JSON parsing and delegates to on_prompt_closed().
 *
 * @param weak_handler Weak pointer to handler instance (prevents circular ref)
 * @return Event handler callback function
 */
auto UserPromptHandler::make_closed_event_callback(
    std::weak_ptr<UserPromptHandler> weak_handler)
    -> std::function<void(const boost::json::object &)> {

    return [weak_handler =
                std::move(weak_handler)](const boost::json::object &params) {
        logging::log_debug(
            "UserPromptHandler: Received userPromptClosed event");
        if (auto handler = weak_handler.lock()) {
            try {
                auto parsed = from_json_prompt_closed(params);
                boost::asio::post(handler->client_->get_executor(),
                                  [handler, parsed]() mutable {
                                      handler->on_prompt_closed(parsed);
                                  });
            } catch (const std::exception &e) {
                logging::log_error(std::format(
                    "UserPromptHandler: Failed to parse closed event: {}",
                    e.what()));
            } catch (...) {
                logging::log_error(
                    "UserPromptHandler: Unknown error parsing closed event");
            }
        }
    };
}

} // namespace bidi

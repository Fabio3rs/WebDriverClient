#pragma once
/**
 * @file user_prompt_handler.hpp
 * @brief Policy-based user prompt handling for WebDriver BiDi
 *
 * Provides three levels of abstraction for browser user prompt handling:
 * 1. Policy system (this file): Zero-cost policy-based dispatch
 * 2. RAII guard: Automatic subscription management
 * 3. Convenience APIs: Strongly-typed event callbacks
 *
 * Architectural rationale:
 * - Zero-cost abstractions: Policy dispatch via switch (no virtual calls)
 * - C.45 compliance: Factory methods for construction
 * - F.51 compliance: Default arguments over overloading
 * - ES.20 compliance: All members explicitly initialized
 * - Type safety: Strongly-typed UserPromptResolution instead of raw JSON
 *
 * @see USER_PROMPT_HANDLER_ARCHITECTURE.md
 */

#include "asyncx.hpp"
#include "bidi/client.hpp"
#include "bidi/types/browsing_context.hpp"
#include <boost/json/object.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace bidi {

// Forward declaration
class Client;

/**
 * @brief User prompt handling policy
 *
 * Zero-cost dispatch mechanism for common prompt handling patterns.
 * Switch-based dispatch avoids virtual call overhead.
 *
 * @see F.51 - Prefer default arguments over overloading
 * @see ES.20 - Always initialize objects
 */
enum class UserPromptPolicy : std::uint8_t {
    AcceptAll,  ///< Accept all prompts (alerts, confirms), provide empty text
                ///< for prompts
    DismissAll, ///< Dismiss all prompts
    IgnoreAll,  ///< Don't handle automatically (manual handling required)
    Custom      ///< Use callback for custom logic
};

/**
 * @brief User prompt handler callback signature
 *
 * Takes opened parameters, returns action to perform.
 * Return std::nullopt to skip handling (leave prompt open).
 *
 * @param params Strongly-typed prompt opened event parameters
 * @return Resolution to apply, or std::nullopt to skip handling
 *
 * Example:
 * @code
 * UserPromptCallback callback = [](const auto& params) {
 *     using enum bidi::types::browsing_context::UserPromptType;
 *     switch (params.type) {
 *         case Alert:
 *             return UserPromptResolution{};  // Accept
 *         case Confirm:
 *             return UserPromptResolution{.accept = false};  // Dismiss
 *         case Prompt:
 *             return UserPromptResolution{.accept = true, .user_text =
 * "input"}; default: return std::nullopt;  // Skip
 *     }
 * };
 * @endcode
 */
using UserPromptCallback =
    std::function<std::optional<types::browsing_context::UserPromptResolution>(
        const types::browsing_context::UserPromptOpenedParameters &)>;

/**
 * @brief User prompt handler configuration
 *
 * Combines policy with optional custom callback.
 * Factory methods provide ergonomic construction following C.45.
 *
 * @see C.45 - Prefer factory functions for construction
 * @see ES.20 - Always initialize objects
 *
 * Example:
 * @code
 * // Accept all prompts automatically
 * auto config = UserPromptHandlerConfig::accept_all();
 *
 * // Custom logic per prompt type
 * auto config = UserPromptHandlerConfig::custom(
 *     [](const auto& params) { return some_resolution; });
 * @endcode
 */
struct UserPromptHandlerConfig {
    UserPromptPolicy policy{UserPromptPolicy::AcceptAll};
    std::optional<UserPromptCallback>
        custom_handler; ///< Only for Custom policy

    // Factory methods (C.45 - prefer factory functions for construction)

    /**
     * @brief Create config that accepts all prompts automatically
     * @return Config with AcceptAll policy
     */
    [[nodiscard]] static auto accept_all() -> UserPromptHandlerConfig {
        return UserPromptHandlerConfig{.policy = UserPromptPolicy::AcceptAll,
                                       .custom_handler = std::nullopt};
    }

    /**
     * @brief Create config that dismisses all prompts automatically
     * @return Config with DismissAll policy
     */
    [[nodiscard]] static auto dismiss_all() -> UserPromptHandlerConfig {
        return UserPromptHandlerConfig{.policy = UserPromptPolicy::DismissAll,
                                       .custom_handler = std::nullopt};
    }

    /**
     * @brief Create config that ignores all prompts (manual handling required)
     * @return Config with IgnoreAll policy
     */
    [[nodiscard]] static auto ignore_all() -> UserPromptHandlerConfig {
        return UserPromptHandlerConfig{.policy = UserPromptPolicy::IgnoreAll,
                                       .custom_handler = std::nullopt};
    }

    /**
     * @brief Create config with custom callback logic
     * @param callback Function to invoke for each prompt
     * @return Config with Custom policy and provided callback
     */
    [[nodiscard]] static auto custom(UserPromptCallback callback)
        -> UserPromptHandlerConfig {
        return UserPromptHandlerConfig{.policy = UserPromptPolicy::Custom,
                                       .custom_handler = std::move(callback)};
    }
};

// ======================== JSON Conversion Helpers ========================

/**
 * @brief Convert JSON object to UserPromptOpenedParameters
 *
 * Extracts fields from browsingContext.userPromptOpened event params.
 * Defensive parsing: validates all required fields, gracefully handles optional
 * fields.
 *
 * @param obj JSON object from BiDi event
 * @return Strongly-typed parameters struct
 * @throws std::runtime_error if required fields are missing or invalid
 */
[[nodiscard]] auto from_json_prompt_opened(const boost::json::object &obj)
    -> types::browsing_context::UserPromptOpenedParameters;

/**
 * @brief Convert JSON object to UserPromptClosedParameters
 *
 * Extracts fields from browsingContext.userPromptClosed event params.
 * Defensive parsing: validates all required fields, gracefully handles optional
 * fields.
 *
 * @param obj JSON object from BiDi event
 * @return Strongly-typed parameters struct
 * @throws std::runtime_error if required fields are missing or invalid
 */
[[nodiscard]] auto from_json_prompt_closed(const boost::json::object &obj)
    -> types::browsing_context::UserPromptClosedParameters;

// ======================== RAII UserPromptHandler ========================

/**
 * @brief RAII guard for automatic user prompt handling
 *
 * Subscribes to browsingContext.userPromptOpened event and applies policy-based
 * handling. Automatically unsubscribes on destruction (RAII).
 *
 * Architectural guarantees:
 * - C.21: Move-only semantics (subscription is move-only resource)
 * - C.31: Destructor never throws (RAII cleanup)
 * - C.45: Factory method for construction (private constructor)
 * - F.15: Return values over out-parameters (Task<UserPromptHandler>)
 * - ES.20: All members explicitly initialized
 *
 * Zero-cost abstractions:
 * - Policy dispatch via switch statement (no virtual dispatch)
 * - Uses existing Client::Subscription (no additional allocations)
 * - Fire-and-forget prompt handling via .finally() terminal
 *
 * Example usage:
 * @code
 * auto client = co_await bidi::Client::connect(io, url)();
 * auto prompt_handler = co_await bidi::UserPromptHandler::create(
 *     client, bidi::UserPromptHandlerConfig::accept_all())();
 * // All prompts automatically handled until prompt_handler destroyed
 * co_await client->navigate(ctx, "http://example.com/alerts")();
 * // RAII: prompt_handler auto-unsubscribes when out of scope
 * @endcode
 *
 * @see USER_PROMPT_HANDLER_ARCHITECTURE.md
 * @see C.21 - Prefer RAII and move semantics for resources
 * @see C.31 - Destructors must not throw
 * @see C.45 - Prefer factory functions for non-trivial initialization
 */
class UserPromptHandler {
  public:
    /**
     * @brief Factory method to create UserPromptHandler with async
     * subscription
     *
     * Subscribes to browsingContext.userPromptOpened event and returns RAII
     * guard. Returns Task<UserPromptHandler> for lazy evaluation (must be
     * materialized).
     *
     * @param client Shared pointer to Client (must outlive handler)
     * @param config Policy configuration (default: accept_all)
     * @return Task<UserPromptHandler> Lazy task that creates handler when
     * materialized
     *
     * @see F.15 - Return values over out-parameters
     * @see C.45 - Prefer factory functions for construction
     *
     * Usage:
     * @code
     * auto handler = co_await UserPromptHandler::create(client)();
     * // or with custom config:
     * auto handler = co_await UserPromptHandler::create(
     *     client, UserPromptHandlerConfig::dismiss_all())();
     * @endcode
     */
    [[nodiscard]] static auto create(
        std::shared_ptr<Client> client,
        UserPromptHandlerConfig config = UserPromptHandlerConfig::accept_all())
        -> Task<std::shared_ptr<UserPromptHandler>>;

    /**
     * @brief Destructor automatically unsubscribes (RAII)
     *
     * @see C.31 - Destructors must not throw
     */
    ~UserPromptHandler() noexcept = default;

    /**
     * @brief Move constructor (C.21 - move-only semantics)
     */
    UserPromptHandler(UserPromptHandler &&) noexcept = default;

    /**
     * @brief Move assignment operator (C.21 - move-only semantics)
     */
    auto operator=(UserPromptHandler &&) noexcept
        -> UserPromptHandler & = default;

    /**
     * @brief Deleted copy constructor (move-only resource)
     */
    UserPromptHandler(const UserPromptHandler &) = delete;

    /**
     * @brief Deleted copy assignment (move-only resource)
     */
    auto operator=(const UserPromptHandler &) -> UserPromptHandler & = delete;

    /**
     * @brief Update policy configuration at runtime
     *
     * Thread-safe: config updates are serialized via strand.
     *
     * @param new_config New policy configuration to apply
     *
     * Usage:
     * @code
     * handler.set_policy(UserPromptHandlerConfig::dismiss_all());
     * @endcode
     */
    void set_policy(UserPromptHandlerConfig new_config) {
        config_ = std::move(new_config);
    }

    /**
     * @brief Check if subscription is active
     *
     * @return true if handler is subscribed to events, false otherwise
     *
     * @see [[nodiscard]] - Result should not be ignored
     */
    [[nodiscard]] auto is_active() const -> bool;

  private:
    /**
     * @brief Private constructor (C.45 - use factory method)
     *
     * @param client Shared pointer to Client
     * @param config Policy configuration
     * @param subscription RAII subscription handle (Client::Subscription)
     */
    UserPromptHandler(std::shared_ptr<Client> client,
                      UserPromptHandlerConfig config,
                      Client::Subscription subscription);

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
    void handle_prompt(
        const types::browsing_context::UserPromptOpenedParameters &params);

    /**
     * @brief Internal: handle userPromptClosed event
     *
     * Logs prompt closure for observability and metrics.
     *
     * @param params Strongly-typed prompt closed event parameters
     */
    void on_prompt_closed(
        const types::browsing_context::UserPromptClosedParameters &params);

    /**
     * @brief Helper: Create event handler callback for userPromptOpened
     *
     * Reduces cognitive complexity by extracting lambda logic.
     *
     * @param weak_handler Weak pointer to handler instance
     * @return Event handler callback
     */
    [[nodiscard]] static auto
    make_opened_event_callback(std::weak_ptr<UserPromptHandler> weak_handler)
        -> std::function<void(const boost::json::object &)>;

    /**
     * @brief Helper: Create event handler callback for userPromptClosed
     *
     * Reduces cognitive complexity by extracting lambda logic.
     *
     * @param weak_handler Weak pointer to handler instance
     * @return Event handler callback
     */
    [[nodiscard]] static auto
    make_closed_event_callback(std::weak_ptr<UserPromptHandler> weak_handler)
        -> std::function<void(const boost::json::object &)>;

    /**
     * @brief Helper: Register event handler as Task (wraps awaitable)
     *
     * Converts Client::set_event_handler (awaitable) to Task for composition.
     *
     * @param client Client instance
     * @param event_name Event identifier (from bidi::ids::events)
     * @param callback Event handler callback
     * @return Task that completes when handler is registered
     */
    [[nodiscard]] static auto register_event_as_task(
        std::shared_ptr<Client> client, std::string_view event_name,
        std::function<void(const boost::json::object &)> callback)
        -> Task<void>;

    std::shared_ptr<Client> client_;    ///< Client reference (shared ownership)
    UserPromptHandlerConfig config_{};  ///< Current policy configuration
    Client::Subscription subscription_; ///< RAII subscription handle

    std::shared_ptr<bidi::core::BiDiSession::Subscription> opened_subscription_;
    std::shared_ptr<bidi::core::BiDiSession::Subscription> closed_subscription_;
};

} // namespace bidi

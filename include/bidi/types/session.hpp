#pragma once
/**
 * @file types/session.hpp
 * @brief W3C WebDriver BiDi session module types
 *
 * Types for session capabilities, proxy configuration, and subscription
 * management. Implements types from W3C BiDi spec session module.
 *
 * @see https://w3c.github.io/webdriver-bidi/#module-session
 */

#include "bidi/types/core.hpp"
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bidi::types::session {

// ==================== Proxy Configuration ====================

/**
 * @brief SOCKS proxy configuration
 */
struct SocksProxyConfiguration {
    std::string host;
    std::uint16_t port{0};
    std::uint8_t version{5}; // SOCKS version (4 or 5)

    auto operator==(const SocksProxyConfiguration &) const -> bool = default;
};

/**
 * @brief Proxy type enumeration
 */
enum class ProxyType : std::uint8_t { Autodetect, Direct, Manual, Pac, System };

[[nodiscard]] constexpr auto
to_string(ProxyType type) noexcept -> std::string_view {
    using enum ProxyType;
    switch (type) {
    case Autodetect:
        return "autodetect";
    case Direct:
        return "direct";
    case Manual:
        return "manual";
    case Pac:
        return "pac";
    case System:
        return "system";
    default:
        return "direct";
    }
}

[[nodiscard]] constexpr auto
parse_proxy_type(std::string_view text) noexcept -> std::optional<ProxyType> {
    using enum ProxyType;
    if (text == "autodetect")
        return Autodetect;
    if (text == "direct")
        return Direct;
    if (text == "manual")
        return Manual;
    if (text == "pac")
        return Pac;
    if (text == "system")
        return System;
    return std::nullopt;
}

/**
 * @brief Proxy configuration for session
 */
struct ProxyConfiguration {
    ProxyType type{ProxyType::Direct};
    std::optional<std::string> http_proxy;
    std::optional<std::string> ssl_proxy;
    std::optional<SocksProxyConfiguration> socks;
    std::optional<std::vector<std::string>> no_proxy;
    std::optional<std::string> pac_url;

    auto operator==(const ProxyConfiguration &) const -> bool = default;
};

// ==================== User Prompt Handling ====================

/**
 * @brief User prompt action enumeration
 */
enum class UserPromptAction : std::uint8_t { Accept, Dismiss, Ignore };

[[nodiscard]] constexpr auto
to_string(UserPromptAction action) noexcept -> std::string_view {
    using enum UserPromptAction;
    switch (action) {
    case Accept:
        return "accept";
    case Dismiss:
        return "dismiss";
    case Ignore:
        return "ignore";
    default:
        return "dismiss";
    }
}

[[nodiscard]] constexpr auto parse_user_prompt_action(
    std::string_view text) noexcept -> std::optional<UserPromptAction> {
    using enum UserPromptAction;
    if (text == "accept")
        return Accept;
    if (text == "dismiss")
        return Dismiss;
    if (text == "ignore")
        return Ignore;
    return std::nullopt;
}

/**
 * @brief User prompt handler configuration
 */
struct UserPromptHandler {
    std::optional<UserPromptAction> alert;
    std::optional<UserPromptAction> before_unload;
    std::optional<UserPromptAction> confirm;
    std::optional<UserPromptAction> prompt;
    std::optional<UserPromptAction> file;
    std::optional<UserPromptAction> default_action;

    auto operator==(const UserPromptHandler &) const -> bool = default;
};

// ==================== Capabilities ====================

/**
 * @brief Single capability request
 */
struct CapabilityRequest {
    std::optional<bool> accept_insecure_certs;
    std::optional<std::string> browser_name;
    std::optional<std::string> browser_version;
    std::optional<std::string> platform_name;
    std::optional<ProxyConfiguration> proxy;
    std::optional<UserPromptHandler> unhandled_prompt_behavior;
    // Extension point for additional capabilities
    std::optional<boost::json::object> additional_capabilities;

    auto operator==(const CapabilityRequest &) const -> bool = default;
};

/**
 * @brief Capabilities request for session creation
 */
struct CapabilitiesRequest {
    std::optional<CapabilityRequest> always_match;
    std::optional<std::vector<CapabilityRequest>> first_match;

    auto operator==(const CapabilitiesRequest &) const -> bool = default;
};

// ==================== Subscription Management ====================

/**
 * @brief Subscription identifier (opaque string)
 */
using SubscriptionId = std::string;

/**
 * @brief Subscription request parameters
 */
struct SubscriptionRequest {
    std::vector<std::string> events;
    std::optional<std::vector<std::string>> contexts;
    std::optional<std::vector<std::string>> user_contexts;

    auto operator==(const SubscriptionRequest &) const -> bool = default;
};

/**
 * @brief Unsubscribe by subscription ID
 */
struct UnsubscribeByIdRequest {
    std::vector<SubscriptionId> subscriptions;

    auto operator==(const UnsubscribeByIdRequest &) const -> bool = default;
};

/**
 * @brief Unsubscribe by event filter
 */
struct UnsubscribeByFilterRequest {
    std::vector<std::string> events;
    std::optional<std::vector<std::string>> contexts;
    std::optional<std::vector<std::string>> user_contexts;

    auto operator==(const UnsubscribeByFilterRequest &) const -> bool = default;
};

} // namespace bidi::types::session

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// ProxyType serialization
inline void tag_invoke(value_from_tag, value &jv,
                       bidi::types::session::ProxyType type) {
    jv = bidi::types::session::to_string(type);
}

inline auto tag_invoke(value_to_tag<bidi::types::session::ProxyType>,
                       const value &jv) -> bidi::types::session::ProxyType {
    auto text = value_to<std::string_view>(jv);
    auto type = bidi::types::session::parse_proxy_type(text);
    if (!type) {
        throw std::runtime_error("Invalid proxy type");
    }
    return *type;
}

// UserPromptAction serialization
inline void tag_invoke(value_from_tag, value &jv,
                       bidi::types::session::UserPromptAction action) {
    jv = bidi::types::session::to_string(action);
}

inline auto
tag_invoke(value_to_tag<bidi::types::session::UserPromptAction>,
           const value &jv) -> bidi::types::session::UserPromptAction {
    auto text = value_to<std::string_view>(jv);
    auto action = bidi::types::session::parse_user_prompt_action(text);
    if (!action) {
        throw std::runtime_error("Invalid user prompt action");
    }
    return *action;
}

} // namespace boost::json

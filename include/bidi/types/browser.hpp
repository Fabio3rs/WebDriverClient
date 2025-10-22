#pragma once
/**
 * @file types/browser.hpp
 * @brief W3C WebDriver BiDi browser module types
 *
 * Types for browser windows, user contexts, and client window management.
 * Implements types from W3C BiDi spec browser module.
 *
 * @see https://w3c.github.io/webdriver-bidi/#module-browser
 */

#include <cstdint>
#include <optional>
#include <string>

namespace bidi::types::browser {

// ==================== Identifiers ====================

using ClientWindow = std::string;
using UserContext = std::string;

// ==================== Client Window State ====================

/**
 * @brief Client window state
 */
enum class ClientWindowState : std::uint8_t {
    Fullscreen,
    Maximized,
    Minimized,
    Normal
};

[[nodiscard]] constexpr auto to_string(ClientWindowState value) noexcept
    -> std::string_view {
    using enum ClientWindowState;
    switch (value) {
    case Fullscreen:
        return "fullscreen";
    case Maximized:
        return "maximized";
    case Minimized:
        return "minimized";
    case Normal:
        return "normal";
    default:
        return "normal";
    }
}

[[nodiscard]] constexpr auto
parse_client_window_state(std::string_view text) noexcept
    -> std::optional<ClientWindowState> {
    using enum ClientWindowState;
    if (text == "fullscreen") {
        return Fullscreen;
    }
    if (text == "maximized") {
        return Maximized;
    }
    if (text == "minimized") {
        return Minimized;
    }
    if (text == "normal") {
        return Normal;
    }
    return std::nullopt;
}

// ==================== Client Window Info ====================

/**
 * @brief Client window information
 */
struct ClientWindowInfo {
    bool active{false};
    ClientWindow client_window;
    std::uint64_t height{0};
    ClientWindowState state{ClientWindowState::Normal};
    std::uint64_t width{0};
    std::int64_t x{0};
    std::int64_t y{0};

    auto operator==(const ClientWindowInfo &) const -> bool = default;
};

// ==================== User Context Info ====================

/**
 * @brief User context information
 */
struct UserContextInfo {
    UserContext user_context;

    auto operator==(const UserContextInfo &) const -> bool = default;
};

} // namespace bidi::types::browser

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// ClientWindowState serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::browser::ClientWindowState state) {
    jv = bidi::types::browser::to_string(state);
}

inline auto
tag_invoke(value_to_tag<bidi::types::browser::ClientWindowState> /*unused*/,
           const value &jv) -> bidi::types::browser::ClientWindowState {
    auto text = value_to<std::string_view>(jv);
    auto state = bidi::types::browser::parse_client_window_state(text);
    if (!state) {
        throw std::runtime_error("Invalid ClientWindowState value");
    }
    return *state;
}

// ClientWindowInfo serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::browser::ClientWindowInfo &info) {
    object obj;
    obj["active"] = info.active;
    obj["clientWindow"] = info.client_window;
    obj["height"] = info.height;
    obj["state"] = bidi::types::browser::to_string(info.state);
    obj["width"] = info.width;
    obj["x"] = info.x;
    obj["y"] = info.y;
    jv = std::move(obj);
}

// UserContextInfo serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::browser::UserContextInfo &info) {
    object obj;
    obj["userContext"] = info.user_context;
    jv = std::move(obj);
}

} // namespace boost::json

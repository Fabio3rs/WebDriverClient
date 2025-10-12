#pragma once
/**
 * @file types/log.hpp
 * @brief W3C WebDriver BiDi log module types
 *
 * Types for log entries, console logging, and JavaScript exceptions.
 * Implements types from W3C BiDi spec log module.
 *
 * @see https://w3c.github.io/webdriver-bidi/#module-log
 */

#include "bidi/types/core.hpp"
#include "bidi/types/script.hpp" // For RealmInfo
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bidi::types::log {

/**
 * @brief Log level (migrated from commands, kept for compatibility)
 */
enum class Level : std::uint8_t { Debug, Info, Warn, Error };

[[nodiscard]] constexpr auto
to_string(Level level) noexcept -> std::string_view {
    using enum Level;
    switch (level) {
    case Debug:
        return "debug";
    case Info:
        return "info";
    case Warn:
        return "warn";
    case Error:
        return "error";
    default:
        return "info";
    }
}

[[nodiscard]] constexpr auto
parse_level(std::string_view text) noexcept -> std::optional<Level> {
    using enum Level;
    if (text == "debug")
        return Debug;
    if (text == "info")
        return Info;
    if (text == "warn")
        return Warn;
    if (text == "error")
        return Error;
    return std::nullopt;
}

/**
 * @brief Console log entry
 */
struct ConsoleLogEntry {
    std::string method; // "log", "warn", "error", etc.
    std::vector<boost::json::value>
        args; // Simplified - full spec uses script::RemoteValue
    Level level;
    std::string text;
    std::uint64_t timestamp_ms{0};
    std::optional<script::RealmInfo> realm;

    auto operator==(const ConsoleLogEntry &) const -> bool = default;
};

/**
 * @brief JavaScript exception log entry
 */
struct JavaScriptLogEntry {
    Level level;
    std::string text;
    std::uint64_t timestamp_ms{0};
    std::optional<script::RealmInfo> realm;
    std::optional<std::string> stack_trace;

    auto operator==(const JavaScriptLogEntry &) const -> bool = default;
};

} // namespace bidi::types::log

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// Level serialization
inline void tag_invoke(value_from_tag, value &jv,
                       bidi::types::log::Level level) {
    jv = bidi::types::log::to_string(level);
}

inline auto tag_invoke(value_to_tag<bidi::types::log::Level>,
                       const value &jv) -> bidi::types::log::Level {
    auto text = value_to<std::string_view>(jv);
    auto level = bidi::types::log::parse_level(text);
    if (!level) {
        throw std::runtime_error("Invalid log level");
    }
    return *level;
}

} // namespace boost::json

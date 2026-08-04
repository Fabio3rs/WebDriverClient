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
#include <variant>
#include <vector>

#ifdef DEBUG_LOG_DESERIALIZATION
#include <iostream> // For std::cerr debug output
#endif

namespace bidi::types::log {

/**
 * @brief Log level (migrated from commands, kept for compatibility)
 */
enum class Level : std::uint8_t { Debug, Info, Warn, Error };

[[nodiscard]] constexpr auto to_string(Level level) noexcept
    -> std::string_view {
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

[[nodiscard]] constexpr auto parse_level(std::string_view text) noexcept
    -> std::optional<Level> {
    using enum Level;
    if (text == "debug") {
        return Debug;
    }
    if (text == "info") {
        return Info;
    }
    if (text == "warn") {
        return Warn;
    }
    if (text == "error") {
        return Error;
    }
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

/**
 * @brief Log entry union type
 *
 * W3C Spec: log.Entry = log.ConsoleLogEntry | log.JavaScriptLogEntry
 *
 * Discriminated by "type" field in JSON:
 * - "console" → ConsoleLogEntry
 * - "javascript" → JavaScriptLogEntry
 *
 * NOTE: This is a struct wrapper (not a type alias) to prevent Boost.JSON's
 * built-in std::variant support from interfering with our custom discriminator
 * logic that checks the "type" field.
 */
struct LogEntry {
    std::variant<ConsoleLogEntry, JavaScriptLogEntry> value;

    // Constructors
    LogEntry() = default;

    template <typename T>
        requires std::same_as<std::decay_t<T>, ConsoleLogEntry> ||
                 std::same_as<std::decay_t<T>, JavaScriptLogEntry>
    LogEntry(T &&v) : value(std::forward<T>(v)) {}

    // Comparison
    auto operator==(const LogEntry &) const -> bool = default;
};

} // namespace bidi::types::log

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// Level serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::log::Level level) {
    jv = bidi::types::log::to_string(level);
}

inline auto tag_invoke(value_to_tag<bidi::types::log::Level> /*unused*/,
                       const value &jv) -> bidi::types::log::Level {
    auto text = value_to<std::string_view>(jv);
    auto level = bidi::types::log::parse_level(text);
    if (!level) {
        throw std::runtime_error("Invalid log level");
    }
    return *level;
}

// ConsoleLogEntry serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::log::ConsoleLogEntry &entry) {
    jv = {{"type", "console"},
          {"method", entry.method},
          {"args", value_from(entry.args)},
          {"level", bidi::types::log::to_string(entry.level)},
          {"text", entry.text},
          {"timestamp", entry.timestamp_ms}};

    // TODO: Add realm serialization when script::RealmInfo has Boost.JSON
    // support if (entry.realm) {
    //     jv.as_object()["realm"] = value_from(*entry.realm);
    // }
}

inline auto
tag_invoke(value_to_tag<bidi::types::log::ConsoleLogEntry> /*unused*/,
           const value &jv) -> bidi::types::log::ConsoleLogEntry {
    const auto &obj = jv.as_object();
    bidi::types::log::ConsoleLogEntry entry;

#ifdef DEBUG_LOG_DESERIALIZATION
    std::cerr << "ConsoleLogEntry deserialization started\n";
    std::cerr << "JSON keys: ";
    for (const auto &[key, val] : obj) {
        std::cerr << key << " ";
    }
    std::cerr << "\n";
#endif

    entry.method = value_to<std::string>(obj.at("method"));
    entry.args = value_to<std::vector<boost::json::value>>(obj.at("args"));
    entry.level = value_to<bidi::types::log::Level>(obj.at("level"));
    entry.text = value_to<std::string>(obj.at("text"));
    entry.timestamp_ms = value_to<std::uint64_t>(obj.at("timestamp"));

    // TODO: Add realm deserialization when script::RealmInfo has Boost.JSON
    // support if (obj.contains("realm")) {
    //     entry.realm =
    //         value_to<bidi::types::script::RealmInfo>(obj.at("realm"));
    // }

#ifdef DEBUG_LOG_DESERIALIZATION
    std::cerr << "ConsoleLogEntry deserialization completed successfully\n";
#endif

    return entry;
}

// JavaScriptLogEntry serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::log::JavaScriptLogEntry &entry) {
    jv = {{"type", "javascript"},
          {"level", bidi::types::log::to_string(entry.level)},
          {"text", entry.text},
          {"timestamp", entry.timestamp_ms}};

    // TODO: Add realm serialization when script::RealmInfo has Boost.JSON
    // support if (entry.realm) {
    //     jv.as_object()["realm"] = value_from(*entry.realm);
    // }
    if (entry.stack_trace) {
        jv.as_object()["stackTrace"] = *entry.stack_trace;
    }
}

inline auto
tag_invoke(value_to_tag<bidi::types::log::JavaScriptLogEntry> /*unused*/,
           const value &jv) -> bidi::types::log::JavaScriptLogEntry {
    const auto &obj = jv.as_object();
    bidi::types::log::JavaScriptLogEntry entry;

#ifdef DEBUG_LOG_DESERIALIZATION
    std::cerr << "JavaScriptLogEntry deserialization started\n";
    std::cerr << "JSON keys: ";
    for (const auto &[key, val] : obj) {
        std::cerr << key << " ";
    }
    std::cerr << "\n";
#endif

    entry.level = value_to<bidi::types::log::Level>(obj.at("level"));
    entry.text = value_to<std::string>(obj.at("text"));
    entry.timestamp_ms = value_to<std::uint64_t>(obj.at("timestamp"));

    // TODO: Add realm deserialization when script::RealmInfo has Boost.JSON
    // support if (obj.contains("realm")) {
    //     entry.realm =
    //         value_to<bidi::types::script::RealmInfo>(obj.at("realm"));
    // }
    if (obj.contains("stackTrace")) {
        entry.stack_trace = value_to<std::string>(obj.at("stackTrace"));
    }

#ifdef DEBUG_LOG_DESERIALIZATION
    std::cerr << "JavaScriptLogEntry deserialization completed successfully\n";
#endif

    return entry;
}

// LogEntry struct serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::log::LogEntry &entry) {
    std::visit([&jv](const auto &e) { jv = value_from(e); }, entry.value);
}

inline auto tag_invoke(value_to_tag<bidi::types::log::LogEntry> /*unused*/,
                       const value &jv) -> bidi::types::log::LogEntry {
    const auto &obj = jv.as_object();

    if (!obj.contains("type")) {
        throw std::runtime_error("Log entry missing 'type' discriminator");
    }

    auto type = value_to<std::string_view>(obj.at("type"));

// Debug: print type
#ifdef DEBUG_LOG_DESERIALIZATION
    std::cerr << "LogEntry variant: detected type = '" << type << "'\n";
    std::cerr << "Full JSON: " << boost::json::serialize(jv) << "\n";
#endif

    if (type == "console") {
#ifdef DEBUG_LOG_DESERIALIZATION
        std::cerr << "Taking console path\n";
#endif
        return value_to<bidi::types::log::ConsoleLogEntry>(jv);
    }
    if (type == "javascript") {
#ifdef DEBUG_LOG_DESERIALIZATION
        std::cerr << "Taking javascript path\n";
#endif
        return value_to<bidi::types::log::JavaScriptLogEntry>(jv);
    }

    throw std::runtime_error("Invalid log entry type: " + std::string(type));
}

} // namespace boost::json

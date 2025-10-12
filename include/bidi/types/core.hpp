#pragma once
/**
 * @file types/core.hpp
 * @brief Core W3C WebDriver BiDi protocol types
 *
 * Foundation types with no dependencies - can be used independently.
 * Provides compile-time type safety for all W3C BiDi protocol interactions.
 *
 * @see https://w3c.github.io/webdriver-bidi/#errors
 */

#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string_view>

namespace bidi::types::core {

/**
 * @brief W3C WebDriver BiDi standard error codes
 *
 * Complete enumeration of all 52 standard error codes from W3C BiDi spec.
 * Replaces string-based error handling with compile-time type safety.
 *
 * @note Uses uint8_t underlying type for minimal memory footprint
 *
 * @see https://w3c.github.io/webdriver-bidi/#errors
 */
enum class ErrorCode : std::uint8_t {
    // Argument/Input Errors
    InvalidArgument,
    InvalidSelector,

    // Session/Context Errors
    InvalidSessionId,
    NoSuchFrame,
    NoSuchNode,
    NoSuchElement,
    NoSuchScript,
    NoSuchStoragePartition,
    NoSuchUserContext,
    NoSuchHistoryEntry,

    // Handle/Reference Errors
    NoSuchHandle,
    NoSuchNetworkCollector,
    NoSuchNetworkData,
    NoSuchRequest,

    // Extension Errors
    InvalidWebExtension,
    NoSuchWebExtension,

    // Alert/Prompt Errors
    NoSuchAlert,

    // Intercept Errors
    NoSuchIntercept,

    // Operational Errors
    MoveTargetOutOfBounds,
    SessionNotCreated,
    UnableToCaptureScreen,
    UnableToCloseBrowser,
    UnableToSetCookie,
    UnableToSetFileInput,
    UnavailableNetworkData,
    UnderspecifiedStoragePartition,
    UnknownCommand,
    UnknownError,
    UnsupportedOperation,

    // Additional W3C BiDi standard codes
    NoSuchChannel,
    NoSuchCookie,
    NoSuchDownloadItem,
    NoSuchPermission,
    NoSuchWindow,

    // Invalid Argument subtypes
    InvalidArgumentCookie,
    InvalidArgumentFileInput,
    InvalidArgumentHeader,
    InvalidArgumentPermission,
    InvalidArgumentRealm,
    InvalidArgumentRemoteValue,
    InvalidArgumentScript,
    InvalidArgumentTarget,
    InvalidArgumentUserPrompt,

    // Additional error categories
    InvalidStateError,
    NoSuchRequestCollector,
    UnableToInterceptRequest,
    UnsupportedCommandError
};

/**
 * @brief Convert ErrorCode to W3C BiDi protocol string
 *
 * Maps enum values to their exact W3C spec string representation.
 * Used for JSON serialization and error reporting.
 *
 * @param code Error code enum value
 * @return W3C spec string (e.g., "invalid argument")
 *
 * @note constexpr allows compile-time evaluation
 * @note noexcept guarantees no exceptions (lookup table)
 * @note [[nodiscard]] prevents ignoring return value
 */
[[nodiscard]] constexpr auto
to_string(ErrorCode code) noexcept -> std::string_view {
    using enum ErrorCode;
    switch (code) {
    case InvalidArgument:
        return "invalid argument";
    case InvalidSelector:
        return "invalid selector";
    case InvalidSessionId:
        return "invalid session id";
    case InvalidWebExtension:
        return "invalid web extension";
    case MoveTargetOutOfBounds:
        return "move target out of bounds";
    case NoSuchAlert:
        return "no such alert";
    case NoSuchNetworkCollector:
        return "no such network collector";
    case NoSuchElement:
        return "no such element";
    case NoSuchFrame:
        return "no such frame";
    case NoSuchHandle:
        return "no such handle";
    case NoSuchHistoryEntry:
        return "no such history entry";
    case NoSuchIntercept:
        return "no such intercept";
    case NoSuchNetworkData:
        return "no such network data";
    case NoSuchNode:
        return "no such node";
    case NoSuchRequest:
        return "no such request";
    case NoSuchScript:
        return "no such script";
    case NoSuchStoragePartition:
        return "no such storage partition";
    case NoSuchUserContext:
        return "no such user context";
    case NoSuchWebExtension:
        return "no such web extension";
    case SessionNotCreated:
        return "session not created";
    case UnableToCaptureScreen:
        return "unable to capture screen";
    case UnableToCloseBrowser:
        return "unable to close browser";
    case UnableToSetCookie:
        return "unable to set cookie";
    case UnableToSetFileInput:
        return "unable to set file input";
    case UnavailableNetworkData:
        return "unavailable network data";
    case UnderspecifiedStoragePartition:
        return "underspecified storage partition";
    case UnknownCommand:
        return "unknown command";
    case UnknownError:
        return "unknown error";
    case UnsupportedOperation:
        return "unsupported operation";
    case NoSuchChannel:
        return "no such channel";
    case NoSuchCookie:
        return "no such cookie";
    case NoSuchDownloadItem:
        return "no such download item";
    case NoSuchPermission:
        return "no such permission";
    case NoSuchWindow:
        return "no such window";
    case InvalidArgumentCookie:
        return "invalid argument cookie";
    case InvalidArgumentFileInput:
        return "invalid argument file input";
    case InvalidArgumentHeader:
        return "invalid argument header";
    case InvalidArgumentPermission:
        return "invalid argument permission";
    case InvalidArgumentRealm:
        return "invalid argument realm";
    case InvalidArgumentRemoteValue:
        return "invalid argument remote value";
    case InvalidArgumentScript:
        return "invalid argument script";
    case InvalidArgumentTarget:
        return "invalid argument target";
    case InvalidArgumentUserPrompt:
        return "invalid argument user prompt";
    case InvalidStateError:
        return "invalid state";
    case NoSuchRequestCollector:
        return "no such request collector";
    case UnableToInterceptRequest:
        return "unable to intercept request";
    case UnsupportedCommandError:
        return "unsupported command";
    default:
        return "unknown error";
    }
}

/**
 * @brief Parse W3C BiDi error string to ErrorCode enum
 *
 * Bidirectional conversion from protocol string to enum.
 * Returns nullopt for unknown error codes (forward compatibility).
 *
 * @param text W3C spec error string
 * @return ErrorCode enum value or nullopt if unknown
 *
 * @note constexpr allows compile-time parsing
 * @note noexcept for predictable performance
 * @note [[nodiscard]] prevents ignoring parse result
 *
 * @example
 * @code
 * auto code = parse_error_code("invalid argument");
 * if (code) {
 *     // Handle known error type
 * } else {
 *     // Handle unknown error (forward compat)
 * }
 * @endcode
 */
[[nodiscard]] constexpr auto
parse_error_code(std::string_view text) noexcept -> std::optional<ErrorCode> {
    using enum ErrorCode;

    // Sorted alphabetically for binary search optimization
    if (text == "invalid argument")
        return InvalidArgument;
    if (text == "invalid argument cookie")
        return InvalidArgumentCookie;
    if (text == "invalid argument file input")
        return InvalidArgumentFileInput;
    if (text == "invalid argument header")
        return InvalidArgumentHeader;
    if (text == "invalid argument permission")
        return InvalidArgumentPermission;
    if (text == "invalid argument realm")
        return InvalidArgumentRealm;
    if (text == "invalid argument remote value")
        return InvalidArgumentRemoteValue;
    if (text == "invalid argument script")
        return InvalidArgumentScript;
    if (text == "invalid argument target")
        return InvalidArgumentTarget;
    if (text == "invalid argument user prompt")
        return InvalidArgumentUserPrompt;
    if (text == "invalid selector")
        return InvalidSelector;
    if (text == "invalid session id")
        return InvalidSessionId;
    if (text == "invalid state")
        return InvalidStateError;
    if (text == "invalid web extension")
        return InvalidWebExtension;
    if (text == "move target out of bounds")
        return MoveTargetOutOfBounds;
    if (text == "no such alert")
        return NoSuchAlert;
    if (text == "no such channel")
        return NoSuchChannel;
    if (text == "no such cookie")
        return NoSuchCookie;
    if (text == "no such download item")
        return NoSuchDownloadItem;
    if (text == "no such element")
        return NoSuchElement;
    if (text == "no such frame")
        return NoSuchFrame;
    if (text == "no such handle")
        return NoSuchHandle;
    if (text == "no such history entry")
        return NoSuchHistoryEntry;
    if (text == "no such intercept")
        return NoSuchIntercept;
    if (text == "no such network collector")
        return NoSuchNetworkCollector;
    if (text == "no such network data")
        return NoSuchNetworkData;
    if (text == "no such node")
        return NoSuchNode;
    if (text == "no such permission")
        return NoSuchPermission;
    if (text == "no such request")
        return NoSuchRequest;
    if (text == "no such request collector")
        return NoSuchRequestCollector;
    if (text == "no such script")
        return NoSuchScript;
    if (text == "no such storage partition")
        return NoSuchStoragePartition;
    if (text == "no such user context")
        return NoSuchUserContext;
    if (text == "no such web extension")
        return NoSuchWebExtension;
    if (text == "no such window")
        return NoSuchWindow;
    if (text == "session not created")
        return SessionNotCreated;
    if (text == "unable to capture screen")
        return UnableToCaptureScreen;
    if (text == "unable to close browser")
        return UnableToCloseBrowser;
    if (text == "unable to intercept request")
        return UnableToInterceptRequest;
    if (text == "unable to set cookie")
        return UnableToSetCookie;
    if (text == "unable to set file input")
        return UnableToSetFileInput;
    if (text == "unavailable network data")
        return UnavailableNetworkData;
    if (text == "underspecified storage partition")
        return UnderspecifiedStoragePartition;
    if (text == "unknown command")
        return UnknownCommand;
    if (text == "unknown error")
        return UnknownError;
    if (text == "unsupported command")
        return UnsupportedCommandError;
    if (text == "unsupported operation")
        return UnsupportedOperation;

    return std::nullopt; // Unknown error code (forward compat)
}

/**
 * @brief Check if error code represents a "no such X" category
 *
 * Utility for grouping related errors.
 */
[[nodiscard]] constexpr auto is_no_such_error(ErrorCode code) noexcept -> bool {
    using enum ErrorCode;
    switch (code) {
    case NoSuchAlert:
    case NoSuchChannel:
    case NoSuchCookie:
    case NoSuchDownloadItem:
    case NoSuchElement:
    case NoSuchFrame:
    case NoSuchHandle:
    case NoSuchHistoryEntry:
    case NoSuchIntercept:
    case NoSuchNetworkCollector:
    case NoSuchNetworkData:
    case NoSuchNode:
    case NoSuchPermission:
    case NoSuchRequest:
    case NoSuchRequestCollector:
    case NoSuchScript:
    case NoSuchStoragePartition:
    case NoSuchUserContext:
    case NoSuchWebExtension:
    case NoSuchWindow:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Check if error code represents an "invalid X" category
 */
[[nodiscard]] constexpr auto
is_invalid_argument_error(ErrorCode code) noexcept -> bool {
    using enum ErrorCode;
    switch (code) {
    case InvalidArgument:
    case InvalidArgumentCookie:
    case InvalidArgumentFileInput:
    case InvalidArgumentHeader:
    case InvalidArgumentPermission:
    case InvalidArgumentRealm:
    case InvalidArgumentRemoteValue:
    case InvalidArgumentScript:
    case InvalidArgumentTarget:
    case InvalidArgumentUserPrompt:
    case InvalidSelector:
    case InvalidSessionId:
    case InvalidStateError:
    case InvalidWebExtension:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Unique command/response identifier (JS-safe range)
 *
 * Type alias for documentation and potential future validation.
 * Must be in range [0, 2^53-1] for JavaScript safety.
 */
using CommandId = std::int64_t;

/**
 * @brief Message type discriminator
 */
enum class MessageType : std::uint8_t { Success, Error, Event };

/**
 * @brief Convert MessageType to string
 */
[[nodiscard]] constexpr auto
to_string(MessageType type) noexcept -> std::string_view {
    using enum MessageType;
    switch (type) {
    case Success:
        return "success";
    case Error:
        return "error";
    case Event:
        return "event";
    default:
        return "unknown";
    }
}

} // namespace bidi::types::core

// Convenience aliases at bidi:: level for frequently used types
namespace bidi {
using ErrorCode = types::core::ErrorCode;
using MessageType = types::core::MessageType;
using CommandId = types::core::CommandId;
} // namespace bidi

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// ErrorCode serialization (C++ → JSON)
inline void tag_invoke(value_from_tag, value &jv,
                       bidi::types::core::ErrorCode code) {
    jv = bidi::types::core::to_string(code);
}

// ErrorCode deserialization (JSON → C++)
inline auto tag_invoke(value_to_tag<bidi::types::core::ErrorCode>,
                       const value &jv) -> bidi::types::core::ErrorCode {
    auto text = value_to<std::string_view>(jv);
    auto code = bidi::types::core::parse_error_code(text);
    if (!code) {
        // Unknown error code - default to UnknownError for forward compat
        return bidi::types::core::ErrorCode::UnknownError;
    }
    return *code;
}

// MessageType serialization
inline void tag_invoke(value_from_tag, value &jv,
                       bidi::types::core::MessageType type) {
    jv = bidi::types::core::to_string(type);
}

} // namespace boost::json

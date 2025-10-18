#pragma once
/**
 * @file types/script.hpp
 * @brief W3C WebDriver BiDi script module types
 *
 * Types for script evaluation, remote objects, realms, and handles.
 * Implements types from W3C BiDi spec script module.
 *
 * @see https://w3c.github.io/webdriver-bidi/#module-script
 */

#include "bidi/types/core.hpp"
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bidi::types::script {

// ==================== Identifiers ====================

/**
 * @brief Remote object handle (opaque string)
 */
using Handle = std::string;

/**
 * @brief Internal object ID (opaque string)
 */
using InternalId = std::string;

/**
 * @brief Realm identifier (opaque string)
 */
using Realm = std::string;

/**
 * @brief Shared reference ID (opaque string)
 */
using SharedId = std::string;

/**
 * @brief Channel identifier for script.message events
 * @see https://w3c.github.io/webdriver-bidi/#type-script-Channel
 */
using Channel = std::string;

/**
 * @brief Preload script identifier (handle to script that runs on realm
 * creation)
 * @see https://w3c.github.io/webdriver-bidi/#type-script-PreloadScript
 */
using PreloadScript = std::string;

// ==================== Enums ====================

/**
 * @brief Realm type enumeration
 */
enum class RealmType : std::uint8_t {
    Window,
    DedicatedWorker,
    SharedWorker,
    ServiceWorker,
    Worker,
    PaintWorklet,
    AudioWorklet,
    Worklet
};

[[nodiscard]] constexpr auto to_string(RealmType type) noexcept
    -> std::string_view {
    using enum RealmType;
    switch (type) {
    case Window:
        return "window";
    case DedicatedWorker:
        return "dedicated-worker";
    case SharedWorker:
        return "shared-worker";
    case ServiceWorker:
        return "service-worker";
    case Worker:
        return "worker";
    case PaintWorklet:
        return "paint-worklet";
    case AudioWorklet:
        return "audio-worklet";
    case Worklet:
        return "worklet";
    default:
        return "window";
    }
}

/**
 * @brief Result ownership (migrated from commands, kept for compatibility)
 */
enum class ResultOwnership : std::uint8_t { Root, None };

[[nodiscard]] constexpr auto to_string(ResultOwnership ownership) noexcept
    -> std::string_view {
    using enum ResultOwnership;
    switch (ownership) {
    case Root:
        return "root";
    case None:
        return "none";
    default:
        return "root";
    }
}

// ==================== Remote References ====================

/**
 * @brief Shared reference (cross-realm object)
 */
struct SharedReference {
    SharedId shared_id;
    std::optional<Handle> handle;

    auto operator==(const SharedReference &) const -> bool = default;
};

/**
 * @brief Remote object reference (realm-local)
 */
struct RemoteObjectReference {
    Handle handle;
    std::optional<SharedId> shared_id;

    auto operator==(const RemoteObjectReference &) const -> bool = default;
};

/**
 * @brief Remote reference variant
 */
using RemoteReference = std::variant<SharedReference, RemoteObjectReference>;

// ==================== Realm Info ====================

/**
 * @brief Realm information
 */
struct RealmInfo {
    Realm realm;
    RealmType type;
    std::string origin;
    std::optional<std::string> agent_cluster_id;

    auto operator==(const RealmInfo &) const -> bool = default;
};

// ==================== Target ====================

/**
 * @brief Evaluation target (migrated from commands, kept for compatibility)
 */
struct Target {
    std::string context;
    std::optional<std::string> sandbox;
    std::optional<Realm> realm;

    auto operator==(const Target &) const -> bool = default;
};

// ==================== Serialization Options ====================

/**
 * @brief Serialization options for RemoteValue objects
 * @see https://w3c.github.io/webdriver-bidi/#type-script-SerializationOptions
 */
struct SerializationOptions {
    std::optional<std::uint64_t> max_dom_depth;    // null or uint, default 0
    std::optional<std::uint64_t> max_object_depth; // null or uint, default null
    std::string include_shadow_tree{"none"};       // "none" | "open" | "all"

    auto operator==(const SerializationOptions &) const -> bool = default;
};

/**
 * @brief Source information for script events
 * @see https://w3c.github.io/webdriver-bidi/#type-script-Source
 */
struct Source {
    Realm realm;
    std::optional<std::string> context; // BrowsingContextId

    auto operator==(const Source &) const -> bool = default;
};

// ==================== Channel Types ====================

/**
 * @brief Channel properties for bidirectional messaging
 * @see https://w3c.github.io/webdriver-bidi/#type-script-ChannelProperties
 */
struct ChannelProperties {
    Channel channel;
    std::optional<SerializationOptions> serialization_options;
    std::optional<ResultOwnership> ownership;

    auto operator==(const ChannelProperties &) const -> bool = default;
};

/**
 * @brief Channel value type for script arguments
 * @see https://w3c.github.io/webdriver-bidi/#type-script-ChannelValue
 */
struct ChannelValue {
    std::string type; // Always "channel"
    ChannelProperties value;

    auto operator==(const ChannelValue &) const -> bool = default;
};

// ==================== RemoteValue System (Basic) ====================

/**
 * @brief Primitive protocol value
 */
struct PrimitiveProtocolValue {
    enum class Type : std::uint8_t {
        Undefined,
        Null,
        String,
        Number,
        Boolean,
        BigInt
    };

    Type type;
    std::variant<std::monostate, std::nullptr_t, std::string, double, bool>
        value;
    std::optional<std::string>
        special_number; // "NaN", "Infinity", "-Infinity", "-0"

    auto operator==(const PrimitiveProtocolValue &) const -> bool = default;
};

/**
 * @brief Symbol remote value
 */
struct SymbolRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;

    auto operator==(const SymbolRemoteValue &) const -> bool = default;
};

// Forward declarations for complex types
struct ArrayRemoteValue;
struct ObjectRemoteValue;

/**
 * @brief Array remote value
 */
struct ArrayRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;
    std::optional<std::vector<boost::json::value>>
        value; // Simplified - full spec uses RemoteReference

    auto operator==(const ArrayRemoteValue &) const -> bool = default;
};

/**
 * @brief Object property descriptor
 */
struct ObjectProperty {
    std::string name;
    boost::json::value value; // Simplified - full spec uses RemoteReference

    auto operator==(const ObjectProperty &) const -> bool = default;
};

/**
 * @brief Object remote value
 */
struct ObjectRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;
    std::optional<std::vector<ObjectProperty>> value; // Full object or preview

    auto operator==(const ObjectRemoteValue &) const -> bool = default;
};

/**
 * @brief Function remote value
 */
struct FunctionRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;

    auto operator==(const FunctionRemoteValue &) const -> bool = default;
};

/**
 * @brief RegExp remote value
 */
struct RegExpRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;
    std::string pattern;
    std::optional<std::string> flags;

    auto operator==(const RegExpRemoteValue &) const -> bool = default;
};

/**
 * @brief Date remote value
 */
struct DateRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;
    std::string value; // ISO 8601 timestamp

    auto operator==(const DateRemoteValue &) const -> bool = default;
};

/**
 * @brief Map remote value (simplified)
 */
struct MapRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;

    auto operator==(const MapRemoteValue &) const -> bool = default;
};

/**
 * @brief Set remote value (simplified)
 */
struct SetRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;

    auto operator==(const SetRemoteValue &) const -> bool = default;
};

/**
 * @brief Node remote value (DOM element)
 */
struct NodeRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;
    std::optional<SharedId> shared_id; // For cross-realm references
    std::optional<std::string> node_type;
    std::optional<std::string> local_name;

    auto operator==(const NodeRemoteValue &) const -> bool = default;
};

/**
 * @brief Error remote value (JavaScript Error objects)
 *
 * Represents JavaScript Error instances (TypeError, ReferenceError, etc.)
 * when serialized across the WebDriver BiDi protocol.
 *
 * @note This represents the Error object itself, NOT exception handling
 *       metadata. For exception metadata, see
 * bidi::script::ScriptExceptionDetails or the W3C script.ExceptionDetails type.
 *
 * @see https://w3c.github.io/webdriver-bidi/#type-script-ErrorRemoteValue
 */
struct ErrorRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;

    auto operator==(const ErrorRemoteValue &) const -> bool = default;
};

/**
 * @brief RemoteValue variant (basic set)
 *
 * This is a simplified subset of the full W3C BiDi RemoteValue type system.
 * Use std::visit for type-safe dispatch.
 */
using RemoteValue =
    std::variant<PrimitiveProtocolValue, SymbolRemoteValue, ArrayRemoteValue,
                 ObjectRemoteValue, FunctionRemoteValue, RegExpRemoteValue,
                 DateRemoteValue, MapRemoteValue, SetRemoteValue,
                 NodeRemoteValue, ErrorRemoteValue>;

// ==================== LocalValue (for sending to browser) ====================

/**
 * @brief LocalValue type (for script arguments)
 *
 * Simpler than RemoteValue - used for C++ → Browser marshalling.
 */
struct LocalValue {
    std::string type; // "undefined", "null", "string", "number", "boolean",
                      // "array", "object", etc.
    std::optional<boost::json::value> value;

    auto operator==(const LocalValue &) const -> bool = default;
};

// ==================== Evaluation Results ====================

/**
 * @brief Exception details for script evaluation
 * @see https://w3c.github.io/webdriver-bidi/#type-script-ExceptionDetails
 *
 * Note: For higher-level exception handling, see
 * bidi::script::ScriptExceptionDetails
 */
struct ExceptionDetails {
    std::uint64_t column_number{0};
    RemoteValue exception; // The thrown value
    std::uint64_t line_number{0};
    std::optional<boost::json::object> stack_trace;
    std::string text;

    auto operator==(const ExceptionDetails &) const -> bool = default;
};

/**
 * @brief Successful script evaluation result
 * @see https://w3c.github.io/webdriver-bidi/#type-script-EvaluateResultSuccess
 */
struct EvaluateResultSuccess {
    std::string type{"success"};
    RemoteValue result;
    Realm realm;

    auto operator==(const EvaluateResultSuccess &) const -> bool = default;
};

/**
 * @brief Exception script evaluation result
 * @see
 * https://w3c.github.io/webdriver-bidi/#type-script-EvaluateResultException
 */
struct EvaluateResultException {
    std::string type{"exception"};
    ExceptionDetails exception_details;
    Realm realm;

    auto operator==(const EvaluateResultException &) const -> bool = default;
};

/**
 * @brief Script evaluation result variant
 * @see https://w3c.github.io/webdriver-bidi/#type-script-EvaluateResult
 */
using EvaluateResult =
    std::variant<EvaluateResultSuccess, EvaluateResultException>;

} // namespace bidi::types::script

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// RealmType serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::script::RealmType type) {
    jv = bidi::types::script::to_string(type);
}

// ResultOwnership serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::script::ResultOwnership ownership) {
    jv = bidi::types::script::to_string(ownership);
}

// SerializationOptions serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::script::SerializationOptions &opts) {
    object obj;
    if (opts.max_dom_depth.has_value()) {
        obj["maxDomDepth"] = *opts.max_dom_depth;
    }
    if (opts.max_object_depth.has_value()) {
        obj["maxObjectDepth"] = *opts.max_object_depth;
    }
    obj["includeShadowTree"] = opts.include_shadow_tree;
    jv = std::move(obj);
}

// Source serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::script::Source &source) {
    object obj;
    obj["realm"] = source.realm;
    if (source.context.has_value()) {
        obj["context"] = *source.context;
    }
    jv = std::move(obj);
}

// ChannelProperties serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::script::ChannelProperties &props) {
    object obj;
    obj["channel"] = props.channel;
    if (props.serialization_options.has_value()) {
        obj["serializationOptions"] = value_from(*props.serialization_options);
    }
    if (props.ownership.has_value()) {
        obj["ownership"] = bidi::types::script::to_string(*props.ownership);
    }
    jv = std::move(obj);
}

// ChannelValue serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::script::ChannelValue &channel_val) {
    object obj;
    obj["type"] = channel_val.type;
    obj["value"] = value_from(channel_val.value);
    jv = std::move(obj);
}

} // namespace boost::json

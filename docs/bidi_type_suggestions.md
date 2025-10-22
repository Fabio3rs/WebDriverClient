# BiDi Type Gap Suggestions

Derived from `W3C_IMPLEMENTATION_GUIDE.md`, `TIPOS_W3C.md`, and `WEBSCRAPING_GAPS.md`. Summaries of existing headers reference `include/bidi/...`.

## Summary

| Module | Key spec types | Implemented (current headers) | Missing per spec |
| --- | --- | --- | --- |
| Core | Message envelope, `ErrorCode` enum | `ParsedResponse`, `ParsedEvent`, `MessageKind` (`include/bidi/core.hpp`) | Strongly typed `ErrorCode` enum + converters |
| Session | `CapabilitiesRequest`, `ProxyConfiguration`, `UserPromptHandler` | None (builders only) | Capabilities/Proxy/UserPrompt structs, subscription helpers |
| Browser | `ClientWindow`, `ClientWindowInfo`, `UserContext`, `UserContextInfo` | None | All browser info structs |
| BrowsingContext | `CreateType`, `ReadinessState`, `Locator`, `NavigationInfo`, `ClipRectangle`, `ImageFormat`, `UserPromptType` | `CreateType`, `ReadinessState` (`include/bidi/commands/browsing_context.hpp`) | Locators, navigation snapshot, prompt enums, screenshot helpers |
| Script | Remote identifiers (`Handle`, `Realm`…), `RemoteValue`, stack/exception types | `ScriptStackFrame`, `ScriptExceptionDetails`, `script_eval_policy`, `ScriptEvalOutcome` (`include/bidi/script_eval.hpp`) | Remote identifiers, `RemoteReference`, full `RemoteValue` union |
| Network/Storage | `BytesValue`, `Header`, `Cookie`, `ResponseData`, `AuthChallenge` | None | Network payload structs reused by storage |

## Core

### `bidi::ErrorCode`

```cpp
namespace bidi {
enum class ErrorCode {
    InvalidArgument,
    InvalidSelector,
    InvalidSessionId,
    InvalidWebExtension,
    MoveTargetOutOfBounds,
    NoSuchAlert,
    NoSuchNetworkCollector,
    NoSuchElement,
    NoSuchFrame,
    NoSuchHandle,
    NoSuchHistoryEntry,
    NoSuchIntercept,
    NoSuchNetworkData,
    NoSuchNode,
    NoSuchRequest,
    NoSuchScript,
    NoSuchStoragePartition,
    NoSuchUserContext,
    NoSuchWebExtension,
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
    // ... add remaining spec codes
};

[[nodiscard]] constexpr auto to_string(ErrorCode code) noexcept
    -> std::string_view;

[[nodiscard]] constexpr auto parse_error_code(std::string_view text) noexcept
    -> std::optional<ErrorCode>;
}
```

Helper functions allow forwards/backwards conversion while keeping string fallbacks for unknown codes.

## Session

### Capability negotiation

```cpp
namespace bidi::session {
struct SocksProxyConfiguration {
    std::string host;
    std::uint16_t port{0};
    std::uint8_t version{5};
};

struct ProxyConfiguration {
    enum class Type { autodetect, direct, manual, pac, system };

    Type type{Type::direct};
    std::optional<std::string> http_proxy;
    std::optional<std::string> ssl_proxy;
    std::optional<SocksProxyConfiguration> socks;
    std::optional<std::vector<std::string>> no_proxy;
    std::optional<std::string> pac_url;
};

enum class UserPromptAction { accept, dismiss, ignore };

struct UserPromptHandler {
    std::optional<UserPromptAction> alert;
    std::optional<UserPromptAction> before_unload;
    std::optional<UserPromptAction> confirm;
    std::optional<UserPromptAction> prompt;
    std::optional<UserPromptAction> file;
    std::optional<UserPromptAction> default_action;
};

struct CapabilityRequest {
    std::optional<bool> accept_insecure_certs;
    std::optional<std::string> browser_name;
    std::optional<std::string> browser_version;
    std::optional<std::string> platform_name;
    std::optional<ProxyConfiguration> proxy;
    std::optional<UserPromptHandler> unhandled_prompt_behavior;
};

struct CapabilitiesRequest {
    std::optional<CapabilityRequest> always_match;
    std::optional<std::vector<CapabilityRequest>> first_match;
};

using SubscriptionId = std::string;

struct SubscriptionRequest {
    std::vector<std::string> events;
    std::optional<std::vector<std::string>> contexts;
    std::optional<std::vector<std::string>> user_contexts;
};

struct UnsubscribeByIdRequest {
    std::vector<SubscriptionId> subscriptions;
};

struct UnsubscribeByFilterRequest {
    std::vector<std::string> events;
};
} // namespace bidi::session
```

## Browser

```cpp
namespace bidi::browser {
using ClientWindow = std::string;
using UserContext = std::string;

struct ClientWindowInfo {
    ClientWindow window_id;
    std::optional<std::string> origin;
    bool active{false};
};

struct UserContextInfo {
    UserContext user_context;
    std::optional<std::string> name;
};
}
```

## Browsing Context

```cpp
namespace bidi::browsing_context {
using BrowsingContextId = std::string;
using NavigationId = std::string;

enum class UserPromptType { alert, before_unload, confirm, prompt };

enum class LocateMatchType { full, partial };

struct AccessibilityLocator {
    std::optional<std::string> name;
    std::optional<std::string> role;
};

struct CssLocator {
    std::string value;
};

struct InnerTextLocator {
    std::string value;
    std::optional<bool> ignore_case;
    std::optional<LocateMatchType> match_type;
    std::optional<std::uint64_t> max_depth;
};

struct XPathLocator {
    std::string value;
};

struct ContextLocator {
    BrowsingContextId context;
};

using Locator = std::variant<AccessibilityLocator, CssLocator, InnerTextLocator,
                             XPathLocator, ContextLocator>;

struct NavigationInfo {
    BrowsingContextId context;
    std::optional<NavigationId> navigation;
    std::uint64_t timestamp_ms{0};
    std::string url;
};

struct ElementClipRectangle {
    std::string shared_reference; // script.SharedReference id
};

struct BoxClipRectangle {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};

using ClipRectangle = std::variant<ElementClipRectangle, BoxClipRectangle>;

struct ImageFormat {
    std::string type; // "png" | "jpeg"
    std::optional<double> quality; // only for jpeg
};
}
```

## Script

```cpp
namespace bidi::script {
using Handle = std::string;
using InternalId = std::string;
using Realm = std::string;
using SharedId = std::string;

struct SharedReference {
    SharedId shared_id;
    std::optional<Handle> handle;
};

struct RemoteObjectReference {
    Handle handle;
    std::optional<SharedId> shared_id;
};

using RemoteReference = std::variant<SharedReference, RemoteObjectReference>;

enum class RealmType {
    window,
    dedicated_worker,
    shared_worker,
    service_worker,
    worker,
    paint_worklet,
    audio_worklet,
    worklet
};

struct RealmInfo {
    Realm realm;
    RealmType type;
    std::string origin;
    std::optional<std::string> agent_cluster_id;
};

struct PrimitiveProtocolValue {
    std::variant<std::nullptr_t, bool, double, std::string> value;
    std::optional<std::string> special_number; // "NaN", "Infinity", "-Infinity", "-0"
};

struct SymbolRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;
};

struct ArrayRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;
    std::optional<std::vector<RemoteReference>> preview;
};

struct ObjectProperty {
    std::variant<std::string, RemoteReference> name;
    RemoteReference value;
};

struct ObjectRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internal_id;
    std::optional<std::vector<ObjectProperty>> preview;
};

// ... add FunctionRemoteValue, DateRemoteValue, NodeRemoteValue, etc. mirroring spec

using RemoteValue = std::variant<PrimitiveProtocolValue, SymbolRemoteValue,
                                 ArrayRemoteValue, ObjectRemoteValue
                                 /*, other RemoteValue variants */>;
}
```

## Network & Storage

```cpp
namespace bidi::network {
struct StringBytes { std::string value; };
struct Base64Bytes { std::string value; };
using BytesValue = std::variant<StringBytes, Base64Bytes>;

enum class SameSite { none, lax, strict };

struct Cookie {
    std::string name;
    BytesValue value;
    std::string domain;
    std::string path;
    std::uint64_t size{0};
    bool http_only{false};
    bool secure{false};
    SameSite same_site{SameSite::lax};
    std::optional<std::uint64_t> expiry_epoch_seconds;
};

struct Header {
    std::string name;
    BytesValue value;
};

struct ResponseContent {
    std::uint64_t size{0};
};

struct AuthChallenge {
    std::string scheme;
    std::string realm;
};

struct ResponseData {
    std::string url;
    std::string protocol;
    std::uint64_t status{0};
    std::string status_text;
    bool from_cache{false};
    std::vector<Header> headers;
    std::string mime_type;
    std::uint64_t bytes_received{0};
    std::optional<std::uint64_t> headers_size;
    std::optional<std::uint64_t> body_size;
    ResponseContent content;
    std::optional<std::vector<AuthChallenge>> auth_challenges;
};

struct RequestData {
    std::string request_id;
    std::string url;
    std::string method;
    std::vector<Header> headers;
};
}

namespace bidi::storage {
using Cookie = network::Cookie;
}
```

These definitions keep optional fields explicit and use strong types (enums, variants) to match the W3C CDDL while integrating cleanly with existing async workflows.

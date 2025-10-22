#pragma once
/**
 * @file types/network.hpp
 * @brief W3C WebDriver BiDi network module types
 *
 * Types for network requests, responses, cookies, headers, and intercepts.
 * Implements types from W3C BiDi spec network module.
 *
 * @see https://w3c.github.io/webdriver-bidi/#module-network
 */

#include "bidi/types/core.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bidi::types::network {

// ==================== Identifiers ====================

using RequestId = std::string;
using InterceptId = std::string;
using Collector = std::string;

// ==================== BytesValue System ====================

/**
 * @brief String-based bytes (UTF-8 text)
 */
struct StringBytes {
    std::string value;

    auto operator==(const StringBytes &) const -> bool = default;
};

/**
 * @brief Base64-encoded bytes (binary data)
 */
struct Base64Bytes {
    std::string value; // Base64-encoded

    auto operator==(const Base64Bytes &) const -> bool = default;
};

/**
 * @brief BytesValue variant (string or base64)
 */
using BytesValue = std::variant<StringBytes, Base64Bytes>;

// ==================== Cookie ====================

/**
 * @brief SameSite cookie attribute
 */
enum class SameSite : std::uint8_t { None, Lax, Strict };

[[nodiscard]] constexpr auto to_string(SameSite value) noexcept
    -> std::string_view {
    using enum SameSite;
    switch (value) {
    case None:
        return "none";
    case Lax:
        return "lax";
    case Strict:
        return "strict";
    default:
        return "lax";
    }
}

[[nodiscard]] constexpr auto parse_same_site(std::string_view text) noexcept
    -> std::optional<SameSite> {
    using enum SameSite;
    if (text == "none") {
        return None;
    }
    if (text == "lax") {
        return Lax;
    }
    if (text == "strict") {
        return Strict;
    }
    return std::nullopt;
}

/**
 * @brief HTTP cookie
 */
struct Cookie {
    std::string name;
    BytesValue value;
    std::string domain;
    std::string path;
    std::uint64_t size{0};
    bool http_only{false};
    bool secure{false};
    SameSite same_site{SameSite::Lax};
    std::optional<std::uint64_t> expiry_epoch_seconds;

    auto operator==(const Cookie &) const -> bool = default;
};

// ==================== Header ====================

/**
 * @brief HTTP header
 */
struct Header {
    std::string name;
    BytesValue value;

    auto operator==(const Header &) const -> bool = default;
};

// ==================== Response Data ====================

/**
 * @brief Response content metadata
 */
struct ResponseContent {
    std::uint64_t size{0};

    auto operator==(const ResponseContent &) const -> bool = default;
};

/**
 * @brief Authentication challenge
 */
struct AuthChallenge {
    std::string scheme;
    std::string realm;

    auto operator==(const AuthChallenge &) const -> bool = default;
};

/**
 * @brief Authentication credentials (password-based)
 */
struct AuthCredentials {
    std::string type{"password"};
    std::string username;
    std::string password;

    auto operator==(const AuthCredentials &) const -> bool = default;
};

/**
 * @brief HTTP response data
 */
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

    auto operator==(const ResponseData &) const -> bool = default;
};

// ==================== Request Data ====================

/**
 * @brief HTTP request data
 */
struct RequestData {
    RequestId request_id;
    std::string url;
    std::string method;
    std::vector<Header> headers;
    std::optional<std::uint64_t> body_size;
    std::optional<BytesValue> initial_priority;
    std::optional<std::string> referrer;

    auto operator==(const RequestData &) const -> bool = default;
};

// ==================== URL Pattern ====================

/**
 * @brief URL pattern using pattern matching
 */
struct UrlPatternPattern {
    std::string type{"pattern"};
    std::optional<std::string> protocol;
    std::optional<std::string> hostname;
    std::optional<std::string> port;
    std::optional<std::string> pathname;
    std::optional<std::string> search;

    auto operator==(const UrlPatternPattern &) const -> bool = default;
};

/**
 * @brief URL pattern using string matching
 */
struct UrlPatternString {
    std::string type{"string"};
    std::string pattern;

    auto operator==(const UrlPatternString &) const -> bool = default;
};

/**
 * @brief URL pattern (pattern-based or string-based)
 */
using UrlPattern = std::variant<UrlPatternPattern, UrlPatternString>;

// ==================== Interception Phase ====================

/**
 * @brief Network intercept phase
 * @see https://w3c.github.io/webdriver-bidi/#network-InterceptPhase
 */
enum class InterceptPhase : std::uint8_t {
    BeforeRequestSent,
    ResponseStarted,
    AuthRequired
};

[[nodiscard]] constexpr auto to_string(InterceptPhase phase) noexcept
    -> std::string_view {
    using enum InterceptPhase;
    switch (phase) {
    case BeforeRequestSent:
        return "beforeRequestSent";
    case ResponseStarted:
        return "responseStarted";
    case AuthRequired:
        return "authRequired";
    }
    return "beforeRequestSent"; // Default
}

[[nodiscard]] constexpr auto
parse_intercept_phase(std::string_view text) noexcept
    -> std::optional<InterceptPhase> {
    using enum InterceptPhase;
    if (text == "beforeRequestSent") {
        return BeforeRequestSent;
    }
    if (text == "responseStarted") {
        return ResponseStarted;
    }
    if (text == "authRequired") {
        return AuthRequired;
    }
    return std::nullopt;
}

// ==================== Cookie Headers ====================

/**
 * @brief Cookie header for request modification
 * @see https://w3c.github.io/webdriver-bidi/#network-CookieHeader
 */
struct CookieHeader {
    std::string name;
    BytesValue value;

    auto operator==(const CookieHeader &) const -> bool = default;
};

/**
 * @brief Set-Cookie header for response modification
 * @see https://w3c.github.io/webdriver-bidi/#network-SetCookieHeader
 */
struct SetCookieHeader {
    std::string name;
    BytesValue value;
    std::optional<std::string> domain;
    std::optional<std::string> path;
    std::optional<std::uint64_t> expiry_epoch_seconds;
    std::optional<bool> http_only;
    std::optional<bool> secure;
    std::optional<SameSite> same_site;

    auto operator==(const SetCookieHeader &) const -> bool = default;
};

// ==================== Auth Actions ====================

/**
 * @brief Auth action types for continueWithAuth
 */
enum class AuthAction : std::uint8_t { ProvideCredentials, Default, Cancel };

[[nodiscard]] constexpr auto to_string(AuthAction action) noexcept
    -> std::string_view {
    using enum AuthAction;
    switch (action) {
    case ProvideCredentials:
        return "provideCredentials";
    case Default:
        return "default";
    case Cancel:
        return "cancel";
    }
    return "default";
}

[[nodiscard]] constexpr auto parse_auth_action(std::string_view text) noexcept
    -> std::optional<AuthAction> {
    using enum AuthAction;
    if (text == "provideCredentials") {
        return ProvideCredentials;
    }
    if (text == "default") {
        return Default;
    }
    if (text == "cancel") {
        return Cancel;
    }
    return std::nullopt;
}

// ==================== Command Parameters ====================

/**
 * @brief Parameters for network.addIntercept command
 * @see https://w3c.github.io/webdriver-bidi/#command-network-addIntercept
 */
struct AddInterceptParameters {
    std::vector<InterceptPhase> phases;
    std::optional<std::vector<std::string>> contexts; // BrowsingContext IDs
    std::optional<std::vector<UrlPattern>> url_patterns;

    auto operator==(const AddInterceptParameters &) const -> bool = default;
};

/**
 * @brief Result for network.addIntercept command
 */
struct AddInterceptResult {
    InterceptId intercept;

    auto operator==(const AddInterceptResult &) const -> bool = default;
};

/**
 * @brief Parameters for network.continueRequest command
 * @see https://w3c.github.io/webdriver-bidi/#command-network-continueRequest
 */
struct ContinueRequestParameters {
    RequestId request;
    std::optional<BytesValue> body;
    std::optional<std::vector<CookieHeader>> cookies;
    std::optional<std::vector<Header>> headers;
    std::optional<std::string> method;
    std::optional<std::string> url;

    auto operator==(const ContinueRequestParameters &) const -> bool = default;
};

/**
 * @brief Parameters for network.continueResponse command
 * @see https://w3c.github.io/webdriver-bidi/#command-network-continueResponse
 */
struct ContinueResponseParameters {
    RequestId request;
    std::optional<std::vector<SetCookieHeader>> cookies;
    std::optional<AuthCredentials> credentials;
    std::optional<std::vector<Header>> headers;
    std::optional<std::string> reason_phrase;
    std::optional<std::uint64_t> status_code;

    auto operator==(const ContinueResponseParameters &) const -> bool = default;
};

/**
 * @brief Parameters for network.provideResponse command
 * @see https://w3c.github.io/webdriver-bidi/#command-network-provideResponse
 */
struct ProvideResponseParameters {
    RequestId request;
    std::optional<BytesValue> body;
    std::optional<std::vector<SetCookieHeader>> cookies;
    std::optional<std::vector<Header>> headers;
    std::optional<std::string> reason_phrase;
    std::optional<std::uint64_t> status_code;

    auto operator==(const ProvideResponseParameters &) const -> bool = default;
};

/**
 * @brief Auth action with credentials
 */
struct ContinueWithAuthCredentials {
    AuthAction action{AuthAction::ProvideCredentials};
    AuthCredentials credentials;

    auto operator==(const ContinueWithAuthCredentials &) const
        -> bool = default;
};

/**
 * @brief Auth action without credentials
 */
struct ContinueWithAuthNoCredentials {
    AuthAction action; // Default or Cancel

    auto operator==(const ContinueWithAuthNoCredentials &) const
        -> bool = default;
};

/**
 * @brief Parameters for network.continueWithAuth command
 * @see https://w3c.github.io/webdriver-bidi/#command-network-continueWithAuth
 */
struct ContinueWithAuthParameters {
    RequestId request;
    std::variant<ContinueWithAuthCredentials, ContinueWithAuthNoCredentials>
        action;

    auto operator==(const ContinueWithAuthParameters &) const -> bool = default;
};

/**
 * @brief Parameters for network.removeIntercept command
 * @see https://w3c.github.io/webdriver-bidi/#command-network-removeIntercept
 */
struct RemoveInterceptParameters {
    InterceptId intercept;

    auto operator==(const RemoveInterceptParameters &) const -> bool = default;
};

/**
 * @brief Parameters for network.failRequest command
 * @see https://w3c.github.io/webdriver-bidi/#command-network-failRequest
 */
struct FailRequestParameters {
    RequestId request;

    auto operator==(const FailRequestParameters &) const -> bool = default;
};

// ==================== Event Parameters ====================

/**
 * @brief Base parameters for network events
 * @see https://w3c.github.io/webdriver-bidi/#network-BaseParameters
 */
struct BaseParameters {
    RequestId request;
    std::optional<std::string> navigation;
    std::optional<std::string> context; // BrowsingContext ID
    std::uint64_t timestamp{0};
    std::uint64_t redirect_count{0};
    bool is_blocked{false};
    std::optional<std::vector<InterceptId>> intercepts;

    auto operator==(const BaseParameters &) const -> bool = default;
};

/**
 * @brief Parameters for network.beforeRequestSent event
 * @see https://w3c.github.io/webdriver-bidi/#event-network-beforeRequestSent
 */
struct BeforeRequestSentParameters {
    BaseParameters base;
    RequestData request;

    auto operator==(const BeforeRequestSentParameters &) const
        -> bool = default;
};

/**
 * @brief Parameters for network.responseStarted event
 * @see https://w3c.github.io/webdriver-bidi/#event-network-responseStarted
 */
struct ResponseStartedParameters {
    BaseParameters base;
    ResponseData response;

    auto operator==(const ResponseStartedParameters &) const -> bool = default;
};

/**
 * @brief Parameters for network.authRequired event
 * @see https://w3c.github.io/webdriver-bidi/#event-network-authRequired
 */
struct AuthRequiredParameters {
    BaseParameters base;
    ResponseData response;

    auto operator==(const AuthRequiredParameters &) const -> bool = default;
};

} // namespace bidi::types::network

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// SameSite serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::network::SameSite same_site) {
    jv = bidi::types::network::to_string(same_site);
}

inline auto tag_invoke(value_to_tag<bidi::types::network::SameSite> /*unused*/,
                       const value &jv) -> bidi::types::network::SameSite {
    auto text = value_to<std::string_view>(jv);
    auto same_site = bidi::types::network::parse_same_site(text);
    if (!same_site) {
        throw std::runtime_error("Invalid SameSite value");
    }
    return *same_site;
}

// AuthCredentials serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::network::AuthCredentials &creds) {
    object obj;
    obj["type"] = creds.type;
    obj["username"] = creds.username;
    obj["password"] = creds.password;
    jv = std::move(obj);
}

// UrlPatternPattern serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::network::UrlPatternPattern &pattern) {
    object obj;
    obj["type"] = pattern.type;
    if (pattern.protocol.has_value()) {
        obj["protocol"] = *pattern.protocol;
    }
    if (pattern.hostname.has_value()) {
        obj["hostname"] = *pattern.hostname;
    }
    if (pattern.port.has_value()) {
        obj["port"] = *pattern.port;
    }
    if (pattern.pathname.has_value()) {
        obj["pathname"] = *pattern.pathname;
    }
    if (pattern.search.has_value()) {
        obj["search"] = *pattern.search;
    }
    jv = std::move(obj);
}

// UrlPatternString serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::network::UrlPatternString &pattern) {
    object obj;
    obj["type"] = pattern.type;
    obj["pattern"] = pattern.pattern;
    jv = std::move(obj);
}

// InterceptPhase serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::network::InterceptPhase phase) {
    jv = bidi::types::network::to_string(phase);
}

inline auto
tag_invoke(value_to_tag<bidi::types::network::InterceptPhase> /*unused*/,
           const value &jv) -> bidi::types::network::InterceptPhase {
    auto text = value_to<std::string_view>(jv);
    auto phase = bidi::types::network::parse_intercept_phase(text);
    if (!phase) {
        throw std::runtime_error("Invalid InterceptPhase value");
    }
    return *phase;
}

// AuthAction serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::network::AuthAction action) {
    jv = bidi::types::network::to_string(action);
}

inline auto
tag_invoke(value_to_tag<bidi::types::network::AuthAction> /*unused*/,
           const value &jv) -> bidi::types::network::AuthAction {
    auto text = value_to<std::string_view>(jv);
    auto action = bidi::types::network::parse_auth_action(text);
    if (!action) {
        throw std::runtime_error("Invalid AuthAction value");
    }
    return *action;
}

// BytesValue serialization helper
inline void
serialize_bytes_value(object &obj, std::string_view key,
                      const bidi::types::network::BytesValue &bytes) {
    std::visit(
        [&obj, key](const auto &bytes_val) {
            using T = std::decay_t<decltype(bytes_val)>;
            if constexpr (std::is_same_v<T,
                                         bidi::types::network::StringBytes>) {
                obj[key] =
                    object{{"type", "string"}, {"value", bytes_val.value}};
            } else {
                obj[key] =
                    object{{"type", "base64"}, {"value", bytes_val.value}};
            }
        },
        bytes);
}

// CookieHeader serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::network::CookieHeader &cookie) {
    object obj;
    obj["name"] = cookie.name;
    serialize_bytes_value(obj, "value", cookie.value);
    jv = std::move(obj);
}

// SetCookieHeader serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::network::SetCookieHeader &cookie) {
    object obj;
    obj["name"] = cookie.name;
    serialize_bytes_value(obj, "value", cookie.value);

    if (cookie.domain) {
        obj["domain"] = *cookie.domain;
    }
    if (cookie.path) {
        obj["path"] = *cookie.path;
    }
    if (cookie.expiry_epoch_seconds) {
        obj["expiry"] = *cookie.expiry_epoch_seconds;
    }
    if (cookie.http_only) {
        obj["httpOnly"] = *cookie.http_only;
    }
    if (cookie.secure) {
        obj["secure"] = *cookie.secure;
    }
    if (cookie.same_site) {
        obj["sameSite"] = bidi::types::network::to_string(*cookie.same_site);
    }

    jv = std::move(obj);
}

// AddInterceptParameters serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::AddInterceptParameters &params) {
    object obj;

    array phases_arr;
    for (const auto &phase : params.phases) {
        phases_arr.emplace_back(bidi::types::network::to_string(phase));
    }
    obj["phases"] = std::move(phases_arr);

    if (params.contexts) {
        obj["contexts"] = value_from(*params.contexts);
    }

    if (params.url_patterns) {
        array patterns_arr;
        for (const auto &pattern : *params.url_patterns) {
            std::visit(
                [&patterns_arr](const auto &pat) {
                    patterns_arr.emplace_back(value_from(pat));
                },
                pattern);
        }
        obj["urlPatterns"] = std::move(patterns_arr);
    }

    jv = std::move(obj);
}

// ContinueRequestParameters serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::ContinueRequestParameters &params) {
    object obj;
    obj["request"] = params.request;

    if (params.body) {
        serialize_bytes_value(obj, "body", *params.body);
    }

    if (params.cookies) {
        obj["cookies"] = value_from(*params.cookies);
    }
    if (params.headers) {
        obj["headers"] = value_from(*params.headers);
    }
    if (params.method) {
        obj["method"] = *params.method;
    }
    if (params.url) {
        obj["url"] = *params.url;
    }

    jv = std::move(obj);
}

// ContinueResponseParameters serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::ContinueResponseParameters &params) {
    object obj;
    obj["request"] = params.request;

    if (params.cookies) {
        obj["cookies"] = value_from(*params.cookies);
    }
    if (params.credentials) {
        obj["credentials"] = value_from(*params.credentials);
    }
    if (params.headers) {
        obj["headers"] = value_from(*params.headers);
    }
    if (params.reason_phrase) {
        obj["reasonPhrase"] = *params.reason_phrase;
    }
    if (params.status_code) {
        obj["statusCode"] = *params.status_code;
    }

    jv = std::move(obj);
}

// ProvideResponseParameters serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::ProvideResponseParameters &params) {
    object obj;
    obj["request"] = params.request;

    if (params.body) {
        serialize_bytes_value(obj, "body", *params.body);
    }

    if (params.cookies) {
        obj["cookies"] = value_from(*params.cookies);
    }
    if (params.headers) {
        obj["headers"] = value_from(*params.headers);
    }
    if (params.reason_phrase) {
        obj["reasonPhrase"] = *params.reason_phrase;
    }
    if (params.status_code) {
        obj["statusCode"] = *params.status_code;
    }

    jv = std::move(obj);
}

// ContinueWithAuthCredentials serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::ContinueWithAuthCredentials &params) {
    object obj;
    obj["action"] = bidi::types::network::to_string(params.action);
    obj["credentials"] = value_from(params.credentials);
    jv = std::move(obj);
}

// ContinueWithAuthNoCredentials serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::ContinueWithAuthNoCredentials &params) {
    object obj;
    obj["action"] = bidi::types::network::to_string(params.action);
    jv = std::move(obj);
}

// ContinueWithAuthParameters serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::ContinueWithAuthParameters &params) {
    object obj;
    obj["request"] = params.request;

    std::visit(
        [&obj](const auto &action_params) {
            auto action_obj = value_from(action_params);
            if (action_obj.is_object()) {
                for (const auto &pair : action_obj.as_object()) {
                    obj[pair.key()] = pair.value();
                }
            }
        },
        params.action);

    jv = std::move(obj);
}

// RemoveInterceptParameters serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::RemoveInterceptParameters &params) {
    object obj;
    obj["intercept"] = params.intercept;
    jv = std::move(obj);
}

// FailRequestParameters serialization
inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::network::FailRequestParameters &params) {
    object obj;
    obj["request"] = params.request;
    jv = std::move(obj);
}

// BaseParameters serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::network::BaseParameters &params) {
    object obj;
    obj["request"] = params.request;
    obj["timestamp"] = params.timestamp;
    obj["redirectCount"] = params.redirect_count;
    obj["isBlocked"] = params.is_blocked;

    if (params.navigation) {
        obj["navigation"] = *params.navigation;
    }
    if (params.context) {
        obj["context"] = *params.context;
    }
    if (params.intercepts) {
        obj["intercepts"] = value_from(*params.intercepts);
    }

    jv = std::move(obj);
}

inline auto
tag_invoke(value_to_tag<bidi::types::network::BaseParameters> /*unused*/,
           const value &jv) -> bidi::types::network::BaseParameters {
    const auto &obj = jv.as_object();
    bidi::types::network::BaseParameters params;

    // The "request" field might be a string (ID) or omitted in some events
    // For events like beforeRequestSent, it's embedded in the event params
    // separately
    const auto *req_ptr = obj.if_contains("request");
    if (req_ptr != nullptr && req_ptr->is_string()) {
        params.request = value_to<std::string>(*req_ptr);
    }

    params.timestamp = value_to<std::uint64_t>(obj.at("timestamp"));
    params.redirect_count = value_to<std::uint64_t>(obj.at("redirectCount"));
    params.is_blocked = value_to<bool>(obj.at("isBlocked"));

    if (obj.contains("navigation")) {
        params.navigation = value_to<std::string>(obj.at("navigation"));
    }
    if (obj.contains("context")) {
        params.context = value_to<std::string>(obj.at("context"));
    }
    if (obj.contains("intercepts")) {
        params.intercepts =
            value_to<std::vector<std::string>>(obj.at("intercepts"));
    }

    return params;
}

// Header serialization (for RequestData/ResponseData)
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::network::Header &header) {
    object obj;
    obj["name"] = header.name;
    serialize_bytes_value(obj, "value", header.value);
    jv = std::move(obj);
}

inline auto tag_invoke(value_to_tag<bidi::types::network::Header> /*unused*/,
                       const value &jv) -> bidi::types::network::Header {
    const auto &obj = jv.as_object();
    bidi::types::network::Header header;

    header.name = value_to<std::string>(obj.at("name"));

    const auto &val_obj = obj.at("value").as_object();
    const auto type_str = value_to<std::string_view>(val_obj.at("type"));
    const auto val_data = value_to<std::string>(val_obj.at("value"));

    if (type_str == "string") {
        header.value = bidi::types::network::StringBytes{val_data};
    } else {
        header.value = bidi::types::network::Base64Bytes{val_data};
    }

    return header;
}

// RequestData deserialization
inline auto
tag_invoke(value_to_tag<bidi::types::network::RequestData> /*unused*/,
           const value &jv) -> bidi::types::network::RequestData {
    const auto &obj = jv.as_object();
    bidi::types::network::RequestData data;

    data.request_id = value_to<std::string>(obj.at("request"));
    data.url = value_to<std::string>(obj.at("url"));
    data.method = value_to<std::string>(obj.at("method"));
    data.headers =
        value_to<std::vector<bidi::types::network::Header>>(obj.at("headers"));

    if (obj.contains("bodySize")) {
        data.body_size = value_to<std::uint64_t>(obj.at("bodySize"));
    }

    return data;
}

// ResponseData deserialization
inline auto
tag_invoke(value_to_tag<bidi::types::network::ResponseData> /*unused*/,
           const value &jv) -> bidi::types::network::ResponseData {
    const auto &obj = jv.as_object();
    bidi::types::network::ResponseData data;

    data.url = value_to<std::string>(obj.at("url"));
    data.protocol = value_to<std::string>(obj.at("protocol"));
    data.status = value_to<std::uint64_t>(obj.at("status"));
    data.status_text = value_to<std::string>(obj.at("statusText"));
    data.from_cache = value_to<bool>(obj.at("fromCache"));
    data.headers =
        value_to<std::vector<bidi::types::network::Header>>(obj.at("headers"));
    data.mime_type = value_to<std::string>(obj.at("mimeType"));
    data.bytes_received = value_to<std::uint64_t>(obj.at("bytesReceived"));

    if (obj.contains("headersSize")) {
        data.headers_size = value_to<std::uint64_t>(obj.at("headersSize"));
    }
    if (obj.contains("bodySize")) {
        data.body_size = value_to<std::uint64_t>(obj.at("bodySize"));
    }

    const auto &content_obj = obj.at("content").as_object();
    data.content.size = value_to<std::uint64_t>(content_obj.at("size"));

    return data;
}

// BeforeRequestSentParameters deserialization
inline auto tag_invoke(
    value_to_tag<bidi::types::network::BeforeRequestSentParameters> /*unused*/,
    const value &jv) -> bidi::types::network::BeforeRequestSentParameters {
    const auto &obj = jv.as_object();
    bidi::types::network::BeforeRequestSentParameters params;

    // Note: In beforeRequestSent events, "request" is an object (RequestData),
    // not a string ID like in other network events. So we skip trying to parse
    // it as string in base.request (which would fail).
    // It will be parsed separately below as params.request (RequestData).

    params.base.timestamp = value_to<std::uint64_t>(obj.at("timestamp"));
    params.base.redirect_count =
        value_to<std::uint64_t>(obj.at("redirectCount"));
    params.base.is_blocked = value_to<bool>(obj.at("isBlocked"));

    if (obj.contains("navigation")) {
        params.base.navigation = value_to<std::string>(obj.at("navigation"));
    }
    if (obj.contains("context")) {
        params.base.context = value_to<std::string>(obj.at("context"));
    }
    if (obj.contains("intercepts")) {
        params.base.intercepts =
            value_to<std::vector<std::string>>(obj.at("intercepts"));
    }

    if (obj.contains("request")) {
        params.request =
            value_to<bidi::types::network::RequestData>(obj.at("request"));
    }

    return params;
}

// ResponseStartedParameters deserialization
inline auto tag_invoke(
    value_to_tag<bidi::types::network::ResponseStartedParameters> /*unused*/,
    const value &jv) -> bidi::types::network::ResponseStartedParameters {
    const auto &obj = jv.as_object();
    bidi::types::network::ResponseStartedParameters params;

    params.base = value_to<bidi::types::network::BaseParameters>(jv);

    if (obj.contains("response")) {
        params.response =
            value_to<bidi::types::network::ResponseData>(obj.at("response"));
    }

    return params;
}

// AuthRequiredParameters deserialization
inline auto tag_invoke(
    value_to_tag<bidi::types::network::AuthRequiredParameters> /*unused*/,
    const value &jv) -> bidi::types::network::AuthRequiredParameters {
    const auto &obj = jv.as_object();
    bidi::types::network::AuthRequiredParameters params;

    params.base = value_to<bidi::types::network::BaseParameters>(jv);

    if (obj.contains("response")) {
        params.response =
            value_to<bidi::types::network::ResponseData>(obj.at("response"));
    }

    return params;
}

// AddInterceptResult deserialization
inline auto
tag_invoke(value_to_tag<bidi::types::network::AddInterceptResult> /*unused*/,
           const value &jv) -> bidi::types::network::AddInterceptResult {
    const auto &obj = jv.as_object();
    bidi::types::network::AddInterceptResult result;
    result.intercept = value_to<std::string>(obj.at("intercept"));
    return result;
}

} // namespace boost::json

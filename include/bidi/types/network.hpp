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

[[nodiscard]] constexpr auto
to_string(SameSite value) noexcept -> std::string_view {
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

[[nodiscard]] constexpr auto
parse_same_site(std::string_view text) noexcept -> std::optional<SameSite> {
    using enum SameSite;
    if (text == "none")
        return None;
    if (text == "lax")
        return Lax;
    if (text == "strict")
        return Strict;
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

} // namespace bidi::types::network

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// SameSite serialization
inline void tag_invoke(value_from_tag, value &jv,
                       bidi::types::network::SameSite same_site) {
    jv = bidi::types::network::to_string(same_site);
}

inline auto tag_invoke(value_to_tag<bidi::types::network::SameSite>,
                       const value &jv) -> bidi::types::network::SameSite {
    auto text = value_to<std::string_view>(jv);
    auto same_site = bidi::types::network::parse_same_site(text);
    if (!same_site) {
        throw std::runtime_error("Invalid SameSite value");
    }
    return *same_site;
}

} // namespace boost::json

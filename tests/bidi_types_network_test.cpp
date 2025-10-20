// tests/bidi_types_network_test.cpp - Unit tests for network types
#include "bidi/types/network.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>
#include <variant>

using namespace bidi::types::network;

// ==================== BytesValue Tests ====================

TEST(BidiTypesNetwork, StringBytesEquality) {
    StringBytes bytes1{"hello"};
    StringBytes bytes2{"hello"};
    StringBytes bytes3{"world"};

    EXPECT_EQ(bytes1, bytes2);
    EXPECT_NE(bytes1, bytes3);
}

TEST(BidiTypesNetwork, Base64BytesEquality) {
    Base64Bytes bytes1{"aGVsbG8="};
    Base64Bytes bytes2{"aGVsbG8="};
    Base64Bytes bytes3{"d29ybGQ="};

    EXPECT_EQ(bytes1, bytes2);
    EXPECT_NE(bytes1, bytes3);
}

TEST(BidiTypesNetwork, BytesValueVariantConstruction) {
    // Test construction with each bytes type
    BytesValue val1 = StringBytes{"text"};
    EXPECT_TRUE(std::holds_alternative<StringBytes>(val1));

    BytesValue val2 = Base64Bytes{"YmFzZTY0"};
    EXPECT_TRUE(std::holds_alternative<Base64Bytes>(val2));
}

TEST(BidiTypesNetwork, BytesValueVariantVisitor) {
    auto get_type = [](const BytesValue &val) -> std::string {
        return std::visit(
            [](const auto &v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, StringBytes>) {
                    return "string";
                } else if constexpr (std::is_same_v<T, Base64Bytes>) {
                    return "base64";
                }
                return "unknown";
            },
            val);
    };

    EXPECT_EQ(get_type(StringBytes{"test"}), "string");
    EXPECT_EQ(get_type(Base64Bytes{"dGVzdA=="}), "base64");
}

TEST(BidiTypesNetwork, BytesValueVariantAccess) {
    BytesValue val = StringBytes{"hello"};

    // std::get (throws if wrong type)
    const auto &str_bytes = std::get<StringBytes>(val);
    EXPECT_EQ(str_bytes.value, "hello");

    // std::get_if (returns nullptr if wrong type)
    auto *str_ptr = std::get_if<StringBytes>(&val);
    ASSERT_NE(str_ptr, nullptr);
    EXPECT_EQ(str_ptr->value, "hello");

    auto *b64_ptr = std::get_if<Base64Bytes>(&val);
    EXPECT_EQ(b64_ptr, nullptr);
}

TEST(BidiTypesNetwork, BytesValueVariantIndexTest) {
    BytesValue val1 = StringBytes{""};
    EXPECT_EQ(val1.index(), 0U);

    BytesValue val2 = Base64Bytes{""};
    EXPECT_EQ(val2.index(), 1U);
}

// ==================== SameSite Enum Tests ====================

TEST(BidiTypesNetwork, SameSiteToString) {
    using enum SameSite;

    EXPECT_EQ(to_string(None), "none");
    EXPECT_EQ(to_string(Lax), "lax");
    EXPECT_EQ(to_string(Strict), "strict");
}

TEST(BidiTypesNetwork, SameSiteConstexpr) {
    constexpr auto str = to_string(SameSite::Lax);
    static_assert(str == "lax");
}

TEST(BidiTypesNetwork, SameSiteParse) {
    auto val1 = parse_same_site("none");
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(*val1, SameSite::None);

    auto val2 = parse_same_site("lax");
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(*val2, SameSite::Lax);

    auto val3 = parse_same_site("strict");
    ASSERT_TRUE(val3.has_value());
    EXPECT_EQ(*val3, SameSite::Strict);

    auto unknown = parse_same_site("invalid");
    EXPECT_FALSE(unknown.has_value());
}

TEST(BidiTypesNetwork, SameSiteRoundTrip) {
    using enum SameSite;

    auto test_round_trip = [](SameSite value) {
        auto str = to_string(value);
        auto parsed = parse_same_site(str);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, value);
    };

    test_round_trip(None);
    test_round_trip(Lax);
    test_round_trip(Strict);
}

TEST(BidiTypesNetwork, SameSiteBoostJson) {
    using enum SameSite;

    // Serialization
    auto jv1 = boost::json::value_from(None);
    EXPECT_EQ(jv1.as_string(), "none");

    auto jv2 = boost::json::value_from(Lax);
    EXPECT_EQ(jv2.as_string(), "lax");

    auto jv3 = boost::json::value_from(Strict);
    EXPECT_EQ(jv3.as_string(), "strict");

    // Deserialization
    boost::json::value jv4 = "lax";
    auto val = boost::json::value_to<SameSite>(jv4);
    EXPECT_EQ(val, Lax);

    // Round-trip
    auto jv5 = boost::json::value_from(Strict);
    auto parsed = boost::json::value_to<SameSite>(jv5);
    EXPECT_EQ(parsed, Strict);
}

TEST(BidiTypesNetwork, SameSiteInvalidJson) {
    boost::json::value jv = "invalid_same_site";
    EXPECT_THROW(boost::json::value_to<SameSite>(jv), std::runtime_error);
}

// ==================== Cookie Tests ====================

TEST(BidiTypesNetwork, CookieEquality) {
    Cookie cookie1;
    cookie1.name = "session";
    cookie1.value = StringBytes{"abc123"};
    cookie1.domain = "example.com";
    cookie1.path = "/";
    cookie1.size = 100;
    cookie1.http_only = true;
    cookie1.secure = true;
    cookie1.same_site = SameSite::Strict;
    cookie1.expiry_epoch_seconds = 1234567890;

    Cookie cookie2;
    cookie2.name = "session";
    cookie2.value = StringBytes{"abc123"};
    cookie2.domain = "example.com";
    cookie2.path = "/";
    cookie2.size = 100;
    cookie2.http_only = true;
    cookie2.secure = true;
    cookie2.same_site = SameSite::Strict;
    cookie2.expiry_epoch_seconds = 1234567890;

    Cookie cookie3;
    cookie3.name = "other";
    cookie3.value = StringBytes{"xyz"};
    cookie3.domain = "different.com";
    cookie3.path = "/other";

    EXPECT_EQ(cookie1, cookie2);
    EXPECT_NE(cookie1, cookie3);
}

TEST(BidiTypesNetwork, CookieDefaultValues) {
    Cookie cookie;

    EXPECT_TRUE(cookie.name.empty());
    EXPECT_TRUE(cookie.domain.empty());
    EXPECT_TRUE(cookie.path.empty());
    EXPECT_EQ(cookie.size, 0U);
    EXPECT_FALSE(cookie.http_only);
    EXPECT_FALSE(cookie.secure);
    EXPECT_EQ(cookie.same_site, SameSite::Lax);
    EXPECT_FALSE(cookie.expiry_epoch_seconds.has_value());
}

TEST(BidiTypesNetwork, CookieOptionalExpiry) {
    Cookie cookie;
    cookie.name = "test";
    cookie.value = StringBytes{"value"};

    // expiry is optional
    EXPECT_FALSE(cookie.expiry_epoch_seconds.has_value());

    cookie.expiry_epoch_seconds = 9999999999;
    EXPECT_TRUE(cookie.expiry_epoch_seconds.has_value());
    EXPECT_EQ(*cookie.expiry_epoch_seconds, 9999999999U);
}

TEST(BidiTypesNetwork, CookieWithByteValues) {
    Cookie cookie1;
    cookie1.name = "token";
    cookie1.value = StringBytes{"plain text token"};

    Cookie cookie2;
    cookie2.name = "binary_token";
    cookie2.value = Base64Bytes{"YmluYXJ5IGRhdGE="};

    EXPECT_TRUE(std::holds_alternative<StringBytes>(cookie1.value));
    EXPECT_TRUE(std::holds_alternative<Base64Bytes>(cookie2.value));
}

// ==================== Header Tests ====================

TEST(BidiTypesNetwork, HeaderEquality) {
    Header header1;
    header1.name = "Content-Type";
    header1.value = StringBytes{"application/json"};

    Header header2;
    header2.name = "Content-Type";
    header2.value = StringBytes{"application/json"};

    Header header3;
    header3.name = "Authorization";
    header3.value = StringBytes{"Bearer token"};

    EXPECT_EQ(header1, header2);
    EXPECT_NE(header1, header3);
}

TEST(BidiTypesNetwork, HeaderWithByteValues) {
    Header header1;
    header1.name = "X-Custom-Text";
    header1.value = StringBytes{"plain text"};

    Header header2;
    header2.name = "X-Custom-Binary";
    header2.value = Base64Bytes{"YmluYXJ5"};

    EXPECT_TRUE(std::holds_alternative<StringBytes>(header1.value));
    EXPECT_TRUE(std::holds_alternative<Base64Bytes>(header2.value));
}

TEST(BidiTypesNetwork, HeaderDefaultConstruction) {
    Header header;

    EXPECT_TRUE(header.name.empty());
}

// ==================== ResponseContent Tests ====================

TEST(BidiTypesNetwork, ResponseContentEquality) {
    ResponseContent content1{100};
    ResponseContent content2{100};
    ResponseContent content3{200};

    EXPECT_EQ(content1, content2);
    EXPECT_NE(content1, content3);
}

TEST(BidiTypesNetwork, ResponseContentDefaultValue) {
    ResponseContent content;
    EXPECT_EQ(content.size, 0U);
}

// ==================== AuthChallenge Tests ====================

TEST(BidiTypesNetwork, AuthChallengeEquality) {
    AuthChallenge challenge1{.scheme = "Basic", .realm = "Protected Area"};
    AuthChallenge challenge2{.scheme = "Basic", .realm = "Protected Area"};
    AuthChallenge challenge3{.scheme = "Digest", .realm = "Secure Zone"};

    EXPECT_EQ(challenge1, challenge2);
    EXPECT_NE(challenge1, challenge3);
}

TEST(BidiTypesNetwork, AuthChallengeDefaultConstruction) {
    AuthChallenge challenge;

    EXPECT_TRUE(challenge.scheme.empty());
    EXPECT_TRUE(challenge.realm.empty());
}

// ==================== ResponseData Tests ====================

TEST(BidiTypesNetwork, ResponseDataEquality) {
    ResponseData resp1;
    resp1.url = "https://example.com";
    resp1.protocol = "http/2";
    resp1.status = 200;
    resp1.status_text = "OK";
    resp1.from_cache = false;
    resp1.mime_type = "text/html";
    resp1.bytes_received = 5000;

    ResponseData resp2;
    resp2.url = "https://example.com";
    resp2.protocol = "http/2";
    resp2.status = 200;
    resp2.status_text = "OK";
    resp2.from_cache = false;
    resp2.mime_type = "text/html";
    resp2.bytes_received = 5000;

    ResponseData resp3;
    resp3.url = "https://different.com";
    resp3.status = 404;
    resp3.status_text = "Not Found";

    EXPECT_EQ(resp1, resp2);
    EXPECT_NE(resp1, resp3);
}

TEST(BidiTypesNetwork, ResponseDataDefaultValues) {
    ResponseData resp;

    EXPECT_TRUE(resp.url.empty());
    EXPECT_TRUE(resp.protocol.empty());
    EXPECT_EQ(resp.status, 0U);
    EXPECT_TRUE(resp.status_text.empty());
    EXPECT_FALSE(resp.from_cache);
    EXPECT_TRUE(resp.headers.empty());
    EXPECT_TRUE(resp.mime_type.empty());
    EXPECT_EQ(resp.bytes_received, 0U);
    EXPECT_FALSE(resp.headers_size.has_value());
    EXPECT_FALSE(resp.body_size.has_value());
    EXPECT_FALSE(resp.auth_challenges.has_value());
}

TEST(BidiTypesNetwork, ResponseDataOptionalFields) {
    ResponseData resp;
    resp.url = "https://example.com";

    // Optional fields
    EXPECT_FALSE(resp.headers_size.has_value());
    EXPECT_FALSE(resp.body_size.has_value());
    EXPECT_FALSE(resp.auth_challenges.has_value());

    resp.headers_size = 1024;
    resp.body_size = 4096;

    EXPECT_TRUE(resp.headers_size.has_value());
    EXPECT_EQ(*resp.headers_size, 1024U);
    EXPECT_TRUE(resp.body_size.has_value());
    EXPECT_EQ(*resp.body_size, 4096U);
}

TEST(BidiTypesNetwork, ResponseDataWithHeaders) {
    ResponseData resp;
    resp.url = "https://example.com";

    Header h1;
    h1.name = "Content-Type";
    h1.value = StringBytes{"application/json"};

    Header h2;
    h2.name = "Content-Length";
    h2.value = StringBytes{"1234"};

    resp.headers.push_back(h1);
    resp.headers.push_back(h2);

    EXPECT_EQ(resp.headers.size(), 2U);
    EXPECT_EQ(resp.headers[0].name, "Content-Type");
    EXPECT_EQ(resp.headers[1].name, "Content-Length");
}

TEST(BidiTypesNetwork, ResponseDataWithAuthChallenges) {
    ResponseData resp;
    resp.url = "https://example.com";

    std::vector<AuthChallenge> challenges;
    challenges.push_back(AuthChallenge{.scheme = "Basic", .realm = "Realm 1"});
    challenges.push_back(AuthChallenge{.scheme = "Digest", .realm = "Realm 2"});

    resp.auth_challenges = challenges;

    EXPECT_TRUE(resp.auth_challenges.has_value());
    EXPECT_EQ(resp.auth_challenges->size(), 2U);
    EXPECT_EQ((*resp.auth_challenges)[0].scheme, "Basic");
    EXPECT_EQ((*resp.auth_challenges)[1].scheme, "Digest");
}

TEST(BidiTypesNetwork, ResponseDataWithContent) {
    ResponseData resp;
    resp.url = "https://example.com";
    resp.content.size = 8192;

    EXPECT_EQ(resp.content.size, 8192U);
}

// ==================== RequestData Tests ====================

TEST(BidiTypesNetwork, RequestDataEquality) {
    RequestData req1;
    req1.request_id = "req-123";
    req1.url = "https://example.com/api";
    req1.method = "GET";

    RequestData req2;
    req2.request_id = "req-123";
    req2.url = "https://example.com/api";
    req2.method = "GET";

    RequestData req3;
    req3.request_id = "req-456";
    req3.url = "https://different.com";
    req3.method = "POST";

    EXPECT_EQ(req1, req2);
    EXPECT_NE(req1, req3);
}

TEST(BidiTypesNetwork, RequestDataDefaultValues) {
    RequestData req;

    EXPECT_TRUE(req.request_id.empty());
    EXPECT_TRUE(req.url.empty());
    EXPECT_TRUE(req.method.empty());
    EXPECT_TRUE(req.headers.empty());
    EXPECT_FALSE(req.body_size.has_value());
    EXPECT_FALSE(req.initial_priority.has_value());
    EXPECT_FALSE(req.referrer.has_value());
}

TEST(BidiTypesNetwork, RequestDataOptionalFields) {
    RequestData req;
    req.request_id = "req-123";

    // Optional fields
    EXPECT_FALSE(req.body_size.has_value());
    EXPECT_FALSE(req.initial_priority.has_value());
    EXPECT_FALSE(req.referrer.has_value());

    req.body_size = 2048;
    req.initial_priority = StringBytes{"high"};
    req.referrer = "https://referrer.com";

    EXPECT_TRUE(req.body_size.has_value());
    EXPECT_EQ(*req.body_size, 2048U);
    EXPECT_TRUE(req.initial_priority.has_value());
    EXPECT_TRUE(req.referrer.has_value());
    EXPECT_EQ(*req.referrer, "https://referrer.com");
}

TEST(BidiTypesNetwork, RequestDataWithHeaders) {
    RequestData req;
    req.request_id = "req-123";
    req.url = "https://example.com/api";
    req.method = "POST";

    Header h1;
    h1.name = "Content-Type";
    h1.value = StringBytes{"application/json"};

    Header h2;
    h2.name = "Authorization";
    h2.value = StringBytes{"Bearer token123"};

    req.headers.push_back(h1);
    req.headers.push_back(h2);

    EXPECT_EQ(req.headers.size(), 2U);
    EXPECT_EQ(req.headers[0].name, "Content-Type");
    EXPECT_EQ(req.headers[1].name, "Authorization");
}

TEST(BidiTypesNetwork, RequestDataWithPriority) {
    RequestData req1;
    req1.request_id = "req-1";
    req1.initial_priority = StringBytes{"high"};

    RequestData req2;
    req2.request_id = "req-2";
    req2.initial_priority = Base64Bytes{"bG93"};

    EXPECT_TRUE(req1.initial_priority.has_value());
    EXPECT_TRUE(std::holds_alternative<StringBytes>(*req1.initial_priority));

    EXPECT_TRUE(req2.initial_priority.has_value());
    EXPECT_TRUE(std::holds_alternative<Base64Bytes>(*req2.initial_priority));
}

// ==================== Type Safety Tests ====================

TEST(BidiTypesNetwork, StructDefaultConstruction) {
    // Verify all structs are default-constructible
    StringBytes str_bytes;
    EXPECT_TRUE(str_bytes.value.empty());

    Base64Bytes b64_bytes;
    EXPECT_TRUE(b64_bytes.value.empty());

    Cookie cookie;
    EXPECT_TRUE(cookie.name.empty());
    EXPECT_EQ(cookie.same_site, SameSite::Lax);

    Header header;
    EXPECT_TRUE(header.name.empty());

    ResponseContent content;
    EXPECT_EQ(content.size, 0U);

    AuthChallenge challenge;
    EXPECT_TRUE(challenge.scheme.empty());

    ResponseData response;
    EXPECT_EQ(response.status, 0U);
    EXPECT_FALSE(response.from_cache);

    RequestData request;
    EXPECT_TRUE(request.method.empty());
}

TEST(BidiTypesNetwork, ComplexStructWithAllFields) {
    // Test a fully populated ResponseData structure
    ResponseData resp;
    resp.url = "https://api.example.com/data";
    resp.protocol = "http/2";
    resp.status = 200;
    resp.status_text = "OK";
    resp.from_cache = false;
    resp.mime_type = "application/json";
    resp.bytes_received = 5120;
    resp.headers_size = 512;
    resp.body_size = 4608;
    resp.content.size = 4608;

    Header h;
    h.name = "Content-Type";
    h.value = StringBytes{"application/json"};
    resp.headers.push_back(h);

    std::vector<AuthChallenge> challenges;
    challenges.push_back(AuthChallenge{.scheme = "Bearer", .realm = "API"});
    resp.auth_challenges = challenges;

    // Verify all fields are set correctly
    EXPECT_EQ(resp.url, "https://api.example.com/data");
    EXPECT_EQ(resp.status, 200U);
    EXPECT_EQ(resp.headers.size(), 1U);
    EXPECT_TRUE(resp.auth_challenges.has_value());
    EXPECT_EQ(resp.auth_challenges->size(), 1U);
}

// ==================== InterceptPhase Enum Tests ====================

TEST(BidiTypesNetwork, InterceptPhaseToString) {
    using enum InterceptPhase;

    EXPECT_EQ(to_string(BeforeRequestSent), "beforeRequestSent");
    EXPECT_EQ(to_string(ResponseStarted), "responseStarted");
    EXPECT_EQ(to_string(AuthRequired), "authRequired");
}

TEST(BidiTypesNetwork, InterceptPhaseConstexpr) {
    constexpr auto str = to_string(InterceptPhase::BeforeRequestSent);
    static_assert(str == "beforeRequestSent");
}

TEST(BidiTypesNetwork, InterceptPhaseParse) {
    auto val1 = parse_intercept_phase("beforeRequestSent");
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(*val1, InterceptPhase::BeforeRequestSent);

    auto val2 = parse_intercept_phase("responseStarted");
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(*val2, InterceptPhase::ResponseStarted);

    auto val3 = parse_intercept_phase("authRequired");
    ASSERT_TRUE(val3.has_value());
    EXPECT_EQ(*val3, InterceptPhase::AuthRequired);

    auto unknown = parse_intercept_phase("invalid");
    EXPECT_FALSE(unknown.has_value());
}

TEST(BidiTypesNetwork, InterceptPhaseRoundTrip) {
    using enum InterceptPhase;

    auto test_round_trip = [](InterceptPhase value) {
        auto str = to_string(value);
        auto parsed = parse_intercept_phase(str);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, value);
    };

    test_round_trip(BeforeRequestSent);
    test_round_trip(ResponseStarted);
    test_round_trip(AuthRequired);
}

TEST(BidiTypesNetwork, InterceptPhaseBoostJson) {
    using enum InterceptPhase;

    // Serialization
    auto jv1 = boost::json::value_from(BeforeRequestSent);
    EXPECT_EQ(jv1.as_string(), "beforeRequestSent");

    auto jv2 = boost::json::value_from(ResponseStarted);
    EXPECT_EQ(jv2.as_string(), "responseStarted");

    auto jv3 = boost::json::value_from(AuthRequired);
    EXPECT_EQ(jv3.as_string(), "authRequired");

    // Deserialization
    boost::json::value jv4 = "responseStarted";
    auto val = boost::json::value_to<InterceptPhase>(jv4);
    EXPECT_EQ(val, ResponseStarted);

    // Round-trip
    auto jv5 = boost::json::value_from(AuthRequired);
    auto parsed = boost::json::value_to<InterceptPhase>(jv5);
    EXPECT_EQ(parsed, AuthRequired);
}

TEST(BidiTypesNetwork, InterceptPhaseInvalidJson) {
    boost::json::value jv = "invalid_phase";
    EXPECT_THROW(boost::json::value_to<InterceptPhase>(jv), std::runtime_error);
}

// ==================== AuthAction Enum Tests ====================

TEST(BidiTypesNetwork, AuthActionToString) {
    using enum AuthAction;

    EXPECT_EQ(to_string(ProvideCredentials), "provideCredentials");
    EXPECT_EQ(to_string(Default), "default");
    EXPECT_EQ(to_string(Cancel), "cancel");
}

TEST(BidiTypesNetwork, AuthActionConstexpr) {
    constexpr auto str = to_string(AuthAction::Default);
    static_assert(str == "default");
}

TEST(BidiTypesNetwork, AuthActionParse) {
    auto val1 = parse_auth_action("provideCredentials");
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(*val1, AuthAction::ProvideCredentials);

    auto val2 = parse_auth_action("default");
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(*val2, AuthAction::Default);

    auto val3 = parse_auth_action("cancel");
    ASSERT_TRUE(val3.has_value());
    EXPECT_EQ(*val3, AuthAction::Cancel);

    auto unknown = parse_auth_action("invalid");
    EXPECT_FALSE(unknown.has_value());
}

TEST(BidiTypesNetwork, AuthActionRoundTrip) {
    using enum AuthAction;

    auto test_round_trip = [](AuthAction value) {
        auto str = to_string(value);
        auto parsed = parse_auth_action(str);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, value);
    };

    test_round_trip(ProvideCredentials);
    test_round_trip(Default);
    test_round_trip(Cancel);
}

TEST(BidiTypesNetwork, AuthActionBoostJson) {
    using enum AuthAction;

    // Serialization
    auto jv1 = boost::json::value_from(ProvideCredentials);
    EXPECT_EQ(jv1.as_string(), "provideCredentials");

    auto jv2 = boost::json::value_from(Default);
    EXPECT_EQ(jv2.as_string(), "default");

    auto jv3 = boost::json::value_from(Cancel);
    EXPECT_EQ(jv3.as_string(), "cancel");

    // Deserialization
    boost::json::value jv4 = "cancel";
    auto val = boost::json::value_to<AuthAction>(jv4);
    EXPECT_EQ(val, Cancel);

    // Round-trip
    auto jv5 = boost::json::value_from(ProvideCredentials);
    auto parsed = boost::json::value_to<AuthAction>(jv5);
    EXPECT_EQ(parsed, ProvideCredentials);
}

TEST(BidiTypesNetwork, AuthActionInvalidJson) {
    boost::json::value jv = "invalid_action";
    EXPECT_THROW(boost::json::value_to<AuthAction>(jv), std::runtime_error);
}

// ==================== CookieHeader Tests ====================

TEST(BidiTypesNetwork, CookieHeaderEquality) {
    CookieHeader cookie1{.name = "session", .value = StringBytes{"abc123"}};

    CookieHeader cookie2{.name = "session", .value = StringBytes{"abc123"}};

    CookieHeader cookie3{.name = "other", .value = StringBytes{"xyz"}};

    EXPECT_EQ(cookie1, cookie2);
    EXPECT_NE(cookie1, cookie3);
}

TEST(BidiTypesNetwork, CookieHeaderBase64Value) {
    CookieHeader cookie1{.name = "binary", .value = StringBytes{"text"}};

    CookieHeader cookie2{.name = "binary", .value = Base64Bytes{"dGV4dA=="}};

    EXPECT_NE(cookie1, cookie2); // Different BytesValue variants
}

TEST(BidiTypesNetwork, CookieHeaderBoostJson) {
    CookieHeader cookie{.name = "auth", .value = StringBytes{"token123"}};

    // Serialization
    auto jv = boost::json::value_from(cookie);
    auto obj = jv.as_object();

    EXPECT_EQ(obj.at("name").as_string(), "auth");
    EXPECT_TRUE(obj.contains("value"));

    // Verify BytesValue structure
    const auto &value_obj = obj.at("value").as_object();
    EXPECT_EQ(value_obj.at("type").as_string(), "string");
    EXPECT_EQ(value_obj.at("value").as_string(), "token123");
}

// ==================== SetCookieHeader Tests ====================

TEST(BidiTypesNetwork, SetCookieHeaderEquality) {
    // Order: name, value, domain, path, expiry_epoch_seconds, http_only,
    // secure, same_site
    SetCookieHeader cookie1{.name = "session",
                            .value = StringBytes{"abc123"},
                            .domain = "example.com",
                            .path = "/",
                            .expiry_epoch_seconds = std::nullopt,
                            .http_only = true,
                            .secure = true,
                            .same_site = SameSite::Strict};

    SetCookieHeader cookie2{.name = "session",
                            .value = StringBytes{"abc123"},
                            .domain = "example.com",
                            .path = "/",
                            .expiry_epoch_seconds = std::nullopt,
                            .http_only = true,
                            .secure = true,
                            .same_site = SameSite::Strict};

    SetCookieHeader cookie3{.name = "other",
                            .value = StringBytes{"xyz"},
                            .domain = std::nullopt,
                            .path = std::nullopt,
                            .expiry_epoch_seconds = std::nullopt,
                            .http_only = std::nullopt,
                            .secure = std::nullopt,
                            .same_site = std::nullopt};

    EXPECT_EQ(cookie1, cookie2);
    EXPECT_NE(cookie1, cookie3);
}

TEST(BidiTypesNetwork, SetCookieHeaderDefaultValues) {
    SetCookieHeader cookie{.name = "test",
                           .value = StringBytes{"value"},
                           .domain = std::nullopt,
                           .path = std::nullopt,
                           .expiry_epoch_seconds = std::nullopt,
                           .http_only = std::nullopt,
                           .secure = std::nullopt,
                           .same_site = std::nullopt};

    EXPECT_FALSE(cookie.domain.has_value());
    EXPECT_FALSE(cookie.path.has_value());
    EXPECT_FALSE(cookie.expiry_epoch_seconds.has_value());
    EXPECT_FALSE(cookie.http_only.has_value());
    EXPECT_FALSE(cookie.secure.has_value());
    EXPECT_FALSE(cookie.same_site.has_value());
}

TEST(BidiTypesNetwork, SetCookieHeaderOptionalFields) {
    SetCookieHeader cookie{.name = "test",
                           .value = StringBytes{"value"},
                           .domain = std::nullopt,
                           .path = std::nullopt,
                           .expiry_epoch_seconds = std::nullopt,
                           .http_only = std::nullopt,
                           .secure = std::nullopt,
                           .same_site = std::nullopt};

    cookie.domain = "example.com";
    cookie.path = "/api";
    cookie.expiry_epoch_seconds = 1729497600; // Unix timestamp
    cookie.http_only = true;
    cookie.secure = true;
    cookie.same_site = SameSite::None;

    EXPECT_TRUE(cookie.domain.has_value());
    EXPECT_EQ(*cookie.domain, "example.com");
    EXPECT_TRUE(cookie.path.has_value());
    EXPECT_EQ(*cookie.path, "/api");
    EXPECT_TRUE(cookie.expiry_epoch_seconds.has_value());
    EXPECT_EQ(*cookie.expiry_epoch_seconds, 1729497600U);
    EXPECT_TRUE(cookie.http_only.has_value());
    EXPECT_TRUE(*cookie.http_only);
    EXPECT_TRUE(cookie.secure.has_value());
    EXPECT_TRUE(*cookie.secure);
    EXPECT_TRUE(cookie.same_site.has_value());
    EXPECT_EQ(*cookie.same_site, SameSite::None);
}

TEST(BidiTypesNetwork, SetCookieHeaderBoostJson) {
    SetCookieHeader cookie{.name = "session",
                           .value = StringBytes{"token456"},
                           .domain = "secure.example.com",
                           .path = std::nullopt,
                           .expiry_epoch_seconds = 1729497600,
                           .http_only = std::nullopt,
                           .secure = true,
                           .same_site = SameSite::Strict};

    // Serialization
    auto jv = boost::json::value_from(cookie);
    auto obj = jv.as_object();

    EXPECT_EQ(obj.at("name").as_string(), "session");
    EXPECT_TRUE(obj.contains("value"));
    EXPECT_EQ(obj.at("domain").as_string(), "secure.example.com");
    EXPECT_EQ(obj.at("expiry").as_uint64(), 1729497600U);
    EXPECT_TRUE(obj.at("secure").as_bool());
    EXPECT_EQ(obj.at("sameSite").as_string(), "strict");
}

// ==================== BaseParameters Tests ====================

TEST(BidiTypesNetwork, BaseParametersEquality) {
    // Order: request, navigation, context, timestamp, redirect_count,
    // is_blocked, intercepts
    BaseParameters params1{.request = "req-789",
                           .navigation = "nav-456",
                           .context = "ctx-123",
                           .timestamp = 1234567890,
                           .redirect_count = 2,
                           .is_blocked = true,
                           .intercepts = std::nullopt};

    BaseParameters params2{.request = "req-789",
                           .navigation = "nav-456",
                           .context = "ctx-123",
                           .timestamp = 1234567890,
                           .redirect_count = 2,
                           .is_blocked = true,
                           .intercepts = std::nullopt};

    BaseParameters params3{.request = "req-000",
                           .navigation = std::nullopt,
                           .context = "ctx-999",
                           .timestamp = 0,
                           .redirect_count = 0,
                           .is_blocked = false,
                           .intercepts = std::nullopt};

    EXPECT_EQ(params1, params2);
    EXPECT_NE(params1, params3);
}

TEST(BidiTypesNetwork, BaseParametersDefaultValues) {
    BaseParameters params{.request = "req-123",
                          .navigation = std::nullopt,
                          .context = std::nullopt,
                          .timestamp = 0,
                          .redirect_count = 0,
                          .is_blocked = false,
                          .intercepts = std::nullopt};

    EXPECT_FALSE(params.context.has_value());
    EXPECT_EQ(params.is_blocked, false);
    EXPECT_FALSE(params.navigation.has_value());
    EXPECT_EQ(params.redirect_count, 0U);
    EXPECT_EQ(params.timestamp, 0U);
}

TEST(BidiTypesNetwork, BaseParametersOptionalFields) {
    BaseParameters params{.request = "req-123",
                          .navigation = std::nullopt,
                          .context = std::nullopt,
                          .timestamp = 0,
                          .redirect_count = 0,
                          .is_blocked = false,
                          .intercepts = std::nullopt};

    params.context = "ctx-456";
    params.navigation = "nav-789";

    EXPECT_TRUE(params.context.has_value());
    EXPECT_EQ(*params.context, "ctx-456");
    EXPECT_TRUE(params.navigation.has_value());
    EXPECT_EQ(*params.navigation, "nav-789");
}

TEST(BidiTypesNetwork, BaseParametersBoostJson) {
    BaseParameters params{.request = "req-789",
                          .navigation = "nav-456",
                          .context = "ctx-123",
                          .timestamp = 9876543210,
                          .redirect_count = 3,
                          .is_blocked = true,
                          .intercepts = std::nullopt};

    // Serialization
    auto jv = boost::json::value_from(params);
    auto obj = jv.as_object();

    EXPECT_EQ(obj.at("context").as_string(), "ctx-123");
    EXPECT_TRUE(obj.at("isBlocked").as_bool());
    EXPECT_EQ(obj.at("navigation").as_string(), "nav-456");
    EXPECT_EQ(obj.at("redirectCount").as_uint64(), 3U);
    EXPECT_EQ(obj.at("request").as_string(), "req-789");
    EXPECT_EQ(obj.at("timestamp").as_uint64(), 9876543210U);
}

// ==================== AddInterceptParameters Tests ====================

TEST(BidiTypesNetwork, AddInterceptParametersEquality) {
    using enum InterceptPhase;
    // Order: phases, contexts, url_patterns

    AddInterceptParameters params1{
        .phases = {BeforeRequestSent, ResponseStarted},
        .contexts = std::nullopt,
        .url_patterns = std::nullopt};

    AddInterceptParameters params2{
        .phases = {BeforeRequestSent, ResponseStarted},
        .contexts = std::nullopt,
        .url_patterns = std::nullopt};

    AddInterceptParameters params3{.phases = {AuthRequired},
                                   .contexts = std::nullopt,
                                   .url_patterns = std::nullopt};

    EXPECT_EQ(params1, params2);
    EXPECT_NE(params1, params3);
}

TEST(BidiTypesNetwork, AddInterceptParametersOptionalFields) {
    using enum InterceptPhase;

    AddInterceptParameters params{.phases = {BeforeRequestSent},
                                  .contexts = std::nullopt,
                                  .url_patterns = std::nullopt};

    EXPECT_FALSE(params.contexts.has_value());
    EXPECT_FALSE(params.url_patterns.has_value());

    params.contexts = std::vector<std::string>{"ctx-1", "ctx-2"};

    EXPECT_TRUE(params.contexts.has_value());
    EXPECT_EQ(params.contexts->size(), 2U);
    EXPECT_EQ((*params.contexts)[0], "ctx-1");
}

TEST(BidiTypesNetwork, AddInterceptParametersBoostJson) {
    using enum InterceptPhase;

    AddInterceptParameters params{
        .phases = {BeforeRequestSent, ResponseStarted},
        .contexts = std::vector<std::string>{"ctx-123"},
        .url_patterns = std::nullopt};

    // Serialization
    auto jv = boost::json::value_from(params);
    auto obj = jv.as_object();

    EXPECT_TRUE(obj.contains("phases"));
    auto phases = obj.at("phases").as_array();
    EXPECT_EQ(phases.size(), 2U);
    EXPECT_EQ(phases[0].as_string(), "beforeRequestSent");
    EXPECT_EQ(phases[1].as_string(), "responseStarted");

    EXPECT_TRUE(obj.contains("contexts"));
    auto contexts = obj.at("contexts").as_array();
    EXPECT_EQ(contexts.size(), 1U);
    EXPECT_EQ(contexts[0].as_string(), "ctx-123");
}

// ==================== ContinueRequestParameters Tests ====================

TEST(BidiTypesNetwork, ContinueRequestParametersEquality) {
    // Order: request, body, cookies, headers, method, url
    ContinueRequestParameters params1{.request = "req-123",
                                      .body = std::nullopt,
                                      .cookies = std::nullopt,
                                      .headers = std::nullopt,
                                      .method = "POST",
                                      .url = "https://example.com/api"};

    ContinueRequestParameters params2{.request = "req-123",
                                      .body = std::nullopt,
                                      .cookies = std::nullopt,
                                      .headers = std::nullopt,
                                      .method = "POST",
                                      .url = "https://example.com/api"};

    ContinueRequestParameters params3{.request = "req-456",
                                      .body = std::nullopt,
                                      .cookies = std::nullopt,
                                      .headers = std::nullopt,
                                      .method = std::nullopt,
                                      .url = std::nullopt};

    EXPECT_EQ(params1, params2);
    EXPECT_NE(params1, params3);
}

TEST(BidiTypesNetwork, ContinueRequestParametersOptionalFields) {
    ContinueRequestParameters params{.request = "req-123",
                                     .body = std::nullopt,
                                     .cookies = std::nullopt,
                                     .headers = std::nullopt,
                                     .method = std::nullopt,
                                     .url = std::nullopt};

    EXPECT_FALSE(params.body.has_value());
    EXPECT_FALSE(params.cookies.has_value());
    EXPECT_FALSE(params.headers.has_value());
    EXPECT_FALSE(params.method.has_value());
    EXPECT_FALSE(params.url.has_value());

    params.method = "PUT";
    params.url = "https://modified.com";

    EXPECT_TRUE(params.method.has_value());
    EXPECT_EQ(*params.method, "PUT");
    EXPECT_TRUE(params.url.has_value());
    EXPECT_EQ(*params.url, "https://modified.com");
}

// ==================== ContinueResponseParameters Tests ====================

TEST(BidiTypesNetwork, ContinueResponseParametersEquality) {
    // Order: request, cookies, credentials, headers, reason_phrase, status_code
    ContinueResponseParameters params1{.request = "req-123",
                                       .cookies = std::nullopt,
                                       .credentials = std::nullopt,
                                       .headers = std::nullopt,
                                       .reason_phrase = "OK",
                                       .status_code = 200};

    ContinueResponseParameters params2{.request = "req-123",
                                       .cookies = std::nullopt,
                                       .credentials = std::nullopt,
                                       .headers = std::nullopt,
                                       .reason_phrase = "OK",
                                       .status_code = 200};

    ContinueResponseParameters params3{.request = "req-456",
                                       .cookies = std::nullopt,
                                       .credentials = std::nullopt,
                                       .headers = std::nullopt,
                                       .reason_phrase = std::nullopt,
                                       .status_code = 404};

    EXPECT_EQ(params1, params2);
    EXPECT_NE(params1, params3);
}

TEST(BidiTypesNetwork, ContinueResponseParametersWithCredentials) {
    AuthCredentials creds{
        .type = "password", .username = "user", .password = "pass"};

    ContinueResponseParameters params{.request = "req-123",
                                      .cookies = std::nullopt,
                                      .credentials = creds,
                                      .headers = std::nullopt,
                                      .reason_phrase = std::nullopt,
                                      .status_code = 200};

    EXPECT_TRUE(params.credentials.has_value());
    EXPECT_EQ(params.credentials->username, "user");
}

// ==================== BeforeRequestSentParameters Tests ====================

TEST(BidiTypesNetwork, BeforeRequestSentParametersEquality) {
    RequestData req_data;
    req_data.request_id = "req-123";
    req_data.url = "https://example.com";
    req_data.method = "GET";

    BeforeRequestSentParameters params1{
        .base = BaseParameters{.request = "req-123",
                               .navigation = std::nullopt,
                               .context = std::nullopt,
                               .timestamp = 0,
                               .redirect_count = 0,
                               .is_blocked = false,
                               .intercepts = std::nullopt},
        .request = req_data};

    BeforeRequestSentParameters params2{
        .base = BaseParameters{.request = "req-123",
                               .navigation = std::nullopt,
                               .context = std::nullopt,
                               .timestamp = 0,
                               .redirect_count = 0,
                               .is_blocked = false,
                               .intercepts = std::nullopt},
        .request = req_data};

    EXPECT_EQ(params1, params2);
}

TEST(BidiTypesNetwork, BeforeRequestSentParametersConstruction) {
    RequestData req_data;
    req_data.request_id = "req-456";
    req_data.url = "https://api.example.com";
    req_data.method = "POST";

    BaseParameters base{.request = "req-456",
                        .navigation = std::nullopt,
                        .context = "ctx-789",
                        .timestamp = 0,
                        .redirect_count = 0,
                        .is_blocked = true,
                        .intercepts = std::nullopt};

    BeforeRequestSentParameters params{.base = base, .request = req_data};

    EXPECT_EQ(params.base.request, "req-456");
    EXPECT_TRUE(params.base.is_blocked);
    EXPECT_EQ(params.request.request_id, "req-456");
    EXPECT_EQ(params.request.method, "POST");
}

// ==================== ResponseStartedParameters Tests ====================

TEST(BidiTypesNetwork, ResponseStartedParametersEquality) {
    ResponseData resp_data;
    resp_data.url = "https://example.com";
    resp_data.status = 200;

    ResponseStartedParameters params1{
        .base = BaseParameters{.request = "req-123",
                               .navigation = std::nullopt,
                               .context = std::nullopt,
                               .timestamp = 0,
                               .redirect_count = 0,
                               .is_blocked = false,
                               .intercepts = std::nullopt},
        .response = resp_data};

    ResponseStartedParameters params2{
        .base = BaseParameters{.request = "req-123",
                               .navigation = std::nullopt,
                               .context = std::nullopt,
                               .timestamp = 0,
                               .redirect_count = 0,
                               .is_blocked = false,
                               .intercepts = std::nullopt},
        .response = resp_data};

    EXPECT_EQ(params1, params2);
}

TEST(BidiTypesNetwork, ResponseStartedParametersConstruction) {
    ResponseData resp_data;
    resp_data.url = "https://api.example.com";
    resp_data.status = 201;
    resp_data.status_text = "Created";

    BaseParameters base{.request = "req-456",
                        .navigation = std::nullopt,
                        .context = "ctx-789",
                        .timestamp = 0,
                        .redirect_count = 0,
                        .is_blocked = false,
                        .intercepts = std::nullopt};

    ResponseStartedParameters params{.base = base, .response = resp_data};

    EXPECT_EQ(params.base.request, "req-456");
    EXPECT_EQ(params.response.status, 201U);
    EXPECT_EQ(params.response.status_text, "Created");
}

// ==================== AuthRequiredParameters Tests ====================

TEST(BidiTypesNetwork, AuthRequiredParametersEquality) {
    ResponseData resp_data;
    resp_data.url = "https://example.com";
    resp_data.status = 401;

    AuthRequiredParameters params1{
        .base = BaseParameters{.request = "req-123",
                               .navigation = std::nullopt,
                               .context = std::nullopt,
                               .timestamp = 0,
                               .redirect_count = 0,
                               .is_blocked = false,
                               .intercepts = std::nullopt},
        .response = resp_data};

    AuthRequiredParameters params2{
        .base = BaseParameters{.request = "req-123",
                               .navigation = std::nullopt,
                               .context = std::nullopt,
                               .timestamp = 0,
                               .redirect_count = 0,
                               .is_blocked = false,
                               .intercepts = std::nullopt},
        .response = resp_data};

    EXPECT_EQ(params1, params2);
}

TEST(BidiTypesNetwork, AuthRequiredParametersConstruction) {
    ResponseData resp_data;
    resp_data.url = "https://secure.example.com";
    resp_data.status = 401;
    resp_data.status_text = "Unauthorized";

    AuthChallenge challenge{.scheme = "Basic", .realm = "Protected Area"};
    resp_data.auth_challenges = std::vector<AuthChallenge>{challenge};

    BaseParameters base{.request = "req-456",
                        .navigation = std::nullopt,
                        .context = "ctx-789",
                        .timestamp = 0,
                        .redirect_count = 0,
                        .is_blocked = true,
                        .intercepts = std::nullopt};

    AuthRequiredParameters params{.base = base, .response = resp_data};

    EXPECT_EQ(params.base.request, "req-456");
    EXPECT_TRUE(params.base.is_blocked);
    EXPECT_EQ(params.response.status, 401U);
    EXPECT_TRUE(params.response.auth_challenges.has_value());
    EXPECT_EQ(params.response.auth_challenges->size(), 1U);
    EXPECT_EQ((*params.response.auth_challenges)[0].scheme, "Basic");
}

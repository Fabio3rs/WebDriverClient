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

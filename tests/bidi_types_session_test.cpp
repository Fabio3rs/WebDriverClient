// tests/bidi_types_session_test.cpp - Unit tests for session types
#include "bidi/types/session.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>

using namespace bidi::types::session;

// ==================== ProxyType Tests ====================

TEST(BidiTypesSession, ProxyTypeToString) {
    using enum ProxyType;

    EXPECT_EQ(to_string(Autodetect), "autodetect");
    EXPECT_EQ(to_string(Direct), "direct");
    EXPECT_EQ(to_string(Manual), "manual");
    EXPECT_EQ(to_string(Pac), "pac");
    EXPECT_EQ(to_string(System), "system");
}

TEST(BidiTypesSession, ProxyTypeParse) {
    auto type1 = parse_proxy_type("autodetect");
    ASSERT_TRUE(type1.has_value());
    EXPECT_EQ(*type1, ProxyType::Autodetect);

    auto type2 = parse_proxy_type("manual");
    ASSERT_TRUE(type2.has_value());
    EXPECT_EQ(*type2, ProxyType::Manual);

    auto unknown = parse_proxy_type("unknown_type");
    EXPECT_FALSE(unknown.has_value());
}

TEST(BidiTypesSession, ProxyTypeRoundTrip) {
    using enum ProxyType;

    auto test_round_trip = [](ProxyType type) {
        auto str = to_string(type);
        auto parsed = parse_proxy_type(str);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, type);
    };

    test_round_trip(Autodetect);
    test_round_trip(Direct);
    test_round_trip(Manual);
    test_round_trip(Pac);
    test_round_trip(System);
}

TEST(BidiTypesSession, ProxyTypeBoostJson) {
    using enum ProxyType;

    // Serialization
    auto jv1 = boost::json::value_from(Manual);
    EXPECT_EQ(jv1.as_string(), "manual");

    // Deserialization
    boost::json::value jv2 = "pac";
    auto type = boost::json::value_to<ProxyType>(jv2);
    EXPECT_EQ(type, Pac);

    // Round-trip
    auto jv3 = boost::json::value_from(System);
    auto parsed = boost::json::value_to<ProxyType>(jv3);
    EXPECT_EQ(parsed, System);
}

TEST(BidiTypesSession, ProxyTypeInvalidJson) {
    // Invalid proxy type should throw
    boost::json::value jv = "invalid_proxy_type";
    EXPECT_THROW(boost::json::value_to<ProxyType>(jv), std::runtime_error);
}

// ==================== UserPromptAction Tests ====================

TEST(BidiTypesSession, UserPromptActionToString) {
    using enum UserPromptAction;

    EXPECT_EQ(to_string(Accept), "accept");
    EXPECT_EQ(to_string(Dismiss), "dismiss");
    EXPECT_EQ(to_string(Ignore), "ignore");
}

TEST(BidiTypesSession, UserPromptActionParse) {
    auto action1 = parse_user_prompt_action("accept");
    ASSERT_TRUE(action1.has_value());
    EXPECT_EQ(*action1, UserPromptAction::Accept);

    auto action2 = parse_user_prompt_action("dismiss");
    ASSERT_TRUE(action2.has_value());
    EXPECT_EQ(*action2, UserPromptAction::Dismiss);

    auto unknown = parse_user_prompt_action("unknown");
    EXPECT_FALSE(unknown.has_value());
}

TEST(BidiTypesSession, UserPromptActionBoostJson) {
    using enum UserPromptAction;

    // Serialization
    auto jv = boost::json::value_from(Accept);
    EXPECT_EQ(jv.as_string(), "accept");

    // Deserialization
    boost::json::value jv2 = "dismiss";
    auto action = boost::json::value_to<UserPromptAction>(jv2);
    EXPECT_EQ(action, Dismiss);

    // Invalid action should throw
    boost::json::value jv3 = "invalid_action";
    EXPECT_THROW(boost::json::value_to<UserPromptAction>(jv3),
                 std::runtime_error);
}

// ==================== Struct Tests ====================

TEST(BidiTypesSession, SocksProxyConfigurationEquality) {
    SocksProxyConfiguration cfg1{
        .host = "localhost", .port = 8080, .version = 5};
    SocksProxyConfiguration cfg2{
        .host = "localhost", .port = 8080, .version = 5};
    SocksProxyConfiguration cfg3{
        .host = "remotehost", .port = 9090, .version = 4};

    EXPECT_EQ(cfg1, cfg2);
    EXPECT_NE(cfg1, cfg3);
}

TEST(BidiTypesSession, ProxyConfigurationEquality) {
    ProxyConfiguration cfg1;
    cfg1.type = ProxyType::Manual;
    cfg1.http_proxy = "http://proxy:8080";

    ProxyConfiguration cfg2;
    cfg2.type = ProxyType::Manual;
    cfg2.http_proxy = "http://proxy:8080";

    ProxyConfiguration cfg3;
    cfg3.type = ProxyType::Direct;

    EXPECT_EQ(cfg1, cfg2);
    EXPECT_NE(cfg1, cfg3);
}

TEST(BidiTypesSession, UserPromptHandlerEquality) {
    UserPromptHandler handler1;
    handler1.alert = UserPromptAction::Accept;
    handler1.confirm = UserPromptAction::Dismiss;

    UserPromptHandler handler2;
    handler2.alert = UserPromptAction::Accept;
    handler2.confirm = UserPromptAction::Dismiss;

    UserPromptHandler handler3;
    handler3.alert = UserPromptAction::Ignore;

    EXPECT_EQ(handler1, handler2);
    EXPECT_NE(handler1, handler3);
}

TEST(BidiTypesSession, CapabilityRequestEquality) {
    CapabilityRequest req1;
    req1.accept_insecure_certs = true;
    req1.browser_name = "chrome";

    CapabilityRequest req2;
    req2.accept_insecure_certs = true;
    req2.browser_name = "chrome";

    CapabilityRequest req3;
    req3.browser_name = "firefox";

    EXPECT_EQ(req1, req2);
    EXPECT_NE(req1, req3);
}

TEST(BidiTypesSession, SubscriptionRequestEquality) {
    SubscriptionRequest req1;
    req1.events = {"log.entryAdded", "script.message"};
    req1.contexts = std::vector<std::string>{"ctx-1"};

    SubscriptionRequest req2;
    req2.events = {"log.entryAdded", "script.message"};
    req2.contexts = std::vector<std::string>{"ctx-1"};

    SubscriptionRequest req3;
    req3.events = {"log.entryAdded"};

    EXPECT_EQ(req1, req2);
    EXPECT_NE(req1, req3);
}

// ==================== Optional Field Tests ====================

TEST(BidiTypesSession, ProxyConfigurationOptionalFields) {
    ProxyConfiguration cfg;
    cfg.type = ProxyType::Manual;

    // Optional fields should be empty by default
    EXPECT_FALSE(cfg.http_proxy.has_value());
    EXPECT_FALSE(cfg.ssl_proxy.has_value());
    EXPECT_FALSE(cfg.socks.has_value());
    EXPECT_FALSE(cfg.no_proxy.has_value());
    EXPECT_FALSE(cfg.pac_url.has_value());

    // Set optional fields
    cfg.http_proxy = "http://proxy:8080";
    cfg.no_proxy = std::vector<std::string>{"localhost", "127.0.0.1"};

    EXPECT_TRUE(cfg.http_proxy.has_value());
    EXPECT_EQ(*cfg.http_proxy, "http://proxy:8080");
    EXPECT_TRUE(cfg.no_proxy.has_value());
    EXPECT_EQ(cfg.no_proxy->size(), 2);
}

TEST(BidiTypesSession, UserPromptHandlerOptionalFields) {
    UserPromptHandler handler;

    // All fields optional by default
    EXPECT_FALSE(handler.alert.has_value());
    EXPECT_FALSE(handler.before_unload.has_value());
    EXPECT_FALSE(handler.confirm.has_value());
    EXPECT_FALSE(handler.prompt.has_value());
    EXPECT_FALSE(handler.file.has_value());
    EXPECT_FALSE(handler.default_action.has_value());

    // Set specific handlers
    handler.alert = UserPromptAction::Accept;
    handler.confirm = UserPromptAction::Dismiss;

    EXPECT_TRUE(handler.alert.has_value());
    EXPECT_EQ(*handler.alert, UserPromptAction::Accept);
    EXPECT_TRUE(handler.confirm.has_value());
    EXPECT_EQ(*handler.confirm, UserPromptAction::Dismiss);
    EXPECT_FALSE(handler.prompt.has_value());
}

// ==================== Type Safety Tests ====================

TEST(BidiTypesSession, ProxyTypeConstexpr) {
    // Verify constexpr evaluation
    constexpr auto str = to_string(ProxyType::Direct);
    static_assert(str == "direct");
}

TEST(BidiTypesSession, UserPromptActionConstexpr) {
    // Verify constexpr evaluation
    constexpr auto str = to_string(UserPromptAction::Accept);
    static_assert(str == "accept");
}

TEST(BidiTypesSession, StructDefaultConstruction) {
    // Verify all structs are default-constructible
    SocksProxyConfiguration socks;
    EXPECT_EQ(socks.port, 0);
    EXPECT_EQ(socks.version, 5);

    ProxyConfiguration proxy;
    EXPECT_EQ(proxy.type, ProxyType::Direct);

    UserPromptHandler handler;
    // All optionals should be empty
    EXPECT_FALSE(handler.alert.has_value());

    CapabilityRequest cap;
    EXPECT_FALSE(cap.accept_insecure_certs.has_value());

    SubscriptionRequest sub;
    EXPECT_TRUE(sub.events.empty());
}

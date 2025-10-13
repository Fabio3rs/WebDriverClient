#include "bidi/user_prompt_handler.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>

using namespace bidi;
using namespace bidi::types::browsing_context;
using namespace bidi::types::session;

// ======================== UserPromptHandlerConfig Tests
// ========================

TEST(UserPromptPolicy, ConfigFactoryAcceptAll) {
    auto config = UserPromptHandlerConfig::accept_all();
    EXPECT_EQ(config.policy, UserPromptPolicy::AcceptAll);
    EXPECT_FALSE(config.custom_handler.has_value());
}

TEST(UserPromptPolicy, ConfigFactoryDismissAll) {
    auto config = UserPromptHandlerConfig::dismiss_all();
    EXPECT_EQ(config.policy, UserPromptPolicy::DismissAll);
    EXPECT_FALSE(config.custom_handler.has_value());
}

TEST(UserPromptPolicy, ConfigFactoryIgnoreAll) {
    auto config = UserPromptHandlerConfig::ignore_all();
    EXPECT_EQ(config.policy, UserPromptPolicy::IgnoreAll);
    EXPECT_FALSE(config.custom_handler.has_value());
}

TEST(UserPromptPolicy, ConfigFactoryCustom) {
    bool callback_invoked = false;
    auto callback =
        [&callback_invoked](const UserPromptOpenedParameters & /*ununsed*/) {
            callback_invoked = true;
            return std::optional<UserPromptResolution>{
                UserPromptResolution{.accept = true, .user_text = "input"}};
        };

    auto config = UserPromptHandlerConfig::custom(callback);
    EXPECT_EQ(config.policy, UserPromptPolicy::Custom);
    ASSERT_TRUE(config.custom_handler.has_value());

    // Verify callback is stored and callable
    UserPromptOpenedParameters dummy_params;
    dummy_params.context = "test";
    dummy_params.handler = UserPromptAction::Accept;
    dummy_params.message = "test message";
    dummy_params.type = UserPromptType::Alert;

    auto result = (*config.custom_handler)(dummy_params);
    EXPECT_TRUE(callback_invoked);
    EXPECT_TRUE(result.has_value());
}

// ======================== JSON to UserPromptOpenedParameters Tests
// ========================

TEST(UserPromptPolicy, FromJsonPromptOpenedComplete) {
    boost::json::object obj;
    obj["context"] = "CTX-123";
    obj["handler"] = "accept";
    obj["message"] = "Are you sure?";
    obj["type"] = "confirm";
    obj["defaultValue"] = "default text";

    auto params = from_json_prompt_opened(obj);

    EXPECT_EQ(params.context, "CTX-123");
    EXPECT_EQ(params.handler, UserPromptAction::Accept);
    EXPECT_EQ(params.message, "Are you sure?");
    EXPECT_EQ(params.type, UserPromptType::Confirm);
    ASSERT_TRUE(params.default_value.has_value());
    EXPECT_EQ(*params.default_value, "default text");
}

TEST(UserPromptPolicy, FromJsonPromptOpenedMinimal) {
    boost::json::object obj;
    obj["context"] = "CTX-456";
    obj["handler"] = "dismiss";
    obj["message"] = "Alert!";
    obj["type"] = "alert";
    // No defaultValue (optional)

    auto params = from_json_prompt_opened(obj);

    EXPECT_EQ(params.context, "CTX-456");
    EXPECT_EQ(params.handler, UserPromptAction::Dismiss);
    EXPECT_EQ(params.message, "Alert!");
    EXPECT_EQ(params.type, UserPromptType::Alert);
    EXPECT_FALSE(params.default_value.has_value());
}

TEST(UserPromptPolicy, FromJsonPromptOpenedAllTypes) {
    // Test all UserPromptType values
    const std::vector<std::pair<const char *, UserPromptType>> type_pairs = {
        {"alert", UserPromptType::Alert},
        {"beforeUnload", UserPromptType::BeforeUnload},
        {"confirm", UserPromptType::Confirm},
        {"prompt", UserPromptType::Prompt}};

    for (const auto &[type_str, expected_type] : type_pairs) {
        boost::json::object obj;
        obj["context"] = "CTX";
        obj["handler"] = "ignore";
        obj["message"] = "message";
        obj["type"] = type_str;

        auto params = from_json_prompt_opened(obj);
        EXPECT_EQ(params.type, expected_type);
    }
}

TEST(UserPromptPolicy, FromJsonPromptOpenedMissingContext) {
    boost::json::object obj;
    // Missing "context"
    obj["handler"] = "accept";
    obj["message"] = "message";
    obj["type"] = "alert";

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_opened(obj); },
        std::runtime_error);
}

TEST(UserPromptPolicy, FromJsonPromptOpenedMissingHandler) {
    boost::json::object obj;
    obj["context"] = "CTX";
    // Missing "handler"
    obj["message"] = "message";
    obj["type"] = "alert";

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_opened(obj); },
        std::runtime_error);
}

TEST(UserPromptPolicy, FromJsonPromptOpenedMissingMessage) {
    boost::json::object obj;
    obj["context"] = "CTX";
    obj["handler"] = "accept";
    // Missing "message"
    obj["type"] = "alert";

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_opened(obj); },
        std::runtime_error);
}

TEST(UserPromptPolicy, FromJsonPromptOpenedMissingType) {
    boost::json::object obj;
    obj["context"] = "CTX";
    obj["handler"] = "accept";
    obj["message"] = "message";
    // Missing "type"

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_opened(obj); },
        std::runtime_error);
}

TEST(UserPromptPolicy, FromJsonPromptOpenedInvalidType) {
    boost::json::object obj;
    obj["context"] = "CTX";
    obj["handler"] = "accept";
    obj["message"] = "message";
    obj["type"] = "invalid_type";

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_opened(obj); },
        std::runtime_error);
}

// ======================== JSON to UserPromptClosedParameters Tests
// ========================

TEST(UserPromptPolicy, FromJsonPromptClosedComplete) {
    boost::json::object obj;
    obj["context"] = "CTX-789";
    obj["accepted"] = true;
    obj["type"] = "prompt";
    obj["userText"] = "user input";

    auto params = from_json_prompt_closed(obj);

    EXPECT_EQ(params.context, "CTX-789");
    EXPECT_TRUE(params.accepted);
    EXPECT_EQ(params.type, UserPromptType::Prompt);
    ASSERT_TRUE(params.user_text.has_value());
    EXPECT_EQ(*params.user_text, "user input");
}

TEST(UserPromptPolicy, FromJsonPromptClosedMinimal) {
    boost::json::object obj;
    obj["context"] = "CTX-000";
    obj["accepted"] = false;
    obj["type"] = "confirm";
    // No userText (optional)

    auto params = from_json_prompt_closed(obj);

    EXPECT_EQ(params.context, "CTX-000");
    EXPECT_FALSE(params.accepted);
    EXPECT_EQ(params.type, UserPromptType::Confirm);
    EXPECT_FALSE(params.user_text.has_value());
}

TEST(UserPromptPolicy, FromJsonPromptClosedMissingContext) {
    boost::json::object obj;
    // Missing "context"
    obj["accepted"] = true;
    obj["type"] = "alert";

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_closed(obj); },
        std::runtime_error);
}

TEST(UserPromptPolicy, FromJsonPromptClosedMissingAccepted) {
    boost::json::object obj;
    obj["context"] = "CTX";
    // Missing "accepted"
    obj["type"] = "alert";

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_closed(obj); },
        std::runtime_error);
}

TEST(UserPromptPolicy, FromJsonPromptClosedMissingType) {
    boost::json::object obj;
    obj["context"] = "CTX";
    obj["accepted"] = true;
    // Missing "type"

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_closed(obj); },
        std::runtime_error);
}

TEST(UserPromptPolicy, FromJsonPromptClosedInvalidType) {
    boost::json::object obj;
    obj["context"] = "CTX";
    obj["accepted"] = true;
    obj["type"] = "not_a_valid_type";

    EXPECT_THROW(
        { [[maybe_unused]] auto result = from_json_prompt_closed(obj); },
        std::runtime_error);
}

// ======================== UserPromptResolution Tests ========================

TEST(UserPromptPolicy, UserPromptResolutionDefaultConstruction) {
    UserPromptResolution resolution;
    EXPECT_TRUE(resolution.accept); // Default: true
    EXPECT_FALSE(resolution.user_text.has_value());
}

TEST(UserPromptPolicy, UserPromptResolutionDesignatedInitializers) {
    UserPromptResolution resolution{.accept = false,
                                    .user_text = "custom text"};
    EXPECT_FALSE(resolution.accept);
    ASSERT_TRUE(resolution.user_text.has_value());
    EXPECT_EQ(*resolution.user_text, "custom text");
}

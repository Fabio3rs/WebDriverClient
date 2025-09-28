#include "bidi/core.hpp"
#include <gtest/gtest.h>

using namespace bidi::core;

TEST(BidiParsingNegative, DetectMessageKindVariants) {
    // Response success
    EXPECT_EQ(detect_message_kind("{\"id\":1,\"type\":\"success\"}"),
              MessageKind::Response);
    // Response error
    EXPECT_EQ(detect_message_kind("{\"id\":2, \"type\": \"error\"}"),
              MessageKind::Response);
    // Event
    EXPECT_EQ(detect_message_kind("{\"type\":\"event\",\"method\":\"x\"}"),
              MessageKind::Event);
    // Command
    EXPECT_EQ(detect_message_kind("{\"id\":3,\"method\":\"x\"}"),
              MessageKind::Command);
    // Unknown (missing id for command semantics)
    EXPECT_EQ(detect_message_kind("{\"method\":\"x\"}"), MessageKind::Unknown);
    // Unknown (no markers)
    EXPECT_EQ(detect_message_kind("{}"), MessageKind::Unknown);
}

TEST(BidiParsingNegative, ParseResponseInvalidCases) {
    // Missing id
    EXPECT_FALSE(parse_response("{\"type\":\"success\",\"result\":{}}"));
    // Missing type
    EXPECT_FALSE(parse_response("{\"id\":1,\"result\":{}}"));
    // Unknown type
    EXPECT_FALSE(parse_response("{\"id\":1,\"type\":\"weird\"}"));
    // Error without error/message (allowed but we check fields empty)
    auto err = parse_response(R"({"id":2,"type":"error"})");
    ASSERT_TRUE(err.has_value());
    EXPECT_FALSE(err->is_success);
    EXPECT_TRUE(err->error_code.empty());
    EXPECT_TRUE(err->error_message.empty());
    // Success without result (allowed: result defaults empty)
    auto ok = parse_response(R"({"id":3,"type":"success"})");
    ASSERT_TRUE(ok.has_value());
    EXPECT_TRUE(ok->is_success);
    EXPECT_TRUE(ok->result.empty());
}

TEST(BidiParsingNegative, ParseEventInvalidCases) {
    // Missing type
    EXPECT_FALSE(parse_event("{\"method\":\"x\"}"));
    // Wrong type
    EXPECT_FALSE(parse_event("{\"type\":\"success\",\"method\":\"x\"}"));
    // Missing method
    EXPECT_FALSE(parse_event("{\"type\":\"event\"}"));
}

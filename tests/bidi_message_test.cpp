#include "bidi/commands.hpp"
#include "bidi/core.hpp"
#include "bidi_methods.hpp"
#include <gtest/gtest.h>

using namespace bidi::core;

TEST(BidiMessage, IsIdSafe) {
    // Test ID safety for BiDi protocol (JavaScript safe integers)
    EXPECT_TRUE(is_id_safe(1));
    EXPECT_TRUE(is_id_safe(9007199254740991ULL));
    EXPECT_FALSE(is_id_safe(9007199254740992ULL));
}

TEST(BidiMessage, BuildAndDetect) {
    // Test command building and message detection
    auto command_msg = build_command(42, "session.new",
                                     {{"capabilities", boost::json::object{}}});
    auto kind = detect_message_kind(command_msg);
    // Commands have id + method, so they're detected as Command-type messages
    EXPECT_EQ(kind, MessageKind::Command);
}

TEST(BidiMessage, ParseResponse) {
    // Novo formato: resposta MUST conter type:"success" segundo parser
    // atualizado.
    std::string response_json =
        R"({"id":42,"type":"success","result":{"sessionId":"test-session"}})";
    auto parsed = parse_response(response_json);
    EXPECT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_success);
    EXPECT_EQ(parsed->id, 42);
    ASSERT_TRUE(parsed->result.if_contains("sessionId"));
    EXPECT_EQ(parsed->result["sessionId"].as_string(), "test-session");
}

TEST(BidiMessage, ParseEvent) {
    // Test event parsing
    std::string event_json = R"({"type":"event","method":")" +
                             std::string(bidi::ids::events::log_entryAdded) +
                             R"(","params":{"level":"info","text":"test"}})";
    auto parsed = parse_event(event_json);

    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->method, bidi::ids::events::log_entryAdded);
}

// Novo teste: erro de resposta
TEST(BidiMessage, ParseErrorResponse) {
    std::string error_json =
        R"({"id":7,"type":"error","error":"invalid argument","message":"bad param"})";
    auto parsed = parse_response(error_json);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->is_success);
    EXPECT_EQ(parsed->id, 7);
    EXPECT_EQ(parsed->error_code, "invalid argument");
    EXPECT_EQ(parsed->error_message, "bad param");
}

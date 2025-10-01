#include "bidi/core.hpp"
#include <gtest/gtest.h>

using namespace bidi::core;

TEST(BidiCoreCorrections, ParseNegativeID) {
    // Teste: IDs negativos devem ser rejeitados
    std::string response_json =
        R"({"id": -1, "type": "success", "result": {}})";
    auto parsed = parse_response(response_json);
    EXPECT_FALSE(parsed.has_value());
}

TEST(BidiCoreCorrections, ParseLargeIDAsString) {
    // Teste: IDs maiores que MAX_SAFE_ID devem ser aceitos mas com warning
    std::string response_json = R"({
        "id": "9007199254740992",
        "type": "success",
        "result": {}
    })";
    auto parsed = parse_response(response_json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, 9007199254740992ULL);
}

TEST(BidiCoreCorrections, ParseErrorWithStacktrace) {
    // Teste: Campo stacktrace deve ser extraído
    std::string error_json = R"({
        "id": 1,
        "type": "error",
        "error": "unknown error",
        "message": "Something went wrong",
        "stacktrace": "at function1\nat function2"
    })";
    auto parsed = parse_response(error_json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->is_success);
    EXPECT_EQ(parsed->stacktrace, "at function1\nat function2");
}

TEST(BidiCoreCorrections, DetectMessageKindNoFalsePositives) {
    // Teste: Não deve haver falsos positivos na detecção de tipo
    std::string response_with_event_text = R"({
        "id": 1,
        "type": "success",
        "result": {
            "description": "This contains type:\"event\" text"
        }
    })";
    EXPECT_EQ(detect_message_kind(response_with_event_text),
              MessageKind::Response);

    std::string event_message = R"({
        "type": "event",
        "method": "test.event"
    })";
    EXPECT_EQ(detect_message_kind(event_message), MessageKind::Event);
}

TEST(BidiCoreCorrections, ParseSuccessResponseValidatesResult) {
    // Teste: Resposta de sucesso sem result deve ter result vazio
    std::string success_no_result = R"({"id": 1, "type": "success"})";
    auto parsed = parse_response(success_no_result);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->is_success);
    EXPECT_TRUE(parsed->result.empty());
}

TEST(BidiCoreCorrections, ParseSuccessResponseWithInvalidResult) {
    // Teste: Resposta de sucesso com result não-objeto deve ter result vazio
    std::string success_invalid_result =
        R"({"id": 1, "type": "success", "result": "invalid"})";
    auto parsed = parse_response(success_invalid_result);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->is_success);
    EXPECT_TRUE(parsed->result.empty());
}

// tests/script_eval_parsing_test.cpp - Testes de parsing de exceptions de
// script
#include "bidi/script_eval.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>

using namespace bidi::script;

TEST(ScriptEvalParsingTest, DetectsNoExceptionOnNormalResult) {
    boost::json::object normal;
    normal["type"] = "success"; // qualquer valor != "exception"
    EXPECT_FALSE(is_script_exception_result(normal));
}

TEST(ScriptEvalParsingTest, DetectsExceptionTypeEvenWithoutDetails) {
    boost::json::object obj;
    obj["type"] = "exception"; // faltando exceptionDetails
    EXPECT_TRUE(is_script_exception_result(obj));
    auto details = parse_script_exception(obj);
    EXPECT_EQ(details.exception_type, "exception");
    EXPECT_TRUE(details.raw.empty());
}

TEST(ScriptEvalParsingTest, ParsesFullExceptionDetails) {
    // Simula fragmento típico de resposta BiDi para script exception
    boost::json::object root;
    root["type"] = "exception";
    boost::json::object details;
    details["text"] = "ReferenceError: foo is not defined";
    details["lineNumber"] = 10;
    details["columnNumber"] = 5;
    boost::json::object inner;
    inner["value"] = "ReferenceError: foo is not defined";
    inner["className"] = "ReferenceError";
    inner["type"] = "error";
    details["exception"] = inner;
    boost::json::array stack;
    {
        boost::json::object frame;
        frame["url"] = "https://example.com/app.js";
        frame["functionName"] = "init";
        frame["lineNumber"] = 10;
        frame["columnNumber"] = 5;
        stack.push_back(frame);
    }
    details["stackTrace"] = stack;
    root["exceptionDetails"] = details;

    ASSERT_TRUE(is_script_exception_result(root));
    auto parsed = parse_script_exception(root);
    EXPECT_EQ(parsed.exception_type, "exception");
    EXPECT_EQ(parsed.text, "ReferenceError: foo is not defined");
    EXPECT_EQ(parsed.name, "ReferenceError");
    ASSERT_TRUE(parsed.line_number.has_value());
    EXPECT_EQ(*parsed.line_number, 10);
    ASSERT_EQ(parsed.stack_frames.size(), 1U);
    EXPECT_EQ(parsed.stack_frames[0].function_name, "init");
}

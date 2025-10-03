// tests/script_eval_policy_test.cpp - Testes de aplicação de política
// script_eval (casos A-G)
#include "bidi/core.hpp"
#include "bidi/script_eval.hpp"
#include <gtest/gtest.h>

using namespace bidi::script;
using bidi::core::ParsedResponse;

namespace {
ParsedResponse make_success_response(boost::json::object result_obj) {
    ParsedResponse r;
    r.id = 1;
    r.is_success = true;
    r.result = std::move(result_obj);
    return r;
}

ParsedResponse make_error_response(std::string code, std::string message) {
    ParsedResponse r;
    r.id = 2;
    r.is_success = false;
    r.error_code = std::move(code);
    r.error_message = std::move(message);
    return r;
}
} // namespace

// Caso A: return_outcome + sucesso normal
TEST(ScriptEvalPolicyTest, ReturnOutcomeSuccessNoException) {
    boost::json::object result;
    result["type"] = "value"; // não é "exception"
    result["value"] = 42;
    auto response = make_success_response(result);
    auto applied = apply_policy(response, script_eval_policy::return_outcome);
    EXPECT_EQ(applied.action,
              bidi::script::PolicyApplicationResult::Action::fulfill);
    EXPECT_FALSE(applied.outcome.has_exception());
    ASSERT_TRUE(applied.outcome.result.is_object());
    auto &obj = applied.outcome.result.as_object();
    EXPECT_EQ(obj.at("value").as_int64(), 42);
    EXPECT_EQ(applied.outcome.raw.at("type").as_string(), "value");
}

// Caso B: return_outcome + script exception com detalhes
TEST(ScriptEvalPolicyTest, ReturnOutcomeScriptException) {
    boost::json::object root;
    root["type"] = "exception";
    boost::json::object details;
    details["text"] = "TypeError: x is not a function";
    boost::json::object inner;
    inner["value"] = "TypeError: x is not a function";
    inner["className"] = "TypeError";
    inner["type"] = "error";
    details["exception"] = inner;
    root["exceptionDetails"] = details;
    auto response = make_success_response(root);
    auto applied = apply_policy(response, script_eval_policy::return_outcome);
    EXPECT_EQ(applied.action,
              bidi::script::PolicyApplicationResult::Action::fulfill);
    ASSERT_TRUE(applied.outcome.has_exception());
    ASSERT_TRUE(applied.outcome.exception.has_value());
    EXPECT_EQ(applied.outcome.exception->name, "TypeError");
    EXPECT_EQ(applied.outcome.exception->text,
              "TypeError: x is not a function");
    EXPECT_TRUE(applied.outcome.result.is_null()); // não preenchido
}

// Caso C: throw_on_script_exception + sucesso normal
TEST(ScriptEvalPolicyTest, ThrowPolicySuccessNoException) {
    boost::json::object result;
    result["type"] = "value";
    result["value"] = true;
    auto response = make_success_response(result);
    auto applied =
        apply_policy(response, script_eval_policy::throw_on_script_exception);
    EXPECT_EQ(applied.action,
              bidi::script::PolicyApplicationResult::Action::fulfill);
    EXPECT_FALSE(applied.outcome.has_exception());
    EXPECT_TRUE(applied.outcome.result.is_object());
}

// Caso D: throw_on_script_exception + script exception (gera ação
// throw_exception)
TEST(ScriptEvalPolicyTest, ThrowPolicyScriptException) {
    boost::json::object root;
    root["type"] = "exception";
    boost::json::object details;
    details["text"] = "ReferenceError: foo is not defined";
    boost::json::object inner;
    inner["value"] = "ReferenceError: foo is not defined";
    inner["className"] = "ReferenceError";
    inner["type"] = "error";
    details["exception"] = inner;
    root["exceptionDetails"] = details;
    auto response = make_success_response(root);
    auto applied =
        apply_policy(response, script_eval_policy::throw_on_script_exception);
    EXPECT_EQ(applied.action,
              bidi::script::PolicyApplicationResult::Action::throw_exception);
    EXPECT_EQ(applied.exception.name, "ReferenceError");
    EXPECT_EQ(applied.exception.text, "ReferenceError: foo is not defined");
}

// Caso E: erro de protocolo (is_success=false) não deve classificar script
// exception e não altera action (caller fará throw genérico)
TEST(ScriptEvalPolicyTest, ProtocolErrorBypassesScriptPolicy) {
    auto response = make_error_response("invalid parameter", "bad args");
    auto applied1 = apply_policy(response, script_eval_policy::return_outcome);
    auto applied2 =
        apply_policy(response, script_eval_policy::throw_on_script_exception);
    EXPECT_EQ(applied1.action,
              bidi::script::PolicyApplicationResult::Action::fulfill);
    EXPECT_EQ(applied2.action,
              bidi::script::PolicyApplicationResult::Action::fulfill);
    EXPECT_FALSE(applied1.outcome.has_exception());
    // Manter visibilidade do tipo original (antes era object esperado, agora
    // null)
    EXPECT_TRUE(applied1.outcome.result.is_null())
        << "kind=" << static_cast<int>(applied1.outcome.result.kind())
        << ", str=" << to_string(applied1.outcome.result.kind());
}

// Caso F: return_outcome + type==exception sem exceptionDetails
TEST(ScriptEvalPolicyTest, ReturnOutcomeExceptionWithoutDetails) {
    boost::json::object root;
    root["type"] = "exception";
    auto response = make_success_response(root);
    auto applied = apply_policy(response, script_eval_policy::return_outcome);
    EXPECT_EQ(applied.action,
              bidi::script::PolicyApplicationResult::Action::fulfill);
    ASSERT_TRUE(applied.outcome.has_exception());
    EXPECT_TRUE(applied.outcome.exception->raw.empty());
    EXPECT_EQ(applied.outcome.exception->exception_type, "exception");
}

// Caso G: return_outcome + múltiplos stack frames mantém ordem
TEST(ScriptEvalPolicyTest, ReturnOutcomeMultipleStackFrames) {
    static const int BASE_LINE = 100;
    static const int BASE_COLUMN = 200;
    boost::json::object root;
    root["type"] = "exception";
    boost::json::object details;
    details["text"] = "Error: boom";
    boost::json::object inner;
    inner["value"] = "Error: boom";
    inner["className"] = "Error";
    inner["type"] = "error";
    details["exception"] = inner;
    boost::json::array stack;
    for (int i = 0; i < 3; ++i) {
        boost::json::object frame;
        frame["url"] = std::string("https://app/") + std::to_string(i);
        frame["functionName"] = std::string("fn") + std::to_string(i);
        frame["lineNumber"] = BASE_LINE + i;
        frame["columnNumber"] = BASE_COLUMN + i;
        stack.push_back(frame);
    }
    details["stackTrace"] = stack;
    root["exceptionDetails"] = details;
    auto response = make_success_response(root);
    auto applied = apply_policy(response, script_eval_policy::return_outcome);
    ASSERT_TRUE(applied.outcome.has_exception());
    ASSERT_EQ(applied.outcome.exception->stack_frames.size(), 3U);
    EXPECT_EQ(applied.outcome.exception->stack_frames[0].function_name, "fn0");
    EXPECT_EQ(applied.outcome.exception->stack_frames[1].function_name, "fn1");
    EXPECT_EQ(applied.outcome.exception->stack_frames[2].function_name, "fn2");
}

// Testes de conformidade com a especificação WebDriver BiDi
// Verificam envelopes de comando, resposta de sucesso, resposta de erro e
// evento. Se algum comportamento observado divergir da spec, o teste falha
// explicitamente para forçar correção no código de produção.

#include "bidi/core.hpp"
#include "bidi_methods.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>

using namespace bidi::core;

namespace {

// Helper para construir JSON bruto simples sem depender de serializer aqui.
auto make_success_response(id_type id, const boost::json::object &result = {})
    -> std::string {
    boost::json::object o;
    o["id"] = static_cast<std::int64_t>(id);
    o["type"] = "success";
    o["result"] = result;
    auto s = boost::json::serialize(o);
    return s;
}

auto make_error_response(id_type id,
                         const std::string &code = "invalid argument",
                         const std::string &msg = "bad") -> std::string {
    boost::json::object o;
    o["id"] = static_cast<std::int64_t>(id);
    o["type"] = "error";
    o["error"] = code;
    o["message"] = msg;
    return boost::json::serialize(o);
}

auto make_event(const std::string &method,
                const boost::json::object &params = {}) -> std::string {
    boost::json::object o;
    o["type"] = "event";
    o["method"] = method;
    o["params"] = params;
    return boost::json::serialize(o);
}

} // namespace

TEST(BidiClientSpec, ParseSuccessResponseAccordingToSpec) {
    boost::json::object result{{"value", 123}};
    auto payload = make_success_response(42, result);
    auto resp = parse_response(payload);
    ASSERT_TRUE(resp.has_value()) << "Resposta válida deve ser parseada";
    EXPECT_EQ(resp->id, 42U);
    EXPECT_TRUE(resp->is_success) << "type=success deve marcar is_success=true";
    ASSERT_TRUE(resp->result.contains("value"));
    EXPECT_EQ(resp->result["value"].as_int64(), 123);
    EXPECT_TRUE(resp->error_code_raw.empty());
}

TEST(BidiClientSpec, ParseErrorResponseAccordingToSpec) {
    auto payload = make_error_response(7, "no such node", "not found");
    auto resp = parse_response(payload);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->id, 7U);
    EXPECT_FALSE(resp->is_success) << "type=error deve marcar is_success=false";
    EXPECT_EQ(resp->error_code_raw, "no such node");
    EXPECT_EQ(resp->error_message, "not found");
}

TEST(BidiClientSpec, ParseEventAccordingToSpec) {
    boost::json::object evp{{"text", "hi"}};
    auto payload =
        make_event(std::string(bidi::ids::events::log_entryAdded), evp);
    auto evt = parse_event(payload);
    ASSERT_TRUE(evt.has_value());
    EXPECT_EQ(evt->method, bidi::ids::events::log_entryAdded);
    ASSERT_TRUE(evt->params.contains("text"));
    EXPECT_EQ(evt->params["text"].as_string(), "hi");
}

TEST(BidiClientSpec, RejectResponseMissingTypeIsOffSpec) {
    // Off-spec: resposta deve ter campo type=success|error
    std::string bad = R"({"id":1,"result":{}})"; // falta "type"
    auto resp = parse_response(bad);
    EXPECT_FALSE(resp.has_value())
        << "Resposta sem type deve ser rejeitada (spec exige type)";
}

TEST(BidiClientSpec, RejectEventMissingTypeIsOffSpec) {
    // Off-spec: evento deve ter type=event
    std::string bad = R"({"method":"log.entryAdded","params":{}})";
    auto evt = parse_event(bad);
    EXPECT_FALSE(evt.has_value()) << "Evento sem type=event deve ser rejeitado";
}

TEST(BidiClientSpec, DetectMessageKindFastPath) {
    EXPECT_EQ(detect_message_kind(R"({"id":2,"method":"x","params":{}})"),
              MessageKind::Command);
    EXPECT_EQ(detect_message_kind(make_success_response(2)),
              MessageKind::Response);
    EXPECT_EQ(detect_message_kind(make_event("log.entryAdded")),
              MessageKind::Event);
    EXPECT_EQ(detect_message_kind(R"({"foo":1})"), MessageKind::Unknown);
}

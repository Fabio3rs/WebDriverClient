// Minimal integration test: start a tiny HTTP server that responds to
// /session/<id>/execute/sync and /session/<id>/execute_async and verify
// WebDriver::executeSyncScript / executeAsyncScript round-trip.

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

#include "WebDriverClient.hpp"
#include "test_http_server.hpp"

using json = nlohmann::json;
using bidi::testing::HttpResponse;
using bidi::testing::TestHttpServer;

// Helper: constrói o JSON de resposta a partir do body da requisição e do path
static auto make_response_body_from(const std::string &body,
                                    const std::string &path) -> std::string
/* NOLINT(bugprone-easily-swappable-parameters) */ {
    nlohmann::json resp_body;
    try {
        if (!body.empty()) {
            auto parsed = json::parse(body);
            // Expect object with script and args
            json value = json::object();
            if (parsed.contains("script")) {
                value["script"] = parsed["script"];
            } else {
                value["script"] = nullptr;
            }

            if (parsed.contains("args") && parsed["args"].is_array()) {
                value["args"] = parsed["args"];
            } else {
                value["args"] = json::array();
            }

            // Decide by path whether sync or async
            if (path.find("execute/sync") != std::string::npos ||
                path.find("execute/sync") != std::string::npos) {
                value["type"] = "sync";
            }
            if (path.find("execute_async") != std::string::npos ||
                path.find("execute/async") != std::string::npos) {
                value["type"] = "async";
            }
            resp_body = json::object({{"value", value}});
        } else {
            resp_body = json::object({{"value", json::object()}});
        }
    } catch (const std::exception &e) {
        resp_body = json::object(
            {{"value", json::object({{"error", std::string("parse_error")}})}});
    }
    return resp_body.dump();
}

TEST(ExecuteScriptIntegration, ExecuteSyncAndAsync) {
    TestHttpServer srv;
    srv.set_default_handler([](const auto &request) {
        auto body = make_response_body_from(request.body, request.path);
        return HttpResponse::json(body);
    });
    uint16_t port = srv.start();

    WebDriver wd;
    wd.sessionId = "testsession";
    wd.webDriverUrl = "http://127.0.0.1:" + std::to_string(port);

    // call sync
    constexpr int kReturnValue = 123;
    auto res_sync = wd.executeSyncScript("return 1;", "arg1", kReturnValue);
    ASSERT_TRUE(res_sync.is_object());
    EXPECT_EQ(res_sync.value("type", ""), "sync");
    EXPECT_TRUE(res_sync.contains("args"));
    EXPECT_EQ(res_sync["args"].size(), 2);

    // call async
    auto res_async = wd.executeAsyncScript("(done)=>done(42);", "a", "b");
    ASSERT_TRUE(res_async.is_object());
    EXPECT_EQ(res_async.value("type", ""), "async");
    EXPECT_TRUE(res_async.contains("args"));
    EXPECT_EQ(res_async["args"].size(), 2);

    srv.stop();
}

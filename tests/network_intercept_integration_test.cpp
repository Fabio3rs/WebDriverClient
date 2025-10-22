#include "test_http_server.hpp"
#include <atomic>
#include <bidi/client.hpp>
#include <bidi/commands.hpp>
#include <bidi/guards.hpp>
#include <bidi/network_intercept_handler.hpp>
#include <bidi/types/network.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <gtest/gtest.h>

using namespace std::chrono_literals;
namespace asio = boost::asio;

class NetworkInterceptBiDiTest : public ::testing::Test {
  public:
    asio::io_context io_context;
    bidi::testing::TestHttpServer server;
    uint16_t port = 0;
    std::string webdriver_url = "http://127.0.0.1:9515";
    std::atomic<bool> finished{false};

    void SetUp() override {
        server.set_default_handler([](const auto &) {
            return bidi::testing::HttpResponse::json(R"({"status": "ok"})");
        });
        port = server.start();
    }
    void TearDown() override { server.stop(); }
    [[nodiscard]] auto testUrl() const -> std::string {
        return "http://127.0.0.1:" + std::to_string(port) + "/test";
    }
};

TEST_F(NetworkInterceptBiDiTest, InterceptSimpleRequest) {
    auto run_test = [this]() -> asio::awaitable<void> {
        bidi::SessionGuard session(webdriver_url);
        std::vector<std::string> args{"--headless", "--no-sandbox"};
        auto ws_url_result = session.connect(args, "chrome", true);
        if (!ws_url_result.has_value()) {
            throw std::runtime_error("Falha ao conectar ao WebSocket");
        }
        auto client_ptr =
            co_await bidi::Client::connect(io_context, ws_url_result.value());
        if (!client_ptr) {
            throw std::runtime_error("Client nulo");
        }
        bidi::ClientGuard client_guard(client_ptr);

        // Configura interceptação para todas as URLs
        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::ContinueAll;
        config.phases = {
            bidi::types::network::InterceptPhase::BeforeRequestSent};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        std::atomic<bool> intercepted{false};
        config.before_request_handler =
            [&](const auto &params
                [[maybe_unused]]) -> std::optional<bidi::RequestResolution> {
            intercepted = true;
            bidi::RequestResolution res;
            res.action = bidi::InterceptAction::Continue;
            res.body = std::nullopt;
            return res;
        };

        auto intercept_handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);
        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);
        co_await client_guard.client()->navigate(context_id, testUrl());

        // Aguarda evento
        for (int i = 0; i < 20 && !intercepted.load(); ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (!intercepted.load()) {
            throw std::runtime_error("Intercept não ocorreu");
        }
        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get(); // Aguarda e propaga exceções da corrotina
    ASSERT_TRUE(finished.load());
}

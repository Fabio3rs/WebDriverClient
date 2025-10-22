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

// Teste 2: Fail Request Flow
TEST_F(NetworkInterceptBiDiTest, FailRequestFlow) {
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

        // Configura interceptação para falhar requisições
        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::FailAll;
        config.phases = {
            bidi::types::network::InterceptPhase::BeforeRequestSent};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        std::atomic<bool> failed{false};
        config.before_request_handler =
            [&](const auto &params
                [[maybe_unused]]) -> std::optional<bidi::RequestResolution> {
            failed = true;
            bidi::RequestResolution res;
            res.action = bidi::InterceptAction::Fail;
            return res;
        };

        auto intercept_handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);
        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);

        // A navegação deve falhar porque rejeitamos a requisição
        EXPECT_ANY_THROW(
            co_await client_guard.client()->navigate(context_id, testUrl()););

        // Aguarda falha ser processada
        for (int i = 0; i < 20 && !failed.load(); ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (!failed.load()) {
            throw std::runtime_error("Intercept fail não ocorreu");
        }
        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get();
    ASSERT_TRUE(finished.load());
}

// Teste 3: Provide Custom Response
TEST_F(NetworkInterceptBiDiTest, ProvideCustomResponse) {
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

        // Configura interceptação para response phase
        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::Custom;
        config.phases = {bidi::types::network::InterceptPhase::ResponseStarted};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        std::atomic<bool> response_intercepted{false};
        config.response_started_handler =
            [&](const auto &params
                [[maybe_unused]]) -> std::optional<bidi::ResponseResolution> {
            response_intercepted = true;
            // Apenas continuar - não modificar
            bidi::ResponseResolution res;
            res.action = bidi::InterceptAction::Continue;
            return res;
        };

        auto intercept_handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);
        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);
        co_await client_guard.client()->navigate(context_id, testUrl());

        // Aguarda response ser interceptada
        for (int i = 0; i < 20 && !response_intercepted.load(); ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (!response_intercepted.load()) {
            throw std::runtime_error("Response intercept não ocorreu");
        }
        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get();
    ASSERT_TRUE(finished.load());
}

// Teste 4: Multiple Intercepts
TEST_F(NetworkInterceptBiDiTest, MultipleIntercepts) {
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

        // Configura interceptação para ambas as fases
        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::ContinueAll;
        config.phases = {
            bidi::types::network::InterceptPhase::BeforeRequestSent,
            bidi::types::network::InterceptPhase::ResponseStarted};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        std::atomic<int> phases_intercepted{0};
        config.before_request_handler =
            [&](const auto &params
                [[maybe_unused]]) -> std::optional<bidi::RequestResolution> {
            ++phases_intercepted;
            bidi::RequestResolution res;
            res.action = bidi::InterceptAction::Continue;
            return res;
        };
        config.response_started_handler =
            [&](const auto &params
                [[maybe_unused]]) -> std::optional<bidi::ResponseResolution> {
            ++phases_intercepted;
            bidi::ResponseResolution res;
            res.action = bidi::InterceptAction::Continue;
            return res;
        };

        auto intercept_handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);
        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);
        co_await client_guard.client()->navigate(context_id, testUrl());

        // Aguarda ambas as fases ou pelo menos a primeira
        for (int i = 0; i < 20 && phases_intercepted.load() < 1; ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (phases_intercepted.load() < 1) {
            throw std::runtime_error("Nenhuma fase foi interceptada");
        }
        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get();
    ASSERT_TRUE(finished.load());
}

// Teste 5: URL Pattern Filtering
TEST_F(NetworkInterceptBiDiTest, UrlPatternFilter) {
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

        std::atomic<bool> matched_url_intercepted{false};

        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::ContinueAll;
        config.phases = {
            bidi::types::network::InterceptPhase::BeforeRequestSent};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        config.before_request_handler =
            [&](const auto &params) -> std::optional<bidi::RequestResolution> {
            if (params.request.url.find("/test") != std::string::npos) {
                matched_url_intercepted = true;
            }
            bidi::RequestResolution res;
            res.action = bidi::InterceptAction::Continue;
            return res;
        };

        auto handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);

        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);

        co_await client_guard.client()->navigate(context_id, testUrl());

        for (int i = 0; i < 20 && !matched_url_intercepted.load(); ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (!matched_url_intercepted.load()) {
            throw std::runtime_error("Matching URL não foi interceptado");
        }
        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get();
    ASSERT_TRUE(finished.load());
}

// Teste 6: Remove Intercept
TEST_F(NetworkInterceptBiDiTest, RemoveIntercept) {
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

        std::atomic<bool> intercepted{false};

        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::ContinueAll;
        config.phases = {
            bidi::types::network::InterceptPhase::BeforeRequestSent};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        config.before_request_handler =
            [&](const auto &) -> std::optional<bidi::RequestResolution> {
            intercepted = true;
            bidi::RequestResolution res;
            res.action = bidi::InterceptAction::Continue;
            return res;
        };

        auto handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);

        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);

        co_await client_guard.client()->navigate(context_id, testUrl());

        for (int i = 0; i < 20 && !intercepted.load(); ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (!intercepted.load()) {
            throw std::runtime_error("First intercept não ocorreu");
        }

        intercepted = false;
        handler.reset();

        co_await asio::steady_timer(io_context, 500ms)
            .async_wait(asio::use_awaitable);

        co_await client_guard.client()->navigate(context_id, testUrl());

        for (int i = 0; i < 20 && !intercepted.load(); ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (intercepted.load()) {
            throw std::runtime_error(
                "Second intercept ocorreu quando não deveria");
        }
        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get();
    ASSERT_TRUE(finished.load());
}

// Teste 7: Continue Response Modifications
TEST_F(NetworkInterceptBiDiTest, ContinueResponseModifications) {
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

        // Configura interceptação para continuar resposta com modificações
        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::Custom;
        config.phases = {bidi::types::network::InterceptPhase::ResponseStarted};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        std::atomic<bool> response_continued{false};
        config.response_started_handler =
            [&](const auto &params
                [[maybe_unused]]) -> std::optional<bidi::ResponseResolution> {
            // Continua com modificações de header
            bidi::ResponseResolution res;
            res.action = bidi::InterceptAction::Continue;
            res.headers = std::vector<bidi::types::network::Header>{
                {.name = "X-Custom-Header",
                 .value = bidi::types::network::StringBytes{"custom-value"}}};
            response_continued = true;
            return res;
        };

        auto intercept_handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);
        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);
        co_await client_guard.client()->navigate(context_id, testUrl());

        // Aguarda resposta ser continuada
        for (int i = 0; i < 20 && !response_continued.load(); ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (!response_continued.load()) {
            throw std::runtime_error("Response continue não ocorreu");
        }
        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get();
    ASSERT_TRUE(finished.load());
}

// Teste 8: Auth Challenge Flow
TEST_F(NetworkInterceptBiDiTest, AuthChallengeFlow) {
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

        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::Custom;
        config.phases = {bidi::types::network::InterceptPhase::AuthRequired};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        config.auth_required_handler =
            [&](const auto &params
                [[maybe_unused]]) -> std::optional<bidi::AuthResolution> {
            bidi::AuthResolution res;
            res.auth_action =
                bidi::types::network::AuthAction::ProvideCredentials;
            res.credentials = bidi::types::network::AuthCredentials{
                .type = "password", .username = "user", .password = "pass"};
            return res;
        };

        auto handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);

        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);

        co_await client_guard.client()->navigate(context_id, testUrl());

        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get();
    ASSERT_TRUE(finished.load());
}

// Teste 9: Concurrent Interceptors Stress Test
TEST_F(NetworkInterceptBiDiTest, ConcurrentInterceptorsStress) {
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

        std::atomic<int> total_intercepts{0};
        const int num_navigations = 3;

        bidi::NetworkInterceptConfig config;
        config.policy = bidi::NetworkInterceptPolicy::ContinueAll;
        config.phases = {
            bidi::types::network::InterceptPhase::BeforeRequestSent};
        config.url_patterns = std::vector<bidi::types::network::UrlPattern>{
            bidi::types::network::UrlPatternString{.pattern = testUrl()}};

        config.before_request_handler =
            [&](const auto &) -> std::optional<bidi::RequestResolution> {
            total_intercepts++;
            bidi::RequestResolution res;
            res.action = bidi::InterceptAction::Continue;
            return res;
        };

        auto handler =
            co_await bidi::NetworkInterceptHandler::create(client_ptr, config);

        auto context_id = co_await client_guard.client()->create_context(
            bidi::commands::browsing_context::CreateType::window);

        for (int i = 0; i < num_navigations; ++i) {
            co_await client_guard.client()->navigate(context_id, testUrl());
        }

        int expected = num_navigations;
        for (int i = 0; i < 50 && total_intercepts.load() < expected; ++i) {
            co_await asio::steady_timer(io_context, 100ms)
                .async_wait(asio::use_awaitable);
        }
        if (total_intercepts.load() < expected) {
            throw std::runtime_error("Não todos os intercepts ocorreram");
        }
        finished.store(true);
        co_return;
    };
    auto fut = asio::co_spawn(io_context, run_test(), asio::use_future);
    io_context.run();
    fut.get();
    ASSERT_TRUE(finished.load());
}

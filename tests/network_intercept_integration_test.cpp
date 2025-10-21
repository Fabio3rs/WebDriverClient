#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <chrono>

#include "WebDriverClient.hpp"
#include "test_http_server.hpp"

using json = nlohmann::json;
using bidi::testing::HttpResponse;
using bidi::testing::TestHttpServer;
using namespace std::chrono_literals;

class NetworkInterceptIoFixture : public ::testing::Test {
protected:
    TestHttpServer test_server;
    WebDriver driver;
    uint16_t server_port;

    void SetUp() override {
        // Configura o servidor de teste com uma resposta padrão
        test_server.set_default_handler([](const auto &request) {
            return HttpResponse::json(R"({"status": "ok"})");
        });
        
        server_port = test_server.start();
        
        // Configura o WebDriver para usar o chromedriver real na porta 9515
        driver.webDriverUrl = "http://127.0.0.1:9515";
        
        // Cria uma nova sessão
        json capabilities = {
            {"capabilities", {
                {"alwaysMatch", {
                    {"browserName", "chrome"},
                    {"goog:chromeOptions", {
                        {"args", {"--headless", "--no-sandbox", "--disable-dev-shm-usage"}}
                    }}
                }}
            }}
        };

        auto session = driver.newSession(capabilities);
        ASSERT_TRUE(session.contains("sessionId"));
        driver.sessionId = session["sessionId"].get<std::string>();
    }

    void TearDown() override {
        if (!driver.sessionId.empty()) {
            driver.deleteSession();
        }
        test_server.stop();
    }

    std::string getTestUrl() const {
        return "http://127.0.0.1:" + std::to_string(server_port) + "/test";
    }
};

TEST_F(NetworkInterceptIoFixture, InterceptSimpleRequest) {
    // Configura interceptação para todas as requisições
    json interceptParams = {
        {"patterns", {{
            {"urlPattern", "*"} // Intercepta todas as URLs
        }}}
    };
    
    auto intercept = driver.setNetworkIntercepts(interceptParams);
    ASSERT_TRUE(intercept.is_object());

    // Navega para a URL de teste que deve ser interceptada
    auto result = driver.navigateTo(getTestUrl());
    ASSERT_TRUE(result.is_object());

    // Verifica se a requisição foi interceptada
    auto requests = driver.getInterceptedRequests();
    ASSERT_FALSE(requests.empty());
    
    // Verifica se a URL interceptada corresponde à URL de teste
    EXPECT_EQ(requests[0]["url"], getTestUrl());
}

TEST_F(NetworkInterceptIoFixture, ModifyInterceptedResponse) {
    // Configura o servidor para retornar uma resposta específica
    test_server.set_handler("/test", [](const auto &) {
        return HttpResponse::json(R"({"original": "response"})");
    });

    // Configura interceptação com modificação de resposta
    json interceptParams = {
        {"patterns", {{
            {"urlPattern", getTestUrl()}
        }}},
        {"modifyResponse", true}
    };
    
    auto intercept = driver.setNetworkIntercepts(interceptParams);
    ASSERT_TRUE(intercept.is_object());

    // Navega para a URL
    auto result = driver.navigateTo(getTestUrl());
    ASSERT_TRUE(result.is_object());

    // Modifica a resposta interceptada
    json modifiedResponse = {
        {"status", 200},
        {"body", R"({"modified": "response"})"}
    };
    
    auto modifyResult = driver.continueWithResponse(requests[0]["requestId"], modifiedResponse);
    ASSERT_TRUE(modifyResult.is_object());

    // Verifica se a resposta foi modificada
    auto pageSource = driver.getPageSource();
    EXPECT_TRUE(pageSource.contains("modified"));
    EXPECT_FALSE(pageSource.contains("original"));
}

TEST_F(NetworkInterceptIoFixture, CancelInterceptedRequest) {
    // Configura interceptação
    json interceptParams = {
        {"patterns", {{
            {"urlPattern", "*"}
        }}}
    };
    
    auto intercept = driver.setNetworkIntercepts(interceptParams);
    ASSERT_TRUE(intercept.is_object());

    // Inicia navegação assíncrona que será cancelada
    auto future_result = std::async(std::launch::async, [this]() {
        return driver.navigateTo(getTestUrl());
    });

    // Espera pela interceptação
    auto requests = driver.getInterceptedRequests();
    ASSERT_FALSE(requests.empty());

    // Cancela a requisição
    auto cancelResult = driver.failRequest(requests[0]["requestId"], "Cancelled");
    ASSERT_TRUE(cancelResult.is_object());

    // Verifica se a navegação falhou como esperado
    EXPECT_THROW({
        future_result.get();
    }, std::exception);
}

#include "bidi/client.hpp"
#include <gtest/gtest.h>

using namespace bidi;

TEST(BidiCompileTest, ConstructCoreComponents) {
    boost::asio::io_context ioc;
    {
        auto ws_client = std::make_shared<core::WebSocketClient>(ioc);
        EXPECT_NE(ws_client, nullptr);
        auto session = std::make_shared<core::BiDiSession>(ws_client);
        EXPECT_NE(session, nullptr);
        // Construímos um Client de alto nível diretamente sem iniciar operações
        // assíncronas para evitar handlers pendentes no io_context que geravam
        // leaks sob ASan.
        auto client = std::make_shared<Client>(session);
        EXPECT_NE(client, nullptr);
        // Encerramos o escopo para forçar destrutores antes do final do teste e
        // garantir liberação determinística sem operações em voo.
    }
    SUCCEED();
}

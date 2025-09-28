#include "bidi.hpp"
#include <gtest/gtest.h>

using namespace bidi;

TEST(BidiCompileTest, ConstructWsAndClient) {
    boost::asio::io_context ioc;
    auto ws = std::make_shared<WsSession>(ioc);
    auto client = std::make_shared<BidiClient>(ws);
    (void)ws;
    (void)client;
    SUCCEED();
}

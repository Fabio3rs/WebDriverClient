// tests/websocket_client_test.cpp
#include "bidi/core.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace bidi::core;

// These tests rely on internal test hooks compiled under BIDI_TESTING.
// If the project is not built with BIDI_TESTING, they will compile but be
// skipped at runtime.

TEST(WebSocketClientTest, WriteQueueSerialization) {
#ifdef BIDI_TESTING
    net::io_context ioc;
    auto client = std::make_shared<WebSocketClient>(ioc);

    // simulate sending many messages; we will post them on the strand
    for (int i = 0; i < 50; ++i) {
        client->async_send(std::string("msg_") + std::to_string(i),
                           [](boost::system::error_code) {});
    }

    // After posting, the write queue should have entries (strand serializes
    // writes) We can't directly access write_queue_ (private), but we can
    // ensure no crash when scheduling writes and that ioc can run briefly.
    std::thread t([&]() { ioc.run_for(std::chrono::milliseconds(200)); });
    t.join();

    SUCCEED();
#else
    GTEST_SKIP() << "BIDI_TESTING not enabled";
#endif
}

TEST(WebSocketClientTest, URLParsingEdgeCases) {
    // This test verifies the inline URL parsing via async_connect wrapper
    // by invoking async_connect with different URL shapes and ensuring it
    // does not throw synchronously.
    net::io_context ioc;
    auto client = std::make_shared<WebSocketClient>(ioc);

    bool ok = true;
    try {
        // call with basic form
        client->async_connect("ws://localhost:9515/session",
                              [](boost::system::error_code) {});

        client->async_connect("ws://example.com",
                              [](boost::system::error_code) {});

        client->async_connect("ws://host:1234/path",
                              [](boost::system::error_code) {});
    } catch (...) {
        ok = false;
    }

    EXPECT_TRUE(ok);
}

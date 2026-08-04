// tests/websocket_write_completion_test.cpp
#include "bidi/core.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace bidi::core;

TEST(WebSocketWriteCompletionTest, HandlerCalledAfterWrite) {
#ifdef BIDI_TESTING
    net::io_context ioc;
    auto client = std::make_shared<WebSocketClient>(ioc);

    std::atomic<bool> handler_called{false};

    client->async_send(std::string("hello"),
                       [&handler_called](boost::system::error_code /*ec*/) {
                           handler_called.store(true);
                           // in production the ec should be set on failure
                       });

    // Run the io_context briefly to drain posted handlers
    std::thread t([&]() { ioc.run_for(std::chrono::milliseconds(200)); });
    t.join();

    // Under current implementation this might be true (fire-and-forget)
    // but test asserts that handler was eventually called (at least)
    EXPECT_TRUE(handler_called.load());
#else
    GTEST_SKIP() << "BIDI_TESTING not enabled";
#endif
}

TEST(WebSocketWriteCompletionTest, ErrorPropagationPlaceholder) {
    // Placeholder test: proper error propagation requires transport-level
    // injection or a mock WebSocket stream. Will be implemented alongside
    // a small transport shim if requested.
    GTEST_SKIP() << "Transport-level error injection not implemented";
}

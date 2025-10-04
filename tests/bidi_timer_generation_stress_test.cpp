/**
 * @file bidi_timer_generation_stress_test.cpp
 * @brief Stress tests for timer_generation counter under high load
 */

#include "bidi/core.hpp"
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <random>

using namespace bidi::core;
namespace net = boost::asio;

/**
 * @test HighFrequencyTimeouts
 * @brief 1000 concurrent requests with 5-15ms timeouts
 *
 * Pattern: Uses run_for() like other tests (websocket_client_test.cpp)
 * to ensure proper resource cleanup without background threads.
 */
TEST(BiDiTimerGenerationStress, HighFrequencyTimeouts) {
    net::io_context ioc;
    auto ws = std::make_shared<WebSocketClient>(ioc);
    auto session = std::make_shared<BiDiSession>(ws);

    constexpr int N = 1000;
    std::atomic<int> timeout_count{0};
    std::atomic<int> completed_count{0};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> timeout_dist(5, 15); // 5-15ms

    // Launch N requests with random short timeouts
    for (int i = 0; i < N; ++i) {
        auto timeout_ms = timeout_dist(gen);
        session->send_command(
            "script.evaluate", boost::json::object{{"expression", "1+1"}},
            [&timeout_count, &completed_count](const ParsedResponse &resp) {
                ++completed_count;
                if (resp.timeout_expired) {
                    ++timeout_count;
                }
            },
            std::chrono::milliseconds(timeout_ms));
    }

    // Process all async operations using run_for() pattern
    // (same pattern as websocket_client_test.cpp and
    // websocket_write_completion_test.cpp)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (completed_count.load() < N &&
           std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(std::chrono::milliseconds(50));
        ioc.restart();
    }

    // All requests should have completed (either timeout or success)
    EXPECT_EQ(completed_count.load(), N)
        << "Some requests were stuck and never completed";

    // Most should have timed out (since we never send responses)
    EXPECT_GT(timeout_count.load(), N * 0.9)
        << "Expected most requests to timeout";

    // Verify no memory leaks in pending map (all entries cleaned up)
    EXPECT_EQ(session->test_pending_size(), 0)
        << "Memory leak: pending entries not cleaned up";

    // Explicitly stop io_context to cancel remaining operations
    // (pattern from bidi_session_routing_test.cpp for proper cleanup)
    ioc.stop();

    // RAII cleanup: session and ws destroyed when leaving scope

    session->disconnect();
}

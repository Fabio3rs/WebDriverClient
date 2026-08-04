// tests/threading_strand_test.cpp
#include "bidi/threading.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace bidi::core;

TEST(ThreadingStrandTest, StrandSerializesOperations) {
    ThreadingContext ctx(2, 2);

    std::atomic<int> concurrent{0};
    std::atomic<int> max_concurrent{0};

    // Post many work items to ws_strand and verify they don't run concurrently
    const int tasks = 100;
    for (int i = 0; i < tasks; ++i) {
        ctx.post_ws([&]() {
            ++concurrent;
            int cur = concurrent.load(std::memory_order_relaxed);
            int prev = max_concurrent.load(std::memory_order_relaxed);
            while (cur > prev &&
                   !max_concurrent.compare_exchange_weak(prev, cur)) {
            }
            // simulate short work
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            --concurrent;
        });
    }

    // Give some time for tasks to finish
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // On a strand, max_concurrent should be 1
    EXPECT_LE(max_concurrent.load(), 1);
}

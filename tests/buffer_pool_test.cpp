// tests/buffer_pool_test.cpp
#include "bidi/buffer_pool.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace bidi::core;

TEST(BufferPoolTest, ConcurrentAcquireRelease) {
    BufferPool pool(4);

    const int threads = 16;
    const int ops_per_thread = 1000;

    std::vector<std::thread> ths;
    ths.reserve(threads);

    for (int t = 0; t < threads; ++t) {
        ths.emplace_back([&pool]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                auto h = pool.acquire_buffer(BufferSize::Small);
                // write a small payload
                h->write_data("hello");
                // handle drops out of scope and returns to pool
            }
        });
    }

    for (auto &th : ths)
        th.join();

    auto stats = pool.get_stats();
    // We expect at least threads*ops_per_thread acquisitions recorded
    EXPECT_GE(stats.total_acquired,
              static_cast<std::size_t>(threads * ops_per_thread));
}

TEST(BufferPoolTest, PoolExhaustionAndPreallocate) {
    BufferPool pool(1);
    // preallocate a few
    pool.preallocate(BufferSize::Small, 8);

    std::vector<BufferHandle> handles;
    handles.reserve(16);
    for (int i = 0; i < 16; ++i) {
        // Should always return a handle even when pool empty (creates new)
        handles.push_back(pool.acquire_buffer(BufferSize::Small));
        handles.back()->write_data("x");
    }

    // release half
    {
        constexpr int to_release = 8;
        for (int i = 0; i < to_release; ++i) {
            handles.pop_back();
        }
    }

    auto stats = pool.get_stats();
    EXPECT_GE(stats.total_created, 1U);
}

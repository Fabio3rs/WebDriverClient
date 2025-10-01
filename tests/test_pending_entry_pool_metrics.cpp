#include <bidi/pending_entry_pool_vec.hpp>
#include <chrono>
#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(PendingEntryPoolMetricsTest, AcquireWithinCapacityUpdatesMetrics) {
    bidi::core::PendingEntryPoolVec pool(4);
    // Acquire all capacity
    auto h1 = pool.acquire_nonblocking();
    auto h2 = pool.acquire_nonblocking();
    auto h3 = pool.acquire_nonblocking();
    auto h4 = pool.acquire_nonblocking();
    auto metrics = pool.get_metrics();
    EXPECT_EQ(metrics.capacity, 4u);
    EXPECT_EQ(metrics.in_use, 4u);
    EXPECT_EQ(metrics.fallback, 0u);
    EXPECT_EQ(metrics.acquired, 4u);
    EXPECT_EQ(metrics.created, 4u); // Policy::Recreate => sempre created
}

TEST(PendingEntryPoolMetricsTest, FallbackPathIncrementsFallbackCounter) {
    bidi::core::PendingEntryPoolVec pool(1);
    auto h1 = pool.acquire_nonblocking();
    ASSERT_TRUE(h1);
    // Force fallback (pool size 1 already used)
    auto h2 = pool.acquire_nonblocking();
    ASSERT_TRUE(h2);
    auto metrics = pool.get_metrics();
    EXPECT_EQ(metrics.capacity, 1u);
    EXPECT_EQ(metrics.in_use, 1u);   // apenas um slot do pool em uso
    EXPECT_EQ(metrics.fallback, 1u); // segundo é fallback
    EXPECT_EQ(metrics.acquired, 2u);
    EXPECT_EQ(metrics.created, 1u); // apenas primeiro slot construído;
}

TEST(PendingEntryPoolMetricsTest, TimeoutCountsAsFailure) {
    bidi::core::PendingEntryPoolVec pool(1);
    auto h1 = pool.acquire_nonblocking();
    ASSERT_TRUE(h1);
    auto h_timeout = pool.acquire_for(1ms); // deve falhar por timeout
    ASSERT_FALSE(static_cast<bool>(h_timeout));
    auto metrics = pool.get_metrics();
    // Timeout contabilizado como falha (failures >= 1)
    EXPECT_GE(metrics.failures, 1u);
}

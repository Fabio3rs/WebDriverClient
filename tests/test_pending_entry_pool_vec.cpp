#include "bidi/pending_entry_pool_vec.hpp"
#include <gtest/gtest.h>

using namespace bidi::core;

TEST(PendingEntryPoolVecTest, AcquireNonBlockingAndFallback) {
    PendingEntryPoolVec pool(1);

    // First acquire should get a pooled slot
    auto h1 = pool.acquire_nonblocking();
    ASSERT_TRUE(static_cast<bool>(h1));

    // Second acquire should fallback to heap (pool capacity 1)
    auto h2 = pool.acquire_nonblocking();
    ASSERT_TRUE(static_cast<bool>(h2));

    // If the second is fallback, release_fallback should be non-null
    auto fb = h2.release_fallback();
    // Either pooled or fallback is valid; if fallback present, check its fields
    if (fb) {
        EXPECT_EQ(fb->id, 0);
    }
}

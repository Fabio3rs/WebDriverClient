// tests/test_poolvec.cpp
#include "utils/PoolVec.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace utils;

struct Payload {
    int x{0};
    Payload() = default;
    Payload(int v) : x(v) {}
};

TEST(PoolVecTest, RecreateSingleThread) {
    PoolVec<Payload> pool(4, PoolVec<Payload>::Policy::Recreate);

    auto h1 = pool.borrow(std::chrono::milliseconds(0), 10);
    ASSERT_TRUE(static_cast<bool>(h1));
    EXPECT_EQ(h1->x, 10);

    h1.reset();

    auto h2 = pool.borrow(std::chrono::milliseconds(0), 20);
    ASSERT_TRUE(static_cast<bool>(h2));
    EXPECT_EQ(h2->x, 20);
}

TEST(PoolVecTest, KeepSingleThread) {
    PoolVec<Payload> pool(2, PoolVec<Payload>::Policy::Keep);

    auto h1 = pool.borrow(std::chrono::milliseconds(0), 7);
    ASSERT_TRUE(static_cast<bool>(h1));
    EXPECT_EQ(h1->x, 7);

    h1.reset();

    // Re-borrow should return same constructed object but allowed to be reused
    auto h2 = pool.borrow(std::chrono::milliseconds(0), 0);
    ASSERT_TRUE(static_cast<bool>(h2));
    // In Keep mode the object persists; value remains 7 unless user resets
    EXPECT_EQ(h2->x, 7);
}

TEST(PoolVecTest, ConcurrentBorrowReturn) {
    constexpr int threads = 8;
    constexpr int iters = 1000;
    PoolVec<Payload> pool(16, PoolVec<Payload>::Policy::Recreate);

    std::atomic<int> total_acquired{0};

    auto worker = [&](int id) {
        for (int i = 0; i < iters; ++i) {
            auto h = pool.borrow(std::chrono::milliseconds(100), id);
            if (h) {
                total_acquired.fetch_add(1);
                h.reset();
            }
        }
    };

    std::vector<std::thread> ths;
    ths.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        ths.emplace_back(worker, i);
    }
    for (auto &t : ths) {
        t.join();
    }

    EXPECT_GT(total_acquired.load(), 0);
}

TEST(PoolVecTest, TimeoutWhenExhausted) {
    PoolVec<Payload> pool(1, PoolVec<Payload>::Policy::Recreate);

    // Acquire the only slot
    constexpr auto kNoWait = std::chrono::milliseconds(0);
    constexpr auto kShortTimeout = std::chrono::milliseconds(50);

    auto handle = pool.borrow(kNoWait, 1);
    ASSERT_TRUE(static_cast<bool>(handle));

    // Another thread should time out when trying to borrow
    std::atomic<bool> got{false};
    std::thread thr([&] {
        auto handle2 = pool.borrow(kShortTimeout, 2);
        if (handle2) {
            got.store(true);
        }
    });

    thr.join();
    EXPECT_FALSE(got.load());

    handle.reset();

    // Now borrowing should succeed
    auto handle3 = pool.borrow(kShortTimeout, 3);
    ASSERT_TRUE(static_cast<bool>(handle3));
    EXPECT_EQ(handle3->x, 3);
}

struct CountingPayload {
    static std::atomic<int> constructed;
    int v{0};
    CountingPayload() { constructed.fetch_add(1); }
    CountingPayload(int val) : v(val) { constructed.fetch_add(1); }
};

std::atomic<int> CountingPayload::constructed{0};

TEST(PoolVecTest, KeepConstructsAtMostCapacity) {
    constexpr int cap = 4;
    CountingPayload::constructed.store(0);

    PoolVec<CountingPayload> pool(cap, PoolVec<CountingPayload>::Policy::Keep);

    constexpr int num_threads = 8;
    constexpr int iterations = 200;
    constexpr auto kWorkerTimeout = std::chrono::milliseconds(100);

    auto worker = [&](int worker_id) {
        for (int i = 0; i < iterations; ++i) {
            auto handle = pool.borrow(kWorkerTimeout, worker_id);
            if (handle) {
                // touch value
                handle->v = worker_id;
                handle.reset();
            }
        }
    };

    std::vector<std::thread> ths;
    ths.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        ths.emplace_back(worker, i);
    }
    for (auto &th : ths) {
        th.join();
    }

    // Constructed count must be <= capacity
    EXPECT_LE(CountingPayload::constructed.load(), cap);
}

struct ThrowingPayload {
    static std::atomic<int> attempts;
    static std::atomic<int> constructed;
    int v{0};
    ThrowingPayload(int val) : v(val) {
        int attempt_index = attempts.fetch_add(1) + 1;
        // Force exception in the first two attempts to simulate failure
        if (attempt_index <= 2) {
            throw std::runtime_error("forced construction failure");
        }
        constructed.fetch_add(1);
    }
};

std::atomic<int> ThrowingPayload::attempts{0};
std::atomic<int> ThrowingPayload::constructed{0};

TEST(PoolVecTest, ConstructionFailureRollsBackSlot) {
    using namespace std::chrono_literals;
    ThrowingPayload::attempts.store(0);
    ThrowingPayload::constructed.store(0);

    PoolVec<ThrowingPayload> pool(1,
                                  PoolVec<ThrowingPayload>::Policy::Recreate);

    // Primeira tentativa: falha silenciosa e não consome slot
    {
        constexpr int kFirstFailValue = 10;
        auto start_attempts = ThrowingPayload::attempts.load();
        auto hfail = pool.borrow(0ms, kFirstFailValue);
        EXPECT_FALSE(static_cast<bool>(hfail));
        EXPECT_EQ(pool.borrowed_count(), 0U);
        EXPECT_EQ(ThrowingPayload::constructed.load(), 0);
        EXPECT_EQ(ThrowingPayload::attempts.load(), start_attempts + 1);
    }
    // Segunda tentativa: também lança
    {
        constexpr int kSecondFailValue = 20;
        auto hfail2 = pool.borrow(0ms, kSecondFailValue);
        EXPECT_FALSE(static_cast<bool>(hfail2));
        EXPECT_EQ(pool.borrowed_count(), 0U);
        EXPECT_EQ(ThrowingPayload::constructed.load(), 0);
    }
    // Terceira deve finalmente construir
    {
        constexpr int kConstructValue = 30;
        auto handle_ok = pool.borrow(0ms, kConstructValue);
        ASSERT_TRUE(static_cast<bool>(handle_ok));
        EXPECT_EQ(handle_ok->v, kConstructValue);
        handle_ok.reset();
        EXPECT_EQ(pool.borrowed_count(), 0U);
    }
    // Verifica métrica de failures (se exposta)
    // Apenas valida que houve 2 falhas iniciais -> failures() == 2
    EXPECT_EQ(pool.failures(), 2U);
}

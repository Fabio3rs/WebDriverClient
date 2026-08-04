#include "asyncx.hpp"
#include <atomic>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>
#include <utility>

using namespace asyncx;

// Helper: creates an Async<int> that runs for `work_iters` iterations and
// checks stop_token between iterations. When cancelled, it sets cancelled_flag
// to true and returns early.
auto make_cooperative_async(
    net::any_io_executor ex, int work_iters,
    const std::shared_ptr<std::atomic<bool>> &cancelled_flag) -> Async<int> {
    return Async<int>::from_callback(
        std::move(ex), [work_iters, cancelled_flag](auto cb, const auto &st) {
            std::thread([cb = std::move(cb), st, work_iters,
                         cancelled_flag]() mutable {
                for (int i = 0; i < work_iters; ++i) {
                    if (st.stop_requested()) {
                        cancelled_flag->store(true);
                        cb(boost::system::error_code{}, 0);
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                // completed normally
                cb(boost::system::error_code{}, 42);
            }).detach();
        });
}

TEST(AsyncxCancel, RaceCancelsLosers) {
    auto ex = boost::asio::system_executor{};
    auto flag1 = std::make_shared<std::atomic<bool>>(false);
    auto flag2 = std::make_shared<std::atomic<bool>>(false);

    // first takes long, second is short
    auto a1 = make_cooperative_async(ex, 100, flag1);
    auto a2 = make_cooperative_async(ex, 2, flag2);

    auto composed = race<int>(ex, std::vector<Async<int>>{a1, a2});

    // wait for composed to complete
    auto fut = boost::asio::co_spawn(
        ex,
        [composed]() -> boost::asio::awaitable<int> {
            co_return co_await asyncx::as_awaitable(composed);
        },
        boost::asio::use_future);

    auto res = fut.get();
    EXPECT_EQ(res, 42);

    // give some time for cancellation to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(flag1->load());
}

TEST(AsyncxCancel, TimeoutCancelsOperation) {
    auto ex = boost::asio::system_executor{};
    auto flag = std::make_shared<std::atomic<bool>>(false);

    auto long_op = make_cooperative_async(ex, 100, flag);

    auto timed = timeout<int>(long_op, ex, std::chrono::milliseconds(20));

    auto fut = boost::asio::co_spawn(
        ex,
        [timed]() -> boost::asio::awaitable<int> {
            co_return co_await asyncx::as_awaitable(timed);
        },
        boost::asio::use_future);

    // The operation should time out; we expect an exception or error handling
    EXPECT_ANY_THROW({
        auto r = fut.get();
        // if get returns, it's an unexpected success
        (void)r;
        FAIL() << "Expected timeout to fail";
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(flag->load());
}

TEST(AsyncxCancel, AllCancelsRemainingOnFailure) {
    auto ex = boost::asio::system_executor{};
    auto flag1 = std::make_shared<std::atomic<bool>>(false);
    auto flag2 = std::make_shared<std::atomic<bool>>(false);

    // a1 will fail early by throwing via callback
    auto a1 = Async<int>::from_callback(ex, [](const auto &cb, const auto &) {
        cb(boost::system::errc::make_error_code(
               boost::system::errc::invalid_argument),
           0);
    });

    auto a2 = make_cooperative_async(ex, 100, flag2);

    auto composed = all<int>(ex, std::vector<Async<int>>{a1, a2});

    auto fut = boost::asio::co_spawn(
        ex,
        [composed]() -> boost::asio::awaitable<std::vector<int>> {
            co_return co_await asyncx::as_awaitable(composed);
        },
        boost::asio::use_future);

    try {
        auto r = fut.get();
        // debug: std::cerr << "DEBUG: all returned size=" << r.size() << '\n';
        (void)r;
        FAIL() << "Expected all to fail due to child error";
    } catch (...) {
        // expected
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(flag2->load());
}

TEST(AsyncxCancel, RaceExternalCancelPropagates) {
    auto ex = boost::asio::system_executor{};
    auto flag1 = std::make_shared<std::atomic<bool>>(false);
    auto flag2 = std::make_shared<std::atomic<bool>>(false);

    auto a1 = make_cooperative_async(ex, 100, flag1);
    auto a2 = make_cooperative_async(ex, 100, flag2);

    auto composed = race<int>(ex, std::vector<Async<int>>{a1, a2});

    // Spawn and then request stop on the composed operation
    auto fut = boost::asio::co_spawn(
        ex,
        [composed]() -> boost::asio::awaitable<int> {
            co_return co_await asyncx::as_awaitable(composed);
        },
        boost::asio::use_future);

    // give them some time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // ask composed to stop via request_stop
    composed.request_stop();

    try {
        auto r = fut.get();
        (void)r;
    } catch (...) {
        // expected or not, ensure children saw stop
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(flag1->load() || flag2->load());
}

TEST(AsyncxCancel, AllCompletesSuccessfully) {
    auto ex = boost::asio::system_executor{};

    auto a1 = Async<int>::from_callback(ex, [](const auto &cb, const auto &) {
        cb(boost::system::error_code{}, 1);
    });
    auto a2 = Async<int>::from_callback(ex, [](const auto &cb, const auto &) {
        cb(boost::system::error_code{}, 2);
    });

    auto composed = all<int>(ex, std::vector<Async<int>>{a1, a2});

    auto fut = boost::asio::co_spawn(
        ex,
        [composed]() -> boost::asio::awaitable<std::vector<int>> {
            co_return co_await asyncx::as_awaitable(composed);
        },
        boost::asio::use_future);

    auto r = fut.get();
    EXPECT_EQ(r.size(), 2U);
    EXPECT_EQ(r[0], 1);
    EXPECT_EQ(r[1], 2);
}

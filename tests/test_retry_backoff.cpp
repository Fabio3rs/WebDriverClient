#include "asyncx.hpp"
#include "retry_backoff.hpp"
#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <utility>

using namespace asyncx;

// Constants reused between tests to avoid magic numbers and duplication.
namespace {
constexpr int kFailTimesShort = 3;
constexpr int kFailTimesLong = 1000;
constexpr int kExpectedMinAttemptsAfterSuccess = 4; // SucceedsAfterRetries
constexpr int kMaxTriesShort = 5;
constexpr int kMaxTriesLong = 10;
constexpr auto kFirstDelayShort = std::chrono::milliseconds(1);
constexpr auto kFirstDelayCancel = std::chrono::milliseconds(50);
constexpr double kMultiplier = 2.0;
constexpr auto kPerAttemptSleep = std::chrono::milliseconds(5);
constexpr auto kCancelWait = std::chrono::milliseconds(10);
} // namespace

// Helper fake async which fails first N times with a retryable EC, then
// succeeds
template <class T>
auto make_fail_then_succeed(
    const net::any_io_executor &executor, int fail_times,
    std::shared_ptr<std::atomic<int>> &attempt_counter) -> Async<T> {
    return Async<T>::from_callback(
        executor, [fail_times, attempt_counter](auto callback_fn,
                                                const auto &stop_token) {
            std::thread([callback_fn = std::move(callback_fn), stop_token,
                         fail_times, attempt_counter]() mutable {
                // debug: std::cerr << "DEBUG-TEST: thread started for attempt"
                // <<
                // '\n';
                int before = attempt_counter->fetch_add(1);
                std::this_thread::sleep_for(kPerAttemptSleep);
                if (before < fail_times) {
                    // debug: std::cerr << "DEBUG-TEST: timed_out before=" <<
                    // before
                    // << " fail_times=" << fail_times << '\n';
                    callback_fn(boost::system::errc::make_error_code(
                                    boost::system::errc::timed_out),
                                T{});
                    return;
                }
                // debug: std::cerr << "DEBUG-TEST: success before=" << before
                // <<
                // '\n';
                callback_fn(boost::system::error_code{}, T{});
            }).detach();
        });
}

// Specialization for void-returning Async
template <>
auto make_fail_then_succeed<void>(
    const net::any_io_executor &executor, int fail_times,
    std::shared_ptr<std::atomic<int>> &attempt_counter) -> Async<void> {
    return Async<void>::from_callback(
        executor, [fail_times, attempt_counter](auto callback_fn,
                                                const auto &stop_token) {
            std::thread([callback_fn = std::move(callback_fn), stop_token,
                         fail_times, attempt_counter]() mutable {
                // debug: std::cerr << "DEBUG-TEST: thread started for attempt
                // (void)" << '\n';
                int before = attempt_counter->fetch_add(1);
                std::this_thread::sleep_for(kPerAttemptSleep);
                if (before < fail_times) {
                    // debug: std::cerr << "DEBUG-TEST: timed_out void before="
                    // << before << " fail_times=" << fail_times << '\n';
                    callback_fn(boost::system::errc::make_error_code(
                        boost::system::errc::timed_out));
                    return;
                }
                // debug: std::cerr << "DEBUG-TEST: success void before=" <<
                // before
                // << '\n';
                callback_fn(boost::system::error_code{});
            }).detach();
        });
}

TEST(RetryBackoff, SucceedsAfterRetries) {
    auto ex = boost::asio::system_executor{};
    auto counter = std::make_shared<std::atomic<int>>(0);
    bidi::RetryPolicy pol;
    pol.max_tries = kMaxTriesShort;
    pol.first_delay = kFirstDelayShort;
    pol.multiplier = kMultiplier;
    pol.jitter = 0.0;

    auto composed_raw = bidi::retry_with_backoff<void>(
        [&] {
            return make_fail_then_succeed<void>(ex, kFailTimesShort, counter);
        },
        ex, pol);
    auto composed =
        std::make_shared<decltype(composed_raw)>(std::move(composed_raw));

    // debug: std::cerr << "[TEST] starting SucceedsAfterRetries" << '\n';
    auto composed_copy1 = composed; // garante lifetime
    auto fut = boost::asio::co_spawn(
        ex,
        [composed_copy1]() -> boost::asio::awaitable<void> {
            // debug: std::cerr << "[TEST] inside coroutine awaiting composed"
            // << '\n';
            co_await asyncx::as_awaitable(*composed_copy1);
            // debug: std::cerr << "[TEST] composed returned" << '\n';
            co_return;
        },
        boost::asio::use_future);

    fut.get();
    EXPECT_GE(counter->load(), kExpectedMinAttemptsAfterSuccess);
}

TEST(RetryBackoff, CancelsDuringWait) {
    auto ex = boost::asio::system_executor{};
    auto counter = std::make_shared<std::atomic<int>>(0);
    bidi::RetryPolicy pol;
    pol.max_tries = kMaxTriesLong;
    pol.first_delay = kFirstDelayCancel;
    pol.multiplier = kMultiplier;
    pol.jitter = 0.0;

    auto composed_raw = bidi::retry_with_backoff<void>(
        [&] {
            return make_fail_then_succeed<void>(ex, kFailTimesLong, counter);
        },
        ex, pol);
    auto composed =
        std::make_shared<decltype(composed_raw)>(std::move(composed_raw));

    // debug: std::cerr << "[TEST] starting CancelsDuringWait" << '\n';

    auto composed_copy2 = composed;
    auto fut = boost::asio::co_spawn(
        ex,
        [composed_copy2]() -> boost::asio::awaitable<void> {
            // debug: std::cerr << "[TEST] inside coroutine awaiting composed
            // (cancel)" << '\n';
            co_await asyncx::as_awaitable(*composed_copy2);
            // debug: std::cerr << "[TEST] composed returned (cancel)" << '\n';
            co_return;
        },
        boost::asio::use_future);

    // wait a bit for first attempt to start and schedule retry
    std::this_thread::sleep_for(kCancelWait);
    composed->request_stop();

    EXPECT_ANY_THROW(fut.get());

    // ensure that at least one attempt started
    EXPECT_GT(counter->load(), 0);
}

TEST(RetryBackoff, OnRetryHookCalled) {
    auto ex = boost::asio::system_executor{};
    auto counter = std::make_shared<std::atomic<int>>(0);
    bidi::RetryPolicy pol;
    pol.max_tries = kMaxTriesShort;
    pol.first_delay = kFirstDelayShort;
    pol.multiplier = kMultiplier;
    pol.jitter = 0.0;

    std::atomic<int> hook_calls{0};
    pol.on_retry = [&hook_calls](int tries_attempts,
                                 std::chrono::milliseconds elapsed_delay,
                                 const asyncx::EC &retry_ec) {
        (void)tries_attempts;
        (void)elapsed_delay;
        (void)retry_ec;
        hook_calls.fetch_add(1);
    };

    auto composed_raw = bidi::retry_with_backoff<void>(
        [&] {
            return make_fail_then_succeed<void>(ex, kFailTimesShort, counter);
        },
        ex, pol);
    auto composed =
        std::make_shared<decltype(composed_raw)>(std::move(composed_raw));

    // debug: std::cerr << "[TEST] starting OnRetryHookCalled" << '\n';

    auto composed_copy3 = composed;
    auto fut = boost::asio::co_spawn(
        ex,
        [composed_copy3]() -> boost::asio::awaitable<void> {
            // debug: std::cerr << "[TEST] inside coroutine awaiting composed
            // (hook)" << '\n';
            co_await asyncx::as_awaitable(*composed_copy3);
            // debug: std::cerr << "[TEST] composed returned (hook)" << '\n';
            co_return;
        },
        boost::asio::use_future);

    fut.get();
    EXPECT_GT(hook_calls.load(), 0);
}

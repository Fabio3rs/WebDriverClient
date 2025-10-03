#include "asyncx.hpp"
#include <atomic>
#include <gtest/gtest.h>

using namespace asyncx;

static net::io_context &test_io() {
    static net::io_context io;
    return io;
}

// Helper para criar Async<int> imediato
static Async<int> make_value(int v) {
    auto a = Async<int>::make(test_io().get_executor());
    a.fulfill(v);
    return a;
}

TEST(PipeOperator, MapChain) {
    auto result = make_value(10) | map_p([](int x) { return x + 5; }) |
                  map_p([](int x) { return x * 2; });
    std::optional<int> out;
    result.finally(
        [&](std::optional<int> v, std::optional<EC> ec, std::exception_ptr) {
            ASSERT_FALSE(ec);
            ASSERT_TRUE(v);
            out = *v;
        });
    test_io().poll();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, 30); // (10+5)*2
}

TEST(PipeOperator, TapSideEffect) {
    std::atomic<int> observed{0};
    auto result = make_value(3) | tap([&](int v) { observed = v; }) |
                  map_p([](int v) { return v * v; });
    std::optional<int> out;
    result.finally(
        [&](std::optional<int> v, std::optional<EC> ec, std::exception_ptr) {
            ASSERT_FALSE(ec);
            out = *v;
        });
    test_io().poll();
    EXPECT_EQ(observed.load(), 3);
    EXPECT_EQ(*out, 9);
}

TEST(PipeOperator, FilterTransformsToOptional) {
    auto result =
        make_value(4) | filter([](int v) { return v % 2 == 0; }) |
        map_p([](std::optional<int> opt) { return opt.value_or(-1); });
    std::optional<int> out;
    result.finally(
        [&](std::optional<int> v, std::optional<EC> ec, std::exception_ptr) {
            ASSERT_FALSE(ec);
            out = *v;
        });
    test_io().poll();
    EXPECT_EQ(*out, 4);
}

TEST(PipeOperator, FilterDropsValue) {
    auto result = make_value(5) | filter([](int v) { return v % 2 == 0; }) |
                  map_p([](std::optional<int> opt) { return opt.has_value(); });
    std::optional<bool> out;
    result.finally(
        [&](std::optional<bool> v, std::optional<EC> ec, std::exception_ptr) {
            ASSERT_FALSE(ec);
            out = *v;
        });
    test_io().poll();
    EXPECT_FALSE(*out);
}

TEST(PipeOperator, RecoverOnError) {
    auto ex = test_io().get_executor();
    auto faulty = Async<int>::make(ex);
    faulty.fail(make_error_code(boost::system::errc::operation_canceled));
    auto recovered = faulty | recover_p([](EC) { return 42; });
    std::optional<int> out;
    recovered.finally(
        [&](std::optional<int> v, std::optional<EC> ec, std::exception_ptr) {
            ASSERT_FALSE(ec);
            out = *v;
        });
    test_io().poll();
    EXPECT_EQ(*out, 42);
}

TEST(PipeOperator, ZipTwoAsyncs) {
    auto a = make_value(2);
    auto b = make_value(5);
    auto zipped = zip(a, b) | map_p([](const std::tuple<int, int> &tup) {
                      return std::get<0>(tup) + std::get<1>(tup);
                  });
    std::optional<int> out;
    zipped.finally(
        [&](std::optional<int> v, std::optional<EC> ec, std::exception_ptr) {
            ASSERT_FALSE(ec);
            out = *v;
        });
    test_io().poll();
    EXPECT_EQ(*out, 7);
}

TEST(PipeOperator, AndThenBind) {
    auto a = make_value(6);
    // and_then_p should allow binding to another Async-producing function
    auto chained = a | and_then_p([](int v) { return make_value(v + 1); });
    std::optional<int> out;
    chained.finally(
        [&](std::optional<int> v, std::optional<EC> ec, std::exception_ptr) {
            ASSERT_FALSE(ec);
            out = *v;
        });
    test_io().poll();
    EXPECT_EQ(*out, 7);
}

TEST(PipeOperator, OnErrorSideEffect) {
    auto ex = test_io().get_executor();
    auto faulty = Async<int>::make(ex);
    faulty.fail(make_error_code(boost::system::errc::permission_denied));
    std::atomic<int> observed{0};
    auto result = faulty | on_error_p([&](EC ec) { observed = ec.value(); });
    result.finally([&](std::optional<int> /*v*/, std::optional<EC> ec,
                       std::exception_ptr) {
        // on_error_p should not swallow the error; original combinator
        // semantics call the handler and propagate the error.
        ASSERT_TRUE(ec);
    });
    test_io().poll();
    EXPECT_NE(observed.load(), 0);
}

TEST(PipeOperator, TimeoutApplies) {
    // Create an Async that never completes and ensure timeout causes fail
    auto ex = test_io().get_executor();
    auto never = Async<int>::make(ex);
    auto timed = never | timeout_p(std::chrono::milliseconds(1));
    std::optional<EC> observed_ec;
    timed.finally([&](std::optional<int> /*v*/, std::optional<EC> ec,
                      std::exception_ptr) { observed_ec = ec; });
    // run the io_context to let the timer fire
    test_io().run_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(observed_ec.has_value());
    // Some platforms report the timeout as operation_aborted; accept either
    // timed_out or operation_aborted to keep the test robust.
    auto timed_val = make_error_code(boost::system::errc::timed_out).value();
    auto aborted_val = boost::asio::error::operation_aborted;
    EXPECT_TRUE(observed_ec.has_value());
    EXPECT_TRUE(observed_ec->value() == timed_val ||
                observed_ec->value() == aborted_val);
}

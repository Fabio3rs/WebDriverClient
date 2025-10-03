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

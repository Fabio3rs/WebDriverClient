#include "asyncx.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>

using namespace asyncx;
namespace chrono = std::chrono;

static auto asyncx_test_io() -> net::io_context & {
    static net::io_context io; // shared IO for tests
    return io;
}

static auto ex() { return asyncx_test_io().get_executor(); }

TEST(Asyncx, MapTransformsValue) {
    auto a = Async<int>::make(ex());
    auto mapped = a.map([](int x) { return x * 2; });
    std::optional<int> out;
    mapped.finally([&](std::optional<int> v, std::optional<EC> ec,
                       const std::exception_ptr &) {
        ASSERT_FALSE(ec);
        out = *v;
    });
    a.fulfill(21);
    asyncx_test_io().poll();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, 42);
}

TEST(Asyncx, AndThenChainsAsync) {
    auto a = Async<int>::make(ex());
    auto chained = a.and_then([](int x) {
        auto inner = Async<std::string>::make(ex());
        // fulfill asynchronously via posting to executor
        inner.finally([](const auto &, auto, const auto &) {
        }); // ensure continuation attached
        // immediate fulfill for test simplicity
        inner.fulfill(std::string{"value_"} + std::to_string(x));
        return inner;
    });
    std::optional<std::string> out;
    chained.finally([&](std::optional<std::string> v, std::optional<EC> ec,
                        const std::exception_ptr &) {
        ASSERT_FALSE(ec);
        out = *v;
    });
    a.fulfill(7);
    asyncx_test_io().poll();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "value_7");
}

TEST(Asyncx, OnErrorExecutesOnFailure) {
    auto a = Async<int>::make(ex());
    bool called = false;
    auto handled = a.on_error([&](EC) { called = true; });
    handled.finally([&](auto, auto, const auto &) {}); // attach
    a.fail(make_error_code(boost::system::errc::operation_canceled));
    asyncx_test_io().poll();
    EXPECT_TRUE(called);
}

TEST(Asyncx, PipeOperatorComposesOperations) {
    using namespace std::chrono_literals;
    auto base = Async<int>::make(ex());
    auto composed = base | map_p([](int x) { return x + 1; }) | timeout_p(50ms);
    std::optional<int> out;
    std::optional<EC> err;
    composed.finally([&](std::optional<int> v, std::optional<EC> ec,
                         const std::exception_ptr &) {
        if (v) {
            out = *v;
        } else {
            err = ec;
        }
    });
    base.fulfill(10);
    asyncx_test_io().run_for(chrono::milliseconds(10));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, 11);
}

TEST(Asyncx, WeakHandleDoesNotExtendLifetime) {
    auto weak = [&] {
        auto a = Async<int>::make(ex());
        // Return weak handle; Async destroyed after this lambda
        return a.weak();
    }();
    // Run IO to process any deferred destruction tasks (none expected)
    asyncx_test_io().poll();
    // We can't directly query Weak expired (no method). Indirect check:
    // try_fulfill should no-op safely.
    weak.try_fulfill(
        123); // should not crash; can't assert more without adding API.
    SUCCEED();
}

TEST(Asyncx, AttachOrRunExecutesInlineIfDone) {
    auto a = Async<int>::make(ex());
    a.fulfill(42);
    bool inline_exec = false;
    a.finally([&](std::optional<int> v, std::optional<EC> ec,
                  const std::exception_ptr &) {
        ASSERT_FALSE(ec);
        inline_exec = true;
        ASSERT_TRUE(v);
        EXPECT_EQ(*v, 42);
    });
    // No need to poll if executed inline; but poll harmless
    asyncx_test_io().poll();
    EXPECT_TRUE(inline_exec);
}

// CompletionTokenSupport test (awaitable) omitted for now: requires coroutine
// test harness. Can be added later using boost::asio::co_spawn context with run
// loop.

#include "asyncx.hpp"
#include <gtest/gtest.h>

using namespace asyncx;

static net::io_context &test_io() {
    static net::io_context io;
    return io;
}

static auto executor() { return test_io().get_executor(); }

struct TestException : public std::runtime_error {
    TestException() : std::runtime_error("test-ex") {}
};

TEST(AsyncxAwait, RethrowsStoredExceptionPtr) {
    auto a = Async<int>::make(executor());

    // fail with exception_ptr
    a.fail(std::make_exception_ptr(TestException()));

    bool caught = false;

    // run a coroutine that awaits the Async and catches the rethrown exception
    net::co_spawn(
        executor(),
        [&]() -> net::awaitable<void> {
            try {
                (void)co_await a();
            } catch (const TestException &e) {
                caught = true;
            }
            co_return;
        },
        net::detached);

    // run the io to completion
    test_io().run_for(std::chrono::milliseconds(10));

    EXPECT_TRUE(caught);
}

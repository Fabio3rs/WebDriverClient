#include "asyncx.hpp"
#include <atomic>
#include <boost/asio/thread_pool.hpp>
#include <future>
#include <gtest/gtest.h>
#include <thread>

using namespace std::chrono_literals;

TEST(PoolInjection, FromFutureRunsOnInjectedPoolThread) {
    auto pool = std::make_shared<boost::asio::thread_pool>(1);

    std::atomic<std::thread::id> pool_thread_id{};
    std::promise<void> ready;
    auto ready_f = ready.get_future();

    // Post a task to capture one of the pool thread ids and signal ready.
    boost::asio::post(*pool, [&] {
        pool_thread_id.store(std::this_thread::get_id());
        ready.set_value();
    });

    // Wait for pool thread to capture its id
    ready_f.wait();

    // deferred future: its callable will run in the thread that calls get()
    std::atomic<std::thread::id> deferred_get_thread_id{};
    auto fut = std::async(std::launch::deferred, [&]() {
        deferred_get_thread_id.store(std::this_thread::get_id());
        return 12345;
    });

    // Use Async::from_future to convert; it should post the get() onto the
    // injected pool
    auto a = asyncx::Async<int>::from_future(boost::asio::system_executor{},
                                             std::move(fut), pool);

    std::promise<int> done;
    auto done_f = done.get_future();

    a.finally([&](std::optional<int> v,
                  [[maybe_unused]] std::optional<asyncx::EC> ec,
                  const std::exception_ptr &ep) {
        if (v) {
            done.set_value(*v);
        } else {
            done.set_exception(
                ep ? ep
                   : std::make_exception_ptr(std::runtime_error("no value")));
        }
    });

    // wait for completion
    auto res = done_f.get();
    EXPECT_EQ(res, 12345);
    // Both ids should be set and equal
    ASSERT_NE(pool_thread_id.load(), std::thread::id{});
    ASSERT_NE(deferred_get_thread_id.load(), std::thread::id{});
    EXPECT_EQ(pool_thread_id.load(), deferred_get_thread_id.load());

    pool->join();
}

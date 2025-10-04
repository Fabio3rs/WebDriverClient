#include "bidi/timer_pool.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <gtest/gtest.h>
#include <thread>

using namespace std::chrono_literals;
using bidi::core::TimeoutId;
using bidi::core::TimerWheel;

TEST(TimerWheelPostTest, HeavyHandlersDoNotBlockTicks) {
    boost::asio::io_context ioc;
    TimerWheel wheel(ioc, 5ms); // fast tick
    wheel.start();

    std::atomic<int> executed{0};
    constexpr int kTimers = 200;

    for (int i = 0; i < kTimers; ++i) {
        const auto tid = wheel.schedule_timeout(1ms, [&](TimeoutId) {
            // Simulates a heavy handler (short busy loop)
            volatile int acc = 0;
            for (int j = 0; j < 10000; ++j) {
                acc += j;
            }
            (void)acc;
            executed.fetch_add(1, std::memory_order_relaxed);
        });
        (void)tid; // suppress warning nodiscard
    }

    // Rodar o io_context em thread separada
    std::thread th([&] { ioc.run_for(200ms); });
    th.join();

    auto stats = wheel.get_stats();
    // Todos devem ter sido postados e expirados
    EXPECT_EQ(executed.load(), kTimers);
    EXPECT_EQ(stats.posted_handlers, static_cast<size_t>(kTimers));
}

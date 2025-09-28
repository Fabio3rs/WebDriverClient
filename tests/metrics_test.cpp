#include "bidi/metrics.hpp"
#include <gtest/gtest.h>

using namespace bidi::metrics;

TEST(Metrics, CounterIncrement) {
    Registry reg;
    auto &counter = reg.counter("test.counter");
    EXPECT_EQ(counter.value(), 0U);
    counter.inc();
    EXPECT_EQ(counter.value(), 1U);
    counter.inc(4);
    EXPECT_EQ(counter.value(), 5U);
}

TEST(Metrics, HistogramObserve) {
    Registry reg;
    auto &hist = reg.histogram("test.hist");
    EXPECT_EQ(hist.count(), 0U);
    hist.observe(1);
    hist.observe(2);
    constexpr int sample_us = 10;
    hist.observe(sample_us);
    EXPECT_EQ(hist.count(), 3U);
    auto snap = hist.snapshot();
    // at least one bucket should be non-zero
    bool any = false;
    for (auto value : snap) {
        if (value > 0) {
            any = true;
            break;
        }
    }
    EXPECT_TRUE(any);
}

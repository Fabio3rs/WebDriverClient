#include <gtest/gtest.h>
#include "SubscriptionManager.hpp"

using namespace bidi::ws;

TEST(SubscriptionManager, SubscribeAndDispatch) {
    SubscriptionManager mgr;
    bool called = false;
    auto h = mgr.subscribe("topic1", [&](std::string_view payload){ called = true; EXPECT_EQ(payload, "hello"); });
    mgr.dispatch("topic1", "hello");
    EXPECT_TRUE(called);
}

TEST(SubscriptionManager, UnsubscribeOnDestroy) {
    SubscriptionManager mgr;
    bool called = false;
    {
        auto h = mgr.subscribe("topic2", [&](std::string_view){ called = true; });
    }
    mgr.dispatch("topic2", "x");
    EXPECT_FALSE(called);
}

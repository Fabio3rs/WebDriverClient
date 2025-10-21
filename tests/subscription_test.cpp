#include "SubscriptionManager.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <gtest/gtest.h>

using namespace bidi::ws;

TEST(SubscriptionManager, SubscribeAndDispatch) {
    boost::asio::io_context io;
    SubscriptionManager mgr{io.get_executor()};
    bool called = false;
    SubscriptionHandle handle;

    boost::asio::post(mgr.executor(), [&] {
        handle = mgr.subscribe("topic1", [&](std::string_view payload) {
            called = true;
            EXPECT_EQ(payload, "hello");
        });
        mgr.dispatch("topic1", "hello");
    });

    io.run();
    EXPECT_TRUE(called);
}

TEST(SubscriptionManager, UnsubscribeOnDestroy) {
    boost::asio::io_context io;
    SubscriptionManager mgr{io.get_executor()};
    bool called = false;

    boost::asio::post(mgr.executor(), [&] {
        {
            auto handle = mgr.subscribe(
                "topic2", [&](std::string_view) { called = true; });
            (void)handle;
        }
        mgr.dispatch("topic2", "x");
    });

    io.run();
    EXPECT_FALSE(called);
}

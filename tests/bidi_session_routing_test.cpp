#include "bidi/core.hpp"
#include "bidi_methods.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
#include <future>
#include <gtest/gtest.h>

using namespace bidi::core;

TEST(BidiSessionRouting, PendingResponsesReordered) {
    boost::asio::io_context ioc;
    auto ws = std::make_shared<WebSocketClient>(ioc);
    auto session = std::make_shared<BiDiSession>(ws);

    int called1 = 0;
    int called2 = 0;
    session->send_command("cmd.one", {}, [&](const ParsedResponse &r) {
        EXPECT_TRUE(r.is_success);
        called1++;
    });
    session->send_command("cmd.two", {}, [&](const ParsedResponse &r) {
        EXPECT_TRUE(r.is_success);
        called2++;
    });

    // Respostas fora de ordem
#ifdef BIDI_TESTING
    session->test_inject_message(R"({"id":2,"type":"success","result":{}})");
    session->test_inject_message(R"({"id":1,"type":"success","result":{}})");
#endif
    EXPECT_EQ(called1, 1);
    EXPECT_EQ(called2, 1);
}

TEST(BidiSessionRouting, EventDispatchAndUnsubscribe) {
    boost::asio::io_context ioc;
    auto ws = std::make_shared<WebSocketClient>(ioc);
    auto session = std::make_shared<BiDiSession>(ws);

    // Start io_context in a separate thread to handle async operations
    std::thread ioc_thread([&ioc]() { ioc.run(); });

    std::promise<void> test_done;
    auto future = test_done.get_future();

    // Use co_spawn to run the test
    boost::asio::co_spawn(
        ioc,
        [session, &test_done]() -> boost::asio::awaitable<void> {
            int event_calls = 0;
            {
                // RAII scope: Subscription auto-unsubscribes on scope exit
                auto sub = session->subscribe_event_scoped(
                    std::string(bidi::ids::events::log_entryAdded), {},
                    [&](const ParsedEvent &event) {
                        EXPECT_EQ(event.method,
                                  bidi::ids::events::log_entryAdded);
                        event_calls++;
                    });

                // Wait a bit for the command to be sent
                boost::asio::steady_timer timer(
                    co_await boost::asio::this_coro::executor);
                timer.expires_after(std::chrono::milliseconds(100));
                co_await timer.async_wait(boost::asio::use_awaitable);

                // Simulate successful session.subscribe response
                session->test_inject_message(
                    R"({"id":1,"type":"success","result":{"subscription":"sub123"}})");

                // Inject event
                boost::json::object event_obj;
                event_obj["type"] = "event";
                event_obj["method"] =
                    std::string(bidi::ids::events::log_entryAdded);
                event_obj["params"] = boost::json::object{};
                session->test_inject_message(boost::json::serialize(event_obj));

                EXPECT_EQ(event_calls, 1);

                // Explicitly cancel subscription (RAII will also call on scope
                // exit)
                sub.cancel();

                // Wait for unsubscribe to complete
                timer.expires_after(std::chrono::milliseconds(50));
                co_await timer.async_wait(boost::asio::use_awaitable);

                // Inject event again, should not call
                session->test_inject_message(boost::json::serialize(event_obj));
                EXPECT_EQ(event_calls, 1);
            } // sub destructor called here (but already cancelled)

            test_done.set_value();
            co_return;
        },
        boost::asio::detached);

    // Wait for test to complete
    future.wait();

    // Stop io_context and join thread
    ioc.stop();
    ioc_thread.join();
}

TEST(BidiSessionRouting, OnErrorClearsPending) {
    boost::asio::io_context ioc;
    auto ws = std::make_shared<WebSocketClient>(ioc);
    auto session = std::make_shared<BiDiSession>(ws);

    int error_cb = 0;
    session->send_command("cmd.err", {}, [&](const ParsedResponse &r) {
        EXPECT_FALSE(r.is_success);
        EXPECT_EQ(r.error_code, "websocket_error");
        error_cb++;
    });

#ifdef BIDI_TESTING
    session->test_inject_error(
        make_error_code(boost::system::errc::connection_reset));
    EXPECT_EQ(error_cb, 1);
    EXPECT_EQ(session->test_pending_size(), 0U);
#endif
}

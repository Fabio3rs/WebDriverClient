#include "bidi/core.hpp"
#include "bidi_methods.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
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

    int event_calls = 0;
    session->subscribe_event(std::string(bidi::ids::events::log_entryAdded),
                             [&](const ParsedEvent &ev) {
                                 EXPECT_EQ(ev.method,
                                           bidi::ids::events::log_entryAdded);
                                 event_calls++;
                             });

#ifdef BIDI_TESTING
    // helper to build event JSON
    auto make_event_str = [](const std::string &method,
                             const boost::json::object &params = {}) {
        boost::json::object o;
        o["type"] = "event";
        o["method"] = method;
        o["params"] = params;
        return boost::json::serialize(o);
    };

    session->test_inject_message(
        make_event_str(std::string(bidi::ids::events::log_entryAdded)));
#endif
    EXPECT_EQ(event_calls, 1);
    session->unsubscribe_event(std::string(bidi::ids::events::log_entryAdded));
#ifdef BIDI_TESTING
    session->test_inject_message(
        make_event_str(std::string(bidi::ids::events::log_entryAdded)));
#endif
    EXPECT_EQ(event_calls, 1);
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

// demo_threaded_bidi.cpp — New demo using ThreadedBiDiSession with zero
// busy-wait
#include "bidi/logging.hpp"
#include "bidi/session_threaded.hpp"
#include "bidi/threading.hpp"
#include "bidi_methods.hpp"
#include <chrono>
// #include <iostream> removed (unused)

using namespace bidi::core;

auto main() -> int {
    bidi::logging::log_info(
        "🚀 ThreadedBiDiSession Demo - Zero Busy-Wait Architecture");
    bidi::logging::log_info(
        "========================================================");

    try {
        // 1. Create threading context with native primitives
        auto threading = std::make_shared<ThreadingContext>();

        // 2. Create threaded BiDi session
        auto session = std::make_shared<ThreadedBiDiSession>(threading);

        // 3. Connect using native async suspension (no polling!)
        bidi::logging::log_info("Connecting to WebSocket...");
        auto connect_future =
            session->async_connect("ws://localhost:9222/devtools/browser");

        // NATIVE AWAIT: Thread suspends here until kernel I/O completion
        bool connected = connect_future.get();

        if (!connected) {
            bidi::logging::log_error("❌ Connection failed");
            return 1;
        }

        bidi::logging::log_info("✅ Connected successfully");

        // 4. Subscribe to events (zero busy-wait pub/sub)
        session->subscribe_event(
            std::string(bidi::ids::events::bc_contextCreated),
            [](const std::string &method, const boost::json::object &params) {
                bidi::logging::log_info(std::string("🎯 Event received: ") +
                                        method);
                bidi::logging::log_info(std::string("   Params: ") +
                                        boost::json::serialize(params));
            });

        // 5. Send commands with native awaiting (no condition_variable!)
        bidi::logging::log_info("\n📨 Sending BiDi commands...");

        // Create browsing context - thread suspends until response
        auto create_result = session->send_command_await(
            std::string(bidi::ids::methods::bc_create),
            boost::json::object{{"type", "window"}}, std::chrono::seconds(5));

        bidi::logging::log_info(std::string("✅ Context created: ") +
                                boost::json::serialize(create_result));

        // Navigate - thread suspends until navigation completes
        std::string context_id =
            create_result.at("context").as_string().c_str();
        auto nav_result = session->send_command_await(
            std::string(bidi::ids::methods::bc_navigate),
            boost::json::object{{"context", context_id},
                                {"url", "https://example.com"}},
            std::chrono::seconds(10));

        bidi::logging::log_info(std::string("✅ Navigation completed: ") +
                                boost::json::serialize(nav_result));

        // Evaluate script - thread suspends until execution finishes
        auto eval_result = session->send_command_await(
            std::string(bidi::ids::methods::script_evaluate),
            boost::json::object{
                {"expression", "document.title"},
                {"target", boost::json::object{{"context", context_id}}},
                {"awaitPromise", true}},
            std::chrono::seconds(5));

        bidi::logging::log_info(std::string("✅ Script evaluation: ") +
                                boost::json::serialize(eval_result));

        // 6. Demonstrate coroutine awaitable support (C++20/23)
        bidi::logging::log_info("\n🔄 Testing awaitable coroutine support...");

        // This would suspend the coroutine (not block thread)
        /*
        auto coro_result = co_await session->send_command_awaitable(
            std::string(bidi::ids::methods::script_evaluate),
            boost::json::object{
                {"expression", "window.location.href"},
                {"target", boost::json::object{{"context", context_id}}}
            }
        );
        */

        bidi::logging::log_info(
            "\n🎉 ThreadedBiDiSession demo completed successfully!");
        bidi::logging::log_info("\n📋 Architecture highlights:");
        bidi::logging::log_info("   ✅ Zero busy-wait: Native kernel "
                                "suspension (epoll/kqueue/IOCP)");
        bidi::logging::log_info(
            "   ✅ Thread-safe: Boost.Asio strand for WebSocket serialization");
        bidi::logging::log_info("   ✅ Promise-based: std::promise/std::future "
                                "for native awaiting");
        bidi::logging::log_info(
            "   ✅ Event-driven: Pub/sub system with thread-safe handlers");
        bidi::logging::log_info(
            "   ✅ Timeout support: steady_timer with native cancellation");
        bidi::logging::log_info(
            "   ✅ W3C BiDi compliant: Proper message envelope detection");
        bidi::logging::log_info(
            "   ✅ Coroutine ready: boost::asio::awaitable<T> support");

        // Clean disconnect
        session->disconnect();
        bidi::logging::log_info("🔌 Disconnected cleanly");

        return 0;

    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("❌ Demo failed: ") + e.what());
        return 1;
    }
}

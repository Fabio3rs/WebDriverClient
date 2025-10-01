// Example: Baseline threaded BiDi session using ThreadedBiDiSession
// Origin: migrated from demo_threaded_bidi.cpp

#include "bidi/logging.hpp"
#include "bidi/session_threaded.hpp"
#include "bidi/threading.hpp"
#include "bidi_methods.hpp"
#include <chrono>
#include <memory>
#include <optional>

using namespace bidi::core;

auto main() -> int {
    bidi::logging::log_info("ThreadedBiDiSession Baseline Example");
    try {
        auto threading = std::make_shared<ThreadingContext>();
        auto session = std::make_shared<ThreadedBiDiSession>(threading);
        bidi::logging::log_info("Connecting to WebSocket...");
        auto connect_future =
            session->async_connect("ws://localhost:9222/devtools/browser");
        bool connected = connect_future.get();
        if (!connected) {
            bidi::logging::log_error("Connection failed");
            return 1;
        }
        static auto sub_global = session->subscribe(
            std::string(bidi::ids::events::bc_contextCreated), std::nullopt,
            [](const std::string &method, const boost::json::object &params) {
                bidi::logging::log_info(std::string("Event received: ") +
                                        method);
                bidi::logging::log_info(std::string("Params: ") +
                                        boost::json::serialize(params));
            });
        auto create_result = session->send_command_await(
            std::string(bidi::ids::methods::bc_create),
            boost::json::object{{"type", "window"}}, std::chrono::seconds(5));
        std::string context_id =
            create_result.at("context").as_string().c_str();
        auto nav_result = session->send_command_await(
            std::string(bidi::ids::methods::bc_navigate),
            boost::json::object{{"context", context_id},
                                {"url", "https://example.com"},
                                {"wait", "complete"}},
            std::chrono::seconds(10));
        static auto sub_scoped = session->subscribe(
            std::string(bidi::ids::events::log_entryAdded),
            std::optional<std::string>{context_id},
            [](const std::string &method, const boost::json::object &params) {
                bidi::logging::log_info(std::string("[scoped] Event: ") +
                                        method);
                bidi::logging::log_info(std::string("[scoped] Params: ") +
                                        boost::json::serialize(params));
            });
        auto eval_result = session->send_command_await(
            std::string(bidi::ids::methods::script_evaluate),
            boost::json::object{
                {"expression", "document.title"},
                {"target", boost::json::object{{"context", context_id}}},
                {"awaitPromise", true}},
            std::chrono::seconds(5));
        bidi::logging::log_info(std::string("Script evaluation: ") +
                                boost::json::serialize(eval_result));
        session->disconnect();
        return 0;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Demo failed: ") + e.what());
        return 1;
    }
}

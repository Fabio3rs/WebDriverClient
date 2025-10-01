// Example: Minimal BiDi flow using high-level API and generic async_send
// Origin: migrated from demo_new_bidi.cpp

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <memory>

#include "bidi/client.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include "bidi_methods.hpp"

namespace net = boost::asio;

net::awaitable<void>
example_coroutine_flow(std::shared_ptr<bidi::Client> client, std::string ctx) {
    boost::json::object params{
        {"expression", "document.title"},
        {"target", boost::json::object{{"context", ctx}}},
        {"awaitPromise", true}};
    auto result = co_await client->async_send(
        std::string(bidi::ids::methods::script_evaluate), params,
        net::use_awaitable);
    bidi::logging::log_info(std::string("[co_await] title result: ") +
                            boost::json::serialize(result));
    co_return;
}

net::awaitable<int> minimal_flow(net::io_context *ioc_ptr) {
    // Obtain websocket url via temporary SessionGuard (RAII cleanup)
    bidi::SessionGuard session_guard("http://localhost:9515");
    WebDriver::json args =
        WebDriver::json::array({"--headless", "--no-sandbox"});
    auto ws_url = session_guard.connect(args, "chrome", true);
    if (!ws_url) {
        bidi::logging::log_error(ws_url.error());
        co_return 1;
    }
    auto client_ptr = co_await bidi::Client::connect(*ioc_ptr, *ws_url)();
    if (!client_ptr) {
        bidi::logging::log_error("Connection failed");
        co_return 1;
    }
    bidi::ClientGuard client_guard(client_ptr);
    auto context_id = co_await client_guard.client()->create_context()();
    bidi::logging::log_info(std::string("Context: ") + context_id);
    auto nav_url = co_await client_guard.client()->navigate(
        context_id, "https://example.com")();
    bidi::logging::log_info(std::string("Navigated to: ") + nav_url);
    // Evaluate readyState and title using awaitable + generic async_send
    net::co_spawn(*ioc_ptr,
                  example_coroutine_flow(client_guard.client(), context_id),
                  net::detached);
    auto ready_obj = co_await client_guard.client()->evaluate(
        "document.readyState", context_id)();
    bidi::logging::log_info(std::string("readyState: ") +
                            boost::json::serialize(ready_obj));
    co_return 0;
}

auto main() -> int {
    try {
        net::io_context ioc;
        int code = 0;
        net::co_spawn(
            ioc, minimal_flow(&ioc),
            [&](const std::exception_ptr &exception_ptr_ref, int result) {
                if (exception_ptr_ref) {
                    try {
                        std::rethrow_exception(exception_ptr_ref);
                    } catch (const std::exception &ex) {
                        bidi::logging::log_error(std::string("Exception: ") +
                                                 ex.what());
                        code = 1;
                    }
                } else {
                    code = result;
                }
            });
        ioc.run();
        return code;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Exception: ") + e.what());
        return 1;
    }
}

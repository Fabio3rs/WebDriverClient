// Example: Minimal BiDi flow using high-level API and generic async_send
// Origin: migrated from demo_new_bidi.cpp

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <memory>

#include "bidi/client.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include "bidi_methods.hpp"

namespace net = boost::asio;

auto example_coroutine_flow(std::shared_ptr<bidi::Client> client,
                            std::string ctx) -> net::awaitable<void> {
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

auto minimal_flow(net::io_context *ioc_ptr) -> net::awaitable<int> {
    // Connect using ConnectionBuilder (fluent helper)
    try {
        // Example of passing explicit capabilities if desired
        // auto caps = WebDriver::json::object();
        // caps["capabilities"] = ...;
        auto client_ptr = co_await bidi::connect_to("http://localhost:9515")
                              .headless()
                              .no_sandbox()
                              .connect(*ioc_ptr);
        bidi::ClientGuard client_guard(client_ptr);
        auto context_id = co_await client_guard.client()->create_context();
        bidi::logging::log_info(std::string("Context: ") + context_id);
        auto nav_url = co_await client_guard.client()->navigate(
            context_id, "https://example.com");
        bidi::logging::log_info(std::string("Navigated to: ") + nav_url);
        // Evaluate readyState and title using awaitable + generic async_send
        net::co_spawn(*ioc_ptr,
                      example_coroutine_flow(client_guard.client(), context_id),
                      net::detached);
        auto ready_obj = co_await client_guard.client()->evaluate(
            "document.readyState", context_id);
        bidi::logging::log_info(std::string("readyState: ") +
                                boost::json::serialize(ready_obj));
        co_return 0;
    } catch (const std::exception &ex) {
        bidi::logging::log_error(std::string("Connection failed: ") +
                                 ex.what());
        co_return 1;
    }
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

// Example: Minimal BiDi flow using IoContextRunner helper (syntactic sugar)

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <memory>
#include <thread>

#include "bidi/client.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/guards.hpp"
#include "bidi/io_context_runner.hpp"
#include "bidi/logging.hpp"

namespace net = boost::asio;

auto example_coroutine_flow(std::shared_ptr<bidi::Client> client,
                            std::string ctx) -> net::awaitable<void> {
    boost::json::object params{
        {"expression", "document.title"},
        {"target", boost::json::object{{"context", ctx}}},
        {"awaitPromise", true}};
    auto result = co_await client->async_send(
        bidi::ids::methods::script_evaluate, params, net::use_awaitable);
    bidi::logging::log_info(std::string("[co_await] title result: ") +
                            boost::json::serialize(result));
    co_return;
}

int main() {
    try {
        // IoContextRunner starts an io_context in a background thread and
        // exposes the io_context for use by the library. This reduces the
        // ceremony of creating a local io_context and calling run() manually
        bidi::IoContextRunner runner; // single background thread
        auto &ioc = runner.get();

        // Spawn the minimal coroutine flow on the background io_context
        net::co_spawn(
            ioc,
            [](net::io_context *ioc_ptr) -> net::awaitable<int> {
                try {
                    auto client_ptr =
                        co_await bidi::connect_to("http://localhost:9515")
                            .headless()
                            .no_sandbox()
                            .connect(*ioc_ptr);
                    bidi::ClientGuard client_guard(client_ptr);
                    auto context_id =
                        co_await client_guard.client()->create_context();
                    bidi::logging::log_info(std::string("Context: ") +
                                            context_id);
                    auto nav_url = co_await client_guard.client()->navigate(
                        context_id, "https://example.com");
                    bidi::logging::log_info(std::string("Navigated to: ") +
                                            nav_url);
                    net::co_spawn(*ioc_ptr,
                                  example_coroutine_flow(client_guard.client(),
                                                         context_id),
                                  net::detached);
                    co_return 0;
                } catch (const std::exception &ex) {
                    bidi::logging::log_error(
                        std::string("Connection failed: ") + ex.what());
                    co_return 1;
                }
            }(&ioc),
            net::detached);

        // The runner owns the background thread; main can continue doing work
        // or sleep until the example completes. For simplicity, block here.
        constexpr auto kWaitSeconds = 5;
        std::this_thread::sleep_for(std::chrono::seconds(kWaitSeconds));
        return 0;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Exception: ") + e.what());
        return 1;
    }
}

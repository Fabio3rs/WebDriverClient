#include "bidi/client.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/json/serialize.hpp>
#include <format>
#include <future>

namespace asio = boost::asio;

namespace {

auto run_flow(asio::io_context &io_context,
              std::string websocket_url) -> asio::awaitable<int> {
    auto client = co_await bidi::Client::connect(io_context, websocket_url);
    bidi::ClientGuard client_guard(client);

    const auto context = co_await client->create_context();
    const auto final_url =
        co_await client->navigate(context, "https://example.com");
    const auto title = co_await client->evaluate("document.title", context);

    bidi::logging::log_info(std::format("Navigated to {}", final_url));
    bidi::logging::log_info(
        std::format("Raw BiDi result: {}", boost::json::serialize(title)));
    co_return 0;
}

} // namespace

auto main() -> int {
    try {
        auto [websocket_url, session_guard] =
            bidi::connect_to("http://localhost:9515")
                .headless()
                .get_websocket_url();
        asio::io_context io_context;
        auto result = asio::co_spawn(
            io_context, run_flow(io_context, std::move(websocket_url)),
            asio::use_future);
        io_context.run();
        (void)session_guard;
        return result.get();
    } catch (const std::exception &error) {
        bidi::logging::log_error(
            std::format("BiDi flow failed: {}", error.what()));
        return 1;
    }
}

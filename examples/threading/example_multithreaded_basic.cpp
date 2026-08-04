// Example: Basic multi-threaded BiDi client with strand serialization
//
// Demonstrates:
// - Multiple io_context threads processing I/O concurrently
// - Strand ensuring serial access to BiDi session state
// - How handlers can run on different threads over time
// - Thread-safe command execution

#include "asyncx.hpp"
#include "bidi/client.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/logging.hpp"
#include "io_context_threads.hpp"
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/json/serialize.hpp>
#include <cstddef>
#include <format>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;

// Helper to log thread IDs
void log_thread(const std::string &message) {
    auto tid = std::this_thread::get_id();
    std::ostringstream oss;
    oss << "[Thread " << tid << "] " << message;
    bidi::logging::log_info(oss.str());
}

// Coroutine demonstrating concurrent command execution
// Importante: todos os parâmetros passados por valor para evitar lifetime
// issues (AddressSanitizer acusou stack-use-after-return quando referências
// eram usadas em frames de corrotinas finalizados). strings são pequenas e
// movidas pelo NRVO.
auto execute_concurrent_commands(
    std::shared_ptr<bidi::Client> client, std::string context_id,
    std::size_t command_count) -> asio::awaitable<void> {

    log_thread("Starting concurrent command execution");

    // Execute multiple commands concurrently
    // Even though they run concurrently, strand ensures WebSocket state is safe
    std::vector<asyncx::Async<boost::json::object>> tasks;
    tasks.reserve(command_count);

    for (std::size_t index = 0; index < command_count; ++index) {
        const std::string expression = std::format(
            "console.log('Command {} from concurrent batch')", index);

        // Each evaluate() creates an async operation
        // They all share the same WebSocket connection (via strand)
        tasks.push_back(client->evaluate(expression, context_id));
    }

    auto results =
        co_await asyncx::all(client->get_executor(), std::move(tasks));
    for (const auto &result : results) {
        log_thread(std::format("Command completed: {}",
                               boost::json::serialize(result)));
    }

    log_thread("All concurrent commands completed");
}

auto run_multithreaded_demo(asio::io_context &io_context,
                            std::string websocket_url) -> asio::awaitable<int> {
    auto executor = co_await asio::this_coro::executor;

    log_thread("Connecting BiDi client");
    auto client = co_await bidi::Client::connect(io_context, websocket_url);

    const auto context = co_await client->create_context();
    co_await client->navigate(context, "https://example.com");

    constexpr std::size_t concurrent_commands = 10;
    co_await execute_concurrent_commands(client, context, concurrent_commands);

    co_await client->close_context(context);
    client->disconnect();
    co_await asio::post(asio::bind_executor(executor, asio::use_awaitable));
    co_return 0;
}

auto run_example() -> int {
    auto [websocket_url, session_guard] =
        bidi::connect_to("http://localhost:9515")
            .headless()
            .no_sandbox()
            .get_websocket_url();

    asio::io_context io_context;
    auto result = asio::co_spawn(
        io_context, run_multithreaded_demo(io_context, websocket_url),
        asio::use_future);

    const unsigned int thread_count =
        std::clamp(std::thread::hardware_concurrency(), 1U, 4U);
    const bidi::examples::IoContextThreads io_threads(io_context, thread_count);
    (void)session_guard;
    return result.get();
}

auto main() -> int {
    try {
        return run_example();
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Fatal error: {}", e.what()));
        return 1;
    }
}

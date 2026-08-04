// Example: Multi-threaded BiDi with separate CPU thread pool
//
// Demonstrates:
// - I/O threads for WebSocket operations
// - Separate CPU thread pool for heavy processing
// - Proper coordination between thread pools
// - Offloading JSON parsing and transformations

#include "bidi/client.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/logging.hpp"
#include "io_context_threads.hpp"
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/json.hpp>
#include <cstdint>
#include <format>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace asio = boost::asio;

// Helper to identify thread type
auto get_thread_info() -> std::string {
    auto tid = std::this_thread::get_id();
    std::ostringstream oss;
    oss << tid;
    return oss.str();
}

// Simulates heavy CPU-bound work (JSON processing, data transformation, etc.)
auto process_heavy_data(const boost::json::object &input)
    -> boost::json::object {
    auto thread_id = get_thread_info();
    bidi::logging::log_info(
        std::format("CPU thread {}: Processing heavy data", thread_id));

    const auto serialized = boost::json::serialize(input);
    std::uint64_t checksum = 1469598103934665603ULL;
    for (int pass = 0; pass < 1000; ++pass) {
        for (const char character : serialized) {
            const auto byte = static_cast<unsigned char>(character);
            checksum ^= byte;
            checksum *= 1099511628211ULL;
        }
    }

    boost::json::object result;
    result["processed"] = true;
    result["thread_id"] = thread_id;
    result["input_size"] = input.size();
    result["checksum"] = checksum;

    return result;
}

// Coroutine that offloads work to CPU pool
// Pass context_id by value to avoid lifetime issues
auto process_with_cpu_offload(
    std::shared_ptr<bidi::Client> client, std::string context_id,
    std::shared_ptr<asio::thread_pool> cpu_pool) -> asio::awaitable<void> {

    bidi::logging::log_info(
        std::format("I/O thread {}: Starting command", get_thread_info()));

    // Execute script (runs on I/O thread via strand)
    auto result =
        co_await client->evaluate("({data: Array(100).fill(0).map((_, i) => "
                                  "({id: i, value: Math.random() "
                                  "* 1000}))})",
                                  context_id);

    bidi::logging::log_info(
        std::format("I/O thread {}: Got result, offloading to CPU pool",
                    get_thread_info()));

    // Offload heavy processing to CPU pool
    // This prevents blocking the I/O threads
    auto processed = co_await asio::co_spawn(
        *cpu_pool,
        [result = std::move(result)]() -> asio::awaitable<boost::json::object> {
            // This lambda runs on CPU thread pool
            auto cpu_result = process_heavy_data(result);
            co_return cpu_result;
        },
        asio::use_awaitable);

    bidi::logging::log_info(
        std::format("I/O thread {}: CPU work completed, result: {}",
                    get_thread_info(), boost::json::serialize(processed)));
}

auto run_cpu_pool_demo(asio::io_context &io_context, std::string websocket_url,
                       std::shared_ptr<asio::thread_pool> cpu_pool)
    -> asio::awaitable<int> {
    auto client = co_await bidi::Client::connect(io_context, websocket_url);
    const auto context = co_await client->create_context();
    co_await client->navigate(context, "https://example.com");

    for (int task = 0; task < 5; ++task) {
        bidi::logging::log_info(std::format("Starting CPU task {}", task));
        co_await process_with_cpu_offload(client, context, cpu_pool);
    }

    co_await client->close_context(context);
    client->disconnect();
    co_await asio::post(asio::use_awaitable);
    co_return 0;
}

auto run_example() -> int {
    auto [websocket_url, session_guard] =
        bidi::connect_to("http://localhost:9515")
            .headless()
            .no_sandbox()
            .get_websocket_url();

    asio::io_context io_context;
    const unsigned int cpu_thread_count =
        std::clamp(std::thread::hardware_concurrency(), 1U, 8U);
    auto cpu_pool = std::make_shared<asio::thread_pool>(cpu_thread_count);
    auto result = asio::co_spawn(
        io_context, run_cpu_pool_demo(io_context, websocket_url, cpu_pool),
        asio::use_future);
    const bidi::examples::IoContextThreads io_threads(io_context, 2U);

    const int exit_code = result.get();
    cpu_pool->stop();
    cpu_pool->join();
    (void)session_guard;
    return exit_code;
}

auto main() -> int {
    try {
        return run_example();
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Fatal error: {}", e.what()));
        return 1;
    }
}

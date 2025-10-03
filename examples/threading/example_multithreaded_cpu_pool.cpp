// Example: Multi-threaded BiDi with separate CPU thread pool
//
// Demonstrates:
// - I/O threads for WebSocket operations
// - Separate CPU thread pool for heavy processing
// - Proper coordination between thread pools
// - Offloading JSON parsing and transformations

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <format>
#include <thread>
#include <vector>

namespace asio = boost::asio;

// Helper to identify thread type
std::string get_thread_info() {
    auto tid = std::this_thread::get_id();
    std::ostringstream oss;
    oss << tid;
    return oss.str();
}

// Simulates heavy CPU-bound work (JSON processing, data transformation, etc.)
boost::json::object process_heavy_data(const boost::json::object &input) {
    auto thread_id = get_thread_info();
    bidi::logging::log_info(
        std::format("CPU thread {}: Processing heavy data", thread_id));

    // Simulate expensive computation
    boost::json::object result;
    result["processed"] = true;
    result["thread_id"] = thread_id;
    result["input_size"] = input.size();

    // Simulate CPU work
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    return result;
}

// Coroutine that offloads work to CPU pool
// Pass context_id by value to avoid lifetime issues
asio::awaitable<void>
process_with_cpu_offload(std::shared_ptr<bidi::Client> client,
                         std::string context_id,
                         std::shared_ptr<asio::thread_pool> cpu_pool) {

    bidi::logging::log_info(
        std::format("I/O thread {}: Starting command", get_thread_info()));

    // Execute script (runs on I/O thread via strand)
    auto result =
        co_await client->evaluate("({data: Array(100).fill(0).map((_, i) => "
                                  "({id: i, value: Math.random() "
                                  "* 1000}))})",
                                  context_id)();

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

// Main demo coroutine
asio::awaitable<int>
run_cpu_pool_demo(std::string websocket_url,
                  std::shared_ptr<asio::thread_pool> cpu_pool) {
    try {
        auto executor = co_await asio::this_coro::executor;
        auto &ioc = static_cast<asio::io_context &>(executor.context());

        bidi::logging::log_info(
            std::format("Connecting on I/O thread {}", get_thread_info()));

        auto client = co_await bidi::Client::connect(ioc, websocket_url)();
        if (!client) {
            bidi::logging::log_error("Failed to connect");
            co_return 1;
        }

        auto ctx = co_await client->create_context()();
        co_await client->navigate(ctx, "https://example.com")();

        // Launch multiple concurrent operations
        // Each will offload CPU work to the thread pool
        std::vector<asio::awaitable<void>> tasks;
        for (int i = 0; i < 5; ++i) {
            tasks.push_back(process_with_cpu_offload(client, ctx, cpu_pool));
        }

        // Wait for all to complete
        for (auto &task : tasks) {
            co_await std::move(task);
        }

        bidi::logging::log_info("All tasks completed");

        // Cleanup: close context and disconnect
        try {
            co_await client->close_context(ctx)();
            bidi::logging::log_info("Closed browsing context");
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("close_context error: {}", e.what()));
        }

        client->disconnect();
        co_await asio::post(asio::use_awaitable); // Allow disconnect to post

        co_return 0;

    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Exception: {}", e.what()));
        co_return 1;
    }
}

int main() {
    try {
        bidi::logging::log_info("=== CPU Thread Pool Example ===");

        // Setup ChromeDriver session
        WebDriver driver;
        driver.webDriverUrl = "http://localhost:9515";
        WebDriver::json args =
            WebDriver::json::array({"--headless", "--no-sandbox"});
        auto response = driver.connect(args, "chrome", true);

        if (!response.contains("capabilities") ||
            !response["capabilities"].contains("webSocketUrl")) {
            bidi::logging::log_error("No webSocketUrl");
            return 1;
        }

        std::string ws_url =
            response["capabilities"]["webSocketUrl"].get<std::string>();

        // Create io_context for I/O operations
        asio::io_context ioc;

        // Create separate thread pool for CPU-bound work (use shared_ptr for
        // lifetime safety)
        const unsigned int cpu_threads = std::thread::hardware_concurrency();
        auto cpu_pool = std::make_shared<asio::thread_pool>(cpu_threads);

        bidi::logging::log_info(
            std::format("Created CPU pool with {} threads", cpu_threads));

        // Spawn demo
        auto future = asio::co_spawn(ioc, run_cpu_pool_demo(ws_url, cpu_pool),
                                     asio::use_future);

        // Run I/O context on 2 threads (for WebSocket I/O)
        const unsigned int io_threads = 2;
        bidi::logging::log_info(
            std::format("Starting {} I/O threads", io_threads));

        std::vector<std::thread> io_thread_vec;
        io_thread_vec.reserve(io_threads);
        for (unsigned int i = 0; i < io_threads; ++i) {
            io_thread_vec.emplace_back([&ioc, i]() {
                bidi::logging::log_info(std::format(
                    "I/O thread {} ({}) started", i, get_thread_info()));
                ioc.run();
                bidi::logging::log_info(std::format(
                    "I/O thread {} ({}) finished", i, get_thread_info()));
            });
        }

        // IMPORTANT: Get future result FIRST (waits for coroutine)
        int result = 1;
        try {
            result = future.get();
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("Future exception: {}", e.what()));
        }

        // THEN stop io_context
        ioc.stop();

        // THEN join I/O threads
        for (auto &thread : io_thread_vec) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        // Stop CPU pool
        cpu_pool->stop();
        cpu_pool->join();

        bidi::logging::log_info("Demo completed");
        return result;

    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Fatal error: {}", e.what()));
        return 1;
    }
}

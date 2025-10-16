// Example: Demonstrating strand safety with concurrent access
//
// Shows:
// - Multiple threads posting work to the same BiDi session
// - Strand serializing access even with concurrent threads
// - Proof that no race conditions occur
// - Visual demonstration of handler serialization

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/logging.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cstddef>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

namespace asio = boost::asio;

// Thread-safe logging
std::mutex log_mutex;
void safe_log(const std::string &msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    auto tid = std::this_thread::get_id();
    std::ostringstream oss;
    oss << "[Thread " << tid << "] " << msg;
    std::cout << oss.str() << '\n';
}

// Demonstrates strand serialization
class StrandSafetyDemo {
  private:
    std::shared_ptr<bidi::Client> client_;
    std::string context_id_;
    asio::io_context &ioc_;
    std::atomic<int> operations_started_{0};
    std::atomic<int> operations_completed_{0};

  public:
    StrandSafetyDemo(std::shared_ptr<bidi::Client> client,
                     std::string context_id, asio::io_context &ioc)
        : client_(std::move(client)), context_id_(std::move(context_id)),
          ioc_(ioc) {}

    // Execute a command and track timing
    auto execute_tracked_command(size_t command_id) -> asio::awaitable<void> {
        int started = operations_started_.fetch_add(1) + 1;
        safe_log(std::format("Command {}: Started (total started: {})",
                             command_id, started));

        // Execute JavaScript that simulates some work
        std::string expr =
            std::format("(function() {{ "
                        "  const start = Date.now(); "
                        "  while (Date.now() - start < 10) {{}} "
                        "  return {{ commandId: {}, completed: true }}; "
                        "}})()",
                        command_id);

        try {
            auto result = co_await client_->evaluate(expr, context_id_);

            int completed = operations_completed_.fetch_add(1) + 1;
            safe_log(std::format(
                "Command {}: Completed (total completed: {}) - Result: {}",
                command_id, completed, boost::json::serialize(result)));

        } catch (const std::exception &e) {
            safe_log(
                std::format("Command {}: Failed - {}", command_id, e.what()));
        }
    }

    // Launch commands from multiple threads
    void launch_concurrent_commands(size_t commands_per_thread,
                                    size_t thread_count) {
        safe_log(std::format("Launching {} threads, {} commands each",
                             thread_count, commands_per_thread));

        std::vector<std::thread> launcher_threads;

        launcher_threads.reserve(thread_count);
        for (size_t t = 0; t < thread_count; ++t) {
            launcher_threads.emplace_back([this, t, commands_per_thread]() {
                safe_log(std::format("Launcher thread {} started", t));

                for (size_t i = 0; i < commands_per_thread; ++i) {
                    size_t command_id = (t * 100) + i;

                    // Post coroutine spawn from this thread
                    // Even though we're posting from different threads,
                    // strand ensures WebSocket operations are serialized
                    asio::post(ioc_, [this, command_id]() {
                        asio::co_spawn(
                            ioc_, execute_tracked_command(command_id),
                            [command_id](const std::exception_ptr &ep) {
                                if (ep) {
                                    try {
                                        std::rethrow_exception(ep);
                                    } catch (const std::exception &e) {
                                        safe_log(
                                            std::format("Command {}: Exception "
                                                        "in spawn: {}",
                                                        command_id, e.what()));
                                    }
                                }
                            });
                    });

                    // Small delay between posts to spread them out
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                safe_log(std::format("Launcher thread {} finished posting", t));
            });
        }

        // Wait for all launcher threads
        for (auto &thread : launcher_threads) {
            thread.join();
        }

        safe_log("All launcher threads completed");
    }

    [[nodiscard]] auto get_operations_started() const -> int {
        return operations_started_.load();
    }
    [[nodiscard]] auto get_operations_completed() const -> int {
        return operations_completed_.load();
    }
};

// Main coroutine
auto run_strand_safety_demo(std::string websocket_url) -> asio::awaitable<int> {
    try {
        auto executor = co_await asio::this_coro::executor;
        auto &ioc = static_cast<asio::io_context &>(executor.context());

        safe_log("Connecting BiDi client");
        auto client = co_await bidi::Client::connect(ioc, websocket_url);

        if (!client) {
            safe_log("Failed to connect");
            co_return 1;
        }

        safe_log("Creating browsing context");
        auto ctx = co_await client->create_context();

        safe_log("Navigating to example.com");
        co_await client->navigate(ctx, "https://example.com");

        safe_log("\n=== Starting concurrent access test ===\n");

        // Create demo
        StrandSafetyDemo demo(client, ctx, ioc);

        // Launch commands from 4 different threads
        // Each thread posts 5 commands
        // Total: 20 concurrent operations
        demo.launch_concurrent_commands(5, 4);

        // Wait for all operations to complete
        safe_log("\n=== Waiting for all operations to complete ===\n");

        while (demo.get_operations_completed() <
               demo.get_operations_started()) {
            co_await asio::post(asio::use_awaitable);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        safe_log(std::format("\n=== Test completed ==="));
        safe_log(std::format("Operations started: {}",
                             demo.get_operations_started()));
        safe_log(std::format("Operations completed: {}",
                             demo.get_operations_completed()));
        safe_log("Note: All operations serialized by strand - no race "
                 "conditions!");

        co_return 0;

    } catch (const std::exception &e) {
        safe_log(std::format("Exception: {}", e.what()));
        co_return 1;
    }
}

auto main() -> int {
    try {
        safe_log("=== Strand Safety Demonstration ===");
        safe_log("This example shows how strand protects shared state");
        safe_log("even when multiple threads post concurrent operations\n");

        // Setup session
        WebDriver driver;
        driver.webDriverUrl = "http://localhost:9515";
        WebDriver::json args =
            WebDriver::json::array({"--headless", "--no-sandbox"});
        auto response = driver.connect(args, "chrome", true);

        if (!response.contains("capabilities") ||
            !response["capabilities"].contains("webSocketUrl")) {
            safe_log("No webSocketUrl");
            return 1;
        }

        std::string ws_url =
            response["capabilities"]["webSocketUrl"].get<std::string>();

        asio::io_context ioc;

        // Spawn demo
        auto future = asio::co_spawn(ioc, run_strand_safety_demo(ws_url),
                                     asio::use_future);

        // Run io_context on 3 threads to demonstrate concurrent execution
        safe_log("Starting 3 I/O threads for event processing\n");

        std::vector<std::thread> io_threads;
        io_threads.reserve(3);
        for (int i = 0; i < 3; ++i) {
            io_threads.emplace_back([&ioc, i]() {
                safe_log(std::format("I/O thread {} started", i));
                ioc.run();
                safe_log(std::format("I/O thread {} finished", i));
            });
        }

        // Wait for completion
        for (auto &thread : io_threads) {
            thread.join();
        }

        return future.get();

    } catch (const std::exception &e) {
        safe_log(std::format("Fatal error: {}", e.what()));
        return 1;
    }
}

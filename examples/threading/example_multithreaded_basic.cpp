// Example: Basic multi-threaded BiDi client with strand serialization
//
// Demonstrates:
// - Multiple io_context threads processing I/O concurrently
// - Strand ensuring serial access to BiDi session state
// - How handlers can run on different threads over time
// - Thread-safe command execution

#include "WebDriverClient.hpp"
#include "asyncx.hpp"
#include "bidi/client.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <format>
#include <iostream>
#include <sstream>
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
auto execute_concurrent_commands(std::shared_ptr<bidi::Client> client,
                                 std::string context_id,
                                 int command_count) -> asio::awaitable<void> {

    log_thread("Starting concurrent command execution");

    // Execute multiple commands concurrently
    // Even though they run concurrently, strand ensures WebSocket state is safe
    std::vector<asyncx::Async<boost::json::object>> tasks;
    tasks.reserve(static_cast<size_t>(command_count));

    for (int i = 0; i < command_count; ++i) {
        std::string expr =
            std::format("console.log('Command {} from concurrent batch')", i);

        // Each evaluate() creates an async operation
        // They all share the same WebSocket connection (via strand)
        tasks.push_back(client->evaluate(expr, context_id));
    }

    // Wait for all commands to complete
    // Note: These run concurrently but access to WebSocket is serialized
    for (auto &task : tasks) {
        auto result = co_await std::move(task);
        log_thread(std::format("Command completed: {}",
                               boost::json::serialize(result)));
    }

    log_thread("All concurrent commands completed");
}

// Main coroutine
// Também passamos websocket_url por valor.
auto run_multithreaded_demo(asio::io_context *ioc_ptr,
                            std::string websocket_url) -> asio::awaitable<int> {
    try {
        auto executor = co_await asio::this_coro::executor;

        log_thread("Connecting BiDi client");
        // io_context acessado via ponteiro (lifetime gerenciado por main)
        auto &ioc = *ioc_ptr;
        auto client = co_await bidi::Client::connect(ioc, websocket_url);

        if (!client) {
            bidi::logging::log_error("Failed to connect");
            co_return 1;
        }

        log_thread("Creating browsing context");
        auto ctx = co_await client->create_context();
        log_thread(std::format("Created context: {}", ctx));

        log_thread("Navigating to example.com");
        co_await client->navigate(ctx, "https://example.com");

        // Execute 10 concurrent commands
        // These will be processed across multiple threads
        // but strand ensures no race conditions
        constexpr int kConcurrentCommands = 10; // evitar magic number
        co_await execute_concurrent_commands(client, ctx, kConcurrentCommands);

        // Fechar contexto explicitamente para permitir que o servidor limpe
        // estado
        try {
            co_await client->close_context(ctx);
            log_thread("Closed browsing context");
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("close_context error: {}", e.what()));
        }

        // Desconectar sessão (gera cancelamentos e fecha websocket via strand)
        client->disconnect();

        // Permitir que o post da desconexão rode (um loop turn)
        co_await asio::post(asio::bind_executor(executor, asio::use_awaitable));

        co_return 0;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Exception: {}", e.what()));
        co_return 1;
    }
}

auto main() -> int {
    try {
        bidi::logging::log_info("=== Multi-threaded BiDi Example ===");

        // Get WebSocket URL from ChromeDriver
        WebDriver driver;
        driver.webDriverUrl = "http://localhost:9515";
        WebDriver::json args =
            WebDriver::json::array({"--headless", "--no-sandbox"});
        auto response = driver.connect(args, "chrome", true);

        if (!response.contains("capabilities") ||
            !response["capabilities"].contains("webSocketUrl")) {
            bidi::logging::log_error("No webSocketUrl in session response");
            return 1;
        }

        std::string ws_url =
            response["capabilities"]["webSocketUrl"].get<std::string>();

        // Create io_context
        asio::io_context ioc;

        // Spawn the demo coroutine
        // Spawn the demo coroutine
        auto future = asio::co_spawn(ioc, run_multithreaded_demo(&ioc, ws_url),
                                     asio::use_future);
        // KEY POINT: Run io_context on MULTIPLE threads
        // Each thread can process I/O events concurrently
        // Strand ensures WebSocket handlers run serially
        unsigned int detected = std::thread::hardware_concurrency();
        if (detected == 0) {
            detected = 1; // fallback to single thread if hardware_concurrency
                          // not available
        }
        const unsigned int thread_count = detected;
        bidi::logging::log_info(
            std::format("Starting {} I/O threads", thread_count));

        std::vector<std::thread> io_threads;
        io_threads.reserve(thread_count);

        for (unsigned int i = 0; i < thread_count; ++i) {
            io_threads.emplace_back([&ioc, i]() {
                log_thread(std::format("I/O thread {} started", i));
                ioc.run();
                log_thread(std::format("I/O thread {} finished", i));
            });
        }

        // Primeiro aguardamos o resultado da coroutine para saber quando parar
        // o loop.
        int rc = 1;
        try {
            rc = future.get();
        } catch (const std::exception &e) {
            bidi::logging::log_error(std::format("Exception: {}", e.what()));
        }

        // Solicita parada do io_context (caso não tenha sido naturalmente
        // esgotado)
        ioc.stop();

        // Agora aguardamos as threads terminarem o loop.
        for (auto &thread : io_threads) {
            thread.join();
        }

        bidi::logging::log_info("All threads completed");
        return rc;

    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Fatal error: {}", e.what()));
        return 1;
    }
}

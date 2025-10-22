// Example: Log monitoring with log.entryAdded event
// Demonstrates subscribing to console logs and JavaScript errors with typed
// handlers

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include "bidi/types/log.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <format>
#include <memory>
#include <string>

namespace asio = boost::asio;
using namespace bidi::types::log;

class LogMonitoringDemo {
  private:
    asio::io_context io_context_;
    std::string websocket_url_;
    std::atomic<bool> finished_{false};

    // Statistics
    std::atomic<int> console_logs_{0};
    std::atomic<int> js_errors_{0};
    std::atomic<int> warnings_{0};
    std::atomic<int> errors_{0};

    // Log entry handler with std::visit for type-safe dispatch
    void handle_log_entry(const LogEntry &entry) {
        std::visit(
            [this](const auto &log) {
                using T = std::decay_t<decltype(log)>;
                if constexpr (std::is_same_v<T, ConsoleLogEntry>) {
                    handle_console_log(log);
                } else if constexpr (std::is_same_v<T, JavaScriptLogEntry>) {
                    handle_javascript_error(log);
                }
            },
            entry.value);
    }

    void handle_console_log(const ConsoleLogEntry &log) {
        console_logs_.fetch_add(1);

        auto level_str = to_string(log.level);
        auto timestamp = std::chrono::milliseconds{log.timestamp_ms};

        bidi::logging::log_info(
            std::format("[CONSOLE] [{}] {}ms - {} (method: {})", level_str,
                        timestamp.count(), log.text, log.method));

        if (log.level == Level::Warn) {
            warnings_.fetch_add(1);
        }
        if (log.level == Level::Error) {
            errors_.fetch_add(1);
        }
    }

    void handle_javascript_error(const JavaScriptLogEntry &log) {
        js_errors_.fetch_add(1);

        auto level_str = to_string(log.level);
        auto timestamp = std::chrono::milliseconds{log.timestamp_ms};

        bidi::logging::log_error(std::format("[JAVASCRIPT] [{}] {}ms - {}",
                                             level_str, timestamp.count(),
                                             log.text));

        if (log.stack_trace) {
            bidi::logging::log_error(
                std::format("  Stack trace: {}", *log.stack_trace));
        }
    }

  public:
    LogMonitoringDemo() = default;
    ~LogMonitoringDemo() = default;

    LogMonitoringDemo(const LogMonitoringDemo &) = delete;
    auto operator=(const LogMonitoringDemo &) -> LogMonitoringDemo & = delete;
    LogMonitoringDemo(LogMonitoringDemo &&) = delete;
    auto operator=(LogMonitoringDemo &&) -> LogMonitoringDemo & = delete;

    auto initialize_session() -> bool {
        using namespace bidi::logging;
        try {
            bidi::SessionGuard guard("http://localhost:9515");
            WebDriver::json args =
                WebDriver::json::array({"--headless", "--no-sandbox"});
            auto ws_url = guard.connect(args, "chrome", true);
            if (!ws_url) {
                log_error(ws_url.error());
                return false;
            }
            websocket_url_ = *ws_url;
            log_info(std::format("WebSocket URL: {}", websocket_url_));
            return true;
        } catch (const std::exception &e) {
            log_error(std::format("Session init failed: {}", e.what()));
            return false;
        }
    }

    auto run_monitoring() -> asio::awaitable<int> {
        using namespace bidi::logging;

        log_info("Connecting BiDi Client");
        auto client =
            co_await bidi::Client::connect(io_context_, websocket_url_);
        if (!client) {
            log_error("BiDi connect failed");
            co_return 1;
        }
        bidi::ClientGuard client_guard(client);

        // Subscribe to log.entryAdded
        // Note: std::source_location is automatically captured from call site
        // via default argument (see core.hpp:308-310)
        log_info("Subscribing to log.entryAdded events");
        auto session = client->session();
        auto subscription = co_await session->subscribe_event_async(
            std::string(bidi::ids::events::log_entryAdded),
            [this](const bidi::core::ParsedEvent &event) {
                try {
                    // Parse JSON params into strongly-typed LogEntry variant
                    boost::json::value params_value = event.params;
                    auto entry = boost::json::value_to<LogEntry>(params_value);
                    handle_log_entry(entry);
                } catch (const std::exception &e) {
                    bidi::logging::log_error(
                        std::format("Failed to parse log entry: {}", e.what()));
                }
            }
            // std::source_location loc = std::source_location::current()
            // (default)
        );

        // Create context and trigger various log events
        auto ctx = co_await client->create_context();
        log_info(std::format("Created context: {}", ctx));

        co_await client->navigate(ctx, "https://example.com");
        log_info("Navigated to example.com");

        // Trigger console logs at different levels
        log_info("Triggering console.log messages");
        co_await client->evaluate("console.log('Hello from monitoring demo')",
                                  ctx);
        co_await client->evaluate("console.warn('This is a warning')", ctx);
        co_await client->evaluate("console.error('This is an error')", ctx);
        co_await client->evaluate("console.info('Information message')", ctx);
        co_await client->evaluate("console.debug('Debug message')", ctx);

        // Trigger JavaScript error
        log_info("Triggering JavaScript error");
        try {
            co_await client->evaluate(
                "throw new Error('Test error from log monitoring demo')", ctx);
        } catch (const std::exception &e) {
            log_info(std::format("Expected exception caught: {}", e.what()));
        }

        // Wait for events to be processed
        log_info("Waiting for log events to be processed");
        asio::steady_timer timer(io_context_.get_executor());
        timer.expires_after(std::chrono::seconds{2});
        co_await timer.async_wait(asio::use_awaitable);

        // Print statistics
        log_info("=== Log Monitoring Statistics ===");
        log_info(std::format("  Console logs: {}", console_logs_.load()));
        log_info(std::format("  JavaScript errors: {}", js_errors_.load()));
        log_info(std::format("  Warnings: {}", warnings_.load()));
        log_info(std::format("  Errors: {}", errors_.load()));
        log_info("=================================");

        // Signal completion
        asio::post(io_context_.get_executor(),
                   [this]() { finished_.store(true); });

        co_return 0;
    }

    auto run_bidi_flow() -> asio::awaitable<int> {
        try {
            co_return co_await run_monitoring();
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("Monitoring flow failed: {}", e.what()));
            co_return 1;
        }
    }

    auto run() -> int {
        if (!initialize_session()) {
            return 1;
        }

        constexpr std::chrono::seconds kWatchdogSeconds{30};
        finished_.store(false);

        auto fut =
            asio::co_spawn(io_context_, run_bidi_flow(), asio::use_future);

        // Watchdog timer to prevent hanging
        auto watchdog_timer =
            std::make_shared<asio::steady_timer>(io_context_.get_executor());
        watchdog_timer->expires_after(kWatchdogSeconds);
        watchdog_timer->async_wait(
            [this, watchdog_timer](const boost::system::error_code &ec) {
                if (ec == asio::error::operation_aborted) {
                    return;
                }
                if (!finished_.load()) {
                    bidi::logging::log_warning("Watchdog timeout - stopping");
                    asio::post(io_context_.get_executor(),
                               [this]() { io_context_.stop(); });
                }
            });

        io_context_.run();

        finished_.store(true);
        if (watchdog_timer) {
            boost::system::error_code ec;
            watchdog_timer->cancel(ec);
        }

        try {
            return fut.get();
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("Example failed: {}", e.what()));
            return 1;
        }
    }
};

auto main() -> int {
    bidi::logging::log_info("Log Monitoring Example (log.entryAdded event)");
    try {
        LogMonitoringDemo demo;
        return demo.run();
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Demo failed: {}", e.what()));
        return 1;
    }
}

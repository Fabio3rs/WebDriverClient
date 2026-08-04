#include "asyncx.hpp"
#include "bidi/automation_session.hpp"
#include "bidi/ids.hpp"
#include "bidi/logging.hpp"
#include "bidi/types/log.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/json/value_to.hpp>
#include <chrono>
#include <format>
#include <type_traits>
#include <utility>
#include <variant>

namespace asio = boost::asio;
using namespace std::chrono_literals;
using bidi::types::log::ConsoleLogEntry;
using bidi::types::log::JavaScriptLogEntry;
using bidi::types::log::LogEntry;

namespace {

struct LogCounters {
    std::atomic<int> console_entries{0};
    std::atomic<int> javascript_entries{0};
};

void handle_log_entry(const LogEntry &entry, LogCounters &counters) {
    std::visit(
        [&counters](const auto &log) {
            using Entry = std::decay_t<decltype(log)>;
            if constexpr (std::is_same_v<Entry, ConsoleLogEntry>) {
                ++counters.console_entries;
                bidi::logging::log_info(std::format("console: {}", log.text));
            } else if constexpr (std::is_same_v<Entry, JavaScriptLogEntry>) {
                ++counters.javascript_entries;
                bidi::logging::log_error(
                    std::format("javascript: {}", log.text));
            }
        },
        entry.value);
}

void handle_log_params(boost::json::object params, LogCounters &counters) {
    try {
        const auto entry = boost::json::value_to<LogEntry>(
            boost::json::value(std::move(params)));
        handle_log_entry(entry, counters);
    } catch (const std::exception &error) {
        bidi::logging::log_error(
            std::format("Invalid log event: {}", error.what()));
    }
}

auto monitor_logs(bidi::AutomationSession &session) -> asio::awaitable<int> {
    LogCounters counters;
    auto received_logs = asyncx::make_promise_with_timeout<bool>(
        5s, session.client()->get_executor());
    auto subscription =
        co_await session.client()->set_event_handler_subscription(
            bidi::ids::events::log_entryAdded,
            [&counters,
             signal = received_logs.promise](boost::json::object params) {
                handle_log_params(std::move(params), counters);
                const int total = counters.console_entries.load() +
                                  counters.javascript_entries.load();
                if (total >= 2) {
                    signal.fulfill(true);
                }
            });

    co_await session.navigate("https://example.com");
    (void)co_await session.evaluate("console.log('hello from WebDriver BiDi')");
    (void)co_await session.evaluate("console.warn('typed log subscription')");

    (void)co_await std::move(received_logs.future);

    bidi::logging::log_info(std::format(
        "Received {} console and {} JavaScript entries",
        counters.console_entries.load(), counters.javascript_entries.load()));
    (void)subscription;
    co_return 0;
}

auto run_example() -> int {
    auto session = bidi::AutomationSession::start();
    return session.run([&session] { return monitor_logs(session); });
}

} // namespace

auto main() -> int {
    try {
        return run_example();
    } catch (const std::exception &error) {
        bidi::logging::log_error(
            std::format("Log monitoring failed: {}", error.what()));
        return 1;
    }
}

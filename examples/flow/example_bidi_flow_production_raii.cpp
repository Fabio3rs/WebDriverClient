// Example: Production-grade BiDi flow with RAII wrappers
// Origin: migrated from demo_production.cpp (trimmed for naming unification)

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/commands.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <expected>
#include <string>

namespace asio = boost::asio;
using namespace std::chrono_literals;

struct ExampleProductionConfig {
    std::string webdriver_url{"http://localhost:9515"};
    std::chrono::seconds operation_timeout{30s};
};

class ProductionFlowExample {
  private:
    asio::io_context io_context_; // default construction implicit
    std::atomic<bool> finished_{false};
    ExampleProductionConfig config_{};

  public:
    explicit ProductionFlowExample(ExampleProductionConfig cfg = {})
        : config_(std::move(cfg)) {}
    auto run() -> std::expected<int, std::string> {
        try {
            bidi::SessionGuard session(config_.webdriver_url);
            WebDriver::json args =
                WebDriver::json::array({"--headless", "--no-sandbox"});
            auto ws_url_result = session.connect(args, "chrome", true);
            if (!ws_url_result) {
                return std::unexpected(ws_url_result.error());
            }
            const auto &websocket_url = ws_url_result.value();
            auto future_result = asio::co_spawn(
                io_context_, run_bidi_flow(websocket_url), asio::use_future);
            io_context_.run();
            int flow_result = future_result.get();
            if (flow_result != 0) {
                return std::unexpected("flow error");
            }
            return flow_result;
        } catch (const std::exception &e) {
            return std::unexpected(std::string("Fatal error: ") + e.what());
        } catch (...) {
            return std::unexpected("Unknown fatal error");
        }
    }

  private:
    auto run_bidi_flow(std::string websocket_url) -> asio::awaitable<int> {
        using namespace bidi::commands::browsing_context;
        try {
            auto client_ptr =
                co_await bidi::Client::connect(io_context_, websocket_url);
            if (!client_ptr) {
                co_return 1;
            }
            bidi::ClientGuard client_guard(client_ptr);
            auto context_id = co_await client_guard.client()->create_context(
                CreateType::window);
            auto nav_url = co_await client_guard.client()->navigate(
                context_id, "https://example.com");
            (void)nav_url;
            auto title_obj = co_await client_guard.client()->evaluate(
                "document.title", context_id);
            (void)title_obj;
            finished_.store(true, std::memory_order_release);
            co_return 0;
        } catch (...) {
            finished_.store(true, std::memory_order_release);
            co_return 1;
        }
    }
};

auto main() -> int {
    bidi::logging::log_info("Production RAII BiDi Flow Example");
    try {
        ProductionFlowExample example_flow;
        auto result = example_flow.run();
        if (!result) {
            bidi::logging::log_error(std::string("Example failed: ") +
                                     result.error());
            return 1;
        }
        return *result;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Fatal exception: ") + e.what());
        return 1;
    }
}

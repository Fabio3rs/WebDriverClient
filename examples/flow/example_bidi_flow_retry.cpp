// Example: Retry and timeout patterns for resilient automation
// Demonstrates: retry(), timeout_or(), exponential backoff, graceful
// degradation

#include "bidi/client.hpp"
#include "bidi/commands.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include "bidi/resilience.hpp"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <expected>
#include <string>

namespace asio = boost::asio;
using namespace std::chrono_literals;

struct RetryExampleConfig {
    std::string webdriver_url{"http://localhost:9515"};
    std::string test_url{"https://example.com"};
    int max_retry_attempts{3};
    std::chrono::milliseconds base_backoff{100ms};
    std::chrono::seconds operation_timeout{5s};
};

class RetryFlowExample {
  private:
    asio::io_context io_context_;
    RetryExampleConfig config_{};

  public:
    explicit RetryFlowExample(RetryExampleConfig cfg = {})
        : config_(std::move(cfg)) {}

    auto run() -> std::expected<int, std::string> {
        try {
            auto future_result =
                asio::co_spawn(io_context_, run_bidi_flow(), asio::use_future);
            io_context_.run();
            int flow_result = future_result.get();
            if (flow_result != 0) {
                return std::unexpected("Flow execution failed");
            }
            return flow_result;
        } catch (const std::exception &e) {
            return std::unexpected(std::string("Fatal error: ") + e.what());
        }
    }

  private:
    auto run_bidi_flow() -> asio::awaitable<int> {
        using namespace bidi::commands::browsing_context;
        try {
            auto client_ptr = co_await bidi::connect_to(config_.webdriver_url)
                                  .headless()
                                  .connect(io_context_);

            bidi::ClientGuard client_guard(client_ptr);
            auto &client = client_guard.client();

            // Pattern 1: Retry with exponential backoff
            // Use case: Flaky operations like network requests or element waits
            bidi::logging::log_info(
                "Pattern 1: Retry context creation with exponential backoff");

            bidi::helpers::RetryPolicy policy =
                bidi::helpers::RetryPolicy::exponential(
                    config_.max_retry_attempts);
            policy.base_delay = config_.base_backoff;

            auto context_id =
                co_await client->create_context(CreateType::window);

            auto elementTest = co_await client->evaluate(
                "document.documentElement.outerHTML", context_id);

            bidi::logging::log_info(std::string("  elementTest: ") +
                                    boost::json::serialize(elementTest));

            bidi::logging::log_info(std::string("  Created context: ") +
                                    context_id);

            // Pattern 2: Timeout with fallback value
            // Use case: Non-critical operations where default is acceptable
            bidi::logging::log_info(
                "\nPattern 2: Navigate with timeout and fallback");

            auto nav_result = co_await bidi::helpers::timeout_or(
                client->navigate(context_id, config_.test_url),
                config_.operation_timeout, std::string("navigation-timeout"));

            if (nav_result == "navigation-timeout") {
                bidi::logging::log_info(
                    "  Navigation timed out, using fallback");
            } else {
                bidi::logging::log_info(std::string("  Navigated to: ") +
                                        nav_result);
            }

            // Pattern 3: Timeout for critical operations (throw on timeout)
            // Use case: Operations that MUST complete within time limit
            bidi::logging::log_info(
                "\nPattern 3: Evaluate with timeout (throws on timeout)");

            try {
                auto eval_task = client->evaluate("document.title", context_id);
                auto title_obj = co_await asyncx::timeout(
                    eval_task, io_context_.get_executor(),
                    config_.operation_timeout);

                bidi::logging::log_info(std::string("  Page title: ") +
                                        boost::json::serialize(title_obj));
            } catch (const std::exception &e) {
                bidi::logging::log_error(std::string("  Timeout exception: ") +
                                         e.what());
            }

            // Pattern 4: Combined retry + timeout
            // Use case: Flaky operations with time constraints
            bidi::logging::log_info(
                "\nPattern 4: Retry evaluate with timeout per attempt");

            auto body_text = co_await bidi::helpers::retry<boost::json::object>(
                [client, context_id, &io_context = io_context_,
                 timeout_duration = config_.operation_timeout]() {
                    bidi::logging::log_info("  Attempting evaluate...");
                    auto eval_task = client->evaluate(
                        "document.body.textContent", context_id);
                    return asyncx::timeout(eval_task, io_context.get_executor(),
                                           timeout_duration);
                },
                policy);

            bidi::logging::log_info(std::string("  Body text: ") +
                                    boost::json::serialize(body_text));

            // Pattern 5: Timeout with complex fallback
            // Use case: Graceful degradation with alternative strategy
            bidi::logging::log_info(
                "\nPattern 5: Complex evaluation with fallback");

            boost::json::object fallback_value;
            fallback_value["type"] = "string";
            fallback_value["value"] = "fallback-content";

            auto content = co_await bidi::helpers::timeout_or(
                client->evaluate("document.documentElement.outerHTML",
                                 context_id),
                2s, // Aggressive timeout for demo
                fallback_value);

            auto result_type = content.contains("type")
                                   ? content.at("type").as_string().c_str()
                                   : "unknown";
            bidi::logging::log_info(std::string("  Content type: ") +
                                    result_type);

            bidi::logging::log_info(
                "\nAll retry/timeout patterns demonstrated successfully");
            co_return 0;

        } catch (const std::exception &e) {
            bidi::logging::log_error(std::string("Flow error: ") + e.what());
            co_return 1;
        }
    }
};

auto main() -> int {
    bidi::logging::log_info("Retry and Timeout Patterns Example\n");
    bidi::logging::log_info("Demonstrates:");
    bidi::logging::log_info("  1. Retry with exponential backoff");
    bidi::logging::log_info("  2. Timeout with fallback value");
    bidi::logging::log_info("  3. Timeout for critical operations");
    bidi::logging::log_info("  4. Combined retry + timeout");
    bidi::logging::log_info("  5. Complex fallback strategies\n");

    try {
        RetryFlowExample example_flow;
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
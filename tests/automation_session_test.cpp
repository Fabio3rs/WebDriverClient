#include "bidi/automation_session.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <stdexcept>

using namespace bidi;
using namespace std::chrono_literals;

TEST(AutomationSessionIntegration, WaitForElementUsesBrowserObservation) {
    AutomationSession session = AutomationSession::start();

    const auto result =
        session.run([&session]() -> boost::asio::awaitable<int> {
            co_await session.navigate("http://localhost:8080");

            EXPECT_TRUE(co_await session.wait_for_element("h1", 100ms));
            EXPECT_TRUE(co_await session.wait_for_element(
                "//*[@id='new-timed-element']", 2s,
                ElementSelectorType::xpath));
            EXPECT_FALSE(
                co_await session.wait_for_element("#does-not-exist", 10ms));
            co_return 0;
        });

    EXPECT_EQ(result, 0);
}

TEST(AutomationSessionIntegration, WorkflowExceptionDoesNotLeakPendings) {
    // Start a real session (blocking). Requires chromedriver / webdriver
    // available.
    AutomationSession session = AutomationSession::start();

    std::atomic<bool> taskReturned = false;

    // Run a workflow that spawns two async operations then throws.
    auto workflow = [&session, &taskReturned]() -> boost::asio::awaitable<int> {
        // Start a navigation that takes some time.
        // Should be ok.
        co_await session.navigate("https://example.com");

        session.evaluate("new Promise(r=>setTimeout(()=>r('ok'), 800))")
            .finally([&](const std::optional<boost::json::value> & /*val*/,
                         auto /*ec*/, std::exception_ptr /*ep*/) {
                // Should not be called if workflow throws
                logging::log_info("Evaluation completed in background");
                taskReturned = true;
                return true;
            });

        co_await asyncx::net::steady_timer(session.get_io_context(),
                                           std::chrono::milliseconds(100))
            .async_wait(asyncx::net::use_awaitable);

        // Immediately throw to simulate workflow failure
        throw std::runtime_error("simulated workflow failure");
        co_return 0;
    };

    // Expect run to propagate exception (header documents run may rethrow)
    EXPECT_THROW(session.run(workflow), std::exception);

    EXPECT_TRUE(taskReturned.load());
}

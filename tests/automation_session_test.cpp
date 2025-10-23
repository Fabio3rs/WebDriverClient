#include "bidi/automation_session.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <cstdlib>
#include <gtest/gtest.h>
#include <stdexcept>

using namespace bidi;

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

#include "bidi/automation_session.hpp"
#include "bidi/automation_session_builder.hpp"
#include "bidi/commands/browsing_context.hpp"
#include "bidi/logging.hpp"
#include "bidi/script_eval.hpp"

#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <format>
#include <iostream>
#include <string>

namespace asio = boost::asio;
using namespace std::chrono_literals;

namespace {

auto run_workflow(bidi::AutomationSession &session) -> asio::awaitable<int> {
    co_await session.navigate("https://example.com");

    const auto title = co_await session.get_title();
    const auto description = co_await session.evaluate_as_or(
        "document.querySelector('meta[name=description]')?.content",
        std::string{"No description"});

    std::cout << std::format("{}\n{}\n", title, description);
    co_return 0;
}

auto create_session() -> bidi::AutomationSession {
    return bidi::AutomationSessionBuilder::create()
        .webdriver_url("http://localhost:9515")
        .headless()
        .window_size(1366, 768)
        .with_default_navigation_wait(
            bidi::commands::browsing_context::ReadinessState::interactive)
        .with_default_script_policy(
            bidi::script::script_eval_policy::throw_on_script_exception)
        .with_timeout(10s)
        .start();
}

auto run_example() -> int {
    auto session = create_session();
    return session.run([&session] { return run_workflow(session); });
}

} // namespace

auto main() -> int {
    try {
        return run_example();
    } catch (const bidi::script::ScriptEvaluateException &error) {
        bidi::logging::log_error(
            std::format("JavaScript failed: {}", error.what()));
        return 1;
    } catch (const std::exception &error) {
        bidi::logging::log_error(
            std::format("Production flow failed: {}", error.what()));
        return 1;
    }
}

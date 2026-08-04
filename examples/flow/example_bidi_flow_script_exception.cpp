#include "bidi/automation_session.hpp"
#include "bidi/logging.hpp"
#include "bidi/script_eval.hpp"

#include <boost/asio/awaitable.hpp>
#include <format>

namespace asio = boost::asio;

namespace {

auto script_fails_as_expected(bidi::AutomationSession &session)
    -> asio::awaitable<bool> {
    try {
        (void)co_await session.evaluate_as_outcome<std::string>(
            "(() => { throw new Error('intentional failure'); })()",
            bidi::script::script_eval_policy::throw_on_script_exception);
        co_return false;
    } catch (const bidi::script::ScriptEvaluateException &error) {
        const auto &details = error.details();
        bidi::logging::log_info(
            std::format("Captured script error: {}", details.text));
        if (details.line_number) {
            bidi::logging::log_info(
                std::format("Line: {}", *details.line_number));
        }
        co_return true;
    }
}

auto run_workflow(bidi::AutomationSession &session) -> asio::awaitable<int> {
    co_await session.navigate("https://example.com");
    co_return co_await script_fails_as_expected(session) ? 0 : 1;
}

auto run_example() -> int {
    auto session = bidi::AutomationSession::start();
    return session.run([&session] { return run_workflow(session); });
}

} // namespace

auto main() -> int {
    try {
        return run_example();
    } catch (const std::exception &error) {
        bidi::logging::log_error(
            std::format("Script example failed: {}", error.what()));
        return 1;
    }
}

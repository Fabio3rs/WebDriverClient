#include "bidi/automation_session.hpp"
#include "bidi/logging.hpp"

#include <boost/asio/awaitable.hpp>
#include <format>
#include <iostream>
#include <string>

namespace asio = boost::asio;

namespace {

auto browse_example(bidi::AutomationSession &session) -> asio::awaitable<int> {
    co_await session.navigate("https://example.com");

    const auto title = co_await session.get_title();
    const auto url = co_await session.get_url();
    const int link_count =
        co_await session.evaluate_as<int>("document.links.length");

    std::cout << std::format("{}\n{}\n{} links\n", title, url, link_count);
    co_return 0;
}

auto run_example() -> int {
    auto session = bidi::AutomationSession::start();
    return session.run([&session] { return browse_example(session); });
}

} // namespace

auto main() -> int {
    try {
        return run_example();
    } catch (const std::exception &error) {
        bidi::logging::log_error(
            std::format("Automation failed: {}", error.what()));
        return 1;
    }
}

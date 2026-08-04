#include "bidi/automation_session.hpp"
#include "bidi/automation_session_builder.hpp"
#include "bidi/logging.hpp"

#include <boost/asio/awaitable.hpp>
#include <format>
#include <iostream>
#include <string>

namespace asio = boost::asio;

namespace {

auto wait_for_dom_to_settle(bidi::AutomationSession &session)
    -> bidi::Task<bool> {
    return session.evaluate_as<bool>(R"(
        new Promise((resolve) => {
            let quietTimer;
            const finish = () => {
                observer.disconnect();
                resolve(true);
            };
            const observer = new MutationObserver(() => {
                clearTimeout(quietTimer);
                quietTimer = setTimeout(finish, 300);
            });
            observer.observe(document.documentElement, {
                attributes: true,
                childList: true,
                subtree: true
            });
            quietTimer = setTimeout(finish, 300);
            setTimeout(finish, 10000);
        })
    )");
}

auto load_lazy_content(bidi::AutomationSession &session) -> bidi::Task<int> {
    return session.evaluate_as<int>(R"(
        new Promise(async (resolve) => {
            let previousHeight = -1;
            for (let attempt = 0; attempt < 20; ++attempt) {
                const currentHeight = document.documentElement.scrollHeight;
                if (currentHeight === previousHeight) {
                    break;
                }
                previousHeight = currentHeight;
                window.scrollTo(0, currentHeight);
                await new Promise(done => setTimeout(done, 250));
            }
            resolve(document.querySelectorAll('[data-item]').length);
        })
    )");
}

auto run_workflow(bidi::AutomationSession &session,
                  const std::string &url) -> asio::awaitable<int> {
    co_await session.navigate(url);
    (void)co_await wait_for_dom_to_settle(session);
    const int item_count = co_await load_lazy_content(session);
    const auto title = co_await session.get_title();

    std::cout << std::format("{}: {} lazy items\n", title, item_count);
    co_return 0;
}

auto run_example(const std::string &url) -> int {
    auto session = bidi::AutomationSessionBuilder::create()
                       .headless()
                       .with_viewport(1366, 768)
                       .start();
    return session.run([&session, url] { return run_workflow(session, url); });
}

} // namespace

auto main(int argc, char **argv) -> int {
    try {
        const std::string url = argc > 1 ? argv[1] : "https://example.com";
        return run_example(url);
    } catch (const std::exception &error) {
        bidi::logging::log_error(
            std::format("SPA scraping failed: {}", error.what()));
        return 1;
    }
}

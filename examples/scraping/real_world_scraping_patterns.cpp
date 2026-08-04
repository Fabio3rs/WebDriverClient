#include "asyncx.hpp"
#include "bidi/automation_session.hpp"
#include "bidi/automation_session_builder.hpp"
#include "bidi/logging.hpp"

#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace asio = boost::asio;
using namespace std::chrono_literals;

namespace {

struct ProductSummary {
    std::string title;
    std::string price;
    std::string availability;
};

auto wait_for_primary_content(bidi::AutomationSession &session)
    -> bidi::Task<bool> {
    return session.evaluate_as<bool>(R"(
        new Promise((resolve) => {
            const selector = 'h1';
            if (document.querySelector(selector)) {
                resolve(true);
                return;
            }

            const observer = new MutationObserver(() => {
                if (document.querySelector(selector)) {
                    observer.disconnect();
                    resolve(true);
                }
            });
            observer.observe(document.documentElement, {
                childList: true,
                subtree: true
            });
            setTimeout(() => {
                observer.disconnect();
                resolve(false);
            }, 5000);
        })
    )");
}

auto scrape_product(bidi::AutomationSession &session)
    -> asio::awaitable<ProductSummary> {
    const bool content_found = co_await wait_for_primary_content(session);
    if (!content_found) {
        throw std::runtime_error("primary content did not appear in time");
    }

    std::vector<bidi::Task<std::string>> fields;
    fields.emplace_back(session.evaluate_as_or(
        "document.querySelector('h1')?.textContent?.trim()",
        std::string{"Untitled"}));
    fields.emplace_back(session.evaluate_as_or(
        "document.querySelector('[data-price]')?.textContent?.trim()",
        std::string{"Price unavailable"}));
    fields.emplace_back(session.evaluate_as_or(
        "document.querySelector('[data-stock]')?.textContent?.trim()",
        std::string{"Availability unknown"}));

    auto values = co_await asyncx::all(session.client()->get_executor(),
                                       std::move(fields));
    co_return ProductSummary{.title = std::move(values[0]),
                             .price = std::move(values[1]),
                             .availability = std::move(values[2])};
}

auto run_workflow(bidi::AutomationSession &session,
                  const std::string &url) -> asio::awaitable<int> {
    co_await session.navigate(url);
    const auto product = co_await scrape_product(session);
    std::cout << std::format("{} | {} | {}\n", product.title, product.price,
                             product.availability);
    co_return 0;
}

auto run_example(const std::string &url) -> int {
    auto session = bidi::AutomationSessionBuilder::create()
                       .headless()
                       .with_timeout(10s)
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
            std::format("Scraping failed: {}", error.what()));
        return 1;
    }
}

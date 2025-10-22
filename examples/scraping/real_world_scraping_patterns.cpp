// Real-world web scraping patterns with asyncx
// Handles slow sites, intermittent failures, and dynamic content

#include "WebDriverClient.hpp"
#include "asyncx.hpp"
#include "bidi/client.hpp"
#include "bidi/commands.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

using namespace std::chrono_literals;
namespace asio = boost::asio;

// ============================================================================
// Pattern 1: Retry with exponential backoff for flaky elements
// ============================================================================

auto wait_for_element_with_retry(const std::shared_ptr<bidi::Client> &client,
                                 std::string_view context,
                                 std::string_view selector, int max_retries = 5)
    -> asyncx::Async<boost::json::object> {
    auto executor = client->get_executor();

    // Helper: single attempt to locate element
    auto attempt = [=]() -> asyncx::Async<boost::json::object> {
        return client->evaluate(
            std::format("document.querySelector('{}')", selector), context);
    };

    // Retry with exponential backoff
    auto result = attempt();
    for (int i = 0; i < max_retries; ++i) {
        auto retryElSearch = [=, retry_count = i]([[maybe_unused]] auto ec)
            -> asyncx::Async<boost::json::object> {
            bidi::logging::log_warning(
                std::format("Element '{}' not found, retry {}/{}", selector,
                            retry_count + 1, max_retries));

            // Exponential backoff: 100ms, 200ms, 400ms, 800ms, 1600ms
            auto delay = std::chrono::milliseconds(100 << retry_count);

            // Wait before retry
            auto timer_async = asyncx::Async<void>::make(executor);
            auto timer = std::make_shared<asio::steady_timer>(executor, delay);
            timer->async_wait([timer_async](auto ec) {
                if (!ec) {
                    timer_async.fulfill();
                } else {
                    timer_async.fail(ec);
                }
            });

            return timer_async.and_then([=]() { return attempt(); });
        };
        result = result.recover(retryElSearch);
    }

    return result;
}

// ============================================================================
// Pattern 2: Race between multiple possible selectors (variable layouts)
// ============================================================================

auto find_submit_button_any_layout(std::shared_ptr<bidi::Client> client,
                                   std::string_view context)
    -> asio::awaitable<std::string> {
    auto executor = client->get_executor();

    // E-commerce sites often have different submit button IDs
    std::vector<std::string> selectors = {
        "#checkout-btn", "#buy-now", "#add-to-cart",
        "button[data-action='purchase']", ".checkout-button"};

    std::vector<asyncx::Async<boost::json::object>> attempts;
    for (const auto &sel : selectors) {
        attempts.push_back(
            client
                ->evaluate(std::format("document.querySelector('{}')", sel),
                           context)
                .timeout(2s) // Short timeout per attempt
        );
    }

    try {
        auto winner = co_await asyncx::race(executor, std::move(attempts)
        bidi::logging::log_info("Found submit button");
        co_return "found";
    } catch (...) {
        bidi::logging::log_error("No submit button found in any layout");
        co_return "not-found";
    }
}

// ============================================================================
// Pattern 3: Parallel scraping with different timeouts per section
// ============================================================================

struct ProductData {
    std::optional<std::string> title;
    std::optional<std::string> price;
    std::optional<std::string> description;
    std::optional<std::string> availability;
    std::optional<std::string> reviews;
};

auto scrape_product_page(std::shared_ptr<bidi::Client> client,
                         std::string_view context)
    -> asio::awaitable<ProductData> {
    auto executor = client->get_executor();

    // Each section has different reliability/speed
    // Title: fast and reliable (1s timeout)
    auto get_title =
        client
            ->evaluate(
                "document.querySelector('h1.product-title')?.innerText || ''",
                context)
            .timeout(1s)
            .recover([](auto) { return boost::json::object{{"value", ""}}; });

    // Price: usually fast but might require JS execution (2s timeout)
    auto get_price =
        client
            ->evaluate("document.querySelector('.price')?.innerText || ''",
                       context)
            .timeout(2s)
            .recover([](auto) { return boost::json::object{{"value", ""}}; });

    // Description: might lazy-load (5s timeout)
    auto get_description =
        client
            ->evaluate(
                R"(
            const desc = document.querySelector('.description');
            if (desc && desc.offsetParent === null) {
                desc.scrollIntoView();
            }
            return desc?.innerText || '';
        )",
                context)
            .timeout(5s)
            .recover([](auto) { return boost::json::object{{"value", ""}}; });

    // Availability: often loaded via AJAX (3s timeout)
    auto get_availability =
        client
            ->evaluate(
                "document.querySelector('.stock-status')?.innerText || ''",
                context)
            .timeout(3s)
            .recover([](auto) { return boost::json::object{{"value", ""}}; });

    // Reviews: slowest, might be in iframe (10s timeout)
    auto get_reviews =
        client
            ->evaluate(
                "document.querySelector('.reviews-summary')?.innerText || ''",
                context)
            .timeout(10s)
            .recover([](auto) { return boost::json::object{{"value", ""}}; });

    // All operations sent in parallel, complete independently
    auto [title_obj, price_obj, desc_obj, avail_obj, reviews_obj] =
        co_await asyncx::zip(asyncx::zip(get_title, get_price),
                             asyncx::zip(get_description, get_availability),
                             get_reviews

    ProductData data;
    // Extract values (simplified)
    data.title = "extracted_title";
    data.price = "extracted_price";
    data.description = "extracted_description";
    data.availability = "extracted_availability";
    data.reviews = "extracted_reviews";

    co_return data;
}

// ============================================================================
// Pattern 4: Wait for loading spinner to disappear + content to appear
// ============================================================================

auto wait_page_ready(const std::shared_ptr<bidi::Client> &client,
                     std::string_view context) -> asyncx::Async<void> {
    auto executor = client->get_executor();

    // Poll until spinner is gone
    auto wait_spinner_gone = [=]() -> asyncx::Async<void> {
        auto check = asyncx::Async<void>::make(executor);

        client
            ->evaluate("document.querySelector('.loading-spinner') === null",
                       context)
            .finally([check](auto value_opt, auto ec_opt, const auto &ep) {
                if (value_opt && value_opt->at("value").as_bool()) {
                    check.fulfill();
                } else {
                    check.fail(boost::asio::error::try_again);
                }
            });

        return check;
    };

    // Poll until main content exists
    auto wait_content_exists = [=]() -> asyncx::Async<void> {
        auto check = asyncx::Async<void>::make(executor);

        client
            ->evaluate("document.querySelector('#main-content') !== null",
                       context)
            .finally([check](auto value_opt, auto ec_opt, const auto &ep) {
                if (value_opt && value_opt->at("value").as_bool()) {
                    check.fulfill();
                } else {
                    check.fail(boost::asio::error::try_again);
                }
            });

        return check;
    };

    // Wait for both conditions with timeout
    return asyncx::zip(wait_spinner_gone().timeout(30s),
                       wait_content_exists().timeout(30s))
        .map([](auto) { return; });
}

// ============================================================================
// Pattern 5: Scrape multiple pages in parallel with rate limiting
// ============================================================================

auto scrape_multiple_products(std::shared_ptr<bidi::Client> client,
                              const std::vector<std::string> &urls)
    -> asio::awaitable<std::vector<ProductData>> {
    std::vector<ProductData> results;

    // Batch scraping: 5 concurrent pages max to avoid overwhelming the site
    constexpr size_t BATCH_SIZE = 5;

    for (size_t i = 0; i < urls.size(); i += BATCH_SIZE) {
        std::vector<asyncx::Async<ProductData>> batch;

        for (size_t j = i; j < std::min(i + BATCH_SIZE, urls.size()); ++j) {
            // Create new context (tab) for each URL
            auto scrape_task =
                [](const std::shared_ptr<bidi::Client> &c,
                   const std::string &url) -> asyncx::Async<ProductData> {
                auto ctx = co_await c->create_context(

                co_await c->navigate(ctx, url
                co_await wait_page_ready(c, ctx

                auto data = co_await scrape_product_page(c, ctx);

                co_await c->close_context(ctx
                co_return data;
            };

            batch.push_back(scrape_task(client, urls[j]));
        }

        // Wait for batch to complete
        auto batch_results =
            co_await asyncx::all(client->get_executor(), std::move(batch)

        results.insert(results.end(), batch_results.begin(),
                       batch_results.end());

        // Rate limiting: 500ms delay between batches
        if (i + BATCH_SIZE < urls.size()) {
            co_await asio::steady_timer(client->get_executor(), 500ms)
                .async_wait(asio::use_awaitable);
        }
    }

    co_return results;
}

// ============================================================================
// Pattern 6: Graceful degradation - try premium data, fallback to basic
// ============================================================================

auto get_product_price_with_fallback(
    const std::shared_ptr<bidi::Client> &client, std::string_view context)
    -> asyncx::Async<std::string> {
    auto executor = client->get_executor();

    // Try premium selector (detailed price breakdown)
    auto try_premium =
        client
            ->evaluate("document.querySelector('.price-detail')?.dataset.total",
                       context)
            .timeout(2s);

    // Fallback to basic price if premium fails
    return try_premium
        .recover([=](auto ec) {
            bidi::logging::log_info(
                "Premium price selector failed, using fallback");

            return client
                ->evaluate("document.querySelector('.price')?.innerText",
                           context)
                .timeout(1s);
        })
        .map([](auto obj) { return obj.at("value").as_string().c_str(); });
}

// ============================================================================
// Main: Demonstration
// ============================================================================

auto main() -> int {
    try {
        bidi::SessionGuard session("http://localhost:9515");
        auto ws_url_result = session.connect(
            WebDriver::json::array({"--headless", "--no-sandbox"}), "chrome",
            true);

        if (!ws_url_result) {
            bidi::logging::log_error(ws_url_result.error());
            return 1;
        }

        asio::io_context ioc;
        auto client_ptr = asio::co_spawn(
            ioc, bidi::Client::connect(ioc, ws_url_result.value())(),
            asio::use_future);
        ioc.run();

        auto client = client_ptr.get();

        bidi::logging::log_info("Real-world scraping patterns demonstrated");

        return 0;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Error: {}", e.what()));
        return 1;
    }
}

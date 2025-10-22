// Scraping patterns for SPAs and lazy-loaded content
// Handles React/Vue/Angular apps with dynamic rendering

#include "WebDriverClient.hpp"
#include "asyncx.hpp"
#include "bidi/client.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <format>

using namespace std::chrono_literals;

// ============================================================================
// SPA Pattern: Wait for framework hydration
// ============================================================================

auto wait_for_react_hydration(
    const std::shared_ptr<bidi::Client> &client, std::string_view context,
    std::chrono::milliseconds timeout = 10s) -> asyncx::Async<void> {
    auto executor = client->get_executor();

    // Check multiple hydration signals
    auto check_react = client->evaluate(R"(
        window.__REACT_DEVTOOLS_GLOBAL_HOOK__ !== undefined &&
        document.readyState === 'complete' &&
        document.querySelector('[data-reactroot]') !== null
    )",
                                        context);

    auto check_vue = client->evaluate(R"(
        window.__VUE__ !== undefined &&
        document.readyState === 'complete'
    )",
                                      context);

    auto check_angular = client->evaluate(R"(
        window.getAllAngularRootElements &&
        window.getAllAngularRootElements().length > 0
    )",
                                          context);

    // Race: whichever framework loads first
    return asyncx::race(executor, {check_react.map([](const auto &) {}),
                                   check_vue.map([](const auto &) {}),
                                   check_angular.map([](const auto &) {})})
        .timeout(timeout);
}

// ============================================================================
// Infinite scroll: Scrape all items while scrolling
// ============================================================================

auto scrape_infinite_scroll(
    std::shared_ptr<bidi::Client> client, std::string_view context,
    std::string_view item_selector,
    int max_scrolls = 50) -> boost::asio::awaitable<std::vector<std::string>> {
    std::vector<std::string> all_items;
    int consecutive_no_new_items = 0;

    for (int scroll = 0; scroll < max_scrolls; ++scroll) {
        // Get current items count
        auto count_script = std::format(
            "document.querySelectorAll('{}').length", item_selector);
        auto count_before = co_await client->evaluate(count_script, context
        int items_before = count_before.at("value").as_int64();

        // Scroll to bottom
        co_await client->evaluate(
            "window.scrollTo(0, document.body.scrollHeight)", context

        // Wait for potential new items (with timeout)
        auto delay = asyncx::Async<void>::make(client->get_executor());
        auto timer = std::make_shared<boost::asio::steady_timer>(
            client->get_executor(), 1s);
        timer->async_wait([delay](auto ec) {
            if (!ec) {
                delay.fulfill();
            } else {
                delay.fail(ec);
            }
        });
        co_await delay();

        // Check if new items loaded
        auto count_after = co_await client->evaluate(count_script, context
        int items_after = count_after.at("value").as_int64();

        if (items_after == items_before) {
            consecutive_no_new_items++;
            if (consecutive_no_new_items >= 3) {
                bidi::logging::log_info(
                    "No more items loading, stopping scroll");
                break;
            }
        } else {
            consecutive_no_new_items = 0;
            bidi::logging::log_info(std::format("Scroll {}: {} items loaded",
                                                scroll + 1, items_after));
        }
    }

    // Extract all items at once
    auto extract_script = std::format(R"(
        Array.from(document.querySelectorAll('{}')).map(el => el.innerText)
    )",
                                      item_selector);

    auto items_result = co_await client->evaluate(extract_script, context

    // Parse array (simplified - would need proper JSON array parsing)
    co_return all_items;
}

// ============================================================================
// Lazy-loaded images: Wait until visible in viewport
// ============================================================================

auto wait_for_lazy_images_loaded(const std::shared_ptr<bidi::Client> &client,
                                 std::string_view context)
    -> asyncx::Async<void> {
    auto executor = client->get_executor();

    // Scroll through page to trigger lazy loading
    auto scroll_and_wait =
        client->evaluate(R"(
        new Promise((resolve) => {
            const images = document.querySelectorAll('img[loading="lazy"]');
            const totalImages = images.length;
            let loadedCount = 0;

            if (totalImages === 0) {
                resolve(true);
                return;
            }

            images.forEach(img => {
                // Scroll into view to trigger loading
                img.scrollIntoView({ behavior: 'smooth', block: 'center' });

                img.addEventListener('load', () => {
                    loadedCount++;
                    if (loadedCount === totalImages) {
                        resolve(true);
                    }
                });

                // Fallback: if already loaded
                if (img.complete) {
                    loadedCount++;
                    if (loadedCount === totalImages) {
                        resolve(true);
                    }
                }
            });

            // Timeout after 30s
            setTimeout(() => resolve(true), 30000);
        })
    )",
                         context, true); // await_promise = true

    return scroll_and_wait.map([](const auto &) {}).timeout(35s);
}

// ============================================================================
// Dynamic content: Wait for API response (XHR/fetch completed)
// ============================================================================

auto wait_for_api_requests_complete(const std::shared_ptr<bidi::Client> &client,
                                    std::string_view context)
    -> asyncx::Async<void> {
    auto executor = client->get_executor();

    // Inject monitoring script
    auto inject = client->evaluate(R"(
        if (!window.__pendingRequests) {
            window.__pendingRequests = 0;

            // Intercept fetch
            const originalFetch = window.fetch;
            window.fetch = function(...args) {
                window.__pendingRequests++;
                return originalFetch.apply(this, args).finally(() => {
                    window.__pendingRequests--;
                });
            };

            // Intercept XMLHttpRequest
            const originalXHROpen = XMLHttpRequest.prototype.open;
            const originalXHRSend = XMLHttpRequest.prototype.send;

            XMLHttpRequest.prototype.open = function(...args) {
                this.__tracked = true;
                return originalXHROpen.apply(this, args);
            };

            XMLHttpRequest.prototype.send = function(...args) {
                if (this.__tracked) {
                    window.__pendingRequests++;
                    this.addEventListener('loadend', () => {
                        window.__pendingRequests--;
                    });
                }
                return originalXHRSend.apply(this, args);
            };
        }
    )",
                                   context);

    // Poll until no pending requests
    return inject.and_then([=]() -> asyncx::Async<void> {
        auto poll = [=](const auto &self) -> asyncx::Async<void> {
            return client->evaluate("window.__pendingRequests === 0", context)
                .and_then([=](auto result) -> asyncx::Async<void> {
                    bool no_pending = result.at("value").as_bool();

                    if (no_pending) {
                        auto done = asyncx::Async<void>::make(executor);
                        done.fulfill();
                        return done;
                    }

                    // Wait 100ms and retry
                    auto delay = asyncx::Async<void>::make(executor);
                    auto timer = std::make_shared<boost::asio::steady_timer>(
                        executor, 100ms);
                    timer->async_wait([delay](auto ec) {
                        if (!ec) {
                            delay.fulfill();
                        } else {
                            delay.fail(ec);
                        }
                    });

                    return delay.and_then([=]() { return self(self); });
                });
        };

        return poll(poll).timeout(15s);
    });
}

// ============================================================================
// Modal/Popup handling: Try to close, fallback if doesn't exist
// ============================================================================

auto dismiss_popup_if_exists(
    const std::shared_ptr<bidi::Client> &client, std::string_view context,
    const std::vector<std::string> &close_selectors) -> asyncx::Async<void> {
    auto executor = client->get_executor();

    std::vector<asyncx::Async<void>> attempts;

    for (const auto &selector : close_selectors) {
        auto try_click = client
                             ->evaluate(std::format(R"(
                const btn = document.querySelector('{}');
                if (btn) {{
                    btn.click();
                    return true;
                }}
                return false;
            )",
                                                    selector),
                                        context)
                             .map([](const auto &) {})
                             .timeout(1s)
                             .recover([](auto) {}); // Ignore failures

        attempts.push_back(try_click);
    }

    // Try all selectors in parallel, succeed if any works
    return asyncx::race(executor, std::move(attempts));
}

// ============================================================================
// Complete example: Scraping SPA e-commerce product listing
// ============================================================================

struct Product {
    std::string name;
    std::string price;
    std::string image_url;
};

auto scrape_spa_product_listing(std::shared_ptr<bidi::Client> client,
                                std::string_view url)
    -> boost::asio::awaitable<std::vector<Product>> {
    // Create new context
    auto ctx = co_await client->create_context(

    // Navigate
    co_await client->navigate(ctx, url

    // Wait for SPA framework
    co_await wait_for_react_hydration(client, ctx

    // Dismiss cookie/newsletter popups (parallel attempts)
    co_await dismiss_popup_if_exists(client, ctx,
                                     {
        ".cookie-banner .close", "#newsletter-popup .close",
            "[data-dismiss='modal']"}

    // Wait for API data to load
    co_await wait_for_api_requests_complete(client, ctx

    // Scrape with infinite scroll
    auto items =
        co_await scrape_infinite_scroll(client, ctx, ".product-card", 20);

    // Wait for lazy images in viewport
    co_await wait_for_lazy_images_loaded(client, ctx

    // Extract product data (parallel extraction from all visible items)
    std::vector<asyncx::Async<Product>> extract_ops;

    for (int i = 0; i < 10; ++i) { // First 10 items
        auto extract =
            client
                ->evaluate(std::format(R"(
                const card = document.querySelectorAll('.product-card')[{}];
                if (!card) throw new Error('Not found');
                {{
                    name: card.querySelector('.title')?.innerText || '',
                    price: card.querySelector('.price')?.innerText || '',
                    image: card.querySelector('img')?.src || ''
                }}
            )",
                                       i),
                           ctx)
                .map([](auto obj) -> Product {
                    // Simplified extraction
                    return {
                        .name = "name", .price = "price", .image_url = "image"};
                })
                .timeout(2s)
                .recover([](auto) -> Product {
                    return {.name = "", .price = "", .image_url = ""};
                });

        extract_ops.push_back(extract);
    }

    auto products =
        co_await asyncx::all(client->get_executor(), std::move(extract_ops)

    // Cleanup
    co_await client->close_context(ctx

    co_return products;
}

auto main() -> int {
    bidi::logging::log_info("SPA and lazy-loading scraping patterns");
    return 0;
}

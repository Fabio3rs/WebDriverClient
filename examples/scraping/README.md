# Real-World Web Scraping Patterns with asyncx

This directory; demonstrates practical scraping patterns for slow, unreliable, and dynamic websites using the BiDi client's functional composition API.

## Why asyncx is Perfect for Web Scraping

Modern websites are:
- **Slow**: API calls, lazy loading, dynamic content
- **Unreliable**: Elements appear/disappear, network flakiness
- **Variable**: Different layouts, A/B tests, conditional rendering
- **Async by nature**: SPAs, infinite scroll, websocket updates

Traditional sequential scraping with retries becomes callback hell. **asyncx solves this elegantly.**

## Patterns Demonstrated

### 1. `real_world_scraping_patterns.cpp`

Real-world scenarios for production scraping:

```cpp
// Retry with exponential backoff (100ms → 1600ms)
wait_for_element_with_retry(client, ctx, "#submit-btn", 5);

// Race: find any submit button from 5 possible selectors
find_submit_button_any_layout(client, ctx);

// Parallel scraping with different timeouts
// Title: 1s, Price: 2s, Description: 5s, Reviews: 10s
scrape_product_page(client, ctx);

// Wait for spinner gone AND content loaded
wait_page_ready(client, ctx);

// Batch scraping: 5 pages at a time with rate limiting
scrape_multiple_products(client, urls);

// Graceful degradation: try premium data → fallback to basic
get_product_price_with_fallback(client, ctx);
```

**Key benefits:**
- Each section has independent timeout (fast fails don't block slow ones)
- Race conditions for variable layouts (A/B tests)
- Automatic retry with backoff
- Parallel extraction with `asyncx::all()`

### 2. `spa_and_lazy_loading.cpp`

Patterns for modern SPAs (React/Vue/Angular):

```cpp
// Wait for framework hydration (React/Vue/Angular detection)
wait_for_react_hydration(client, ctx, 10s);

// Infinite scroll: scrape while loading more
scrape_infinite_scroll(client, ctx, ".product-card", max_scrolls=50);

// Wait for lazy-loaded images in viewport
wait_for_lazy_images_loaded(client, ctx);

// Wait for all XHR/fetch requests to complete
wait_for_api_requests_complete(client, ctx);

// Dismiss popups/modals (try multiple selectors in parallel)
dismiss_popup_if_exists(client, ctx, {
    ".cookie-banner .close",
    "#newsletter-popup .close"
});
```

**Key benefits:**
- Framework-agnostic hydration detection
- Automatic scroll pagination
- Network idle detection for SPAs
- Non-blocking popup dismissal

## How This Works Under the Hood

### Traditional Sequential Approach (Callback Hell)
```cpp
// ❌ Hard to maintain, no parallelism, fixed timeouts
find_element("#btn", [](auto elem) {
    if (elem) {
        click(elem, [](auto success) {
            if (success) {
                type("text", [](auto) {
                    submit([](auto) { /* done */ });
                });
            } else {
                retry_click(...); // manual retry logic
            }
        });
    } else {
        fallback_find("#alt-btn", ...); // nested fallback
    }
});
```

### asyncx Functional Approach
```cpp
// ✅ Composable, parallel, declarative timeouts
client->locateNodes(ctx, "#btn")
    .and_then([](auto elem) { return click(elem); })
    .and_then([](auto) { return type("text"); })
    .and_then([](auto) { return submit(); })
    .timeout(10s)
    .recover([](auto ec) { return fallback_action(); })
    | co_await;
```

### WebSocket Multiplexing in Action

```
Client sends (all at once):
├─ ID=1: locateNodes "#title"       │ Browser processes
├─ ID=2: locateNodes "#price"       │ in parallel
├─ ID=3: locateNodes "#description" │
└─ ID=4: evaluate "document.title"  │

Browser responds (out of order):
├─ ID=3 ← (description found first)
├─ ID=1 ← (title found)
├─ ID=4 ← (evaluate complete)
└─ ID=2 ← (price found last)

asyncx automatically:
✓ Correlates responses by ID
✓ Fulfills promises in completion order
✓ Cancels slow operations on race() win
✓ Applies timeouts independently per operation
```

## Real-World Use Cases

### E-commerce Price Monitoring
```cpp
// Monitor 100 products across 5 sites simultaneously
std::vector<asyncx::Async<ProductData>> monitors;
for (auto& product : products) {
    for (auto& site : competitor_sites) {
        monitors.push_back(
            scrape_product_price(client, site, product.id)
                .timeout(5s)
                .recover([](auto) { return ProductData{.price = "N/A"}; })
        );
    }
}

// All 500 operations run in parallel (5 sites × 100 products)
auto results = co_await asyncx::all(executor, std::move(monitors));
```

### Job Board Scraper with Pagination
```cpp
// Scrape 20 pages in parallel (respecting rate limits)
for (int batch = 0; batch < 20; batch += 5) {
    std::vector<asyncx::Async<JobListing>> batch_ops;

    for (int page = batch; page < batch + 5; ++page) {
        batch_ops.push_back(scrape_job_page(client, page));
    }

    auto jobs = co_await asyncx::all(executor, std::move(batch_ops));
    save_to_database(jobs);

    // Rate limiting: 1 second between batches
    co_await delay(1s);
}
```

### Social Media Scraper (Handle Rate Limits)
```cpp
auto scrape_with_backoff = [](auto attempt) -> asyncx::Async<Data> {
    return attempt()
        .recover([=](auto ec) -> asyncx::Async<Data> {
            if (ec == rate_limited) {
                // Exponential backoff: 1s, 2s, 4s, 8s
                co_await delay(std::chrono::seconds(1 << retry_count));
                return scrape_with_backoff(attempt);
            }
            throw std::runtime_error("Permanent failure");
        });
};
```

## Compilation

```bash
cd build
cmake --build . --target real_world_scraping_patterns
cmake --build . --target spa_and_lazy_loading
```

## Running Examples

```bash
# Start ChromeDriver and test server
chromedriver --port=9515 &

# Run examples
./real_world_scraping_patterns
./spa_and_lazy_loading
```

## Performance Characteristics

| Pattern | Sequential | With asyncx | Speedup |
|---------|-----------|-------------|---------|
| 10 products, 5 fields each | 50s (serial) | 10s (parallel) | **5x** |
| 100 pages pagination | 300s | 60s (5 concurrent) | **5x** |
| Flaky element (3 retries avg) | 15s | 5s (parallel attempts) | **3x** |
| Multi-site monitoring | N×T | T (parallel) | **Nx** |

## Best Practices

1. **Independent timeouts**: Fast operations shouldn't wait for slow ones
2. **Parallel extraction**: Use `asyncx::all()` for independent data
3. **Race conditions**: Use `asyncx::race()` for variable layouts
4. **Graceful degradation**: `.recover()` for optional fields
5. **Rate limiting**: Batch operations with delays between batches
6. **Retry with backoff**: Exponential backoff for flaky elements

## See Also

- BiDi WebDriver Specification: https://w3c.github.io/webdriver-bidi/
- Boost.Asio Async Model: https://www.boost.org/doc/libs/release/doc/html/boost_asio.html
- asyncx library documentation: `/include/asyncx.hpp`

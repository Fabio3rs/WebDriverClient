# Scraping examples

These examples use `AutomationSession` for browser lifecycle and typed script
results. Raw `bidi::Client` access appears only where composition needs the
client executor.

Both executables accept an optional URL and default to `https://example.com`:

```bash
chromedriver --port=9515
./build/example_scraping_patterns https://shop.example/product/1
./build/example_spa_and_lazy_loading https://app.example/catalog
```

## Product fields

`real_world_scraping_patterns.cpp` demonstrates:

- waiting with a DOM `MutationObserver`, without C++ polling;
- extracting independent fields concurrently with `asyncx::all`;
- typed values and explicit fallbacks through `evaluate_as_or`.
- event-driven element discovery through `wait_for_element`.

The example waits for `h1` so its default URL has a successful path, then uses
`data-price` and `data-stock` for optional commerce fields. Adapt those
selectors to the target page.

## SPA and lazy content

`spa_and_lazy_loading.cpp` demonstrates:

- waiting until DOM mutations become quiet;
- scrolling incrementally inside an awaited JavaScript promise;
- returning only the typed item count to C++.

The item selector is `[data-item]`. Keeping DOM-specific waiting in the page
avoids blocking an Asio thread and reduces protocol round trips.

## Scope and lifetime

Each `run_example` owns its `AutomationSession`. The named coroutine functions
receive it by reference and finish before `run_example` returns, so the session,
client, context, and pending operations remain valid for the whole workflow.

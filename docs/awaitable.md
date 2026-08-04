# Lazy async operations and `co_await`

BiDi commands return `asyncx::Async<T>`. An operation is lazy: building or
composing it does not start network work. A terminal operation such as direct
`co_await` or `.finally(...)` materializes the chain.

## Direct coroutine use

For a temporary operation, await it directly:

```cpp
boost::asio::awaitable<void> inspect(bidi::Client &client) {
    auto tree = co_await client.browsing_context_get_tree();
    // Use tree here.
}
```

If an operation is stored first, move it into the await because an async chain
represents single-consumer work:

```cpp
auto operation = client.browsing_context_get_tree();
auto tree = co_await std::move(operation);
```

Do not keep a reference or `string_view` into frame-local JSON or an arena after
the handler returns. Copy or convert values that must outlive parsing.

## Composition

Use `map` for synchronous value transformation, `and_then` when the next step
returns another async operation, and `on_error` for deliberate recovery.
Attach a timeout at the operation boundary that owns the latency requirement.

Prefer a named coroutine or extracted function when a long callback chain adds
nested scopes. This keeps ownership and error propagation visible while still
using the library's lazy model.

## Errors and cancellation

Transport, protocol, timeout, and cancellation failures propagate through the
chain. Recovery handlers should handle only errors they can resolve; otherwise
preserve the original error. Cancellation must release pending registrations
and timers.

Avoid these patterns:

- calling `future::get()` from an Asio handler;
- detaching a coroutine whose lifetime is not owned elsewhere;
- starting an operation without retaining its terminal result or callback;
- capturing short-lived references in delayed callbacks.

## Current limitation

The generic `make_promise_with_timeout<void>` instantiation is not currently a
supported public pattern. Use a meaningful completion value or an existing
`Async<void>` operation with the regular timeout composition until the helper
has explicit `void` support and coverage.

The exact operator and callback contracts live in `include/asyncx.hpp`.

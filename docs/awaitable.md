# Awaitable Support and Direct `co_await`

This page documents how `Async<T>` (also known as `Task<T>`) can be used with
`co_await` in coroutines. As of the current implementation, `Async<T>`
implements the awaiter interface directly, allowing direct `co_await` without
any conversion operator in most cases.

Summary
-------
- **Direct co_await (Preferred)**: `Task<T>` objects can be awaited directly
  when they are temporaries (rvalues): `co_await client->operation()`
- **Operator() for lvalues**: When awaiting stored `Task<T>` variables (lvalues),
  use `operator()` or `std::move()` to convert to awaitable:
  `co_await task()` or `co_await std::move(task)`
- On success, the awaitable returns the value (or returns normally for `void`).
- On failure, if the shared state stored a `std::exception_ptr` (for example
  `ScriptEvaluateException`), the awaitable rethrows it — this ensures the
  awaiting caller receives the original typed exception.
- If no `std::exception_ptr` is present and Boost.Asio raised a
  `boost::system::system_error` (e.g. from cancellation or transport), that
  `system_error` is propagated.

Why Two Patterns?
-----------------
Boost.Asio's `await_transform` requires rvalues for awaitable conversion:
- **Temporaries (rvalues)**: Direct function returns can be awaited directly
- **Lvalues (stored variables)**: Must be converted to rvalues via `operator()`
  or `std::move()`

This is a limitation of Boost.Asio's awaitable machinery, not of `Task<T>` itself.

Motivation
----------
Many asynchronous operations store a `std::exception_ptr` when failures are
domain-level (for example, script evaluation errors). Without the proper
exception translation, a `co_await` might only observe a generic system error
(e.g. `operation_aborted`) and lose the higher-level exception information. The
awaiter implementation recovers that `exception_ptr` from the shared state
(under mutex) and rethrows it when appropriate.

Recommended usage
-----------------

**Pattern 1: Direct co_await on temporaries (Preferred)**

Most common case - awaiting function returns directly:

```cpp
try {
    // Direct co_await on temporary (rvalue)
    auto result = co_await client->evaluate("document.title", ctx);
    // use result
} catch (const ScriptEvaluateException &e) {
    // handle script-specific error
} catch (const boost::system::system_error &se) {
    // handle transport / cancellation errors
} catch (const std::exception &ex) {
    // generic fallback
}
```

**Pattern 2: Parallel composition with lvalues**

When launching multiple operations and awaiting them later:

```cpp
// Launch operations (store as lvalues)
auto task1 = client->operation1();
auto task2 = client->operation2();

// ... do other work ...

// Await stored tasks (requires () or std::move for lvalues)
auto result1 = co_await task1();  // or co_await std::move(task1)
auto result2 = co_await task2();  // or co_await std::move(task2)
```

Implementation notes
--------------------
- The implementation in `include/asyncx.hpp` provides two implicit conversion
  operators: `operator boost::asio::awaitable<T>()` for non-void types and
  `operator boost::asio::awaitable<void>()` for void types.
- These operators internally use the CompletionToken path (`use_awaitable`) and
  inspect the `State<T>::result` under `std::scoped_lock` to decide whether to
  rethrow a stored `std::exception_ptr`.
- For `void` operations the behavior is analogous: a stored `std::exception_ptr`
  is rethrown for awaiters, otherwise an `EC` leads to a thrown `system_error`.
- The explicit `operator()()` method is still provided for compatibility and
  for cases where explicit conversion is needed (lvalue to rvalue conversion).

Recommendations for examples
---------------------------
- Prefer direct `co_await` on function returns (temporaries) whenever possible.
- Use `operator()` or `std::move()` only when awaiting stored Task variables
  (parallel composition pattern).
- Do not check `if (!ptr)` after `co_await`; a failed Task throws when
  awaited — use `try/catch` instead.
- Document which exceptions each high-level API can throw (e.g.:
  `ScriptEvaluateException` for `Client::evaluate`).

FAQ
---
- Q: When do I need to use `operator()` or `std::move()`?
  A: Only when awaiting a stored Task variable (lvalue). Direct function returns
  (temporaries/rvalues) can be awaited directly: `co_await client->operation()`.

- Q: Why not only propagate `system_error`?
  A: `system_error` represents transport/cancellation failures; domain
  exceptions (e.g. script evaluation errors) contain additional information
  and should be preserved for diagnostics and local handling.

- Q: Do I need to change my existing handlers?
  A: Existing code using `co_await operation()()` still works, but can be
  simplified to `co_await operation()` for direct function returns. Replace
  pointer-return checks with `try/catch` when using `co_await` on operations
  that may fail with domain exceptions.

- Q: Why does Boost.Asio require rvalues for await_transform?
  A: This is a design decision in Boost.Asio's awaitable machinery to prevent
  accidentally awaiting the same Task object multiple times. The conversion
  operators consume the Task by moving it.

Reference
---------
- Header: `include/asyncx.hpp` (search for `operator boost::asio::awaitable<T>()`
  and `auto operator()() -> boost::asio::awaitable<T>`)

# Project map

## Purpose and status

WebDriverClient, internally called `bidi-x`, is a pre-release C++20/23 client
for Chrome WebDriver BiDi. Its focus is a small asynchronous core, predictable
resource ownership, low allocation overhead, and typed access to the parts of
the W3C BiDi protocol implemented by the project.

The API is not stable. Prefer clear architecture and direct migrations over
compatibility layers.

Non-goal: providing a complete high-level UI automation framework.

## Architectural invariants

- One `io_context` drives a BiDi connection.
- A strand serializes session state and WebSocket handlers.
- At most one WebSocket read and one queued write are active at a time.
- `asyncx::Async<T>` operations are lazy and start at a terminal operation.
- Request IDs map to pending handlers and deterministic steady-timer timeouts.
- Late responses after timeout or cancellation are ignored.
- Subscriptions and sessions use RAII for deterministic cleanup.
- Message parsing uses a fast path for IDs/methods and frame-local memory where
  appropriate.
- Arena-backed pointers and views never escape their handler.
- Protocol JSON is serialized with Boost.JSON; manual escaping is avoided.
- No busy-wait loops and no detached `std::thread` ownership.

## Public API layers

### `bidi::AutomationSession`

The recommended application facade. It owns WebDriver setup, the BiDi client,
the default browsing context, the event loop runner, and cleanup. Common
operations expose typed helpers such as navigation, title/URL access,
`evaluate_as<T>`, fallback extraction, and policy-aware script evaluation.

`AutomationSessionBuilder` configures browser arguments, navigation defaults,
script policy, viewport, timeouts, subscriptions, screenshots, and network
interception.

### `bidi::Client`

The lower-level BiDi facade. It exposes browsing-context, script, session,
network, storage, and event operations as lazy tasks. Use it for advanced
composition or protocol features not wrapped by `AutomationSession`.

Direct client setup has two lifetimes:

1. `SessionGuard` owns the HTTP WebDriver session.
2. `ClientGuard` owns the BiDi connection and cleanup.

Both must outlive the operations they protect.

### `asyncx`

`include/asyncx.hpp` provides executor-aware lazy operations, mapping and
chaining, cancellation, races, aggregation, and timeouts. Direct `co_await`
integrates tasks with Boost.Asio coroutines.

### Protocol types and commands

- `include/bidi/commands/`: command builders grouped by BiDi module.
- `include/bidi/types/`: strongly typed protocol values and Boost.JSON
  conversion.
- `include/bidi/ids.hpp`: method and event identifiers.
- `include/bidi/script/`: typed extraction, marshalling, and function wrappers.

## Repository layout

```text
include/
  asyncx.hpp
  WebDriverClient.hpp
  bidi/
    automation_session.hpp
    automation_session_builder.hpp
    automation_session_config.hpp
    client.hpp
    connection_builder.hpp
    core.hpp
    guards.hpp
    io_context_runner.hpp
    logging.hpp
    network_intercept_handler.hpp
    resilience.hpp
    session_threaded.hpp
    threading.hpp
    user_prompt_handler.hpp
    commands/
    script/
    types/

src/
  bidi_automation_session.cpp
  bidi_automation_session_builder.cpp
  bidi_client.cpp
  bidi_commands.cpp
  bidi_core.cpp
  bidi_network_intercept_handler.cpp
  bidi_script_eval.cpp
  bidi_session_threaded.cpp
  bidi_threading.cpp
  bidi_user_prompt_handler.cpp

examples/
  README.md
  flow/
    example_automation_session_minimal.cpp
    example_bidi_flow_minimal.cpp
    example_bidi_flow_production.cpp
    example_bidi_flow_script_exception.cpp
    example_log_monitoring.cpp
  scraping/
    README.md
    real_world_scraping_patterns.cpp
    spa_and_lazy_loading.cpp
  threading/
    README.md
    example_multithreaded_basic.cpp
    example_multithreaded_cpu_pool.cpp
    io_context_threads.hpp

tests/
  CMakeLists.txt
  html/
  test_http_server.hpp
  *_test.cpp and *_tests.cpp

docs/
  README.md
  architecture and API notes

tools/
  developer utilities
```

Build directories, CTest output, caches, downloaded browser binaries, and logs
are not source-tree components.

## Runtime flow

```text
create lazy operation
        |
        v
terminal operation (`co_await` or `.finally()`)
        |
        v
allocate command ID -> register pending handler and timer
        |
        v
serialize and enqueue WebSocket write
        |
        +-----------------------------+
        |                             |
        v                             v
response/event read             timeout/cancellation
        |                             |
        v                             v
strand dispatches handler       remove pending operation
        |
        v
complete task and cancel timer
```

Events are routed by method name. Subscription handles must remain alive while
the handler is needed and unsubscribe during RAII cleanup.

## Threading and lifetime model

- The strand protects internal BiDi state; callers do not add an external mutex
  around `Client` operations.
- Multiple threads may call `io_context::run()`, but one connection still
  serializes its WebSocket state.
- CPU-heavy transformations belong on a separate `boost::asio::thread_pool`.
- `std::jthread` or another explicit RAII owner joins application worker
  threads.
- `future.get()` is only used outside I/O handlers, at a synchronous boundary.
- Coroutine parameters should make ownership clear. Values copied into the
  coroutine frame are preferred when a reference lifetime is not obvious.

## Error model

Normal asynchronous flow uses error codes for transport errors, cancellation,
timeouts, decoding, and server failures. Synchronous facades and policy-aware
script helpers may surface exceptions with structured details.

Every error path must preserve:

- operation method and command ID when available;
- cancellation and timeout semantics;
- cleanup of pending handlers and timers;
- the original script exception details when policy requests throwing.

## Examples

All example `.cpp` files are CMake targets and compile with the same warnings
and sanitizers as the library.

- Start with `example_automation_session_minimal`.
- Use `example_bidi_flow_production` for configured high-level setup.
- Use `example_bidi_flow_minimal` only when direct `Client` access is required.
- Log, script-exception, scraping, and threading examples each demonstrate one
  focused behavior.

See `examples/README.md` for the current example catalog and prerequisites.

## Build and verification

```bash
cmake -S . -B build -DWEBDRIVER_USE_CONAN=ON -G Ninja
cmake --build build -j $(nproc)
ctest --test-dir build --output-on-failure
```

Browser integration tests additionally require ChromeDriver on port 9515 and
the HTML fixture server on port 8080. Unit-test results and feature readiness
must be reported from the current run; do not preserve changing test counts in
this map.

## Documentation ownership

- `AGENTS.md`: contributor workflow and repository rules.
- `map.md`: stable architecture and source-tree navigation.
- `docs/README.md`: documentation index and document status.
- Header Doxygen: API contracts closest to implementation.
- `examples/`: executable usage guidance.
- Git history: completed reviews, temporary checklists, and dated summaries.

When behavior changes, update the closest authoritative document rather than
adding another summary of the same change.

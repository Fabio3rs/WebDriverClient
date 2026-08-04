# AutomationSession architecture

`bidi::AutomationSession` is the recommended application-facing API. It
combines WebDriver session creation, BiDi connection setup, an Asio runner, a
default browsing context, configured subscriptions, and deterministic cleanup.

Use `bidi::Client` directly when an application needs protocol operations or
composition that the facade does not expose. The lower-level API is an escape
hatch, not the default onboarding path.

## Lifecycle

Session initialization intentionally has two phases:

1. `AutomationSession::start(...)` performs the blocking WebDriver handshake
   and creates the owned execution infrastructure.
2. `run(workflow)` connects BiDi, creates the default browsing context, applies
   configuration, executes the asynchronous workflow, waits for pending
   operations, and cleans up.

Keeping the asynchronous phase in `run()` makes the workflow explicit without
forcing callers to assemble guards, threads, and protocol setup themselves.

```cpp
auto session = bidi::AutomationSessionBuilder::create()
                   .headless()
                   .with_timeout(std::chrono::seconds{5})
                   .start();

session.run([](bidi::AutomationSession &browser) -> boost::asio::awaitable<void> {
    co_await browser.navigate("https://example.com");
    const auto title = co_await browser.title();
    std::cout << title << '\n';
});
```

The compiled flow examples are the authority for complete includes and error
handling.

## Configuration

Use `AutomationSessionBuilder` for discoverable application setup. The plain
`AutomationSessionConfig` overload remains useful when configuration is built
from another source or shared as data.

Configuration is grouped by concern:

- browser process and WebDriver endpoint;
- navigation and script defaults;
- viewport, screenshots, and subscriptions;
- preload scripts;
- pending-operation timeout and resilience policies;
- network interception.

Prefer a focused builder chain over manual mutation after startup. Per-call
overrides are appropriate when one operation intentionally differs from the
session default.

## API boundaries

High-level helpers should make the common path short and typed. They may
delegate to `Client`, but must retain the library's timeout, serialization,
error, and cleanup semantics.

`wait_for_element` is the preferred element-discovery primitive. It uses
browser-side observation with typed `script.callFunction` arguments, avoiding
client-side polling and JavaScript string interpolation.

Expose `client()` only for a feature that is not represented by the facade.
Code using it is responsible for understanding lazy `asyncx` operations and
the lifetime of any returned subscription handle. It must not stop the owned
`io_context` or manually dismantle session resources.

## Extension rules

Before adding a new facade helper:

1. Search for an existing typed command or script extractor.
2. Keep protocol-specific JSON in command/type modules.
3. Return a lazy operation compatible with the existing execution model.
4. Preserve structured server and script errors.
5. Add a focused test and update a compiled example only when it teaches a new
   user-facing pattern.

Exact signatures and defaults live in
`include/bidi/automation_session.hpp`,
`include/bidi/automation_session_builder.hpp`, and
`include/bidi/automation_session_config.hpp`.

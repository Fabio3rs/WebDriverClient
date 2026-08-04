# AutomationSession ownership and cleanup

Cleanup is a library responsibility. Applications should normally let
`AutomationSession` own the full lifecycle and avoid manual shutdown code.

## Ownership model

| Owner | Resource | Responsibility |
| --- | --- | --- |
| `SessionGuard` | HTTP WebDriver session | Ends the browser session on scope exit. |
| `ClientGuard` | BiDi client and subscriptions | Clears subscriptions and disconnects BiDi. |
| `IoContextRunner` | `io_context`, work guard, worker threads | Keeps work alive, stops execution, and joins workers. |
| `AutomationSession` | Guards, runner, context, configured handlers | Coordinates startup, workflow, waiting, and destruction order. |
| `NetworkInterceptHandler` | Intercept and event subscriptions | Removes interception state during cleanup. |

RAII wrappers are non-copyable where duplicate ownership would be ambiguous.
Any direct low-level setup must keep its guards alive longer than the
operations and callbacks they protect.

## Normal shutdown

`run()` completes the user workflow, waits up to the configured
pending-operation timeout, and then releases BiDi resources. Destruction is a
final safety net and must not throw.

The effective order is:

1. finish or time out the pending-operation wait;
2. remove configured handlers and subscriptions;
3. disconnect and release the BiDi client;
4. stop the `io_context` and join its threads;
5. end the HTTP WebDriver session.

This order keeps the executor and browser session available while asynchronous
cleanup still needs them.

## Failure paths

Workflow exceptions are preserved and rethrown after the asynchronous wait and
cleanup attempt. Cleanup code logs failures but destructors remain `noexcept`.
Timeout and cancellation paths must remove their pending handler and cancel the
associated timer so a late response cannot complete an abandoned operation.

Do not block an Asio handler with `future::get()`, join an I/O thread from
itself, or rely on a detached thread. A synchronous wait belongs only at the
outer application boundary.

## Application guidance

- Prefer `AutomationSession::run()` over assembling shutdown callbacks.
- Keep subscription handles in the scope where events are required.
- Capture callback state by value or by an owner with a demonstrably longer
  lifetime.
- Use explicit early `cleanup()` only when the application must release BiDi
  resources before the session object leaves scope.
- Never stop the session's owned `io_context` through the `client()` escape
  hatch.

See `include/bidi/guards.hpp`, `include/bidi/io_context_runner.hpp`, and
`include/bidi/automation_session.hpp` for the exact contracts.

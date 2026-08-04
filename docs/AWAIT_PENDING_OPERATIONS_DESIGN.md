# Waiting for pending operations during cleanup

Status: accepted and implemented.

## Context

A workflow can fail while requests, event callbacks, timers, or writes are
still registered. Immediately destroying the connection would make completion
order depend on timing and could discard useful error or cleanup work.
Waiting without a bound, however, could make shutdown hang forever.

## Decision

`AutomationSession::run()` performs a bounded asynchronous wait for the core's
pending operations before final cleanup and before rethrowing the workflow
exception. The timeout is configured by
`AutomationSessionConfig::pending_operations_timeout` and by the corresponding
builder option.

The wait runs on the same Asio execution model as the connection. It does not
poll, sleep a worker thread, or start an unowned thread.

```text
workflow completes or throws
            |
            v
bounded wait for pending operations
            |
            v
cleanup subscriptions and connection
            |
            v
return or rethrow original workflow error
```

## Consequences

- Normal completion gives already-started operations a deterministic chance to
  settle.
- Failure retains the original workflow exception after cleanup.
- A stuck operation can delay shutdown only up to the configured bound.
- The timeout is a cleanup bound, not a replacement for per-command timeouts.
- Late responses remain harmless because timed-out or cancelled requests are
  removed from the pending registry.

## Validation

Changes to this behavior require tests for normal completion, workflow
exceptions, timeout expiry, and absence of pending operations. Browser-backed
coverage must be reported with its ChromeDriver and fixture-server
prerequisites; do not encode a permanent pass count in this document.

The implementation contract is in `bidi::Core::await_pending_operations_complete`
and `bidi::AutomationSession::run()`.

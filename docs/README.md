# Documentation

This directory contains maintained design and usage notes. Public headers are
the authority for exact signatures; runnable code belongs in `examples/`.

## Start here

- [`../map.md`](../map.md): architecture, invariants, and repository layout.
- [`../AGENTS.md`](../AGENTS.md): contributor workflow and quality rules.
- [`../examples/README.md`](../examples/README.md): runnable examples and
  prerequisites.

## Maintained notes

- [`automation_session_architecture_review.md`](automation_session_architecture_review.md):
  high-level session API and extension points.
- [`automation_session_cleanup_architecture.md`](automation_session_cleanup_architecture.md):
  ownership and shutdown order.
- [`AWAIT_PENDING_OPERATIONS_DESIGN.md`](AWAIT_PENDING_OPERATIONS_DESIGN.md):
  cleanup wait decision record.
- [`awaitable.md`](awaitable.md): lazy `asyncx` tasks and coroutine usage.
- [`io_context.md`](io_context.md): Boost.Asio execution and threading model.
- [`network_implementation_status.md`](network_implementation_status.md):
  network interception surface and validation boundaries.

## Documentation policy

Keep behavior contracts beside the implementation in Doxygen. Use this
directory for decisions and concepts that span multiple headers. Do not add
generated review reports, completion summaries, commit recipes, copied
standards, fixed test counts, or speculative examples for APIs that do not
exist. Git history is the archive for finished reviews.

When an API changes, update its header, its focused document, and any affected
compiled example in the same change.

# Boost.Asio execution model

Each BiDi connection is driven by one `boost::asio::io_context`. Internal
connection state and WebSocket handlers are serialized by a strand. Running the
same context on several threads increases executor capacity but does not make a
single connection issue concurrent socket reads or writes.

## Recommended ownership

Application code should use `AutomationSession`, which owns an
`IoContextRunner`. Direct `Client` users must provide equivalent RAII:

- a work guard while more work may be scheduled;
- explicitly owned worker threads;
- deterministic stop and join;
- session and client guards that outlive callbacks.

Use `std::jthread` or another joining owner for application-created threads.
Do not detach them.

## Choosing a thread count

A single I/O thread is the simplest default and is sufficient for most browser
automation. Multiple `io_context::run()` threads can help when an application
has several independent connections or substantial non-blocking handler work.
The strand still protects per-connection ordering.

Move CPU-heavy parsing or transformation to a separate
`boost::asio::thread_pool`, then post the result back to the appropriate
executor. Do not block an I/O handler with sleeps, synchronous network calls,
or `future::get()`.

## Shutdown

Stop accepting new application work, settle bounded pending work, release BiDi
subscriptions and the connection, reset the work guard, stop the context, and
join workers. See `automation_session_cleanup_architecture.md` for the owned
session order.

## Examples

- `examples/threading/example_multithreaded_basic.cpp` shows multiple Asio
  workers with explicit RAII.
- `examples/threading/example_multithreaded_cpu_pool.cpp` separates I/O from
  CPU-bound work.
- `examples/threading/io_context_threads.hpp` centralizes the repeated thread
  ownership mechanics used by those examples.

These examples are compiled by CMake and are preferable to copied snippets.

# Threading examples

The BiDi client serializes WebSocket state on its internal strand. Applications
may run one `io_context` on multiple threads without adding a mutex around
`bidi::Client` calls. Blocking or CPU-heavy work must still stay off those I/O
threads.

Both examples require ChromeDriver on `localhost:9515`:

```bash
chromedriver --port=9515
```

## Multi-threaded I/O

`example_multithreaded_basic.cpp` creates one client, starts independent lazy
commands with `asyncx::all`, and runs the shared `io_context` on up to four
threads. The client strand preserves WebSocket read/write ordering.

```bash
./build/example_multithreaded_basic
```

Use this pattern for I/O-bound applications. More I/O threads do not make one
WebSocket command execute in parallel; they allow unrelated handlers to make
progress while the strand protects session state.

## Separate CPU pool

`example_multithreaded_cpu_pool.cpp` keeps browser I/O on the `io_context` and
uses `boost::asio::thread_pool` for CPU-bound transformation. Results return to
the awaiting coroutine without blocking the WebSocket event loop.

```bash
./build/example_multithreaded_cpu_pool
```

Use this pattern when parsing or transforming a response is expensive. Never
call `future.get()`, sleep, or perform heavy computation from a handler running
on the client strand.

## Lifetime rule

`ConnectionBuilder::get_websocket_url()` returns both the URL and a
`SessionGuard`. Keep that guard alive until all I/O threads have joined; it owns
the HTTP WebDriver session behind the BiDi connection.

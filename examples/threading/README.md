# Multi-threaded BiDi Examples

This directory; contains comprehensive examples showing how to use the BiDi WebDriver client with multiple threads while maintaining thread safety through Boost.Asio strands.

 **IMPORTANT:** These examples had critical lifetime issues that have been fixed. See [`THREADING_ISSUES_FIXED.md`](THREADING_ISSUES_FIXED.md) for details on the issues and fixes.

## Key Concept: Strand Single-Threaded

**Critical Understanding:** A strand ensures **serialized execution** of handlers, but those handlers can run on **any thread** in the io_context thread pool. This gives you:
- Full multi-threading (concurrent I/O on multiple threads)
- Thread safety (no race conditions on shared state)
- No mutex overhead (strand provides lockless serialization)

## Examples Overview

### 1. `example_strand_explained.cpp` **START HERE**
**Visual demonstration of how strand works**

Shows side-by-side comparisons:
- Race conditions WITHOUT strand
- Thread safety WITH strand
- How handlers migrate between threads
- Performance impact (minimal)

**Run this first to understand the fundamentals!**

```bash
./build/example_strand_explained
```

**Output includes:**
- Visual proof of race conditions
- Demonstration of strand serialization
- Thread ID tracking showing handler migration
- Performance benchmarks

---

### 2. `example_multithreaded_basic.cpp`
**Basic multi-threaded BiDi usage**

Demonstrates:
- Running io_context on multiple threads
- Concurrent command execution
- Strand protecting WebSocket state
- Thread-safe BiDi operations

**Use case:** Simple applications needing concurrent I/O

```bash
# Requires ChromeDriver running on port 9515
chromedriver --port=9515 &
./build/example_multithreaded_basic
```

**Pattern shown:**
```cpp
asio::io_context ioc;
auto client = co_await bidi::Client::connect(ioc, ws_url);

// Run io_context on N threads
std::vector<std::thread> threads;
for (int i = 0; i < N; ++i) {
    threads.emplace_back([&ioc] { ioc.run(); });
}
```

---

### 3. `example_multithreaded_cpu_pool.cpp`
**Separate I/O and CPU thread pools**

Demonstrates:
- I/O threads for WebSocket operations
- CPU thread pool for heavy processing
- Offloading JSON parsing and transformations
- Coordination between thread pools

**Use case:** Applications with CPU-intensive processing (large JSON, data transformation)

```bash
chromedriver --port=9515 &
./build/example_multithreaded_cpu_pool
```

**Pattern shown:**
```cpp
asio::io_context ioc;           // For I/O
asio::thread_pool cpu_pool{8};  // For CPU work

// Run I/O on 2 threads
std::vector<std::thread> io_threads(2);
for (auto &t : io_threads) {
    t = std::thread([&ioc] { ioc.run(); });
}

// Offload heavy work
auto result = co_await asio::co_spawn(
    cpu_pool,
    []() -> asio::awaitable<T> {
        // Heavy processing here
        co_return process_data();
    },
    asio::use_awaitable
);
```

---

### 4. `example_strand_safety.cpp`
**Proof of strand thread safety**

Demonstrates:
- Multiple threads posting concurrent operations
- Strand serializing all access
- Operation tracking and verification
- Zero race conditions

**Use case:** Understanding/verifying strand safety guarantees

```bash
chromedriver --port=9515 &
./build/example_strand_safety
```

**What it proves:**
- 4 launcher threads posting 20 concurrent operations
- All operations serialized by strand
- No data corruption despite high concurrency
- Thread IDs showing different threads executing handlers

---

### 5. `example_production_threading.cpp`
**Production-ready architecture**

Demonstrates:
- Proper thread lifecycle management
- Graceful shutdown
- Error handling across threads
- Task queue pattern
- Resource cleanup

**Use case:** Real production applications

```bash
chromedriver --port=9515 &
./build/example_production_threading
```

**Features:**
- Work queue with thread-safe operations
- Worker coroutine processing tasks
- Multiple I/O threads
- Clean shutdown on completion
- Statistics tracking

**Pattern shown:**
```cpp
class ProductionApp {
    asio::io_context ioc_;
    std::vector<std::thread> io_threads_;
    std::shared_ptr<TaskQueue> tasks_;

    void start() {
        // Start worker
        asio::co_spawn(ioc_, worker_->run(), asio::detached);

        // Start I/O threads
        for (int i = 0; i < N; ++i) {
            io_threads_.emplace_back([this] { ioc_.run(); });
        }
    }

    void shutdown() {
        ioc_.stop();
        for (auto &t : io_threads_) {
            t.join();
        }
    }
};
```

---

## Threading Patterns Comparison

### Pattern 1: Single-threaded (Simplest)
```cpp
asio::io_context ioc;
auto client = co_await bidi::Client::connect(ioc, ws_url);
ioc.run();  // Single thread
```

**Pros:** Simple, deterministic
**Cons:** Limited concurrency
**Use when:** Prototyping, simple scripts

---

### Pattern 2: Multi-threaded I/O (Recommended)
```cpp
asio::io_context ioc;
auto client = co_await bidi::Client::connect(ioc, ws_url);

std::vector<std::thread> threads;
for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
    threads.emplace_back([&ioc] { ioc.run(); });
}
```

**Pros:** Full I/O concurrency, no code changes
**Cons:** CPU work blocks I/O threads
**Use when:** I/O-bound applications

---

### Pattern 3: Separate I/O + CPU Pools (Advanced)
```cpp
asio::io_context ioc;
asio::thread_pool cpu_pool{N};
auto client = co_await bidi::Client::connect(ioc, ws_url);

// I/O threads
std::vector<std::thread> io_threads(2);
for (auto &t : io_threads) {
    t = std::thread([&ioc] { ioc.run(); });
}

// Offload CPU work
co_await asio::co_spawn(cpu_pool, heavy_work(), asio::use_awaitable);
```

**Pros:** Optimal resource utilization
**Cons:** More complex
**Use when:** CPU-intensive processing needed

---

### Pattern 4: ThreadingContext (Highest Level)
```cpp
auto threading = std::make_shared<ThreadingContext>(
    2,  // I/O threads
    8   // CPU threads
);

threading->post_io([](){ /* I/O work */ });
threading->post_cpu([](){ /* CPU work */ });
threading->post_ws([](){ /* WebSocket work (serialized) */ });
```

**Pros:** Explicit separation, convenient helpers
**Cons:** Extra abstraction layer
**Use when:** Large applications with clear separation

---

## Common Pitfalls and Solutions

### ❌ Pitfall 1: Mixing atomics with strand
```cpp
// DON'T DO THIS:
std::atomic<bool> flag;
asio::post(strand, [&]() {
    if (flag.exchange(true)) { /* ... */ }  // ❌ Unnecessary atomic
});
```

**Solution:**
```cpp
// Use regular bool (strand-protected):
bool flag = false;
asio::post(strand, [&]() {
    if (flag) { /* ... */ }  // ✅ Safe, no atomic needed
    flag = true;
});
```

---

### ❌ Pitfall 2: Blocking on strand
```cpp
// DON'T DO THIS:
asio::post(strand, [&]() {
    auto result = future.get();  // ❌ Blocks strand thread
});
```

**Solution:**
```cpp
// Use async operations:
asio::post(strand, [&]() {
    // Initiate async operation, don't block
    async_operation([](result) {
        // Handle result in callback
    });
});
```

---

### ❌ Pitfall 3: Forgetting to post to strand
```cpp
// DON'T DO THIS:
timer->async_wait([&](error_code ec) {
    shared_state.update();  // ❌ Not on strand!
});
```

**Solution:**
```cpp
// Post to strand before accessing state:
timer->async_wait([&, strand](error_code ec) {
    asio::post(strand, [&]() {
        shared_state.update();  // ✅ On strand
    });
});
```

---

## Performance Guidelines

### Thread Count Recommendations

**I/O Threads:**
- Minimum: 1 (works, but limited concurrency)
- Recommended: 2-4 (optimal for most applications)
- Maximum: `std::thread::hardware_concurrency()` (rarely needed)

**CPU Threads:**
- Light processing: Same as I/O threads
- Heavy processing: `std::thread::hardware_concurrency()`
- Mixed workload: 2 I/O + (cores - 2) CPU

### Benchmarks (from examples)

Based on `example_strand_explained.cpp`:
- Strand overhead: < 5% compared to no synchronization
- 1000 operations: ~2-3ms difference
- Conclusion: Safety is worth the minimal cost

---

## Building Examples

### Prerequisites
```bash
# Install dependencies
sudo apt-get install build-essential cmake clang-15
sudo apt-get install libboost-all-dev libgtest-dev

# Start ChromeDriver (for BiDi examples)
chromedriver --port=9515 &
```

### Build
```bash
cd build
cmake .. -G Ninja
cmake --build . --target example_multithreaded_basic
cmake --build . --target example_multithreaded_cpu_pool
cmake --build . --target example_strand_safety
cmake --build . --target example_production_threading
cmake --build . --target example_strand_explained
```

### Run
```bash
# Conceptual example (no ChromeDriver needed)
./example_strand_explained

# BiDi examples (ChromeDriver required)
./example_multithreaded_basic
./example_multithreaded_cpu_pool
./example_strand_safety
./example_production_threading
```

---

## Additional Resources

### Boost.Asio Documentation
- [Strands](https://www.boost.org/doc/libs/1_84_0/doc/html/boost_asio/overview/core/strands.html)
- [Thread Safety](https://www.boost.org/doc/libs/1_84_0/doc/html/boost_asio/overview/core/threads.html)
- [Coroutines](https://www.boost.org/doc/libs/1_84_0/doc/html/boost_asio/overview/composition/coroutines.html)

### Project Documentation
- `../../CLAUDE.md` - Project architecture and build instructions
- `../../map.md` - System architecture map
- `../flow/` - Single-threaded flow examples

---

## Questions and Answers

**Q: Do I need ThreadedBiDiSession for multi-threading?**

A: No! `BiDiSession` already supports multi-threading via strand. `ThreadedBiDiSession` was an experimental implementation that mixed synchronization primitives. Use `BiDiSession` + multiple io_context threads instead.

**Q: How do I know if my code is thread-safe?**

A: If all access to shared state goes through `asio::post(strand, ...)`, it's thread-safe. The examples demonstrate this pattern.

**Q: What's the performance impact of strand?**

A: Minimal (< 5%). See `example_strand_explained.cpp` for benchmarks. The safety is worth it.

**Q: Can I use std::mutex instead of strand?**

A: You can, but strand is better because:
- No lock contention
- No deadlock risk
- Integrates with async operations
- Lower overhead

**Q: How many threads should I use?**

A: Start with 2-4 I/O threads. Add CPU threads only if profiling shows CPU bottleneck. More threads ≠ better performance.

---

## Summary

These examples demonstrate that **multi-threading with strand is the recommended pattern** for BiDi WebDriver applications. Key points:

1. ✅ Strand ensures thread safety without mutexes
2. ✅ Handlers can run on any thread (full concurrency)
3. ✅ Minimal performance overhead
4. ✅ Works seamlessly with coroutines
5. ✅ Production-ready pattern used in examples

**Start with `example_strand_explained.cpp` to understand the fundamentals, then explore the BiDi-specific examples.**

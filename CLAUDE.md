# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**WebDriverClient** (internal codename: **bidi-x**) is a high-performance C++20/23 WebDriver BiDi client library for browser automation. The project focuses on minimalist design, CPU/memory efficiency, and asynchronous operations using Boost.Asio/Beast with coroutine support.

**Key Philosophy:**
- Chrome WebDriver only (tested)
- Zero busy-wait / zero detached threads
- Single event loop architecture (io_context + strand)
- Lazy evaluation model
- RAII for resource management
- Deterministic timeouts via timer racing

## Project Status

**DEVELOPMENT PHASE - NO PRODUCTION RELEASES**

This project is currently in active development. Key implications:

- **No stable releases**: No version has been tagged or published for production use
- **API is unstable**: Breaking changes can occur without deprecation warnings
- **No backward compatibility guarantees**: Refactoring and reorganization happen freely
- **Active experimentation**: Architecture and design patterns are being refined
- **Current branch**: `dev_bidi` (development branch)

**For contributors and users:**
- Expect frequent breaking changes
- Always build from latest commit
- No semantic versioning until first stable release
- Report issues and feedback via GitHub issues
- Production use is NOT recommended at this stage

## Recent Highlights (2025-10-21)
- **Test Infrastructure**: Extracted reusable `TestHttpServer` (`tests/test_http_server.hpp`) with custom handlers, authentication support, and proper RAII thread management
- **Network Interception**: Fixed all compilation errors in `NetworkInterceptHandler` (designated initializers, return type migration to `shared_ptr`, Boost.JSON conversions)
- **Build Status**: 348/350 tests passing (99%), all 30 targets compile successfully

## Build Commands

### Prerequisites
```bash
sudo apt-get install build-essential cmake clang-15 clang-tidy-15 clang-format ninja-build
sudo apt-get install libcurl4 libcurl4-openssl-dev libpoco-dev libgtest-dev googletest
sudo apt-get install python3-pip chromium-chromedriver
```

### Build with Conan (Development)
```bash
mkdir build && cd build
cmake .. -DWEBDRIVER_USE_CONAN=ON -G Ninja
cmake --build . -j$(nproc)
```

### Build with System Packages (Future Production Deploys)
**Note**: This build configuration is prepared for future production deployments. Currently, the project is in development.

```bash
sudo apt install libcurl4-openssl-dev libboost-all-dev libpoco-dev
mkdir build && cd build
export CC=$(which clang-15)
export CXX=$(which clang++-15)
cmake .. -DWEBDRIVER_USE_CONAN=OFF -G Ninja
cmake --build . -j$(nproc)
```

### Running Tests
Tests require ChromeDriver and a test HTTP server:

```bash
# Terminal 1: Start test web server
pushd tests/html && python3 -m http.server 8080 &
popd

# Terminal 2: Start ChromeDriver
chromedriver --port=9515 &

# Run tests
cd build
ctest -j$(nproc) --output-on-failure

# Cleanup
killall python3
killall chromedriver
```

### Single Test Execution
```bash
cd build
./webdriverclientcpp_tests --gtest_filter=TestName.*
```

### Standard Build + Test Cycle
```bash
cmake --build . -j$(nproc) 2>&1 && ctest --output-on-failure -j$(nproc)
```

### Code Formatting
Apply clang-format to modified files:
```bash
./format-code.sh
```

**What it does:**
- Formats staged C++ files (ready for commit)
- Formats modified but uncommitted C++ files
- Formats new untracked C++ files
- Removes trailing whitespace from all text files (.txt, .md, .cpp, .hpp, .py, .sh, etc.)

**Manual formatting:**
```bash
# Format specific file
clang-format -i src/bidi_script_eval.cpp

# Format all C++ files in directory
find src/ -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i
```

## CMake Configuration Options

- `WEBDRIVER_USE_CONAN`: Use Conan package manager (default: OFF)
- `WEBDRIVER_USE_BOOST`: Enable Boost for BiDi protocol (default: ON, required)
- `WEBDRIVER_USE_POCO`: Use Poco libraries (default: ON)
- `WEBDRIVER_BUILD_SHARED`: Build shared library (default: OFF)
- `WEBDRIVER_BUILD_EXAMPLES`: Build examples (default: ON when standalone)
- `WEBDRIVER_BUILD_TESTS`: Build tests (default: ON when standalone)
- `WEBDRIVER_ENABLE_SANITIZERS`: Enable sanitizers in debug (default: ON when standalone)

**C++ Standard:** C++23 when Boost enabled, C++20 otherwise

## Architecture Overview

### Threading Model
- **Single event loop:** One `boost::asio::io_context` with work guard
- **Strand-based synchronization:** No mutexes, all operations serialized through strand
- **One active async_read + one active async_write** at any time
- Write queue for command serialization

### Core Components

**bidi::core** (`include/bidi/core.hpp`, `src/bidi_core.cpp`)
- Central types: `id_type` with safe JS range validation (≤ 2^53-1)
- `build_command()`: JSON command builder
- Pending entry management: maps command ID → (completion handler + timer)
- Timer racing: operation vs `steady_timer`, late responses ignored

**bidi::threading** (`include/bidi/threading.hpp`, `src/bidi_threading.cpp`)
- Strand orchestration for read/write operations
- Ensures exclusive async_read and async_write access

**bidi::pools** (`include/bidi/pools.hpp`, various pool implementations)
- `memory_pool`: Arena/monotonic allocator per message frame
- `buffer_pool` / `buffer_pool_vec`: Reusable buffer management
- `timer_pool`: Timer object reuse
- `pending_entry_pool_vec`: Unified pending entries + metrics
- **Note:** `promise_pool` has been removed (replaced by direct pending model)

**bidi::commands** (`include/bidi/commands.hpp`, `src/bidi_commands.cpp`)
- 1:1 mapping to WebDriver BiDi spec
- Command builders for: `browsingContext.*`, `script.*`, `session.*`

**bidi::ids** (`include/bidi/ids.hpp`)
- String constants for all BiDi command/event methods

**bidi::guards** (`include/bidi/guards.hpp`)
- RAII wrappers: `SessionGuard`, `ClientGuard`, `TimerGuard`
- Automatic cleanup for sessions, clients, timers

**SubscriptionManager** / **session_threaded** (`include/bidi/session_threaded.hpp`)
- RAII subscription management with refcounting
- Automatic resubscription after reconnection

**asyncx** (`include/asyncx.hpp`)
- Lazy composition operators: `.map()`, `.and_then()`, `.recover()`, `.on_error()`, `.finally()`
- Free functions: `asyncx::timeout()`, `asyncx::race()`, `asyncx::all()`
- Terminal operations materialize lazy chains
- Cooperative cancellation support

**bidi::helpers** (`include/bidi/helpers.hpp`)
- `retry()`: Retry with exponential backoff
- `timeout_or()`: Timeout with fallback value
- Built on asyncx primitives for resilient operations

**WebDriverClient** (`include/WebDriverClient.hpp`)
- High-level facade: HTTP handshake → `webSocketUrl` → BiDi channel
- Convenience methods for common operations

### Message Flow (Happy Path)

1. Create operation (e.g., `script.evaluate`) → lazy object
2. Terminal (`.finally` / `co_await` / `sync`) materializes:
   - Generate unique ID
   - Register pending entry with timer
   - Serialize JSON command
   - Enqueue write operation
3. Strand processes write queue:
   - Starts `async_write` if available
   - Advances queue on completion
4. Read loop (`async_read`) fast-path parsing:
   - **Response (has id):** Find pending, cancel timer, satisfy promise
   - **Event (method without id):** Dispatch to listeners/subscriptions
5. **Timeout:** Timer expires → remove pending, signal `errc::timeout`; late response ignored

### Parsing & Serialization Strategy

- **Fast-path:** Scan for `id`/`method` without building full DOM
- **Full parsing:** Arena-based `monotonic_resource` per message frame
  - Bulk deallocation when handler completes
  - **Critical:** No `string_view` or pointer from arena may escape the handler scope
- **Serialization:** `boost::json::serializer` writes directly to reusable buffer
- **Primary JSON backend:** Boost.JSON (PMR-compatible)

### Error Model

Uses `std::error_code` for flow control:
- `errc::timeout`: Timer expired before response
- `errc::cancelled`: Cooperative cancellation
- `errc::transport`: I/O failure / disconnection
- `errc::server_error`: Response type:error from server
- `errc::decode_error`: Parse failure
- `errc::unsupported`: Feature/command not supported

Error details include: method, id, raw JSON, duration

Exceptions only in synchronous facade methods (`WebDriverExceptions.hpp`)

## Critical Development Rules

**DEVELOPMENT PROJECT NOTICE:**
This is an active development project with no production releases. Breaking changes are allowed without deprecation warnings. Focus on clean architecture over backward compatibility.

**BEFORE ANY CODE MODIFICATION:**

1. **Mandatory identifier verification:** Use `Grep` to check if ALL identifiers (variables, functions, classes) you intend to use already exist
2. **Scope verification:** If identifier exists in same scope, REUSE it - NEVER redeclare
3. **Functionality check:** Use `Grep` to search for similar functionality before implementing new code
4. **Required process:**
   - First: `Grep` to verify identifiers
   - Second: `Read` to understand existing context
   - Third: Implement ONLY if non-existent
   - Fourth: Build and test with `cmake --build . -j$(nproc) 2>&1 && ctest --output-on-failure -j$(nproc)`
5. **Single source principle:** One functionality = one function. One identifier = one declaration per scope. NEVER duplicate

**Code Style:**
- NEVER use emojis in logs, commits, branches, filenames, variable names, function names, or class names
- Use clear, descriptive names that explain exactly what the code does
- Functions should be ≤ ~80 lines
- Const correctness is required
- No `string_view`/pointer from arena may escape message handler scope
- **ALWAYS** run `./format-code.sh` before committing to ensure consistent formatting

**Refactoring Policy:**
- **Breaking changes allowed**: No need for deprecation warnings or backward compatibility
- **Direct refactoring**: Replace old code directly instead of creating compatibility layers
- **Clean architecture first**: Prioritize good design over maintaining old APIs
- **Compiler enforcement**: Let compilation errors guide migration to new APIs

**Project Documentation:**
- Read `map.md` to understand the system architecture
- Update `map.md` ONLY after explicit approval
- NEVER commit or update map without authorization

## Modern C++ Patterns

This project leverages C++20/23 features extensively for type safety, expressiveness, and zero-cost abstractions.

### Currently Used Features

**1. [[nodiscard]] Attribute (C++17)** - 144 occurrences across 24 files
```cpp
// REQUIRED on all functions returning values that should not be ignored
[[nodiscard]] auto create_context() -> Task<std::string>;
[[nodiscard]] auto evaluate(std::string_view expression) -> Task<boost::json::object>;
```

**Usage Guidelines:**
- Apply to ALL functions returning Task<T>, std::expected, std::optional
- Apply to factory functions and resource acquisition functions
- Apply to query methods where ignoring the result is likely a bug
- **Exception**: Void-returning functions and setters don't need it

**2. std::string_view (C++17)** - 151 occurrences across 17 files
```cpp
// Prefer string_view for non-owning string parameters
auto navigate(std::string_view context_id, std::string_view url) -> Task<std::string>;
auto evaluate(std::string_view expression, std::string_view context_id) -> Task<boost::json::object>;
```

**Usage Guidelines:**
- Use for all read-only string parameters (replaces `const std::string&`)
- **CRITICAL**: Never store string_view from arena allocations (see Parsing & Serialization Strategy)
- Safe for function parameters that don't outlive the call
- Convert to std::string when storage is needed

**3. std::optional (C++17)** - 21 files
```cpp
// Use for values that may or may not be present
std::optional<work_guard_type> work_guard_;
std::optional<ScriptExceptionDetails> exception;
std::optional<std::int64_t> line_number;
```

**Usage Guidelines:**
- Prefer over nullable pointers for optional values
- Use `.value_or(default)` for fallback patterns
- Use `.has_value()` for existence checks
- Better than sentinel values (-1, empty string, nullptr)

**4. std::expected<T, E> (C++23)** - 6 files
```cpp
// Use for operations that can fail with rich error information
auto connect(Args args, std::string_view browser, bool headless)
    -> std::expected<std::string, std::string>;

auto run() -> std::expected<int, std::string> {
    if (/* error */) {
        return std::unexpected("Connection failed: " + details);
    }
    return exit_code;
}
```

**Usage Guidelines:**
- Use for synchronous operations that can fail
- Prefer over bool return + out parameter
- Prefer over exceptions for expected failure cases
- Use std::unexpected for error values
- Check with `if (!result)` then access error with `.error()`

**5. std::format (C++20)** - 13 files
```cpp
// Prefer std::format over string concatenation
throw std::runtime_error(std::format(
    "Failed to extract value as {}: {}; Result: {}",
    typeid(T).name(), e.what(), boost::json::serialize(result)
));

// Type-safe, readable, and efficient
auto msg = std::format("Context {} navigation completed in {}ms",
                       ctx_id, duration.count());
```

**Usage Guidelines:**
- Prefer over `operator+` for string building
- Prefer over stringstream for formatting
- Use for all error messages with interpolation
- Provides compile-time format string validation
- More efficient than alternatives

**6. Structured Bindings (C++17)** - Currently LIMITED use (3 files)
```cpp
// Destructure returns for cleaner code
const auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + size, value);
if (ec == std::errc{}) { /* success */ }

// With maps/pairs
for (const auto& [key, value] : mapping) {
    // Use key and value directly
}
```

**Usage Guidelines:**
- Use for std::pair and std::tuple returns
- Use in range-for with maps (auto& [key, value])
- Makes code more readable than .first/.second
- **Opportunity**: Could be used more throughout codebase

**7. constexpr if (C++17)** - 4 files
```cpp
// Compile-time conditionals for template optimization
template<typename T>
void process(T value) {
    if constexpr (std::is_integral_v<T>) {
        // Integer-specific code path
    } else if constexpr (std::is_floating_point_v<T>) {
        // Float-specific code path
    }
}
```

**Usage Guidelines:**
- Use for template specialization alternatives
- Eliminates need for SFINAE in many cases
- Enables zero-cost abstraction patterns
- **Opportunity**: Could replace some runtime conditionals in templates

**8. Nested Namespaces (C++17)** - Extensively used
```cpp
// Cleaner namespace declarations
namespace bidi::script { /* ... */ }
namespace bidi::commands::browsing_context { /* ... */ }

// Instead of:
namespace bidi {
namespace script {
    /* ... */
}
}
```

**9. Inline Variables (C++17)** - Used in headers
```cpp
// No separate .cpp definition needed
struct Config {
    static inline int default_timeout{5000};
    static inline std::string default_url{"http://localhost:9515"};
};
```

### Features Ready for Adoption

**1. Concepts (C++20)** - NOT YET USED - High Value Opportunity
```cpp
// Type constraints for better error messages and API clarity
template<typename T>
concept JsonSerializable = requires(T t) {
    { boost::json::value_from(t) } -> std::convertible_to<boost::json::value>;
};

template<typename T>
concept AwaitableTask = requires(T t) {
    typename T::value_type;
    { t.await_ready() } -> std::convertible_to<bool>;
    { t.await_suspend(std::declval<std::coroutine_handle<>>()) };
    { t.await_resume() } -> std::convertible_to<typename T::value_type>;
};

// Usage:
template<JsonSerializable T>
auto serialize(const T& value) -> std::string;

template<AsyncOperation Op>
auto with_timeout(Op operation, std::chrono::milliseconds timeout);
```

**Benefits:**
- Better compile-time error messages
- Self-documenting template constraints
- Enables better IDE support
- Replaces complex SFINAE patterns

**2. [[likely]] / [[unlikely]] Attributes (C++20)** - NOT YET USED
```cpp
// Optimize hot paths
if (pending_entries_.contains(id)) [[likely]] {
    // Fast path: response for pending operation
    auto entry = pending_entries_.extract(id);
    entry.mapped().satisfy(std::move(response));
} else [[unlikely]] {
    // Rare: late response after timeout
    log_warning("Received response for unknown ID");
}
```

**Benefits:**
- Branch prediction hints to compiler
- Improved performance in hot loops
- Zero runtime cost
- **Target areas**: Message routing, fast-path parsing

**3. std::span (C++20)** - NOT YET USED
```cpp
// Replace pointer + length patterns
void process_buffer(std::span<const std::byte> data) {
    // Bounds-checked access
    for (const auto byte : data) {
        // Process
    }
}

// Instead of:
void process_buffer(const std::byte* data, std::size_t length);
```

**Benefits:**
- Bounds-checked by default
- Works with arrays, vectors, and raw pointers
- Clear ownership semantics (non-owning view)
- **Target areas**: Buffer operations, parsing functions

**4. using enum (C++20)** - NOT YET USED
```cpp
// Simplify enum usage in switch statements
enum class Status { pending, completed, failed, cancelled };

auto to_string(Status s) -> std::string_view {
    using enum Status;  // Bring enum members into scope
    switch (s) {
        case pending:   return "pending";
        case completed: return "completed";
        case failed:    return "failed";
        case cancelled: return "cancelled";
    }
}

// Instead of:
// case Status::pending: return "pending";
```

**5. Designated Initializers (C++20)** - NOT YET USED
```cpp
// Clear struct initialization
struct ConnectionConfig {
    std::string url;
    int timeout_ms;
    bool headless;
    bool use_sandbox;
};

ConnectionConfig config{
    .url = "http://localhost:9515",
    .timeout_ms = 5000,
    .headless = true,
    .use_sandbox = false
};
```

**Benefits:**
- Self-documenting initialization
- Harder to misorder parameters
- Explicit about what's being set

### Debugging Async Operations with std::source_location

All async operations capture their call site via `std::source_location` (C++20). Even when not actively logged or used in code, this information remains available for GDB inspection during debugging sessions.

**Pattern: Lambda captures for debugging**
```cpp
// Lambda captures loc even though it's not used in the handler body
timer.async_wait([self, id, loc](const boost::system::error_code &ec) {
    (void)loc;  // Intentionally captured for GDB inspection
    // ... handler logic ...
});
```

**GDB inspection during debugging**:
```gdb
# Break on timeout handler
(gdb) break BiDiSession::send_command
(gdb) condition 1 timeout_expired == true
(gdb) run

# Inspect original call site
(gdb) print loc
$1 = {_M_file_name = "app.cpp", _M_line = 156, _M_column = 47, _M_function_name = "workflow()"}

# Or view all local variables
(gdb) info locals
loc = {_M_file_name = "automation.cpp", _M_line = 89, ...}
```

**Use cases**:
- **Timeout investigation**: Immediately identify which specific operation timed out
- **Error correlation**: Trace transport errors back to the originating call
- **Async lifecycle tracking**: Follow operations through the entire async chain
- **Production debugging**: Source location stored in `PendingEntry` and `ParsedResponse` structures

**Implementation details**:
- All Client API methods accept `const std::source_location &loc = std::source_location::current()` as last parameter
- Source location flows: User code → Client → BiDiSession → PendingEntry → ParsedResponse
- Memory overhead: ~20 bytes per pending operation (compile-time resolved)
- Zero runtime cost: All information captured at compile-time
- GDB can inspect captured `loc` variables even when marked with `(void)loc;` or `[[maybe_unused]]`

This pattern provides zero-cost debugging information with significant diagnostic value during issue investigation.

### Anti-Patterns to Avoid

**1. string_view Lifetime Issues**
```cpp
// BAD: Storing string_view from arena allocation
std::string_view cached_value;
void process_message(const boost::json::object& obj) {
    cached_value = obj.at("method").as_string();  // DANGEROUS!
    // Arena deallocated after handler completes
}

// GOOD: Copy to std::string for storage
std::string cached_value;
void process_message(const boost::json::object& obj) {
    cached_value = std::string(obj.at("method").as_string());
}
```

**2. Unnecessary std::format for Simple Cases**
```cpp
// BAD: Over-engineering simple concatenation
auto msg = std::format("Error: {}", error_str);

// GOOD: Use operator+ for simple cases
auto msg = "Error: " + error_str;
```

**3. Missing [[nodiscard]] on Important Returns**
```cpp
// BAD: Silently ignore important results
auto create_context() -> Task<std::string>;  // Missing [[nodiscard]]!

// GOOD: Enforce result checking
[[nodiscard]] auto create_context() -> Task<std::string>;
```

**4. std::expected for Async Operations**
```cpp
// BAD: Don't use std::expected for coroutine operations
auto navigate(std::string url) -> Task<std::expected<std::string, Error>>;

// GOOD: Use exceptions in coroutine context
auto navigate(std::string url) -> Task<std::string>;  // Throws on error
```

### Reference Documentation

- **C++17 Features**: `modern-cpp-features/CPP17.md`
- **C++20 Features**: `modern-cpp-features/CPP20.md`
- **Comprehensive Guide**: `modern-cpp-features/README.md`

## Using as a Library

**Note**: This section describes future integration patterns. Since the project is in development with no stable releases, API stability is not guaranteed.

### As CMake Subproject (Recommended for Dev)
```cmake
add_subdirectory(webdriverclientcpp)
target_link_libraries(myapp WebDriverClient::webdriverclientcpp)
```

### Via find_package (Future - After First Release)
```cmake
find_package(WebDriverClient REQUIRED)
target_link_libraries(myapp WebDriverClient::webdriverclientcpp)
```

### Feature Detection Macros
Available to library consumers:
- `WEBDRIVER_HAS_BOOST`: Boost.Asio/JSON available
- `WEBDRIVER_HAS_POCO`: Poco libraries available
- `WEBDRIVER_HAS_CONAN`: Built with Conan
- `WEBDRIVER_HAS_NLOHMANN_JSON`: nlohmann_json available

## Examples Location

Examples demonstrating various usage patterns:
- `examples/flow/example_bidi_flow_minimal.cpp`: Basic BiDi usage
- `examples/flow/example_bidi_flow_minimal_runner.cpp`: IoContextRunner for background threads
- `examples/flow/example_bidi_flow_end_to_end.cpp`: Complete workflow
- `examples/flow/example_bidi_flow_production.cpp`: Production-ready patterns
- `examples/flow/example_bidi_flow_retry.cpp`: Retry and timeout patterns
- `examples/flow/example_bidi_flow_script_exception.cpp`: Exception handling
- `examples/threading/example_bidi_threading_baseline.cpp`: Threading baseline
- `examples/threading/example_bidi_threading_optimized.cpp`: Optimized threading

## Test Coverage

Tests located in `tests/` directory:
- Command builders and parsing (positive/negative cases)
- Response vs event routing
- Pending entries, timeouts, cancellation
- Pool implementations (memory, buffer, timer, pending_entry + metrics)
- Subscription RAII (refcounting, move semantics)
- ID serialization safety (JS safe range)
- Retry/backoff and cancellation
- Script evaluation integration
- Wait helpers (zero busy-wait)

## Performance Optimizations

- **Pool-based allocation:** Amortized allocations, reduced fragmentation
- **Zero-copy where possible:** Reusable buffers via `buffer_pool_vec`
- **Fast-path parsing:** Avoid full DOM construction for routing decisions
- **Timer wheel:** Efficient timeout management via `timer_pool`
- **Metrics instrumentation:** `PoolMetrics` for capacity, reuse, failures
- **Backpressure handling:** Write queue prevents overwhelming the connection

## Git Workflow

**Current branch**: `dev_bidi` (active development)

**Development philosophy:**
- **No backward compatibility required**: This is a dev project with no production releases
- **Breaking changes allowed**: API can change without deprecation warnings
- **Refactoring encouraged**: Architecture improvements take priority over stability
- **Clean commits preferred**: Use standard build + test cycle before commits

**Never use:**
- Emojis in commit messages, branch names, or code
- Generic commit messages (be descriptive)
- Force push without explicit request

**Commit process:**
1. Verify all identifiers exist (use Grep)
2. Run `./format-code.sh` before committing
3. Build and test: `cmake --build . -j$(nproc) 2>&1 && ctest --output-on-failure -j$(nproc)`
4. Write clear commit message describing the change

## Understanding the Public API

### Task<T> and Direct co_await

**Quick Reference**: All async operations return `Task<T>` (alias for `asyncx::Async<T>`), which is **directly awaitable**. You can `co_await` Task objects without any conversion operator.

**Simple usage**:
```cpp
auto result = co_await client->navigate(ctx, url);
// Direct co_await - no extra syntax needed
```

**How it works**:
- `Task<T>` implements the C++20 awaiter interface (`await_ready`, `await_suspend`, `await_resume`)
- `co_await` directly uses this interface - no intermediate conversion needed
- Exceptions are properly propagated (e.g., `ScriptEvaluateException`)
- Integrates seamlessly with Boost.Asio coroutines

**Lazy evaluation with direct co_await**:
```cpp
// Phase 1: Lazy composition (no execution)
auto task = client->navigate(ctx, url)
    .map([](auto url) { return process(url); })
    .recover([](auto ec) { return fallback; });
// Type: Task<std::string>

// Phase 2: Execute via co_await (direct)
auto result = co_await task;
// Type: std::string
```

**Detailed documentation**: See Doxygen comments in `include/asyncx.hpp`

---

### Error Handling Model

The library uses **three error patterns** depending on context:

**1. Synchronous methods** → `std::expected<T, std::string>`
```cpp
auto ws_url = session.connect(args, "chrome", true);
if (!ws_url) {
    std::cerr << ws_url.error() << "\n";  // string error message
}
```

**2. Async operations (co_await)** → **Exceptions**
```cpp
try {
    auto result = co_await client->navigate(ctx, url);
} catch (const ScriptEvaluateException& e) {
    // Domain-specific exceptions preserved
    std::cerr << e.reason << ", line " << e.line_number << "\n";
} catch (const boost::system::system_error& e) {
    // Transport/timeout errors
    std::cerr << e.what() << "\n";
}
```

**3. Async recovery** → `std::error_code`
```cpp
operation.recover([](std::error_code ec) {
    if (ec == std::errc::timed_out) {
        return fallback_value;
    }
    throw; // propagate other errors
});
```

**Why three patterns?**
- `std::expected`: Matches C++23 style for simple operations
- Exceptions in coroutines: Idiomatic for Boost.Asio coroutines
- `error_code` in recovery: Lightweight for error handling chains

**Detailed documentation**: See Doxygen comments in `include/asyncx.hpp` and error handling examples in `examples/flow/`

---

### Lazy Evaluation Model

**All operations are lazy** until materialized by a terminal operation.

**Composition operators** (build chains without executing):
```cpp
auto task = client->evaluate("document.title", ctx)
    .map([](auto obj) { return obj.at("value").as_string(); })
    .recover([](auto ec) { return "fallback"; });
// Nothing executed yet!
```

**Available member operators**:
- `.map(fn)`: Transform success value
- `.and_then(fn)`: Chain another Task on success (fn must return Task<U>)
- `.recover(fn)`: Provide fallback value on error (fn must return T directly)
- `.on_error(fn)`: Side-effect tap on error (returns same Task<T>)
- `.finally(fn)`: Observe result without transforming

**Terminal operations** (trigger execution):
```cpp
// 1. co_await (coroutine context)
auto result = co_await task;

// 2. .finally() (callback style)
task.finally([](const auto& value, auto ec_opt, const auto&) {
    if (!ec_opt) {
        std::cout << *value << "\n";
    }
});

// 3. .as_future() (future-based)
auto future = task.as_future();

// 4. .get() (blocking - for scripts/tests)
auto result = task.get();  // BLOCKS until complete
```

**Detailed documentation**: See "Lazy Evaluation" section in `include/asyncx.hpp` (lines 13-24)

---

### Connection Workflow

**Two-phase connection** (HTTP handshake → WebSocket BiDi):

**Option 1: Manual** (full control):
```cpp
// Phase 1: HTTP handshake to get webSocketUrl
bidi::SessionGuard session("http://localhost:9515");
auto ws_url = session.connect(args, "chrome", true);

// Phase 2: BiDi WebSocket connection
auto client = co_await bidi::Client::connect(ioc, *ws_url);
bidi::ClientGuard guard(client);
```

**Option 2: ConnectionBuilder** (fluent, recommended):
```cpp
auto client = co_await bidi::connect_to("http://localhost:9515")
    .headless()
    .no_sandbox()
    .connect(ioc);
bidi::ClientGuard guard(client);
```

**Option 3: Direct WebSocket** (if you already have the URL):
```cpp
auto client = co_await bidi::Client::connect(ioc, "ws://localhost:9222/...");
```

**Implementation details**: See `include/bidi/connection_builder.hpp` and `include/bidi/guards.hpp`

---

### Common Patterns

**Pattern 1: Navigate and evaluate**
```cpp
auto ctx = co_await client->create_context();
co_await client->navigate(ctx, "https://example.com");
auto title_obj = co_await client->evaluate("document.title", ctx);
```

**Pattern 2: Retry with exponential backoff**
```cpp
using namespace bidi::helpers;

auto policy = RetryPolicy::exponential(3);  // 3 attempts
policy.base_delay = std::chrono::milliseconds{100};

auto result = co_await retry<std::string>(
    [client, ctx, expr]() {
        return client->evaluate(expr, ctx);
    },
    policy
);
```

**Pattern 3: Timeout with fallback**
```cpp
using namespace bidi::helpers;

auto value = co_await timeout_or(
    client->evaluate(expr, ctx),
    std::chrono::seconds{2},
    default_value
);
```

**Pattern 4: Timeout for critical operations (throws on timeout)**
```cpp
auto task = client->evaluate(expr, ctx);
auto result = co_await asyncx::timeout(
    task,
    io_context.get_executor(),
    std::chrono::seconds{5}
);
// Throws boost::system::system_error on timeout
```

**Pattern 5: Combined retry + timeout**
```cpp
using namespace bidi::helpers;

auto result = co_await retry<boost::json::object>(
    [client, ctx, expr, &ioc]() {
        auto task = client->evaluate(expr, ctx);
        return asyncx::timeout(task, ioc.get_executor(), std::chrono::seconds{5});
    },
    RetryPolicy::exponential(3)
);
```

**Pattern 6: Event subscription** (RAII cleanup):
```cpp
auto subscription = co_await client->subscribe(
    {bidi::ids::events::log_entryAdded},
    {context_id}
);
// Subscription auto-unsubscribes on destruction
```

**More patterns**: See comprehensive examples in `examples/flow/example_bidi_flow_retry.cpp` and `include/bidi/helpers.hpp`

---

### Helper Functions Reference

**bidi::helpers namespace** (`include/bidi/helpers.hpp`):

**retry<T>(factory, policy)** - Retry with exponential backoff:
```cpp
template <typename T, typename Factory>
auto retry(Factory attempt_factory,
           const RetryPolicy &policy = {}) -> bidi::Task<T>;

// Usage:
auto result = co_await bidi::helpers::retry<std::string>(
    [client, ctx]() { return client->create_context(); },
    bidi::helpers::RetryPolicy::exponential(3)
);
```

**timeout_or<T>(operation, duration, fallback)** - Timeout with fallback:
```cpp
template <typename T, typename Fallback>
auto timeout_or(bidi::Task<T> operation,
                std::chrono::milliseconds timeout_duration,
                Fallback fallback) -> bidi::Task<T>;

// Usage:
auto nav_result = co_await bidi::helpers::timeout_or(
    client->navigate(ctx, url),
    std::chrono::seconds{5},
    std::string("timeout")
);
```

**asyncx namespace free functions** (`include/asyncx.hpp`):

**timeout(async, executor, duration)** - Timeout that throws:
```cpp
template <class T, class Rep, class Per>
auto timeout(Async<T> async,
             net::any_io_executor ex,
             std::chrono::duration<Rep, Per> d) -> Async<T>;

// Usage:
auto task = client->evaluate(expr, ctx);
auto result = co_await asyncx::timeout(
    task,
    io_context.get_executor(),
    std::chrono::seconds{5}
);
// Throws boost::system::errc::timed_out on timeout
```

**Important**: asyncx does NOT provide member methods `.timeout()` or `.retry()`. These are **free functions** only.

---

### Threading Model

**Single event loop architecture**:
- One `boost::asio::io_context` with work guard
- Strand-based synchronization (no mutexes in BiDi core)
- One active `async_read` + one active `async_write` at any time
- Write queue for command serialization

**Client operations are non-blocking**:
```cpp
// These return immediately (lazy Task objects)
auto task1 = client->navigate(ctx, url);
auto task2 = client->evaluate(expr, ctx);

// Execution happens when materialized
co_await task1;
co_await task2;
```

**io_context lifecycle**:
```cpp
net::io_context ioc;
net::co_spawn(ioc, workflow(), net::detached);
ioc.run();  // Blocks until all work completes
```

**Detailed documentation**: See threading architecture in `include/bidi/core.hpp` and `include/bidi/threading.hpp`

---

## Non-Goals

- Not a complete UI automation DSL/framework
- No multiple event loops
- No busy-wait or sleep polling
- No `std::thread().detach()`
- No manual JSON string escaping

## Dependencies

**Required:**
- libcurl (HTTP handshake)
- Boost (system, thread, json; coroutine if available)

**Optional:**
- Poco (Foundation, JSON, Net, NetSSL, Crypto)
- nlohmann_json (header-only, experimental backend)
- GoogleTest (for tests)

## Compiler Support

Tested with:
- Clang 15+
- GCC (modern versions)
- MSVC (Windows, with `/permissive-` strict mode)

Sanitizers enabled in debug builds: address, undefined, leak

# log.entryAdded Event Implementation Guide

## Executive Summary

This document provides a complete architectural analysis and implementation roadmap for the `log.entryAdded` event in the WebDriverClient (bidi-x) project. The event is defined in the W3C WebDriver BiDi specification and allows clients to monitor console logs and JavaScript exceptions in real-time.

**Status**: Event infrastructure exists but needs LogEntry variant type completion and example documentation.

---

## 1. Current State Analysis

### ✅ What Already Exists

**Event Identifier** (Complete)
- Location: `include/bidi_methods.hpp:124`
- Identifier: `bidi::ids::events::log_entryAdded = "log.entryAdded"`
- Legacy alias: `bidi::ids::EV_LOG_ENTRY_ADDED`

**Core Types** (Partially Complete)
- Location: `include/bidi/types/log.hpp`
- ✅ `Level` enum (Debug, Info, Warn, Error) with bidirectional conversion
- ✅ `ConsoleLogEntry` struct (method, args, level, text, timestamp_ms, realm)
- ✅ `JavaScriptLogEntry` struct (level, text, timestamp_ms, realm, stack_trace)
- ✅ Boost.JSON serialization for `Level` enum
- ❌ **MISSING**: `LogEntry` variant type to combine ConsoleLogEntry | JavaScriptLogEntry
- ❌ **MISSING**: Boost.JSON serialization for ConsoleLogEntry and JavaScriptLogEntry

**Event Subscription Infrastructure** (Complete)
- `Client::set_event_handler()` - Available at `include/bidi/client.hpp:284`
- `BiDiSession::subscribe_event()` - Core subscription mechanism
- Example usage exists in `examples/flow/example_bidi_flow_end_to_end.cpp:108-111`

**Example Code** (Exists but minimal)
```cpp
// From example_bidi_flow_end_to_end.cpp:108-111
auto sub2_async = session->subscribe_event(
    ids::events::log_entryAdded, [](const ParsedEvent &event) {
        bidi::logging::log_info(std::string("Log event: ") +
                                event.method);
    });
```

### ❌ What's Missing

1. **LogEntry Variant Type**: W3C spec defines LogEntry as `ConsoleLogEntry | JavaScriptLogEntry`
2. **Complete Boost.JSON Integration**: Need serialization/deserialization for all log types
3. **Comprehensive Example**: Need dedicated example showing log filtering, parsing, and typed handling
4. **Tests**: No tests for log type parsing and event handling

---

## 2. Type System Review

### W3C WebDriver BiDi Specification

According to the W3C spec, the `log.entryAdded` event delivers:

```
log.EntryAdded = {
  type: "event",
  method: "log.entryAdded",
  params: log.Entry
}

log.Entry = log.GenericLogEntry / log.ConsoleLogEntry / log.JavaScriptLogEntry

log.GenericLogEntry = {
  level: log.Level,
  source: script.Source,
  text: text,
  timestamp: js-uint
}

log.ConsoleLogEntry = log.GenericLogEntry & {
  type: "console",
  method: text,
  args: [*script.RemoteValue]
}

log.JavaScriptLogEntry = log.GenericLogEntry & {
  type: "javascript",
  ?stackTrace: script.StackTrace
}
```

### Current Implementation Gap

**Current Code** (include/bidi/types/log.hpp:65-88):
```cpp
struct ConsoleLogEntry {
    std::string method;
    std::vector<boost::json::value> args; // Simplified
    Level level;
    std::string text;
    std::uint64_t timestamp_ms{0};
    std::optional<script::RealmInfo> realm;
};

struct JavaScriptLogEntry {
    Level level;
    std::string text;
    std::uint64_t timestamp_ms{0};
    std::optional<script::RealmInfo> realm;
    std::optional<std::string> stack_trace;
};
```

**Issues**:
1. Missing `type` discriminator field ("console" vs "javascript")
2. ConsoleLogEntry uses simplified `boost::json::value` instead of `script::RemoteValue`
3. No `LogEntry` variant combining both types
4. Missing `source` field (script::Source) in both types

### Recommended Type Architecture

Following project patterns from `include/bidi/types/browsing_context.hpp`, we should use `std::variant`:

```cpp
namespace bidi::types::log {

// Add type discriminator
struct ConsoleLogEntry {
    static constexpr std::string_view type = "console";
    std::string method;
    std::vector<script::RemoteValue> args; // Or boost::json::value as simplified
    Level level;
    std::string text;
    std::uint64_t timestamp_ms{0};
    script::Source source; // Required by spec
    std::optional<std::string> stack_trace;
};

struct JavaScriptLogEntry {
    static constexpr std::string_view type = "javascript";
    Level level;
    std::string text;
    std::uint64_t timestamp_ms{0};
    script::Source source; // Required by spec
    std::optional<std::string> stack_trace;
};

// Union type (W3C: ConsoleLogEntry | JavaScriptLogEntry)
using LogEntry = std::variant<ConsoleLogEntry, JavaScriptLogEntry>;

} // namespace bidi::types::log
```

**Alternative**: If `script::Source` is not yet implemented, keep current simplified version and document technical debt.

---

## 3. Event Subscription Pattern in bidi-x

### Architecture Overview

The bidi-x project uses a **strand-based event dispatching** system with RAII subscriptions:

**Flow**:
1. Client subscribes via `session->subscribe_event(method, handler)`
2. Subscription sends `session.subscribe` BiDi command to server
3. Server sends `log.entryAdded` events on the WebSocket
4. BiDi core fast-path parser routes events to registered handlers
5. Handler receives `ParsedEvent` with raw JSON params
6. Handler parses params into strongly-typed log entry

### Event Handler Signature

```cpp
using EventHandler = std::function<void(const core::ParsedEvent&)>;

struct ParsedEvent {
    std::string method;              // "log.entryAdded"
    boost::json::object params;      // Event parameters (raw JSON)
};
```

**Source Location Tracking**: All subscription methods accept `std::source_location` as the last parameter with a default argument that automatically captures the call site. This enables debugging of async operations (see CLAUDE.md "Debugging Async Operations with std::source_location"):

```cpp
[[nodiscard]] auto subscribe_event_async(
    std::string event_method,
    EventHandler handler,
    std::source_location loc = std::source_location::current()  // Auto-captured
) -> asyncx::Async<std::shared_ptr<Subscription>>;
```

The captured source location flows through the async chain and is available for GDB inspection during debugging, even when not actively used in code.

### Subscription Patterns

**Pattern 1: Raw Event Handler** (Current in examples)
```cpp
auto subscription = session->subscribe_event(
    ids::events::log_entryAdded,
    [](const ParsedEvent &event) {
        // Manual parsing required
        auto level = event.params.at("level").as_string();
        auto text = event.params.at("text").as_string();
        // ...
    }
);
```

**Pattern 2: Typed Handler** (Recommended)
```cpp
void on_log_entry(const types::log::LogEntry& entry) {
    std::visit([](const auto& log) {
        using T = std::decay_t<decltype(log)>;
        if constexpr (std::is_same_v<T, types::log::ConsoleLogEntry>) {
            // Handle console log
        } else if constexpr (std::is_same_v<T, types::log::JavaScriptLogEntry>) {
            // Handle JavaScript error
        }
    }, entry);
}

auto subscription = session->subscribe_event(
    ids::events::log_entryAdded,
    [](const ParsedEvent &event) {
        auto entry = boost::json::value_to<types::log::LogEntry>(
            event.params
        );
        on_log_entry(entry);
    }
);
```

### RAII Subscription Management

The project uses **automatic unsubscribe on destruction**:

```cpp
{
    auto subscription = co_await session->subscribe_event_async(
        ids::events::log_entryAdded, handler
    );
    // Event handler active
    // ...
} // Automatic unsubscribe when subscription goes out of scope
```

---

## 4. Implementation Roadmap

### Phase 1: Complete Type System ✓

**File**: `include/bidi/types/log.hpp`

**Tasks**:
1. ✅ Add `LogEntry` variant type
2. ✅ Add type discriminator fields
3. ✅ Implement Boost.JSON tag_invoke for ConsoleLogEntry
4. ✅ Implement Boost.JSON tag_invoke for JavaScriptLogEntry
5. ✅ Implement Boost.JSON tag_invoke for LogEntry variant
6. ⚠️ Consider adding `script::Source` field (check if type exists)

**Code Changes**:

```cpp
// At end of include/bidi/types/log.hpp after JavaScriptLogEntry definition

/**
 * @brief Union of console log entry or JavaScript log entry
 *
 * W3C Spec: log.Entry = log.ConsoleLogEntry | log.JavaScriptLogEntry
 */
using LogEntry = std::variant<ConsoleLogEntry, JavaScriptLogEntry>;

} // namespace bidi::types::log

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// ConsoleLogEntry serialization
inline void tag_invoke(value_from_tag, value &jv,
                       const bidi::types::log::ConsoleLogEntry &entry) {
    jv = {
        {"type", "console"},
        {"method", entry.method},
        {"args", value_from(entry.args)},
        {"level", bidi::types::log::to_string(entry.level)},
        {"text", entry.text},
        {"timestamp", entry.timestamp_ms}
    };
    if (entry.realm) {
        jv.as_object()["realm"] = value_from(*entry.realm);
    }
}

inline auto tag_invoke(value_to_tag<bidi::types::log::ConsoleLogEntry>,
                       const value &jv)
    -> bidi::types::log::ConsoleLogEntry {
    const auto &obj = jv.as_object();
    bidi::types::log::ConsoleLogEntry entry;

    entry.method = value_to<std::string>(obj.at("method"));
    entry.args = value_to<std::vector<boost::json::value>>(obj.at("args"));
    entry.level = value_to<bidi::types::log::Level>(obj.at("level"));
    entry.text = value_to<std::string>(obj.at("text"));
    entry.timestamp_ms = value_to<std::uint64_t>(obj.at("timestamp"));

    if (obj.contains("realm")) {
        entry.realm = value_to<bidi::types::script::RealmInfo>(obj.at("realm"));
    }

    return entry;
}

// JavaScriptLogEntry serialization
inline void tag_invoke(value_from_tag, value &jv,
                       const bidi::types::log::JavaScriptLogEntry &entry) {
    jv = {
        {"type", "javascript"},
        {"level", bidi::types::log::to_string(entry.level)},
        {"text", entry.text},
        {"timestamp", entry.timestamp_ms}
    };
    if (entry.realm) {
        jv.as_object()["realm"] = value_from(*entry.realm);
    }
    if (entry.stack_trace) {
        jv.as_object()["stackTrace"] = *entry.stack_trace;
    }
}

inline auto tag_invoke(value_to_tag<bidi::types::log::JavaScriptLogEntry>,
                       const value &jv)
    -> bidi::types::log::JavaScriptLogEntry {
    const auto &obj = jv.as_object();
    bidi::types::log::JavaScriptLogEntry entry;

    entry.level = value_to<bidi::types::log::Level>(obj.at("level"));
    entry.text = value_to<std::string>(obj.at("text"));
    entry.timestamp_ms = value_to<std::uint64_t>(obj.at("timestamp"));

    if (obj.contains("realm")) {
        entry.realm = value_to<bidi::types::script::RealmInfo>(obj.at("realm"));
    }
    if (obj.contains("stackTrace")) {
        entry.stack_trace = value_to<std::string>(obj.at("stackTrace"));
    }

    return entry;
}

// LogEntry variant serialization
inline void tag_invoke(value_from_tag, value &jv,
                       const bidi::types::log::LogEntry &entry) {
    std::visit([&jv](const auto &e) { jv = value_from(e); }, entry);
}

inline auto tag_invoke(value_to_tag<bidi::types::log::LogEntry>,
                       const value &jv)
    -> bidi::types::log::LogEntry {
    const auto &obj = jv.as_object();
    auto type = value_to<std::string_view>(obj.at("type"));

    if (type == "console") {
        return value_to<bidi::types::log::ConsoleLogEntry>(jv);
    }
    if (type == "javascript") {
        return value_to<bidi::types::log::JavaScriptLogEntry>(jv);
    }

    throw std::runtime_error("Invalid log entry type");
}

} // namespace boost::json
```

### Phase 2: Create Comprehensive Example

**File**: `examples/flow/example_log_monitoring.cpp`

**Features**:
- Subscribe to log.entryAdded
- Filter by log level (debug, info, warn, error)
- Distinguish console logs vs JavaScript errors
- Pretty-print log entries with timestamps
- Demonstrate RAII subscription cleanup

**Example Structure**:

```cpp
// examples/flow/example_log_monitoring.cpp
#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include "bidi/types/log.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <format>
#include <memory>

namespace asio = boost::asio;
using namespace bidi::types::log;

class LogMonitoringDemo {
private:
    asio::io_context io_context_;
    std::string websocket_url_;

    // Statistics
    std::atomic<int> console_logs_{0};
    std::atomic<int> js_errors_{0};
    std::atomic<int> warnings_{0};
    std::atomic<int> errors_{0};

    // Log entry handler
    void handle_log_entry(const LogEntry& entry) {
        std::visit([this](const auto& log) {
            using T = std::decay_t<decltype(log)>;

            if constexpr (std::is_same_v<T, ConsoleLogEntry>) {
                handle_console_log(log);
            } else if constexpr (std::is_same_v<T, JavaScriptLogEntry>) {
                handle_javascript_error(log);
            }
        }, entry);
    }

    void handle_console_log(const ConsoleLogEntry& log) {
        console_logs_.fetch_add(1);

        auto level_str = to_string(log.level);
        auto timestamp = std::chrono::milliseconds{log.timestamp_ms};

        bidi::logging::log_info(std::format(
            "[CONSOLE] [{}] {} - {} (method: {})",
            level_str, timestamp.count(), log.text, log.method
        ));

        if (log.level == Level::Warn) warnings_.fetch_add(1);
        if (log.level == Level::Error) errors_.fetch_add(1);
    }

    void handle_javascript_error(const JavaScriptLogEntry& log) {
        js_errors_.fetch_add(1);

        auto level_str = to_string(log.level);
        auto timestamp = std::chrono::milliseconds{log.timestamp_ms};

        bidi::logging::log_error(std::format(
            "[JAVASCRIPT] [{}] {} - {}",
            level_str, timestamp.count(), log.text
        ));

        if (log.stack_trace) {
            bidi::logging::log_error(
                std::format("Stack trace: {}", *log.stack_trace)
            );
        }
    }

public:
    auto initialize_session() -> bool {
        try {
            bidi::SessionGuard guard("http://localhost:9515");
            WebDriver::json args = WebDriver::json::array({
                "--headless", "--no-sandbox"
            });
            auto ws_url = guard.connect(args, "chrome", true);
            if (!ws_url) {
                bidi::logging::log_error(ws_url.error());
                return false;
            }
            websocket_url_ = *ws_url;
            return true;
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("Session init failed: {}", e.what())
            );
            return false;
        }
    }

    auto run_monitoring() -> asio::awaitable<int> {
        using namespace bidi::logging;

        log_info("Connecting BiDi Client");
        auto client = co_await bidi::Client::connect(
            io_context_, websocket_url_
        );
        if (!client) {
            log_error("BiDi connect failed");
            co_return 1;
        }
        bidi::ClientGuard client_guard(client);

        // Subscribe to log.entryAdded
        // Note: std::source_location is automatically captured from call site
        // via default argument (see core.hpp:308-310)
        log_info("Subscribing to log.entryAdded events");
        auto session = client->session();
        auto subscription = co_await session->subscribe_event_async(
            bidi::ids::events::log_entryAdded,
            [this](const bidi::core::ParsedEvent &event) {
                try {
                    auto entry = boost::json::value_to<LogEntry>(
                        event.params
                    );
                    handle_log_entry(entry);
                } catch (const std::exception &e) {
                    bidi::logging::log_error(std::format(
                        "Failed to parse log entry: {}", e.what()
                    ));
                }
            }
            // std::source_location loc = std::source_location::current() (default)
        );

        // Create context and trigger various log events
        auto ctx = co_await client->create_context();
        log_info(std::format("Created context: {}", ctx));

        co_await client->navigate(ctx, "https://example.com");

        // Trigger console logs
        co_await client->evaluate("console.log('Hello from monitoring demo')", ctx);
        co_await client->evaluate("console.warn('This is a warning')", ctx);
        co_await client->evaluate("console.error('This is an error')", ctx);
        co_await client->evaluate("console.info('Information message')", ctx);

        // Trigger JavaScript error
        try {
            co_await client->evaluate("throw new Error('Test error')", ctx);
        } catch (...) {
            // Expected
        }

        // Wait for events to be processed
        asio::steady_timer timer(io_context_.get_executor());
        timer.expires_after(std::chrono::seconds{2});
        co_await timer.async_wait(asio::use_awaitable);

        // Print statistics
        log_info("=== Log Monitoring Statistics ===");
        log_info(std::format("Console logs: {}", console_logs_.load()));
        log_info(std::format("JavaScript errors: {}", js_errors_.load()));
        log_info(std::format("Warnings: {}", warnings_.load()));
        log_info(std::format("Errors: {}", errors_.load()));

        co_return 0;
    }

    auto run() -> int {
        if (!initialize_session()) {
            return 1;
        }

        auto fut = asio::co_spawn(
            io_context_, run_monitoring(), asio::use_future
        );
        io_context_.run();

        try {
            return fut.get();
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("Monitoring failed: {}", e.what())
            );
            return 1;
        }
    }
};

auto main() -> int {
    bidi::logging::log_info("Log Monitoring Demo");
    try {
        LogMonitoringDemo demo;
        return demo.run();
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::format("Demo failed: {}", e.what())
        );
        return 1;
    }
}
```

### Phase 3: Add Tests

**File**: `tests/bidi_log_types_test.cpp`

**Test Coverage**:
1. Level enum conversion (to_string/parse_level)
2. ConsoleLogEntry JSON round-trip
3. JavaScriptLogEntry JSON round-trip
4. LogEntry variant serialization
5. Type discriminator parsing
6. Invalid type handling

### Phase 4: Update Documentation

**Files**:
- Update `W3C_IMPLEMENTATION_GUIDE.md` to mark log.entryAdded as ✅ IMPLEMENTED
- Update `CLAUDE.md` examples section to reference log monitoring example
- Add docstring comments to log types

---

## 5. Architectural Recommendations

### C++ Core Guidelines Compliance

✅ **F.51: Prefer default arguments over overloading**
- Current design uses optional fields in structs (good)

✅ **C.131: Avoid trivial getters and setters**
- Direct struct field access (good)

✅ **Enum.3: Prefer class enums over plain enums**
- Using `enum class Level` (good)

✅ **ES.23: Prefer the {} initializer syntax**
- Struct initialization uses aggregate initialization (good)

### Boost.Asio Best Practices

✅ **Strand serialization**
- Event handlers already run on strand (good)

✅ **No blocking in handlers**
- Current example doesn't block (good)

⚠️ **Exception safety in handlers**
- Recommendation: Wrap handler logic in try-catch

### Zero-Cost Abstractions

✅ **std::variant for type-safe unions**
- Matches project pattern from browsing_context types

✅ **constexpr string_view for type discriminators**
- No runtime overhead for type field

✅ **[[nodiscard]] on conversion functions**
- Already used on to_string/parse_level

### Type Safety Improvements

**Current Issue**: `ConsoleLogEntry.args` uses `std::vector<boost::json::value>` instead of `std::vector<script::RemoteValue>`

**Recommendation**:
1. Keep simplified version for now (technical debt)
2. Add TODO comment referencing W3C spec requirement
3. Plan migration when script::RemoteValue serialization is fully tested

```cpp
struct ConsoleLogEntry {
    // TODO: Migrate to std::vector<script::RemoteValue> per W3C spec
    // Currently simplified to avoid dependency on incomplete RemoteValue serialization
    std::vector<boost::json::value> args;
    // ...
};
```

### RAII Pattern Enforcement

**Recommendation**: Add helper function for typed subscription:

```cpp
// In include/bidi/client.hpp or dedicated header
template<typename EventType>
auto subscribe_typed_event(
    std::shared_ptr<core::BiDiSession> session,
    std::string_view event_method,
    std::function<void(const EventType&)> handler,
    std::source_location loc = std::source_location::current()
) -> asyncx::Async<std::shared_ptr<Subscription>> {
    return session->subscribe_event_async(
        event_method,
        [handler = std::move(handler)](const core::ParsedEvent& event) {
            try {
                auto typed_event = boost::json::value_to<EventType>(
                    event.params
                );
                handler(typed_event);
            } catch (const std::exception& e) {
                bidi::logging::log_error(std::format(
                    "Failed to parse event {}: {}",
                    event.method, e.what()
                ));
            }
        },
        loc
    );
}

// Usage:
auto sub = co_await subscribe_typed_event<types::log::LogEntry>(
    session,
    ids::events::log_entryAdded,
    [](const types::log::LogEntry& entry) {
        // Strongly-typed handler
    }
);
```

---

## 6. Summary

### Current Gaps

| Component | Status | Priority |
|-----------|--------|----------|
| LogEntry variant type | ❌ Missing | P0 |
| Boost.JSON serialization | ❌ Missing | P0 |
| Comprehensive example | ❌ Missing | P1 |
| Unit tests | ❌ Missing | P1 |
| Documentation | ⚠️ Partial | P2 |

### Implementation Checklist

- [ ] Add `LogEntry` variant type to `include/bidi/types/log.hpp`
- [ ] Implement Boost.JSON tag_invoke for ConsoleLogEntry
- [ ] Implement Boost.JSON tag_invoke for JavaScriptLogEntry
- [ ] Implement Boost.JSON tag_invoke for LogEntry variant
- [ ] Create `examples/flow/example_log_monitoring.cpp`
  - [ ] Ensure all public APIs use `std::source_location` parameter
  - [ ] Add comments documenting auto-capture behavior
- [ ] Add CMakeLists.txt entry for new example
- [ ] Create `tests/bidi_log_types_test.cpp`
- [ ] Run full test suite and verify no regressions
- [ ] Update `W3C_IMPLEMENTATION_GUIDE.md`
- [ ] Run `./format-code.sh` before committing

### Estimated Effort

- Type system completion: 1-2 hours
- Example implementation: 2-3 hours
- Tests: 1-2 hours
- Documentation: 30 minutes
- **Total**: ~5-8 hours

### Risk Assessment

**Low Risk**:
- Event infrastructure already exists and is battle-tested
- Pattern well-established in codebase (see browsing_context types)
- No breaking changes required

**Potential Issues**:
- Dependency on `script::RemoteValue` serialization (mitigation: use simplified args for now)
- Timestamp format (spec uses js-uint, implementation uses timestamp_ms)

---

## References

- W3C WebDriver BiDi Spec: https://w3c.github.io/webdriver-bidi/#module-log
- Existing example: `examples/flow/example_bidi_flow_end_to_end.cpp`
- Type pattern reference: `include/bidi/types/browsing_context.hpp`
- Event IDs: `include/bidi_methods.hpp`

# User Prompt Handler Implementation Review

**Review Date**: 2025-10-12
**Reviewer**: Claude Code (C++ Core Guidelines Compliance Agent)
**Files Reviewed**:
- `src/bidi_user_prompt_handler.cpp` (305 lines)
- `include/bidi/user_prompt_handler.hpp` (335 lines)

**Architecture References**:
- `USER_PROMPT_HANDLER_ARCHITECTURE.md` (design specification)
- `W3C_IMPLEMENTATION_GUIDE.md` (implementation guide)

---

## Executive Summary

**Overall Compliance Score: 6.5/10**

The user prompt handler implementation demonstrates good understanding of RAII patterns and policy-based design, but contains several critical issues and incomplete implementations. The foundation is solid, but significant work remains before production readiness.

**Status**: Phase 1 (Foundation) - **Partially Complete** (65%)

---

## Critical Issues (P0 - Must Fix)

### Issue 1: Incorrect `.finally()` Callback Signature
**File**: `src/bidi_user_prompt_handler.cpp:291-300`
**Severity**: P0 - Critical (Compilation Error for void tasks)
**Guideline**: Project asyncx API contract

**Problem**:
```cpp
// Line 291-300 - INCORRECT
task.finally([](const std::optional<boost::system::error_code> &error,
                const std::exception_ptr &eptr) {
    // This signature is wrong!
});
```

The callback expects a value parameter for `Task<void>`, but void tasks don't provide one.

**Correct Signature** (from `include/asyncx.hpp`):
```cpp
// For Task<void>, finally() callback signature is:
// void(std::optional<std::error_code>, std::exception_ptr)
// NOT: void(std::optional<T>, std::optional<std::error_code>, std::exception_ptr)

task.finally([](const std::optional<boost::system::error_code> &ec,
                const std::exception_ptr &eptr) {
    if (ec) {
        // Log error: ec->message()
    }
    if (eptr) {
        // Log exception
    }
});
```

**Fix**:
```cpp
// Line 288-301 in src/bidi_user_prompt_handler.cpp
if (resolution && client_) {
    auto task = client_->handle_user_prompt(
        params.context, resolution->accept, resolution->user_text);
    // Fire-and-forget: materialize task with .finally() terminal
    task.finally([](const std::optional<boost::system::error_code> &ec,
                    const std::exception_ptr &eptr) {
        // TODO: Use project logging system (bidi::logging::log_error)
        if (ec) {
            logging::log_error(std::format(
                "UserPromptHandler: Failed to handle prompt: {}",
                ec->message()));
        }
        if (eptr) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception &e) {
                logging::log_error(std::format(
                    "UserPromptHandler: Exception handling prompt: {}",
                    e.what()));
            }
        }
    });
}
```

---

### Issue 2: Inconsistent Constructor Declaration
**File**: `include/bidi/user_prompt_handler.hpp:250`
**Severity**: P0 - Critical (Breaks C.45 factory pattern)
**Guideline**: C.45 - Prefer factory functions for non-trivial initialization

**Problem**:
```cpp
// Line 250 - PUBLIC DEFAULT CONSTRUCTOR (WRONG!)
UserPromptHandler() = default;
```

This violates the factory method pattern. The class has a private constructor (line 306-308), so the public default constructor undermines the design.

**Analysis**:
- Line 238-241: Factory method `create()` returns `Task<shared_ptr<UserPromptHandler>>`
- Line 306-308: Private constructor takes (client, config, subscription)
- Line 250: Public default constructor breaks encapsulation

**Fix**:
```cpp
// Remove line 250 entirely, OR make it private if needed for move semantics:

private:
    UserPromptHandler() = default;  // Only if needed internally
```

**Recommended**: Delete the default constructor entirely since the private constructor provides all necessary initialization.

---

### Issue 3: Ignored Task in Event Handler Registration
**File**: `src/bidi_user_prompt_handler.cpp:203-223`
**Severity**: P0 - Critical (Silent failure)
**Guideline**: Project lazy evaluation model

**Problem**:
```cpp
// Line 203-223
auto set_opened = client->set_event_handler(
    "browsingContext.userPromptOpened",
    [weak_handler](const boost::json::object &params) { /* ... */ });

// We don't co_await set_opened here; ignore result
// (fire-and-forget)

// Complete task with shared_ptr
out.fulfill(std::move(handler_ptr));
```

The `set_event_handler` returns `awaitable<void>`, but it's never materialized. This means the event handler is **never actually registered**.

**Correct Pattern** (from project examples):
```cpp
// Option 1: co_await in coroutine context
co_await client->set_event_handler(method, handler);

// Option 2: Use boost::asio::co_spawn if not in coroutine
boost::asio::co_spawn(
    client->get_executor(),
    client->set_event_handler(method, handler),
    boost::asio::detached
);
```

**Fix**:
```cpp
// Line 199-230 - REVISED
try {
    auto handler_ptr = std::make_shared<UserPromptHandler>(
        client, config, std::move(subscription));
    auto weak_handler = std::weak_ptr<UserPromptHandler>(handler_ptr);

    // Register event handler - MUST materialize awaitable
    boost::asio::co_spawn(
        client->get_executor(),
        [client, weak_handler]() -> boost::asio::awaitable<void> {
            co_await client->set_event_handler(
                "browsingContext.userPromptOpened",
                [weak_handler](const boost::json::object &params) {
                    if (auto handler = weak_handler.lock()) {
                        try {
                            auto parsed = from_json_prompt_opened(params);
                            boost::asio::post(
                                handler->client_->get_executor(),
                                [handler, parsed]() mutable {
                                    handler->handle_prompt(parsed);
                                });
                        } catch (...) {
                            // Intentionally ignore parse errors
                        }
                    }
                });
        }(),
        [out, handler_ptr](std::exception_ptr eptr) mutable {
            if (eptr) {
                out.fail(eptr);
            } else {
                out.fulfill(std::move(handler_ptr));
            }
        }
    );
} catch (...) {
    out.fail(std::current_exception());
}
```

---

### Issue 4: Missing userPromptClosed Event Handler
**File**: `src/bidi_user_prompt_handler.cpp` (entire file)
**Severity**: P1 - High (Incomplete implementation)
**Guideline**: W3C BiDi spec completeness

**Problem**:
The architecture document (USER_PROMPT_HANDLER_ARCHITECTURE.md) specifies subscription to **two events**:
1. `browsingContext.userPromptOpened` ✓ (implemented)
2. `browsingContext.userPromptClosed` ✗ (missing)

**Current Implementation** (line 162-164):
```cpp
std::vector<std::string> events{
    std::string("browsingContext.userPromptOpened"),
    std::string("browsingContext.userPromptClosed")};  // Subscribed but no handler!
```

**Impact**:
- Subscription exists but handler is missing
- Cannot observe prompt resolution results
- Cannot implement custom logging/telemetry for closed prompts

**Fix**:
Add handler registration in `create()` method:

```cpp
// After userPromptOpened handler registration (around line 220)

// Register closed handler
boost::asio::co_spawn(
    client->get_executor(),
    [client, weak_handler]() -> boost::asio::awaitable<void> {
        co_await client->set_event_handler(
            "browsingContext.userPromptClosed",
            [weak_handler](const boost::json::object &params) {
                if (auto handler = weak_handler.lock()) {
                    try {
                        auto parsed = from_json_prompt_closed(params);
                        // Optional: Log or emit metrics
                        boost::asio::post(
                            handler->client_->get_executor(),
                            [handler, parsed]() mutable {
                                handler->on_prompt_closed(parsed);
                            });
                    } catch (...) {
                        // Intentionally ignore parse errors
                    }
                }
            });
    }(),
    boost::asio::detached
);
```

Add callback to header:
```cpp
// include/bidi/user_prompt_handler.hpp (private section)
private:
    void on_prompt_closed(
        const types::browsing_context::UserPromptClosedParameters &params);
```

---

## High Priority Issues (P1)

### Issue 5: Missing Client Integration Methods
**Files**: `include/bidi/client.hpp`, `src/bidi_client.cpp`
**Severity**: P1 - High (API incompleteness)
**Guideline**: USER_PROMPT_HANDLER_ARCHITECTURE.md Phase 3

**Problem**:
Architecture document specifies Client convenience methods (Phase 4), but none are implemented:

**Missing Methods** (from architecture doc):
```cpp
// High-level: Create RAII prompt handler
[[nodiscard]] auto create_prompt_handler(
    UserPromptHandlerConfig config = UserPromptHandlerConfig::accept_all())
    -> Task<std::shared_ptr<UserPromptHandler>>;

// Event subscription helpers (strongly-typed)
[[nodiscard]] auto on_prompt_opened(
    std::function<void(const types::browsing_context::UserPromptOpenedParameters&)> callback)
    -> Task<Subscription>;

[[nodiscard]] auto on_prompt_closed(
    std::function<void(const types::browsing_context::UserPromptClosedParameters&)> callback)
    -> Task<Subscription>;
```

**Implementation Plan**:
1. Add declarations to `include/bidi/client.hpp` after line 89 (after `handle_user_prompt`)
2. Implement in `src/bidi_client.cpp`
3. Forward to `UserPromptHandler::create()` for factory method
4. Use existing `subscribe()` + `set_event_handler()` for typed callbacks

---

### Issue 6: Incomplete Error Handling in JSON Conversion
**File**: `src/bidi_user_prompt_handler.cpp:24-121`
**Severity**: P1 - High (Robustness)
**Guideline**: ES.70 - Prefer switch to if when there is a choice

**Problem**:
JSON conversion functions use generic `std::runtime_error` without context.

**Current** (line 31-33):
```cpp
if (!context_ptr || !context_ptr->is_string()) {
    throw std::runtime_error(
        "UserPromptOpenedParameters: missing or invalid 'context' field");
}
```

**Better Error Context**:
```cpp
if (!context_ptr || !context_ptr->is_string()) {
    throw std::runtime_error(std::format(
        "UserPromptOpenedParameters: missing or invalid 'context' field. "
        "Received JSON: {}",
        boost::json::serialize(obj)));
}
```

**Recommendation**:
- Add full JSON to error message for debugging
- Consider custom exception type: `UserPromptParseException`
- Add validation for enum conversion failures

---

### Issue 7: Missing [[nodiscard]] on Helper Functions
**File**: `include/bidi/user_prompt_handler.hpp:160-175`
**Severity**: P1 - High (API safety)
**Guideline**: F.20 - Use [[nodiscard]] for pure functions

**Problem**:
Factory methods have [[nodiscard]], but helper functions don't:

```cpp
// Line 160-161 - CORRECT
[[nodiscard]] auto from_json_prompt_opened(const boost::json::object &obj)
    -> types::browsing_context::UserPromptOpenedParameters;

// Line 174-175 - CORRECT
[[nodiscard]] auto from_json_prompt_closed(const boost::json::object &obj)
    -> types::browsing_context::UserPromptClosedParameters;
```

Actually, these ARE marked with [[nodiscard]]. **This issue is NOT present**. ✓

---

## Medium Priority Issues (P2)

### Issue 8: Inconsistent String Construction
**File**: `src/bidi_user_prompt_handler.cpp:34, 51, 67, 94, 117`
**Severity**: P2 - Medium (Performance)
**Guideline**: ES.11 - Use auto to avoid redundant type names

**Problem**:
Explicit `std::string()` construction from `c_str()`:

```cpp
// Line 34
params.context = std::string(context_ptr->as_string().c_str());
```

**Better**:
```cpp
// Boost.JSON string_view is null-terminated, direct construction works
params.context = std::string(context_ptr->as_string());

// Or even better, avoid copy if possible:
auto sv = context_ptr->as_string();
params.context = std::string(sv.data(), sv.size());
```

**Analysis**:
Using `.c_str()` is redundant. Boost.JSON's `string` type provides implicit conversion to `std::string_view`, which can construct `std::string` directly.

**Impact**: Minor performance overhead (extra copy), but not critical.

---

### Issue 9: Missing Logging Integration
**File**: `src/bidi_user_prompt_handler.cpp:294-299`
**Severity**: P2 - Medium (Observability)
**Guideline**: Project logging patterns

**Problem**:
TODOs for logging but no implementation:

```cpp
// TODO: Log errors if needed (use project logging system)
if (error) {
    // Log error
}
if (eptr) {
    // Log exception
}
```

**Fix**:
```cpp
#include "bidi/logging.hpp"

// In handle_prompt():
task.finally([](const std::optional<boost::system::error_code> &ec,
                const std::exception_ptr &eptr) {
    if (ec) {
        logging::log_error(std::format(
            "UserPromptHandler: Failed to send handleUserPrompt: {}",
            ec->message()));
    }
    if (eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception &e) {
            logging::log_error(std::format(
                "UserPromptHandler: Exception in handleUserPrompt: {}",
                e.what()));
        }
    }
});
```

---

### Issue 10: Potential Race in set_policy()
**File**: `include/bidi/user_prompt_handler.hpp:285-287`
**Severity**: P2 - Medium (Thread safety)
**Guideline**: CP.20 - Use RAII for mutex management

**Problem**:
```cpp
void set_policy(UserPromptHandlerConfig new_config) {
    config_ = std::move(new_config);
}
```

**Analysis**:
- Comment says "Thread-safe: config updates are serialized via strand"
- But this method doesn't post to strand!
- Could race with `handle_prompt()` reading `config_`

**Fix** (if concurrent updates are needed):
```cpp
void set_policy(UserPromptHandlerConfig new_config) {
    boost::asio::post(client_->get_executor(), [this, cfg = std::move(new_config)]() mutable {
        config_ = std::move(cfg);
    });
}
```

**Alternative**: Document that `set_policy()` must be called from strand context only.

---

## Low Priority Issues (P3)

### Issue 11: Verbose Event String Construction
**File**: `src/bidi_user_prompt_handler.cpp:162-164`
**Severity**: P3 - Low (Style)
**Guideline**: ES.11 - Use auto

**Problem**:
```cpp
std::vector<std::string> events{
    std::string("browsingContext.userPromptOpened"),
    std::string("browsingContext.userPromptClosed")};
```

**Better** (use constants from `bidi::ids`):
```cpp
#include "bidi/ids.hpp"

std::vector<std::string> events{
    bidi::ids::events::browsing_context_userPromptOpened,
    bidi::ids::events::browsing_context_userPromptClosed
};
```

**Check** if these constants exist in `include/bidi_methods.hpp` or `include/bidi/ids.hpp`.

---

### Issue 12: Missing Move Constructor Implementation
**File**: `include/bidi/user_prompt_handler.hpp:255-261`
**Severity**: P3 - Low (Completeness)
**Guideline**: C.21 - Define or delete special members

**Problem**:
Move constructor/assignment are defaulted, which is fine, but `subscription_` member needs verification:

```cpp
UserPromptHandler(UserPromptHandler &&) noexcept = default;
auto operator=(UserPromptHandler &&) noexcept -> UserPromptHandler & = default;
```

**Verification Needed**:
- Is `Client::Subscription` correctly movable?
- Does it have `noexcept` move operations?

**From `include/bidi/client.hpp:129-130`**:
```cpp
Subscription(Subscription &&other) noexcept;
auto operator=(Subscription &&other) noexcept -> Subscription &;
```

✓ **Verified**: Subscription is move-only with noexcept moves. No issue here.

---

## Missing Implementations (Gaps vs Architecture)

### Gap 1: Integration Tests
**Reference**: USER_PROMPT_HANDLER_ARCHITECTURE.md, Section "Testing Requirements"
**Status**: Missing
**Priority**: P1 - High

**Required Tests**:
1. `tests/user_prompt_policy_test.cpp` - Policy dispatch tests ✓ (exists)
2. `tests/user_prompt_handler_test.cpp` - Integration tests ✗ (missing)

**Missing Test Coverage**:
- RAII subscription cleanup
- Move semantics
- Concurrent prompt handling
- Policy updates at runtime
- Fire-and-forget pattern verification
- Weak_ptr expiration handling

---

### Gap 2: Example Code
**Reference**: USER_PROMPT_HANDLER_ARCHITECTURE.md, Phase 4
**Status**: Missing
**Priority**: P2 - Medium

**Required**:
- `examples/flow/example_user_prompts.cpp` - Comprehensive usage examples

**Content Should Include**:
- Accept all prompts automatically
- Custom logic per prompt type
- Manual event handling
- Low-level command usage

---

### Gap 3: Documentation Updates
**Reference**: USER_PROMPT_HANDLER_ARCHITECTURE.md, Phase 4
**Status**: Missing
**Priority**: P2 - Medium

**Required**:
- Update `CLAUDE.md` with UserPromptHandler API
- Add user prompt examples to public API section
- Document fire-and-forget patterns

---

## Positive Findings ✓

### Strength 1: Excellent RAII Pattern
The UserPromptHandler follows project RAII patterns correctly:
- Move-only semantics (C.21) ✓
- Private constructor + public factory (C.45) ✓
- Automatic cleanup via `Client::Subscription` member ✓

### Strength 2: Zero-Cost Policy Dispatch
Policy dispatch uses switch statement (ES.28), avoiding virtual call overhead:
```cpp
// Line 268-284 - EXCELLENT
auto resolution = [&]() -> std::optional<UserPromptResolution> {
    switch (config_.policy) {
    case UserPromptPolicy::AcceptAll:
        return UserPromptResolution{.accept = true, .user_text = std::nullopt};
    // ...
    }
}();
```

### Strength 3: Comprehensive Documentation
Header comments are thorough with:
- Architectural rationale
- C++ Core Guideline references
- Usage examples
- Doxygen tags

### Strength 4: Type Safety
Uses strongly-typed structs instead of raw JSON:
- `UserPromptOpenedParameters` ✓
- `UserPromptClosedParameters` ✓
- `UserPromptResolution` ✓

### Strength 5: Defensive Parsing
JSON conversion functions validate all required fields before access.

---

## Recommendations (Prioritized)

### Critical Actions (Do Immediately)

1. **Fix Issue #1**: Correct `.finally()` callback signature for void tasks
   - File: `src/bidi_user_prompt_handler.cpp:291-300`
   - Impact: Potential runtime error

2. **Fix Issue #2**: Remove public default constructor
   - File: `include/bidi/user_prompt_handler.hpp:250`
   - Impact: Breaks C.45 factory pattern

3. **Fix Issue #3**: Materialize `set_event_handler` awaitable
   - File: `src/bidi_user_prompt_handler.cpp:203-230`
   - Impact: Event handler never registered (silent failure)

4. **Fix Issue #4**: Implement `userPromptClosed` handler
   - File: `src/bidi_user_prompt_handler.cpp`
   - Impact: Incomplete W3C BiDi support

### High Priority (Next Iteration)

5. **Add Client integration methods** (Issue #5)
   - Files: `include/bidi/client.hpp`, `src/bidi_client.cpp`
   - Methods: `create_prompt_handler()`, `on_prompt_opened()`, `on_prompt_closed()`

6. **Write integration tests** (Gap #1)
   - File: `tests/user_prompt_handler_test.cpp`
   - Coverage: RAII, move semantics, concurrent handling

7. **Add error context to JSON parsing** (Issue #6)
   - File: `src/bidi_user_prompt_handler.cpp:24-121`
   - Enhancement: Include full JSON in error messages

### Medium Priority (Follow-up)

8. **Implement logging** (Issue #9)
   - File: `src/bidi_user_prompt_handler.cpp:294-299`
   - Use: `bidi::logging::log_error()`

9. **Fix set_policy() thread safety** (Issue #10)
   - File: `include/bidi/user_prompt_handler.hpp:285-287`
   - Solution: Post to executor or document strand requirement

10. **Create example code** (Gap #2)
    - File: `examples/flow/example_user_prompts.cpp`

### Low Priority (Nice to Have)

11. **Use event constants from bidi::ids** (Issue #11)
12. **Optimize string construction** (Issue #8)
13. **Update documentation** (Gap #3)

---

## Conclusion

The user prompt handler implementation demonstrates solid understanding of modern C++ and project patterns, but contains **4 critical bugs** that must be fixed before the code is functional:

1. Incorrect `.finally()` signature for void tasks
2. Public default constructor breaks factory pattern
3. Event handler never registered (silent failure)
4. Missing `userPromptClosed` handler

**Estimated Effort to Phase 1 Completion**: 4-6 hours
- Fix critical issues: 2 hours
- Add integration tests: 2 hours
- Code review + QA: 2 hours

**Next Milestone**: Phase 2 (Client Integration) - Add convenience methods to Client class

**Recommendation**: **Do not merge** until Issues #1-4 are resolved and integration tests pass.

---

## Appendix: Pattern Comparison

### Pattern: RAII Guard (Comparison with guards.hpp)

**SessionGuard** (reference implementation):
```cpp
class SessionGuard {
private:
    WebDriver driver_;
    std::string session_id_;
    bool connected_{false};
public:
    explicit SessionGuard(std::string url);  // No default constructor
    ~SessionGuard() noexcept;
    // Move-only
    SessionGuard(SessionGuard &&) noexcept = default;
    // Deleted copy
    SessionGuard(const SessionGuard &) = delete;
};
```

**UserPromptHandler** (current implementation):
```cpp
class UserPromptHandler {
private:
    std::shared_ptr<Client> client_;
    UserPromptHandlerConfig config_{};
    Client::Subscription subscription_;
public:
    UserPromptHandler() = default;  // ❌ SHOULD NOT EXIST
    // ... rest is correct
};
```

**Verdict**: Remove public default constructor to match project pattern.

---

## References

- **C++ Core Guidelines**: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
  - C.21: Define or delete special members
  - C.31: Destructors must not throw
  - C.45: Prefer factory functions
  - F.15: Return values over out-parameters
  - F.20: Use [[nodiscard]] for pure functions
  - ES.20: Always initialize objects
  - ES.28: Use switch for enums

- **Project Architecture**:
  - `USER_PROMPT_HANDLER_ARCHITECTURE.md`
  - `W3C_IMPLEMENTATION_GUIDE.md`
  - `CLAUDE.md`

- **W3C BiDi Specification**:
  - https://w3c.github.io/webdriver-bidi/#module-browsingContext
  - Events: userPromptOpened, userPromptClosed
  - Command: browsingContext.handleUserPrompt

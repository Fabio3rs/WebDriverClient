# User Prompt Handler Implementation Task List

**Project**: WebDriverClient (bidi-x)
**Module**: User Prompt Handler
**Status**: Phase 1 - Foundation (65% Complete)
**Last Updated**: 2025-10-12

Based on:
- `USER_PROMPT_HANDLER_REVIEW.md` (code review findings)
- `USER_PROMPT_HANDLER_ARCHITECTURE.md` (design specification)
- `W3C_IMPLEMENTATION_GUIDE.md` (compliance checklist)

---

## Phase 1: Foundation & Critical Fixes (P0)

### TASK-001: Fix .finally() Callback Signature for void Tasks
**Priority**: P0 - Critical
**Phase**: 1
**Status**: ❌ Not Started
**Effort**: 0.5 hours

**Description**:
The `.finally()` callback for `Task<void>` has incorrect signature. For void tasks, the callback should receive `(error_code, exception_ptr)` not `(value, error_code, exception_ptr)`.

**Files to Modify**:
- `src/bidi_user_prompt_handler.cpp:291-300`

**Current Code** (BROKEN):
```cpp
task.finally([](const std::optional<boost::system::error_code> &error,
                const std::exception_ptr &eptr) {
    // TODO: Log errors if needed (use project logging system)
    if (error) {
        // Log error
    }
    if (eptr) {
        // Log exception
    }
});
```

**Fixed Code**:
```cpp
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
        } catch (...) {
            logging::log_error(
                "UserPromptHandler: Unknown exception in handleUserPrompt");
        }
    }
});
```

**Additional Changes**:
- Add `#include "bidi/logging.hpp"` at top of file
- Add `#include <format>` at top of file

**Acceptance Criteria**:
- [ ] Code compiles without errors
- [ ] Logging output appears when prompt handling fails
- [ ] No runtime errors when materializing void tasks

**Dependencies**: None

---

### TASK-002: Remove Public Default Constructor
**Priority**: P0 - Critical
**Phase**: 1
**Status**: ❌ Not Started
**Effort**: 0.25 hours

**Description**:
The public default constructor (line 250) breaks the C.45 factory pattern. The class has a private constructor and factory method, so the public default constructor undermines encapsulation.

**Files to Modify**:
- `include/bidi/user_prompt_handler.hpp:250`

**Current Code** (BROKEN):
```cpp
UserPromptHandler() = default;  // Line 250 - PUBLIC

// ... later ...

private:
    UserPromptHandler(std::shared_ptr<Client> client,
                      UserPromptHandlerConfig config,
                      Client::Subscription subscription);  // Line 306-308
```

**Fix**:
```cpp
// DELETE LINE 250 ENTIRELY
// OR move to private section if needed for move semantics:

private:
    UserPromptHandler() = default;  // Only if needed for implementation
```

**Recommendation**: Delete entirely. The private constructor provides all initialization.

**Acceptance Criteria**:
- [ ] Public API only allows construction via `create()` factory method
- [ ] Code compiles and all tests pass
- [ ] Move semantics still work correctly

**Dependencies**: None

---

### TASK-003: Fix Event Handler Registration (Critical Bug)
**Priority**: P0 - Critical
**Phase**: 1
**Status**: ❌ Not Started
**Effort**: 2 hours

**Description**:
The `set_event_handler()` method returns `awaitable<void>`, but it's never materialized in the factory method. This means the event handler is **never actually registered**. This is a silent failure that makes the entire handler non-functional.

**Files to Modify**:
- `src/bidi_user_prompt_handler.cpp:199-230`

**Current Code** (BROKEN):
```cpp
auto set_opened = client->set_event_handler(
    "browsingContext.userPromptOpened",
    [weak_handler](const boost::json::object &params) { /* ... */ });

// We don't co_await set_opened here; ignore result
// (fire-and-forget)  <-- THIS IS WRONG!

out.fulfill(std::move(handler_ptr));
```

**Fixed Code**:
```cpp
try {
    auto handler_ptr = std::make_shared<UserPromptHandler>(
        client, config, std::move(subscription));
    auto weak_handler = std::weak_ptr<UserPromptHandler>(handler_ptr);

    // Register userPromptOpened handler - MUST materialize awaitable
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
                        } catch (const std::exception &e) {
                            logging::log_error(std::format(
                                "UserPromptHandler: Failed to parse opened event: {}",
                                e.what()));
                        } catch (...) {
                            logging::log_error(
                                "UserPromptHandler: Unknown error parsing opened event");
                        }
                    }
                });
        }(),
        [out, handler_ptr](std::exception_ptr eptr) mutable {
            if (eptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception &e) {
                    out.fail(std::make_exception_ptr(std::runtime_error(
                        std::format("Failed to register userPromptOpened handler: {}",
                                    e.what()))));
                } catch (...) {
                    out.fail(std::make_exception_ptr(std::runtime_error(
                        "Unknown error registering userPromptOpened handler")));
                }
            } else {
                // Handler registered successfully, fulfill with handler_ptr
                out.fulfill(std::move(handler_ptr));
            }
        }
    );
} catch (...) {
    out.fail(std::current_exception());
}
```

**Acceptance Criteria**:
- [ ] Event handler is actually registered (verify with logging or debugger)
- [ ] Prompts are handled when they appear
- [ ] Error handling works if registration fails
- [ ] Integration test passes

**Dependencies**: None

---

### TASK-004: Implement userPromptClosed Event Handler
**Priority**: P0 - Critical
**Phase**: 1
**Status**: ❌ Not Started
**Effort**: 1.5 hours

**Description**:
The architecture document specifies subscription to both `userPromptOpened` and `userPromptClosed`, but only the opened handler is implemented. The closed handler is subscribed but has no implementation.

**Files to Modify**:
- `include/bidi/user_prompt_handler.hpp` (add private method declaration)
- `src/bidi_user_prompt_handler.cpp` (add handler registration and implementation)

**Header Addition** (`include/bidi/user_prompt_handler.hpp`):
```cpp
private:
    void handle_prompt(
        const types::browsing_context::UserPromptOpenedParameters &params);

    // ADD THIS:
    void on_prompt_closed(
        const types::browsing_context::UserPromptClosedParameters &params);
```

**Implementation** (`src/bidi_user_prompt_handler.cpp`):

Add after userPromptOpened handler registration (around line 230):
```cpp
// Register userPromptClosed handler
boost::asio::co_spawn(
    client->get_executor(),
    [client, weak_handler]() -> boost::asio::awaitable<void> {
        co_await client->set_event_handler(
            "browsingContext.userPromptClosed",
            [weak_handler](const boost::json::object &params) {
                if (auto handler = weak_handler.lock()) {
                    try {
                        auto parsed = from_json_prompt_closed(params);
                        boost::asio::post(
                            handler->client_->get_executor(),
                            [handler, parsed]() mutable {
                                handler->on_prompt_closed(parsed);
                            });
                    } catch (const std::exception &e) {
                        logging::log_error(std::format(
                            "UserPromptHandler: Failed to parse closed event: {}",
                            e.what()));
                    } catch (...) {
                        logging::log_error(
                            "UserPromptHandler: Unknown error parsing closed event");
                    }
                }
            });
    }(),
    boost::asio::detached  // Fire-and-forget for closed handler
);
```

Add method implementation at end of file:
```cpp
void UserPromptHandler::on_prompt_closed(
    const types::browsing_context::UserPromptClosedParameters &params) {

    // Log prompt closure for observability
    logging::log_info(std::format(
        "UserPromptHandler: Prompt closed in context '{}', type: {}, accepted: {}",
        params.context,
        types::browsing_context::to_string(params.type),
        params.accepted));

    // Optional: Emit metrics for monitoring
    // metrics::increment("user_prompt.closed", {
    //     {"type", to_string(params.type)},
    //     {"accepted", params.accepted ? "true" : "false"}
    // });
}
```

**Acceptance Criteria**:
- [ ] Closed handler is registered successfully
- [ ] Log messages appear when prompts are closed
- [ ] Both opened and closed events are handled
- [ ] No memory leaks (verify with ASan)

**Dependencies**: TASK-003 (same registration pattern)

---

## Phase 2: Core Implementation Completion (P1)

### TASK-005: Add Event ID Constants to bidi::ids
**Priority**: P1 - High
**Phase**: 2
**Status**: ❌ Not Started
**Effort**: 0.5 hours

**Description**:
Replace hard-coded event strings with constants from `bidi::ids` namespace for consistency with the rest of the codebase.

**Files to Modify**:
- `include/bidi/ids.hpp` or `include/bidi_methods.hpp` (verify location)
- `src/bidi_user_prompt_handler.cpp:162-164`

**Check if these exist**:
```cpp
namespace bidi::ids::events {
    inline constexpr std::string_view browsing_context_userPromptOpened =
        "browsingContext.userPromptOpened";
    inline constexpr std::string_view browsing_context_userPromptClosed =
        "browsingContext.userPromptClosed";
}
```

**If missing, add to appropriate file**. Then update usage:

**Current** (`src/bidi_user_prompt_handler.cpp:162-164`):
```cpp
std::vector<std::string> events{
    std::string("browsingContext.userPromptOpened"),
    std::string("browsingContext.userPromptClosed")};
```

**Fixed**:
```cpp
#include "bidi/ids.hpp"

// In create() method:
std::vector<std::string> events{
    std::string(bidi::ids::events::browsing_context_userPromptOpened),
    std::string(bidi::ids::events::browsing_context_userPromptClosed)
};
```

**Acceptance Criteria**:
- [ ] Constants defined in `bidi::ids` namespace
- [ ] All hard-coded strings replaced with constants
- [ ] Code compiles and tests pass

**Dependencies**: None

---

### TASK-006: Improve JSON Parsing Error Messages
**Priority**: P1 - High
**Phase**: 2
**Status**: ❌ Not Started
**Effort**: 1 hour

**Description**:
Add full JSON context to parsing error messages for better debugging. Currently errors only say "missing or invalid field" without showing what was received.

**Files to Modify**:
- `src/bidi_user_prompt_handler.cpp:24-121` (both conversion functions)

**Pattern to Apply**:

**Current**:
```cpp
if (!context_ptr || !context_ptr->is_string()) {
    throw std::runtime_error(
        "UserPromptOpenedParameters: missing or invalid 'context' field");
}
```

**Improved**:
```cpp
if (!context_ptr || !context_ptr->is_string()) {
    throw std::runtime_error(std::format(
        "UserPromptOpenedParameters: missing or invalid 'context' field. "
        "Expected string, got {}. Full JSON: {}",
        context_ptr ? std::string(context_ptr->kind()) : "missing",
        boost::json::serialize(obj)));
}
```

**Apply to all validation checks** in:
- `from_json_prompt_opened()` (5 fields to check)
- `from_json_prompt_closed()` (3 fields to check)

**Additional**: Add helper function to avoid repetition:
```cpp
namespace {
[[nodiscard]] auto format_parse_error(
    std::string_view struct_name,
    std::string_view field_name,
    std::string_view expected_type,
    const boost::json::value *value_ptr,
    const boost::json::object &full_obj) -> std::string {

    return std::format(
        "{}: missing or invalid '{}' field. Expected {}, got {}. Full JSON: {}",
        struct_name, field_name, expected_type,
        value_ptr ? std::string(value_ptr->kind()) : "missing",
        boost::json::serialize(full_obj));
}
}
```

**Acceptance Criteria**:
- [ ] All parse errors include full JSON
- [ ] Error messages clearly indicate expected vs actual types
- [ ] Helper function reduces code duplication
- [ ] Test with malformed JSON to verify error messages

**Dependencies**: None

---

### TASK-007: Optimize String Construction from JSON
**Priority**: P2 - Medium
**Phase**: 2
**Status**: ❌ Not Started
**Effort**: 0.5 hours

**Description**:
Remove redundant `.c_str()` calls when constructing `std::string` from `boost::json::string`. The JSON string already provides `std::string_view` conversion.

**Files to Modify**:
- `src/bidi_user_prompt_handler.cpp:34, 51, 67, 94, 117`

**Current** (inefficient):
```cpp
params.context = std::string(context_ptr->as_string().c_str());
```

**Optimized**:
```cpp
params.context = std::string(context_ptr->as_string());
// Or even better:
auto sv = context_ptr->as_string();
params.context.assign(sv.data(), sv.size());
```

**Apply to all 5 occurrences**:
1. Line 34: `params.context`
2. Line 51: `params.message`
3. Line 67: `params.default_value`
4. Line 94: `params.context` (in closed handler)
5. Line 117: `params.user_text`

**Acceptance Criteria**:
- [ ] All `.c_str()` calls removed
- [ ] Performance unchanged (verify with benchmark if concerned)
- [ ] Code compiles and tests pass

**Dependencies**: None

---

## Phase 3: Client Integration (P1)

### TASK-008: Add Client::create_prompt_handler() Method
**Priority**: P1 - High
**Phase**: 3
**Status**: ❌ Not Started
**Effort**: 1 hour

**Description**:
Add convenience factory method to Client class for creating prompt handlers. This provides ergonomic API for users.

**Files to Modify**:
- `include/bidi/client.hpp` (add declaration after line 89)
- `src/bidi_client.cpp` (add implementation)

**Header Addition** (`include/bidi/client.hpp`):
```cpp
// After handle_user_prompt() declaration (line 89)

// ======================== User Prompt Handler API ========================

/**
 * @brief Create RAII prompt handler with policy-based handling
 *
 * Subscribes to prompt events and automatically handles according to policy.
 * Handler unsubscribes on destruction (RAII).
 *
 * @param config Policy configuration (default: accept all prompts)
 * @return Task<shared_ptr<UserPromptHandler>> Lazy task that creates handler
 *
 * @see UserPromptHandler
 * @see UserPromptHandlerConfig
 *
 * Usage:
 * @code
 * auto handler = co_await client->create_prompt_handler(
 *     bidi::UserPromptHandlerConfig::accept_all())();
 * // All prompts automatically handled until handler destroyed
 * @endcode
 */
[[nodiscard]] auto create_prompt_handler(
    UserPromptHandlerConfig config = UserPromptHandlerConfig::accept_all())
    -> Task<std::shared_ptr<UserPromptHandler>>;
```

**Implementation** (`src/bidi_client.cpp`):
```cpp
#include "bidi/user_prompt_handler.hpp"

auto Client::create_prompt_handler(UserPromptHandlerConfig config)
    -> Task<std::shared_ptr<UserPromptHandler>> {

    // Forward to UserPromptHandler factory
    return UserPromptHandler::create(shared_from_this(), std::move(config));
}
```

**Acceptance Criteria**:
- [ ] Method declared in header with [[nodiscard]]
- [ ] Implementation forwards to UserPromptHandler::create()
- [ ] Documentation includes usage example
- [ ] Integration test demonstrates usage

**Dependencies**: TASK-001 through TASK-004 (handler must work first)

---

### TASK-009: Add Client::on_prompt_opened() Helper
**Priority**: P1 - High
**Phase**: 3
**Status**: ❌ Not Started
**Effort**: 1.5 hours

**Description**:
Add strongly-typed event subscription helper for userPromptOpened events. Provides cleaner API than raw `set_event_handler()`.

**Files to Modify**:
- `include/bidi/client.hpp` (add declaration)
- `src/bidi_client.cpp` (add implementation)

**Header Addition** (`include/bidi/client.hpp`):
```cpp
/**
 * @brief Subscribe to userPromptOpened events with typed callback
 *
 * Convenience wrapper around set_event_handler that provides strongly-typed
 * parameters instead of raw JSON.
 *
 * @param callback Function to invoke for each opened prompt
 * @return Task<Subscription> RAII subscription handle
 *
 * Usage:
 * @code
 * auto sub = co_await client->on_prompt_opened(
 *     [](const auto& params) {
 *         std::cout << "Prompt: " << params.message << "\n";
 *     })();
 * // Subscription auto-unsubscribes when 'sub' destroyed
 * @endcode
 */
[[nodiscard]] auto on_prompt_opened(
    std::function<void(const types::browsing_context::UserPromptOpenedParameters&)> callback)
    -> Task<Subscription>;
```

**Implementation** (`src/bidi_client.cpp`):
```cpp
auto Client::on_prompt_opened(
    std::function<void(const types::browsing_context::UserPromptOpenedParameters&)> callback)
    -> Task<Subscription> {

    // Create Task that will complete with Subscription
    auto task = Task<Subscription>::make(get_executor());

    // Subscribe to event
    auto sub_task = subscribe({std::string(bidi::ids::events::browsing_context_userPromptOpened)});

    sub_task.finally([this, callback = std::move(callback), task](
        auto sub_opt,
        const std::optional<boost::system::error_code> &ec,
        const std::exception_ptr &eptr) mutable {

        if (eptr) {
            task.fail(eptr);
            return;
        }
        if (ec && *ec) {
            task.fail(std::make_exception_ptr(std::system_error(*ec)));
            return;
        }
        if (!sub_opt) {
            task.fail(std::make_exception_ptr(
                std::runtime_error("subscribe returned no subscription")));
            return;
        }

        // Set event handler with type conversion
        boost::asio::co_spawn(
            get_executor(),
            [this, callback]() -> boost::asio::awaitable<void> {
                co_await set_event_handler(
                    std::string(bidi::ids::events::browsing_context_userPromptOpened),
                    [callback](const boost::json::object &params) {
                        try {
                            auto typed = from_json_prompt_opened(params);
                            callback(typed);
                        } catch (const std::exception &e) {
                            logging::log_error(std::format(
                                "on_prompt_opened: Failed to parse event: {}", e.what()));
                        } catch (...) {
                            logging::log_error(
                                "on_prompt_opened: Unknown parsing error");
                        }
                    });
            }(),
            [task, sub = std::move(*sub_opt)](std::exception_ptr eptr) mutable {
                if (eptr) {
                    task.fail(eptr);
                } else {
                    task.fulfill(std::move(sub));
                }
            }
        );
    });

    return task;
}
```

**Acceptance Criteria**:
- [ ] Method provides strongly-typed callback interface
- [ ] JSON parsing errors are logged, not propagated
- [ ] Returns RAII Subscription that auto-unsubscribes
- [ ] Integration test demonstrates usage

**Dependencies**: TASK-005 (event ID constants), TASK-006 (error handling)

---

### TASK-010: Add Client::on_prompt_closed() Helper
**Priority**: P1 - High
**Phase**: 3
**Status**: ❌ Not Started
**Effort**: 1 hour

**Description**:
Add strongly-typed event subscription helper for userPromptClosed events. Mirrors `on_prompt_opened()` design.

**Files to Modify**:
- `include/bidi/client.hpp` (add declaration)
- `src/bidi_client.cpp` (add implementation)

**Implementation**: Same pattern as TASK-009, but for `userPromptClosed`:
- Use `bidi::ids::events::browsing_context_userPromptClosed`
- Call `from_json_prompt_closed()` for type conversion
- Callback type: `types::browsing_context::UserPromptClosedParameters&`

**Acceptance Criteria**:
- [ ] Method provides strongly-typed callback interface
- [ ] Consistent with on_prompt_opened() design
- [ ] Integration test demonstrates usage

**Dependencies**: TASK-009 (same pattern)

---

### TASK-011: Fix set_policy() Thread Safety
**Priority**: P2 - Medium
**Phase**: 3
**Status**: ❌ Not Started
**Effort**: 0.5 hours

**Description**:
The `set_policy()` method claims to be thread-safe via strand serialization, but doesn't actually post to the strand. This could race with `handle_prompt()`.

**Files to Modify**:
- `include/bidi/user_prompt_handler.hpp:285-287`

**Current** (potentially unsafe):
```cpp
void set_policy(UserPromptHandlerConfig new_config) {
    config_ = std::move(new_config);
}
```

**Option 1: Make async** (preferred):
```cpp
/**
 * @brief Update policy configuration at runtime (async)
 *
 * Thread-safe: posts update to client's executor strand.
 *
 * @param new_config New policy configuration to apply
 *
 * Usage:
 * @code
 * handler->set_policy(UserPromptHandlerConfig::dismiss_all());
 * @endcode
 */
void set_policy(UserPromptHandlerConfig new_config) {
    if (client_) {
        boost::asio::post(
            client_->get_executor(),
            [this, cfg = std::move(new_config)]() mutable {
                config_ = std::move(cfg);
            });
    }
}
```

**Option 2: Document strand requirement**:
```cpp
/**
 * @brief Update policy configuration at runtime
 *
 * Thread-safety: MUST be called from client's executor strand.
 * For cross-thread updates, use post() manually.
 *
 * @param new_config New policy configuration to apply
 */
void set_policy(UserPromptHandlerConfig new_config) {
    config_ = std::move(new_config);
}
```

**Recommendation**: Use Option 1 (async posting).

**Acceptance Criteria**:
- [ ] No data races (verify with TSan)
- [ ] Policy updates are applied correctly
- [ ] Concurrent prompt handling works during policy update

**Dependencies**: None

---

## Phase 4: Testing & Documentation (P1-P2)

### TASK-012: Create Integration Tests
**Priority**: P1 - High
**Phase**: 4
**Status**: ❌ Not Started
**Effort**: 4 hours

**Description**:
Create comprehensive integration tests for UserPromptHandler covering RAII, move semantics, policy dispatch, and concurrent handling.

**Files to Create**:
- `tests/user_prompt_handler_test.cpp`

**Test Cases Required**:

```cpp
#include <gtest/gtest.h>
#include "bidi/user_prompt_handler.hpp"
#include "bidi/client.hpp"
#include "bidi/guards.hpp"

// Test 1: RAII subscription cleanup
TEST(UserPromptHandlerTest, RAIICleanup) {
    // Create handler, verify subscription active
    // Let handler go out of scope
    // Verify unsubscribe called (check subscription count)
}

// Test 2: Move semantics
TEST(UserPromptHandlerTest, MoveSemantics) {
    // Create handler
    // Move to another variable
    // Verify original is empty, moved-to is active
}

// Test 3: AcceptAll policy
TEST(UserPromptHandlerTest, AcceptAllPolicy) {
    // Create handler with AcceptAll
    // Trigger alert prompt
    // Verify handleUserPrompt called with accept=true
}

// Test 4: DismissAll policy
TEST(UserPromptHandlerTest, DismissAllPolicy) {
    // Create handler with DismissAll
    // Trigger confirm prompt
    // Verify handleUserPrompt called with accept=false
}

// Test 5: IgnoreAll policy
TEST(UserPromptHandlerTest, IgnoreAllPolicy) {
    // Create handler with IgnoreAll
    // Trigger prompt
    // Verify handleUserPrompt NOT called
}

// Test 6: Custom policy
TEST(UserPromptHandlerTest, CustomPolicy) {
    // Create handler with custom callback
    // Trigger various prompt types
    // Verify callback invoked and correct resolution sent
}

// Test 7: Runtime policy update
TEST(UserPromptHandlerTest, PolicyUpdate) {
    // Create handler with AcceptAll
    // Trigger prompt, verify accepted
    // Update to DismissAll
    // Trigger prompt, verify dismissed
}

// Test 8: Concurrent prompt handling
TEST(UserPromptHandlerTest, ConcurrentPrompts) {
    // Trigger multiple prompts rapidly
    // Verify all handled correctly
    // No data races (run with TSan)
}

// Test 9: Weak_ptr expiration
TEST(UserPromptHandlerTest, WeakPtrExpiration) {
    // Create handler
    // Destroy shared_ptr
    // Trigger prompt
    // Verify event handler doesn't crash (weak_ptr.lock() returns nullptr)
}

// Test 10: Event handler registration failure
TEST(UserPromptHandlerTest, RegistrationFailure) {
    // Mock client that fails set_event_handler
    // Attempt to create handler
    // Verify Task fails with appropriate error
}

// Test 11: JSON parsing errors
TEST(UserPromptHandlerTest, MalformedJSON) {
    // Send malformed userPromptOpened event
    // Verify handler doesn't crash
    // Verify error logged
}

// Test 12: Both opened and closed events
TEST(UserPromptHandlerTest, OpenedAndClosedEvents) {
    // Create handler
    // Trigger prompt (opened event)
    // Close prompt (closed event)
    // Verify both events handled
    // Verify logs show both events
}
```

**Acceptance Criteria**:
- [ ] All 12 test cases implemented
- [ ] Tests pass consistently (no flakiness)
- [ ] No memory leaks (verify with ASan)
- [ ] No data races (verify with TSan)
- [ ] Code coverage > 90% for user_prompt_handler.cpp

**Dependencies**: TASK-001 through TASK-011 (all implementation must be complete)

---

### TASK-013: Create Example Code
**Priority**: P2 - Medium
**Phase**: 4
**Status**: ❌ Not Started
**Effort**: 2 hours

**Description**:
Create comprehensive example demonstrating all UserPromptHandler usage patterns.

**Files to Create**:
- `examples/flow/example_user_prompts.cpp`

**Example Structure**:
```cpp
#include "bidi/client.hpp"
#include "bidi/guards.hpp"
#include "bidi/user_prompt_handler.hpp"
#include "bidi/connection_builder.hpp"
#include <boost/asio.hpp>
#include <iostream>

namespace net = boost::asio;

// Example 1: Accept all prompts automatically
auto example_accept_all(net::io_context &io) -> net::awaitable<void> {
    auto client = co_await bidi::connect_to("http://localhost:9515")
        .headless()
        .connect(io)();

    bidi::ClientGuard guard(client);

    auto prompt_handler = co_await client->create_prompt_handler(
        bidi::UserPromptHandlerConfig::accept_all());

    auto ctx = co_await client->create_context();

    // Navigate to page with alerts - automatically handled
    co_await client->navigate(ctx, "http://localhost:8080/alerts.html");

    std::cout << "All prompts auto-accepted\n";
}

// Example 2: Custom logic per prompt type
auto example_custom_policy(net::io_context &io) -> net::awaitable<void> {
    auto client = co_await bidi::connect_to("http://localhost:9515")
        .headless()
        .connect(io)();

    auto prompt_handler = co_await client->create_prompt_handler(
        bidi::UserPromptHandlerConfig::custom(
            [](const auto &params)
                -> std::optional<bidi::types::browsing_context::UserPromptResolution> {
                using enum bidi::types::browsing_context::UserPromptType;

                std::cout << "Prompt type: "
                          << bidi::types::browsing_context::to_string(params.type)
                          << ", message: " << params.message << "\n";

                switch (params.type) {
                case Alert:
                    return bidi::types::browsing_context::UserPromptResolution{};
                case Confirm:
                    return params.message.find("delete") != std::string::npos
                        ? bidi::types::browsing_context::UserPromptResolution{.accept = false}
                        : bidi::types::browsing_context::UserPromptResolution{};
                case Prompt:
                    return bidi::types::browsing_context::UserPromptResolution{
                        .accept = true, .user_text = "automated input"};
                case BeforeUnload:
                    return bidi::types::browsing_context::UserPromptResolution{};
                }
                return std::nullopt;
            }))();

    // Use handler...
}

// Example 3: Manual event handling
auto example_manual_handling(net::io_context &io) -> net::awaitable<void> {
    auto client = co_await bidi::connect_to("http://localhost:9515")
        .headless()
        .connect(io)();

    auto subscription = co_await client->on_prompt_opened(
        [](const bidi::types::browsing_context::UserPromptOpenedParameters &params) {
            std::cout << "Prompt opened: " << params.message << "\n";
            // Handle manually later via client->handle_user_prompt()
        })();

    // Use subscription...
}

// Example 4: Low-level command usage
auto example_low_level(net::io_context &io) -> net::awaitable<void> {
    auto client = co_await bidi::connect_to("http://localhost:9515")
        .headless()
        .connect(io)();

    auto ctx = co_await client->create_context();

    // Manual control: materialize the lazy Task explicitly
    auto prompt_task = client->handle_user_prompt(ctx, true, "my answer");
    co_await prompt_task();
}

int main() {
    net::io_context io;

    net::co_spawn(io, example_accept_all(io), net::detached);
    // net::co_spawn(io, example_custom_policy(io), net::detached);
    // net::co_spawn(io, example_manual_handling(io), net::detached);
    // net::co_spawn(io, example_low_level(io), net::detached);

    io.run();
    return 0;
}
```

**Acceptance Criteria**:
- [ ] Example compiles and runs successfully
- [ ] All 4 usage patterns demonstrated
- [ ] Clear comments explain each pattern
- [ ] Example added to CMakeLists.txt

**Dependencies**: TASK-008 through TASK-010 (Client API methods)

---

### TASK-014: Update CLAUDE.md Documentation
**Priority**: P2 - Medium
**Phase**: 4
**Status**: ❌ Not Started
**Effort**: 1 hour

**Description**:
Update project documentation to include UserPromptHandler API in the public API reference section.

**Files to Modify**:
- `CLAUDE.md` (add section after "Script API")

**Content to Add**:

```markdown
## User Prompt Handler API

### Overview
The UserPromptHandler provides policy-based handling of browser user prompts (alert, confirm, prompt, beforeUnload) with RAII subscription management.

### Creating a Prompt Handler

**Factory Method**:
```cpp
auto handler = co_await client->create_prompt_handler(
    bidi::UserPromptHandlerConfig::accept_all())();
// Handler automatically unsubscribes when destroyed
```

**Available Policies**:
- `UserPromptHandlerConfig::accept_all()` - Accept all prompts
- `UserPromptHandlerConfig::dismiss_all()` - Dismiss all prompts
- `UserPromptHandlerConfig::ignore_all()` - No automatic handling
- `UserPromptHandlerConfig::custom(callback)` - Custom logic per prompt

### Event Subscription Helpers

**Strongly-Typed Callbacks**:
```cpp
// Subscribe to opened events
auto sub = co_await client->on_prompt_opened(
    [](const auto& params) {
        std::cout << "Prompt: " << params.message << "\n";
    })();

// Subscribe to closed events
auto sub = co_await client->on_prompt_closed(
    [](const auto& params) {
        std::cout << "Closed, accepted: " << params.accepted << "\n";
    })();
```

### Low-Level Command

**Manual Prompt Handling**:
```cpp
co_await client->handle_user_prompt(
    context_id,
    true,  // accept
    "user input text"  // optional, for prompt dialogs
)();
```

### Custom Policy Example

```cpp
auto handler = co_await client->create_prompt_handler(
    bidi::UserPromptHandlerConfig::custom(
        [](const auto& params)
            -> std::optional<bidi::types::browsing_context::UserPromptResolution> {
            using enum bidi::types::browsing_context::UserPromptType;
            switch (params.type) {
            case Alert:
                return UserPromptResolution{.accept = true};
            case Confirm:
                return params.message.find("delete") != std::string::npos
                    ? UserPromptResolution{.accept = false}
                    : UserPromptResolution{.accept = true};
            case Prompt:
                return UserPromptResolution{.accept = true, .user_text = "automated"};
            case BeforeUnload:
                return UserPromptResolution{.accept = true};
            }
            return std::nullopt;  // Skip handling
        }))();
```

### Architecture

**Design Principles**:
- RAII subscription management
- Zero-cost policy dispatch (switch, no virtual calls)
- Type-safe event parameters
- Fire-and-forget prompt handling
- Weak_ptr lifecycle safety

**Thread Safety**:
- All operations serialized via client's strand
- Policy updates posted to executor
- No data races

**See Also**:
- `include/bidi/user_prompt_handler.hpp` - Full API reference
- `USER_PROMPT_HANDLER_ARCHITECTURE.md` - Design rationale
- `examples/flow/example_user_prompts.cpp` - Usage examples
```

**Acceptance Criteria**:
- [ ] Documentation added to CLAUDE.md
- [ ] All public APIs documented
- [ ] Examples included
- [ ] Cross-references to related docs

**Dependencies**: TASK-013 (example code must exist)

---

### TASK-015: Add Unit Tests for JSON Converters
**Priority**: P2 - Medium
**Phase**: 4
**Status**: ❌ Not Started
**Effort**: 2 hours

**Description**:
Add unit tests for `from_json_prompt_opened()` and `from_json_prompt_closed()` helper functions.

**Files to Create/Modify**:
- `tests/user_prompt_policy_test.cpp` (add more test cases)

**Test Cases**:

```cpp
// Test valid userPromptOpened JSON
TEST(UserPromptParsingTest, ValidPromptOpened) {
    boost::json::object obj{
        {"context", "ctx-123"},
        {"handler", "accept"},
        {"message", "Are you sure?"},
        {"type", "confirm"}
    };

    auto params = bidi::from_json_prompt_opened(obj);
    EXPECT_EQ(params.context, "ctx-123");
    EXPECT_EQ(params.message, "Are you sure?");
    EXPECT_EQ(params.type, bidi::types::browsing_context::UserPromptType::Confirm);
}

// Test prompt with defaultValue
TEST(UserPromptParsingTest, PromptWithDefaultValue) {
    boost::json::object obj{
        {"context", "ctx-123"},
        {"handler", "accept"},
        {"message", "Enter name:"},
        {"type", "prompt"},
        {"defaultValue", "John"}
    };

    auto params = bidi::from_json_prompt_opened(obj);
    ASSERT_TRUE(params.default_value.has_value());
    EXPECT_EQ(*params.default_value, "John");
}

// Test missing required field
TEST(UserPromptParsingTest, MissingContext) {
    boost::json::object obj{
        {"handler", "accept"},
        {"message", "Alert"},
        {"type", "alert"}
    };

    EXPECT_THROW(bidi::from_json_prompt_opened(obj), std::runtime_error);
}

// Test invalid field type
TEST(UserPromptParsingTest, InvalidTypeField) {
    boost::json::object obj{
        {"context", "ctx-123"},
        {"handler", "accept"},
        {"message", "Alert"},
        {"type", 123}  // Should be string
    };

    EXPECT_THROW(bidi::from_json_prompt_opened(obj), std::runtime_error);
}

// Test valid userPromptClosed JSON
TEST(UserPromptParsingTest, ValidPromptClosed) {
    boost::json::object obj{
        {"context", "ctx-123"},
        {"accepted", true},
        {"type", "alert"}
    };

    auto params = bidi::from_json_prompt_closed(obj);
    EXPECT_EQ(params.context, "ctx-123");
    EXPECT_TRUE(params.accepted);
    EXPECT_EQ(params.type, bidi::types::browsing_context::UserPromptType::Alert);
}

// Test closed with userText
TEST(UserPromptParsingTest, ClosedWithUserText) {
    boost::json::object obj{
        {"context", "ctx-123"},
        {"accepted", true},
        {"type", "prompt"},
        {"userText", "User input"}
    };

    auto params = bidi::from_json_prompt_closed(obj);
    ASSERT_TRUE(params.user_text.has_value());
    EXPECT_EQ(*params.user_text, "User input");
}
```

**Acceptance Criteria**:
- [ ] All positive test cases pass
- [ ] All negative test cases pass (error handling)
- [ ] Coverage > 95% for JSON converter functions

**Dependencies**: TASK-006 (improved error messages make tests easier)

---

## Summary

### Phase Breakdown

**Phase 1: Foundation & Critical Fixes** (4.75 hours)
- TASK-001: Fix .finally() callback signature [0.5h] ✓ Critical
- TASK-002: Remove public default constructor [0.25h] ✓ Critical
- TASK-003: Fix event handler registration [2h] ✓ Critical
- TASK-004: Implement userPromptClosed handler [1.5h] ✓ Critical
- TASK-005: Add event ID constants [0.5h]

**Phase 2: Core Implementation** (2 hours)
- TASK-006: Improve JSON error messages [1h]
- TASK-007: Optimize string construction [0.5h]

**Phase 3: Client Integration** (4 hours)
- TASK-008: Add create_prompt_handler() [1h]
- TASK-009: Add on_prompt_opened() [1.5h]
- TASK-010: Add on_prompt_closed() [1h]
- TASK-011: Fix set_policy() thread safety [0.5h]

**Phase 4: Testing & Documentation** (9 hours)
- TASK-012: Create integration tests [4h]
- TASK-013: Create example code [2h]
- TASK-014: Update CLAUDE.md [1h]
- TASK-015: Add unit tests [2h]

### Total Effort Estimate: 19.75 hours (~2.5 work days)

### Critical Path
1. Complete Phase 1 tasks (TASK-001 through TASK-004) first
2. Phase 1 completion enables all other work
3. Testing (Phase 4) should happen in parallel with Phase 3
4. Documentation (Phase 4) is last

### Success Criteria
- [ ] All P0 (critical) issues resolved
- [ ] All P1 (high priority) tasks complete
- [ ] Integration tests pass (> 90% coverage)
- [ ] No memory leaks (ASan clean)
- [ ] No data races (TSan clean)
- [ ] Example code compiles and runs
- [ ] Documentation complete

### Next Steps
1. Start with TASK-001 (fix .finally() signature)
2. Proceed through Phase 1 sequentially
3. Run integration tests after Phase 1
4. Parallelize Phase 3 and Phase 4 work
5. Final QA and code review before merge

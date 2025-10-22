# User Prompt Handler API - Architectural Design

## Overview

This document provides a comprehensive architectural proposal for a higher-level functional API for handling browser user prompts (alert/confirm/prompt/beforeUnload) in the WebDriverClient project.

## Current State Assessment

The project currently requires manual event subscription and command sending for user prompt handling:

```cpp
co_await client->set_event_handler("browsingContext.userPromptOpened",
    [client](boost::json::object params) {
        std::string context = params.at("context").as_string().c_str();
        client->handle_user_prompt(context, true);
    });
```

### Current Limitations

1. **Boilerplate**: Users must manually subscribe to events and extract JSON fields
2. **Type safety**: JSON field access is stringly-typed and error-prone
3. **No policy abstraction**: Common patterns (accept all alerts, dismiss all confirms) require repetitive code
4. **Missing RAII**: No automatic cleanup for prompt subscriptions

## W3C BiDi Specification

### Event: browsingContext.userPromptOpened

```
browsingContext.UserPromptOpenedParameters = {
  context: browsingContext.BrowsingContext,
  handler: session.UserPromptHandlerType,  // "accept", "dismiss", "ignore"
  message: text,
  type: browsingContext.UserPromptType,    // "alert", "confirm", "prompt", "beforeUnload"
  ? defaultValue: text                     // Only for "prompt" type
}
```

### Command: browsingContext.handleUserPrompt

```
browsingContext.HandleUserPromptParameters = {
  context: browsingContext.BrowsingContext,
  ? accept: bool,      // Default: true
  ? userText: text     // Default: ""
}
```

### Event: browsingContext.userPromptClosed

```
browsingContext.UserPromptClosedParameters = {
  context: browsingContext.BrowsingContext,
  accepted: bool,
  type: browsingContext.UserPromptType,
  ? userText: text
}
```

## Core Guidelines Compliance Review

### Current Gaps

- **C.131**: Avoid trivial getters and setters - Need structured types, not raw JSON
- **F.51**: Where there is a choice, prefer default arguments over overloading - Policy-based design needed
- **ES.11**: Use `auto` to avoid redundant repetition of type names - Improve type inference in handler APIs
- **C.2**: Use class if the class has an invariant; use struct if the data members can vary independently - Need proper struct definitions for event parameters

## Proposed Improvements

### Improvement 1: W3C-Compliant Event Parameter Structs

**Guideline Reference:** C.2, C.131, ES.11
**Priority:** Critical

Replace raw `boost::json::object` with strongly-typed structs matching W3C spec. This provides compile-time safety, better IDE support, and self-documenting code.

#### Proposed Structs

Add to `include/bidi/types/browsing_context.hpp`:

```cpp
namespace bidi::types::browsing_context {

/**
 * @brief User prompt opened event parameters
 * @see https://w3c.github.io/webdriver-bidi/#event-browsingContext-userPromptOpened
 */
struct UserPromptOpenedParameters {
    BrowsingContextId context;
    session::UserPromptAction handler; // session.UserPromptHandlerType
    std::string message;
    UserPromptType type;
    std::optional<std::string> default_value; // Only for "prompt" type

    auto operator==(const UserPromptOpenedParameters &) const -> bool = default;
};

/**
 * @brief User prompt closed event parameters
 * @see https://w3c.github.io/webdriver-bidi/#event-browsingContext-userPromptClosed
 */
struct UserPromptClosedParameters {
    BrowsingContextId context;
    bool accepted;
    UserPromptType type;
    std::optional<std::string> user_text;

    auto operator==(const UserPromptClosedParameters &) const -> bool = default;
};

/**
 * @brief Resolution to send with browsingContext.handleUserPrompt
 */
struct UserPromptResolution {
    bool accept{true};
    std::optional<std::string> user_text; // Text for prompt dialogs

    auto operator==(const UserPromptResolution &) const -> bool = default;
};

} // namespace bidi::types::browsing_context
```

**Rationale:**
- POD structs with defaulted equality operator (C.2, C.131)
- `std::optional` for optional fields (no sentinel values)
- Strong type `BrowsingContextId` instead of raw `std::string`
- Reuses `types::session::UserPromptAction` enum so we keep a single source of truth
- Explicit field names matching W3C spec

**Integration Points:**
- Add to `include/bidi/types/browsing_context.hpp` after `NavigationInfo`
- Helper functions to convert from `boost::json::object` to these structs
- Map the new `UserPromptResolution` helper to the low-level command builder

---

### Improvement 2: Policy-Based Prompt Handler

**Guideline Reference:** F.51, ES.20, C.45
**Priority:** High

Common patterns (accept all, dismiss all, custom logic per type) should be expressible as policies, not repetitive callback code. Follows "prefer default arguments over overloading" and enables zero-cost abstractions.

#### Proposed Policy System

```cpp
namespace bidi {

/**
 * @brief User prompt handling policy
 */
enum class UserPromptPolicy : std::uint8_t {
    AcceptAll,      // Accept all prompts (alerts, confirms), provide empty text for prompts
    DismissAll,     // Dismiss all prompts
    IgnoreAll,      // Don't handle automatically (manual handling required)
    Custom          // Use callback for custom logic
};

/**
 * @brief User prompt handler callback signature
 *
 * Takes opened parameters, returns action to perform.
 * Return std::nullopt to skip handling (leave prompt open).
 */
using UserPromptCallback = std::function<
    std::optional<types::browsing_context::UserPromptResolution>(
        const types::browsing_context::UserPromptOpenedParameters&)>;

/**
 * @brief User prompt handler configuration
 */
struct UserPromptHandlerConfig {
    UserPromptPolicy policy{UserPromptPolicy::AcceptAll};
    std::optional<UserPromptCallback> custom_handler;  // Only for Custom policy

    // Factory methods (C.45 - prefer factory functions for construction)
    static auto accept_all() -> UserPromptHandlerConfig {
        return UserPromptHandlerConfig{.policy = UserPromptPolicy::AcceptAll};
    }

    static auto dismiss_all() -> UserPromptHandlerConfig {
        return UserPromptHandlerConfig{.policy = UserPromptPolicy::DismissAll};
    }

    static auto ignore_all() -> UserPromptHandlerConfig {
        return UserPromptHandlerConfig{.policy = UserPromptPolicy::IgnoreAll};
    }

    static auto custom(UserPromptCallback callback) -> UserPromptHandlerConfig {
        return UserPromptHandlerConfig{
            .policy = UserPromptPolicy::Custom,
            .custom_handler = std::move(callback)
        };
    }
};

} // namespace bidi
```

**Rationale:**
- Enum for common policies (zero-cost dispatch via switch)
- Designated initializers (C++20) for clear intent
- Factory methods for ergonomic construction (C.45)
- `std::function` for type-erased callbacks (flexibility)
- Return `std::nullopt` to skip handling (composability)
- Callback returns `UserPromptResolution`, keeping command wiring centralized

---

### Improvement 3: RAII UserPromptHandler Guard

**Guideline Reference:** C.21, C.31, F.15
**Priority:** High

Follow project's RAII pattern (SessionGuard, ClientGuard, TimerGuard). Automatic subscription cleanup on destruction prevents resource leaks.

#### Proposed RAII Guard

```cpp
namespace bidi {

/**
 * @brief RAII guard for user prompt handling
 *
 * Automatically subscribes to browsingContext.userPromptOpened event,
 * handles prompts according to policy, and unsubscribes on destruction.
 *
 * Example:
 * @code
 * auto client = co_await bidi::Client::connect(io, url)();
 * auto prompt_handler = co_await bidi::UserPromptHandler::create(
 *     client, bidi::UserPromptHandlerConfig::accept_all())();
 * // Prompts automatically handled until prompt_handler goes out of scope
 * @endcode
 */
class UserPromptHandler {
  private:
    std::shared_ptr<Client> client_;
    UserPromptHandlerConfig config_;
    Client::Subscription subscription_;  // RAII subscription

  public:
    // Factory method returns Task<UserPromptHandler> (F.15 - return values over out-params)
    [[nodiscard]] static auto create(
        std::shared_ptr<Client> client,
        UserPromptHandlerConfig config = UserPromptHandlerConfig::accept_all())
        -> Task<UserPromptHandler>;

    // Destructor automatically unsubscribes (RAII)
    ~UserPromptHandler() noexcept = default;

    // C.21: Move-only (subscription is move-only)
    UserPromptHandler(UserPromptHandler&&) noexcept = default;
    auto operator=(UserPromptHandler&&) noexcept -> UserPromptHandler& = default;
    UserPromptHandler(const UserPromptHandler&) = delete;
    auto operator=(const UserPromptHandler&) -> UserPromptHandler& = delete;

    // Update policy at runtime
    void set_policy(UserPromptHandlerConfig new_config) {
        config_ = std::move(new_config);
    }

    [[nodiscard]] auto is_active() const -> bool {
        return subscription_.is_active();
    }

  private:
    explicit UserPromptHandler(std::shared_ptr<Client> client,
                              UserPromptHandlerConfig config,
                              Client::Subscription subscription)
        : client_(std::move(client)),
          config_(std::move(config)),
          subscription_(std::move(subscription)) {}

    // Internal: apply policy to opened prompt
    auto handle_prompt(const types::browsing_context::UserPromptOpenedParameters& params)
        -> void {
        using types::browsing_context::UserPromptResolution;

        auto resolution = [&]() -> std::optional<UserPromptResolution> {
            switch (config_.policy) {
            case UserPromptPolicy::AcceptAll:
                return UserPromptResolution{};
            case UserPromptPolicy::DismissAll:
                return UserPromptResolution{.accept = false};
            case UserPromptPolicy::IgnoreAll:
                return std::nullopt;
            case UserPromptPolicy::Custom:
                if (!config_.custom_handler) {
                    return std::nullopt;
                }
                return (*config_.custom_handler)(params);
            }
            return std::nullopt;
        }();

        if (resolution) {
            auto task = client_->handle_user_prompt(
                params.context, resolution->accept, resolution->user_text);
            asyncx::launch(std::move(task));
        }
    }
};

} // namespace bidi
```

**Implementation Logic:**

1. `create()` subscribes to `browsingContext.userPromptOpened`
2. Event callback extracts JSON and converts to `UserPromptOpenedParameters`
3. `handle_prompt()` applies policy:
   - **AcceptAll** → `asyncx::launch(client->handle_user_prompt(context, true))`
   - **DismissAll** → `asyncx::launch(client->handle_user_prompt(context, false))`
   - **IgnoreAll** → no-op
   - **Custom** → call `config_.custom_handler(params)`, and `asyncx::launch` if a resolution is returned
4. Destructor → `subscription_` auto-unsubscribes (RAII)

Using `asyncx::launch` keeps the lazy execution model intact while ensuring the underlying coroutine starts on the session strand without forcing callers to attach terminals manually.

**Rationale:**
- Factory method returning `Task<UserPromptHandler>` for lazy evaluation (F.15)
- Move-only semantics (C.21 - subscription is move-only)
- RAII subscription cleanup via `Client::Subscription` member
- Private constructor + public factory (encapsulation)
- [[nodiscard]] on factory and query methods

---

### Improvement 4: Client API Extensions

**Guideline Reference:** F.51, ES.20
**Priority:** Medium

Add convenience methods to Client class for prompt handling. Prefer default arguments over overloading (F.51).

#### Proposed Client Methods

Add to `include/bidi/client.hpp`:

```cpp
// ======================== User Prompt API ========================

// Low-level command (explicit control)
[[nodiscard]] auto handle_user_prompt(
    std::string_view context,
    std::optional<bool> accept = std::nullopt,
    std::optional<std::string_view> user_text = std::nullopt)
    -> Task<void>;

// High-level: Create RAII prompt handler
[[nodiscard]] auto create_prompt_handler(
    UserPromptHandlerConfig config = UserPromptHandlerConfig::accept_all())
    -> Task<UserPromptHandler>;

// Event subscription helpers (strongly-typed)
[[nodiscard]] auto on_prompt_opened(
    std::function<void(const types::browsing_context::UserPromptOpenedParameters&)> callback)
    -> Task<Subscription>;

[[nodiscard]] auto on_prompt_closed(
    std::function<void(const types::browsing_context::UserPromptClosedParameters&)> callback)
    -> Task<Subscription>;
```

**Integration Points:**
- Add to `Client` class in `include/bidi/client.hpp`
- Implement in `src/bidi_client.cpp`
- Use existing `commands::browsing_context::handle_user_prompt()` builder
- Convert JSON events to strongly-typed structs before calling user callbacks
- Document that `handle_user_prompt` returns a lazy `Task<void>`; callers must `co_await`, `.finally()`, or `asyncx::launch()` it

---

## Usage Examples

### Example 1: Accept All Prompts Automatically

```cpp
auto client = co_await bidi::Client::connect(io, ws_url)();
auto prompt_handler = co_await client->create_prompt_handler(
    bidi::UserPromptHandlerConfig::accept_all())();

// Navigate to page with alerts - automatically handled
co_await client->navigate(ctx, "http://localhost:8080/alerts.html")();
// All prompts auto-accepted until prompt_handler destroyed
```

### Example 2: Custom Logic Per Prompt Type

```cpp
auto prompt_handler = co_await client->create_prompt_handler(
    bidi::UserPromptHandlerConfig::custom(
        [](const auto& params)
            -> std::optional<bidi::types::browsing_context::UserPromptResolution> {
            using enum bidi::types::browsing_context::UserPromptType;
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
            return std::nullopt;  // Skip handling
        }))();
```

### Example 3: Manual Event Handling

```cpp
auto subscription = co_await client->on_prompt_opened(
    [](const bidi::types::browsing_context::UserPromptOpenedParameters& params) {
        std::cout << "Prompt opened: " << params.message << "\n";
        // Handle manually later
    })();
```

### Example 4: Low-Level Command Usage

```cpp
// Manual control: materialize the lazy Task explicitly
auto prompt_task = client->handle_user_prompt(context_id, true, "my answer");
asyncx::launch(std::move(prompt_task));
```

---

## Testing Requirements

### Unit Tests

Add to `tests/bidi_types_browsing_context_test.cpp`:

- Struct equality operators
- JSON to struct conversion
- Policy enum serialization
- UserPromptResolution construction

### Integration Tests

Create new file `tests/user_prompt_handler_test.cpp`:

- Policy application (AcceptAll, DismissAll, Custom)
- RAII subscription cleanup
- Move semantics
- Concurrent prompt handling
- Policy updates at runtime

### Command Builder Tests

Add to `tests/bidi_builders_test.cpp`:

- `handle_user_prompt()` with optional parameters
- JSON structure validation
- Default parameter handling

---

## Implementation Phases

### Phase 1: Foundation (P0 - Critical)

**Files to modify:**
- `include/bidi/types/browsing_context.hpp` - Add structs
- `include/bidi/commands/browsing_context.hpp` - Add command builder
- `src/bidi_commands.cpp` - Implement command builder
- `include/bidi/client.hpp` - Add low-level method declaration
- `src/bidi_client.cpp` - Implement low-level method
- `tests/bidi_builders_test.cpp` - Add command tests

**Deliverables:**
- Strongly-typed structs
- Low-level command functional
- Unit tests passing

### Phase 2: Policy System (P1 - High)

**Files to modify:**
- `include/bidi/user_prompt_handler.hpp` - New file with policy types
- `src/bidi_user_prompt_handler.cpp` - New file with JSON conversion helpers
- `tests/user_prompt_policy_test.cpp` - New test file

**Deliverables:**
- Policy enum and config
- JSON to struct converters
- Policy tests passing

### Phase 3: RAII Guard (P1 - High)

**Files to modify:**
- `include/bidi/user_prompt_handler.hpp` - Add UserPromptHandler class
- `src/bidi_user_prompt_handler.cpp` - Implement handler logic
- `include/bidi/client.hpp` - Add create_prompt_handler method
- `src/bidi_client.cpp` - Implement factory method
- `tests/user_prompt_handler_test.cpp` - Integration tests

**Deliverables:**
- RAII handler functional
- Factory method working
- RAII cleanup verified

### Phase 4: Convenience APIs (P2 - Medium)

**Files to modify:**
- `include/bidi/client.hpp` - Add on_prompt_opened/closed
- `src/bidi_client.cpp` - Implement event subscriptions
- `examples/flow/example_user_prompts.cpp` - New example file
- `docs/IMPLEMENTATION_EXAMPLES.md` - Add user prompt examples

**Deliverables:**
- Convenience event subscriptions
- Example code
- Documentation updated

---

## Performance Implications

### Zero-Cost Abstractions

- **Policy dispatch**: Uses switch statements (no virtual dispatch overhead)
- **Structs**: POD types with no vtable or dynamic allocation
- **RAII**: Uses existing `Client::Subscription` (no additional allocations)

### Runtime Costs

- **JSON conversion**: One-time cost per event (minimal - field extraction only)
- **Callback invocation**: Single `std::function` call per event
- **Policy check**: Constant-time switch statement

### Memory Usage

- **UserPromptHandler**: ~96 bytes (client ptr + config + subscription)
- **Structs**: 64-128 bytes per instance (short-lived, event scope only)

---

## Backward Compatibility

### Non-Breaking Changes

- All new APIs, no modifications to existing interfaces
- Low-level `set_event_handler()` still available for advanced users
- Progressive disclosure: simple policies for common cases, custom callbacks for complex logic

### Migration Path

Users can adopt incrementally:

1. Start with low-level `handle_user_prompt()` command
2. Add policy-based `UserPromptHandler` when needed
3. Use convenience `on_prompt_opened()` for custom event handling
4. Old code continues to work unchanged

---

## Core Guidelines Compliance Summary

- **C.2**: POD structs for data, class for invariants ✓
- **C.21**: Rule of five (move-only semantics) ✓
- **C.31**: Destructors must not throw ✓
- **C.45**: Factory functions for construction ✓
- **F.15**: Return values over out-parameters ✓
- **F.51**: Default arguments over overloading ✓
- **ES.11**: auto and type inference ✓
- **ES.20**: Always initialize objects ✓
- **C.131**: Avoid trivial getters/setters ✓

---

## Risk Assessment

### Low Risk Areas

- Additive changes only, no modifications to existing code
- Comprehensive test coverage planned
- Follows established project patterns (RAII guards)

### Potential Issues

1. **JSON conversion errors**: Mitigated by optional fields and defensive parsing
2. **Subscription leaks**: Prevented by RAII and move-only semantics
3. **Policy state races**: Config updates are single-threaded (strand-based)

### Mitigation Strategies

- Extensive error handling in JSON conversion
- ASan/LSan testing for leak detection
- Integration tests for concurrent scenarios

---

## Summary

This proposal provides **three levels of abstraction** for user prompt handling:

1. **Low-level** (explicit): `client->handle_user_prompt()` command
2. **Mid-level** (events): `client->on_prompt_opened()` with typed callbacks
3. **High-level** (policy): `UserPromptHandler` with RAII and policies

All levels:
- Follow C++ Core Guidelines strictly
- Integrate with existing `Task<T>` lazy evaluation model
- Provide zero-cost abstractions
- Use RAII for automatic cleanup
- Support progressive adoption

The design emphasizes **type safety**, **composability**, and **ergonomics** while maintaining the project's high-performance, minimalist philosophy.

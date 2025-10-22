# AutomationSession Architecture Review & Improvement Tasks

**Date**: 2025-10-17
**Reviewer**: cpp-architecture-advisor
**Subject**: High-level facade design for `bidi::AutomationSession`
**Status**: Active Development (Pre-Release)

---

## Executive Summary

The `AutomationSession` facade is **well-architected** and leverages existing type-safe infrastructure from the `script` module. The primary issue is not missing abstractions, but rather:

1. **Example code demonstrates anti-patterns** (manual JSON parsing instead of type-safe APIs)
2. **Missing convenience integrations** (FunctionBidi wrapper, navigation control)
3. **API surface inconsistencies** (missing source_location parameters)

**Key Discovery**: The codebase already provides comprehensive type-safe abstractions via:
- `bidi/script/extraction.hpp` - BiDi → C++ type conversion
- `bidi/script/marshalling.hpp` - C++ → BiDi type conversion
- `bidi/script/function_wrapper.hpp` - Type-safe JS function calling

The AutomationSession **already uses these correctly** in its implementation, but the example fails to demonstrate this.

**Note on Async Pattern**: Per `docs/awaitable.md`, the project uses **direct `co_await`** on temporaries (rvalues) without the extra `()` operator. This is the modern, preferred pattern throughout this document.

---

## Current Architecture Assessment

### ✅ Strengths

1. **Type-safe template APIs present**:
   - `evaluate_as<T>()` - Type-safe extraction with exceptions
   - `evaluate_as_or<T>()` - Type-safe extraction with fallback
   - `evaluate_outcome()` - Policy-aware evaluation with exception details
   - `evaluate_as_outcome<T>()` - Combined type safety + policy awareness

2. **Proper use of existing infrastructure**:
   ```cpp
   template <typename T>
   auto evaluate_as(std::string_view expression, ...) -> Task<T> {
       return evaluate(expression, loc)
           .map([expr = std::string(expression)](
                    boost::json::object result) -> T {
               return bidi::script::extract_value<T>(result);  // ✓ Uses extraction.hpp
           });
   }
   ```

3. **Escape hatches for advanced users**:
   - `client()` - Access underlying Client
   - `get_io_context()` - Access io_context for custom async ops
   - `evaluate()` - Raw JSON response access

4. **RAII lifecycle management**:
   - Move-only semantics
   - Automatic resource cleanup
   - IoContextRunner integration

5. **Consistent lazy evaluation**:
   - All operations return `Task<T>`
   - Direct `co_await` on temporaries (modern pattern)
   - Zero-cost abstractions

### ❌ Issues Identified

| Issue | Severity | Location | Impact |
|-------|----------|----------|--------|
| Example shows manual JSON parsing | **CRITICAL** | `example_automation_session_minimal.cpp:33-37` | Users learn anti-patterns |
| Missing FunctionBidi integration | **MEDIUM** | API surface | Lost opportunity for type-safe function calling |
| Missing ReadinessState parameter | **MEDIUM** | `navigate()` method | Limited navigation control |
| Inconsistent source_location | **LOW** | `get_title()`, `get_url()` | Debugging inconsistency |
| Raw evaluate() not documented | **LOW** | API documentation | Unclear when to use low-level API |

---

## Existing Type Infrastructure (Already Available)

### extraction.hpp - BiDi → C++ Conversion

```cpp
namespace bidi::script {

// Extract typed value from BiDi JSON response
template <typename T>
[[nodiscard]] auto extract_value(const boost::json::object &result) -> T;

// Optional extraction (returns std::optional<T>)
template <typename T>
[[nodiscard]] auto extract_value_opt(const boost::json::object &result)
    -> std::optional<T>;

// Extraction with fallback value
template <typename T>
[[nodiscard]] auto extract_value_or(const boost::json::object &result,
                                    T fallback) -> T;

// Extract from policy-aware outcome
template <typename T>
[[nodiscard]] auto extract_value_from_outcome(const ScriptEvalOutcome &outcome)
    -> T;

// Check if result contains extractable value
[[nodiscard]] auto has_value(const boost::json::object &result) noexcept
    -> bool;

} // namespace bidi::script
```

### marshalling.hpp - C++ → BiDi Conversion

```cpp
namespace bidi::script {

// Convert C++ value to BiDi LocalValue JSON
auto make_arg_json(T value) -> boost::json::object;

// Build BiDi arguments array from variadic C++ values
template <typename... Args>
auto make_args_array(Args &&...args) -> boost::json::array;

// Map C++ type to JavaScript type name
template <typename T>
constexpr auto js_typename_for() -> std::string_view;

} // namespace bidi::script
```

### function_wrapper.hpp - Type-Safe Function Calling

```cpp
namespace bidi::script {

// Type-safe JavaScript function wrapper
template <typename Result, typename... Args>
struct FunctionBidi {
    auto operator()(Args... args) const -> Task<Result>;
    // Automatic marshalling, type conversion, exception handling
};

// Factory function
template <typename Result, typename... Args>
auto make_function_caller(
    std::shared_ptr<bidi::Client> client,
    std::string_view context_id,
    std::string function_declaration,
    script_eval_policy policy = throw_on_script_exception)
    -> FunctionBidi<Result, Args...>;

} // namespace bidi::script
```

---

## Prioritized Improvement Tasks

### **PRIORITY 1: Fix Example to Demonstrate Best Practices** 🔴 CRITICAL

**File**: `examples/flow/example_automation_session_minimal.cpp`
**Severity**: CRITICAL - Example teaches users the wrong patterns
**Effort**: 30 minutes

#### Current Code (ANTI-PATTERN)

```cpp
// Lines 33-37: BAD - Demonstrates low-level manual JSON parsing
auto result = co_await session.evaluate("document.readyState");
if (result.contains("value")) {
    std::cout << "Ready state: "
              << boost::json::serialize(result.at("value")) << "\n";
}
```

**Problem**:
- Forces users to understand `boost::json::object` API
- Manual key lookup is error-prone
- Defeats the purpose of a high-level facade
- Users will copy-paste this pattern into their code

#### Proposed Fix

Replace entire example with type-safe API demonstrations:

```cpp
// examples/flow/example_automation_session_minimal.cpp

#include "bidi/automation_session.hpp"
#include "bidi/logging.hpp"
#include "bidi/script_eval.hpp"
#include <boost/asio/awaitable.hpp>
#include <iostream>

namespace asio = boost::asio;

auto main() -> int {
    try {
        // Phase 1: Blocking setup (acceptable for one-time initialization)
        auto session = bidi::AutomationSession::start();

        // Phase 2: Async workflow demonstrating TYPE-SAFE API
        return session.run([&]() -> asio::awaitable<int> {
            // Navigate to example.com (direct co_await on temporary)
            co_await session.navigate("https://example.com");

            // Convenience wrappers (recommended for common operations)
            auto title = co_await session.get_title();
            std::cout << "Page title: " << title << "\n";

            auto url = co_await session.get_url();
            std::cout << "Current URL: " << url << "\n";

            // ========== DEMONSTRATING TYPE-SAFE EVALUATION ==========

            // String extraction (recommended approach)
            auto state = co_await session.evaluate_as<std::string>(
                "document.readyState"
            );
            std::cout << "Ready state: " << state << "\n";

            // Integer extraction
            auto link_count = co_await session.evaluate_as<int>(
                "document.links.length"
            );
            std::cout << "Link count: " << link_count << "\n";

            // Boolean extraction
            auto has_focus = co_await session.evaluate_as<bool>(
                "document.hasFocus()"
            );
            std::cout << "Has focus: " << (has_focus ? "yes" : "no") << "\n";

            // Extraction with fallback (no exceptions)
            auto meta_count = co_await session.evaluate_as_or(
                "document.querySelectorAll('meta').length", 0
            );
            std::cout << "Meta tags: " << meta_count << "\n";

            // ========== ADVANCED: POLICY-AWARE EVALUATION ==========

            using namespace bidi::script;

            // Policy-aware evaluation for better error handling
            auto outcome = co_await session.evaluate_outcome(
                "document.title",
                script_eval_policy::return_outcome
            );

            if (outcome.has_exception()) {
                std::cerr << "Script error: "
                          << outcome.exception->text << "\n";
                if (outcome.exception->line_number) {
                    std::cerr << "  at line "
                              << *outcome.exception->line_number << "\n";
                }
            } else {
                // Extract from outcome with type safety
                auto title_str = extract_value_from_outcome<std::string>(outcome);
                std::cout << "Title (from outcome): " << title_str << "\n";
            }

            co_return 0; // Success
        });

    } catch (const bidi::script::ScriptEvaluateException &e) {
        // Handle script-specific errors
        bidi::logging::log_error(
            std::string("Script error: ") + e.what()
        );
        return 1;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Fatal error: ") + e.what());
        return 1;
    }
}
```

#### Rationale

- **Demonstrates best practices**: Users learn type-safe patterns first
- **Shows API progression**: Convenience → type-safe → policy-aware
- **Eliminates manual JSON parsing**: No `boost::json::object` exposure
- **Educational value**: Comments explain each pattern
- **Real-world patterns**: Fallback values, error handling, boolean checks
- **Modern async pattern**: Direct `co_await` on temporaries (no extra `()`)

#### Testing

After implementation:
```bash
cd build
cmake --build . -j24
./example_automation_session_minimal
# Verify output shows type-safe extractions
```

---

### **PRIORITY 2: Add FunctionBidi Integration** 🟡 MEDIUM

**File**: `include/bidi/automation_session.hpp`
**Severity**: MEDIUM - Missing ergonomic feature
**Effort**: 1 hour

#### Problem

`FunctionBidi` provides powerful type-safe JavaScript function calling, but `AutomationSession` doesn't expose it. Users must manually:
1. Access `client()`
2. Pass `context_id()`
3. Call `make_function_caller<R, Args...>()`

This defeats the facade's purpose of hiding boilerplate.

#### Proposed API Addition

Add to `automation_session.hpp`:

```cpp
/**
 * @brief Create type-safe JavaScript function caller
 *
 * Wraps FunctionBidi with automatic context management.
 * The returned callable can be invoked multiple times with C++ arguments
 * that are automatically marshalled to JavaScript.
 *
 * @tparam Result Expected C++ return type
 * @tparam Args C++ argument types (automatically marshalled)
 * @param function_declaration JavaScript function source code
 * @param policy Script exception handling policy
 * @return FunctionBidi callable object
 *
 * @example Basic arithmetic
 * @code
 * auto add = session.make_function<int, int, int>(
 *     "function(a, b) { return a + b; }"
 * );
 * auto result = co_await add(2, 3);  // returns 5
 * @endcode
 *
 * @example DOM queries
 * @code
 * auto get_element_count = session.make_function<int, std::string>(
 *     "function(selector) { "
 *     "  return document.querySelectorAll(selector).length; "
 *     "}"
 * );
 * auto div_count = co_await get_element_count("div");
 * auto link_count = co_await get_element_count("a");
 * @endcode
 *
 * @example String manipulation
 * @code
 * auto get_attribute = session.make_function<std::string, std::string, std::string>(
 *     "function(selector, attr) { "
 *     "  return document.querySelector(selector).getAttribute(attr); "
 *     "}"
 * );
 * auto href = co_await get_attribute("a.first", "href");
 * @endcode
 *
 * @see FunctionBidi for implementation details
 * @see bidi::script::make_function_caller for underlying factory
 */
template<typename Result, typename... Args>
[[nodiscard]] auto make_function(
    std::string function_declaration,
    script::script_eval_policy policy =
        script::script_eval_policy::throw_on_script_exception,
    const std::source_location &loc = std::source_location::current())
    -> script::FunctionBidi<Result, Args...> {
    (void)loc; // Available for debugging via GDB
    return script::make_function_caller<Result, Args...>(
        client_, context_id_, std::move(function_declaration), policy
    );
}
```

#### Implementation

No changes needed in `.cpp` - this is a template that delegates to existing infrastructure.

#### Usage Example

```cpp
return session.run([&]() -> asio::awaitable<int> {
    co_await session.navigate("https://github.com");

    // Define reusable type-safe functions
    auto count_elements = session.make_function<int, std::string>(
        "function(selector) { "
        "  return document.querySelectorAll(selector).length; "
        "}"
    );

    auto get_text = session.make_function<std::string, std::string>(
        "function(selector) { "
        "  return document.querySelector(selector)?.textContent || ''; "
        "}"
    );

    // Call with type safety (direct co_await on temporaries)
    auto repo_count = co_await count_elements(".repo-list li");
    std::cout << "Repositories: " << repo_count << "\n";

    auto username = co_await get_text(".user-profile-name");
    std::cout << "Username: " << username << "\n";

    co_return 0;
});
```

#### Rationale

- **Eliminates boilerplate**: No need to expose `client()` + `context_id()`
- **Maintains type safety**: Full template parameter control
- **Reusable functions**: Define once, call multiple times
- **Consistent with facade philosophy**: High-level convenience over low-level access
- **Zero-cost**: Template inlines to same code as manual approach
- **Modern pattern**: Direct `co_await` on function calls (temporaries)

---

### **PRIORITY 3: Add ReadinessState Parameter to navigate()** 🟡 MEDIUM

**File**: `include/bidi/automation_session.hpp`, `src/bidi_automation_session.cpp`
**Severity**: MEDIUM - Missing critical navigation control
**Effort**: 30 minutes

#### Problem

The underlying `Client::navigate()` accepts `ReadinessState` parameter:
```cpp
enum class ReadinessState { none, interactive, complete };
```

But `AutomationSession::navigate()` doesn't expose it, forcing all navigations to wait for `complete`. This is problematic for:
- Fast page interactions (can start at `interactive`)
- Slow-loading pages with heavy resources
- Testing scenarios requiring different wait strategies

#### Current API

```cpp
[[nodiscard]] auto navigate(
    std::string_view url,
    const std::source_location &loc = std::source_location::current())
    -> Task<std::string>;
```

#### Proposed API

```cpp
/**
 * @brief Navigate to URL in the default context (ASYNC)
 *
 * @param url Target URL to navigate to
 * @param wait Readiness state to wait for (default: complete)
 *             - none: Return immediately after navigation starts
 *             - interactive: Wait for DOM ready (DOMContentLoaded)
 *             - complete: Wait for full page load (window.onload)
 * @param loc Source location for debugging
 * @return Task<std::string> - Navigation ID (lazy, awaitable)
 *
 * @example Fast navigation (interactive)
 * @code
 * using namespace bidi::commands::browsing_context;
 * auto nav_id = co_await session.navigate(
 *     "https://example.com",
 *     ReadinessState::interactive
 * );
 * // Can interact with DOM before all resources load
 * @endcode
 *
 * @example Default navigation (complete)
 * @code
 * auto nav_id = co_await session.navigate("https://example.com");
 * // Waits for full page load including images, scripts, etc.
 * @endcode
 */
[[nodiscard]] auto navigate(
    std::string_view url,
    commands::browsing_context::ReadinessState wait =
        commands::browsing_context::ReadinessState::complete,
    const std::source_location &loc = std::source_location::current())
    -> Task<std::string>;
```

#### Implementation

Update `src/bidi_automation_session.cpp`:

```cpp
auto AutomationSession::navigate(
    std::string_view url,
    commands::browsing_context::ReadinessState wait,
    const std::source_location &loc) -> Task<std::string> {
    return client_->navigate(context_id_, url, wait, loc);
}
```

#### Migration Impact

✅ **Backward compatible**: Default parameter maintains existing behavior
✅ **No breaking changes**: Existing calls continue to work
✅ **Opt-in control**: Advanced users can specify wait state

#### Usage Examples

```cpp
// Fast interaction pattern (direct co_await)
co_await session.navigate(
    "https://dynamic-app.com",
    commands::browsing_context::ReadinessState::interactive
);
// Start interacting before all images/scripts load

// Default complete wait
co_await session.navigate("https://example.com");
// Everything loaded

// Fire-and-forget navigation (rare)
co_await session.navigate(
    "https://redirect.com",
    commands::browsing_context::ReadinessState::none
);
// Returns immediately
```

#### Rationale

- **Exposes important control**: Navigation timing is critical for automation
- **Maintains simplicity**: Default behavior unchanged
- **Aligns with Client API**: Consistent parameter surface
- **Real-world use case**: Interactive wait is common in production automation

---

### **PRIORITY 4: Add Missing source_location Parameters** 🟢 LOW

**Files**: `include/bidi/automation_session.hpp`, `src/bidi_automation_session.cpp`
**Severity**: LOW - Debugging inconsistency
**Effort**: 15 minutes

#### Problem

Most methods have `source_location` parameters for GDB debugging, but these are missing:
- `get_title()` - Line 117
- `get_url()` - Line 124

#### Current API

```cpp
[[nodiscard]] auto get_title() -> Task<std::string>;
[[nodiscard]] auto get_url() -> Task<std::string>;
```

#### Proposed Fix

```cpp
[[nodiscard]] auto get_title(
    const std::source_location &loc = std::source_location::current())
    -> Task<std::string>;

[[nodiscard]] auto get_url(
    const std::source_location &loc = std::source_location::current())
    -> Task<std::string>;
```

#### Implementation

Update method signatures in both header and implementation file. Ensure `loc` is passed to underlying `evaluate()` calls.

#### Rationale

- **Consistency**: All async operations should have `source_location`
- **Debugging**: GDB inspection shows call site information
- **Architectural principle**: Per project guidelines, all async ops need tracing
- **Zero runtime cost**: Default parameter, inlined by compiler

---

### **PRIORITY 5: Document evaluate() as Advanced API** 🟢 LOW

**File**: `include/bidi/automation_session.hpp`
**Severity**: LOW - API guidance
**Effort**: 10 minutes

#### Problem

The raw `evaluate()` method returns `boost::json::object`, which is low-level. Users might use it instead of the better type-safe alternatives if they don't know when each is appropriate.

#### Current Documentation

```cpp
/**
 * @brief Evaluate JavaScript expression in the default context (ASYNC)
 *
 * @param expression JavaScript code to evaluate
 * @return Task<boost::json::object> - Evaluation result (lazy, awaitable)
 *
 * @example
 * @code
 * auto result = co_await session.evaluate("document.title");
 * auto title = result.at("value").as_string();
 * @endcode
 */
[[nodiscard]] auto evaluate(...) -> Task<boost::json::object>;
```

#### Proposed Documentation

```cpp
/**
 * @brief Evaluate JavaScript expression - LOW-LEVEL API (ADVANCED)
 *
 * Returns raw BiDi JSON response. **Most users should use evaluate_as<T>()**
 * for type-safe extraction instead.
 *
 * **When to use this API:**
 * - You need access to full BiDi response structure
 * - You're building custom extraction logic
 * - You need metadata beyond the result value
 *
 * **Recommended alternatives:**
 * - `evaluate_as<T>(expr)` - Type-safe extraction
 * - `evaluate_as_or<T>(expr, fallback)` - With fallback value
 * - `evaluate_outcome(expr)` - Policy-aware with exception details
 *
 * @param expression JavaScript code to evaluate
 * @return Task<boost::json::object> - Raw BiDi evaluation result
 *
 * @example Advanced: Inspecting full response structure
 * @code
 * auto result = co_await session.evaluate("document.title");
 * if (result.contains("type")) {
 *     std::string result_type = result.at("type").as_string();
 *     // Custom logic based on type
 * }
 * @endcode
 *
 * @example Recommended: Use type-safe API instead
 * @code
 * // BETTER: Type-safe, no manual JSON parsing
 * auto title = co_await session.evaluate_as<std::string>("document.title");
 * @endcode
 *
 * @see evaluate_as<T>() for type-safe alternative
 * @see evaluate_outcome() for policy-aware evaluation
 * @note This is an escape hatch for advanced users. Prefer type-safe APIs.
 */
[[nodiscard]] auto evaluate(
    std::string_view expression,
    const std::source_location &loc = std::source_location::current())
    -> Task<boost::json::object>;
```

#### Rationale

- **Guides users**: Clear when to use low-level vs high-level API
- **Preserves flexibility**: Keeps escape hatch available
- **Educational**: Shows better alternatives inline
- **Discourages misuse**: "ADVANCED" label sets expectations

---

## Optional Future Enhancements

### NavigationResult Domain Type

**Priority**: DEFER (not needed for current API)

If navigation metadata becomes important, consider:

```cpp
struct NavigationResult {
    std::string navigation_id;
    std::string final_url;  // After redirects
    std::chrono::milliseconds duration;
};
```

**Rationale for deferring**:
- Current string return (navigation_id) is sufficient
- No user requests for navigation metadata
- Can add later without breaking changes (return type change from string to struct)

### BrowsingContext Abstraction

**Priority**: DEFER (not needed yet)

For multi-window/tab scenarios, extract context operations:

```cpp
class BrowsingContext {
    auto navigate(std::string_view url) -> Task<NavigationResult>;
    auto evaluate_as<T>(std::string_view expr) -> Task<T>;
    auto close() -> Task<void>;
    std::string id_;
};
```

**Rationale for deferring**:
- No multi-context use cases in examples
- Single-context (AutomationSession) covers 90% of cases
- Adds complexity without current demand

---

## Implementation Checklist

### Priority 1: Fix Example ✓
- [ ] Update `example_automation_session_minimal.cpp` with type-safe patterns
- [ ] Remove all manual `boost::json::object` parsing
- [ ] Add comments explaining each pattern
- [ ] Use direct `co_await` pattern (no extra `()`)
- [ ] Test example builds and runs successfully
- [ ] Verify output demonstrates type-safe extractions

### Priority 2: FunctionBidi Integration ✓
- [ ] Add `make_function<R, Args...>()` template to `automation_session.hpp`
- [ ] Add comprehensive documentation with 3+ examples
- [ ] Add source_location parameter
- [ ] Ensure examples use direct `co_await` pattern
- [ ] Test with various function signatures (int, string, bool returns)
- [ ] Update example to demonstrate function wrapper usage

### Priority 3: ReadinessState Parameter ✓
- [ ] Update `navigate()` signature in `automation_session.hpp`
- [ ] Update implementation in `src/bidi_automation_session.cpp`
- [ ] Add documentation for each ReadinessState value
- [ ] Add usage examples with direct `co_await` pattern
- [ ] Test with all three wait states (none, interactive, complete)

### Priority 4: Missing source_location ✓
- [ ] Update `get_title()` signature in header and implementation
- [ ] Update `get_url()` signature in header and implementation
- [ ] Pass `loc` to underlying `evaluate()` calls
- [ ] Verify no compilation warnings

### Priority 5: Document evaluate() ✓
- [ ] Update doc comment for `evaluate()` method
- [ ] Mark as "ADVANCED" / "LOW-LEVEL"
- [ ] Add "When to use" and "Recommended alternatives" sections
- [ ] Update examples to use direct `co_await` pattern
- [ ] Add @see references to type-safe alternatives

---

## Testing Strategy

### Unit Tests
No new unit tests required - all changes are API surface updates using existing implementation.

### Integration Tests
Update existing integration tests to use type-safe APIs:
```cpp
// Before:
auto result = co_await session.evaluate("document.title");
EXPECT_TRUE(result.contains("value"));

// After (modern pattern):
auto title = co_await session.evaluate_as<std::string>("document.title");
EXPECT_FALSE(title.empty());
```

### Example Validation
```bash
cd build
cmake --build . -j24
./example_automation_session_minimal
# Expected output: No boost::json mentions, only clean type-safe operations
```

---

## Alignment with Project Philosophy

| Principle | Alignment |
|-----------|-----------|
| Zero-cost abstractions | ✅ Templates inline to same code as manual |
| RAII lifecycle | ✅ FunctionBidi is value type, no manual cleanup |
| Lazy evaluation | ✅ All operations return Task<T> |
| Modern async pattern | ✅ Direct `co_await` on temporaries (per `docs/awaitable.md`) |
| Type safety | ✅ Compile-time type checking, no runtime overhead |
| Modern C++ | ✅ Uses C++20/23 features (templates, concepts-ready) |
| Development freedom | ✅ Breaking changes allowed (pre-release) |
| Minimalist design | ✅ Leverages existing infrastructure, no duplication |
| Escape hatches | ✅ Low-level APIs remain available for advanced users |

---

## Conclusion

The `AutomationSession` facade is architecturally sound and properly leverages the existing type-safe script infrastructure. The main issues are:

1. **Example quality** (teaches wrong patterns)
2. **API completeness** (missing FunctionBidi, ReadinessState)
3. **Documentation clarity** (when to use low-level vs high-level)

All recommended changes are **non-breaking** and align with the project's zero-cost abstraction philosophy. Total implementation effort: ~3-4 hours.

**Primary focus**: Fix the example first (P1) - it's the primary learning resource for users and currently demonstrates anti-patterns.

**Modern Async Pattern**: All examples now use the modern **direct `co_await`** pattern on temporaries (rvalues) as documented in `docs/awaitable.md`. No extra `()` operator needed.

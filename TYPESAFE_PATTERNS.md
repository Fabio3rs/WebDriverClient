# Type Safety Patterns - Implementation Guide

**Date:** 2025-10-11
**Status:** Design Proposal
**Based on:** `include/bidi/script/marshalling.hpp` and `include/bidi/script/function_wrapper.hpp`

---

## Overview

The project already has excellent type-safe patterns for JavaScript interop:
- **Input marshalling** (C++ → BiDi): `marshalling.hpp`
- **Output unmarshalling** (BiDi → C++): `function_wrapper.hpp`

This document shows how to apply these patterns consistently across the codebase.

---

## Existing Pattern Analysis

### Pattern 1: Input Marshalling (C++ → BiDi)

**File:** `include/bidi/script/marshalling.hpp`

**Key technique:** Overload set with SFINAE for type-specific conversions

```cpp
// Overload set for all C++ types → BiDi LocalValue
inline auto make_arg_json(std::nullptr_t) -> boost::json::object;
inline auto make_arg_json(bool value) -> boost::json::object;
inline auto make_arg_json(double value) -> boost::json::object;
inline auto make_arg_json(std::string_view str) -> boost::json::object;

// Template for integral types (excludes bool)
template <typename Int>
inline auto make_arg_json(Int &&value)
    -> std::enable_if_t<std::is_integral_v<std::decay_t<Int>> &&
                        !std::is_same_v<std::decay_t<Int>, bool>,
                        boost::json::object>;

// Fallback for boost::json serializable types
template <typename T>
inline auto make_arg_json(T &&value) -> std::enable_if_t<...>;

// Variadic wrapper
template <typename... Args>
auto make_args_array(Args &&...args) -> boost::json::array {
    return boost::json::array{make_arg_json(std::forward<Args>(args))...};
}
```

**Benefits:**
- ✅ Zero-cost abstraction (all inline)
- ✅ Compile-time type checking
- ✅ Extensible (add overloads for custom types)
- ✅ Perfect forwarding for efficiency

---

### Pattern 2: Output Unmarshalling (BiDi → C++)

**File:** `include/bidi/script/function_wrapper.hpp`

**Key technique:** Template with `boost::json::value_to<T>` + exception handling

```cpp
template <typename Result, typename... Args>
struct FunctionBidi {
    auto operator()(Args... args) const -> Task<Result> {
        // 1. Marshal inputs
        auto args_array = make_args_array(args...);

        // 2. Call BiDi operation
        auto res = client->call_function(function_declaration, context_id,
                                         args_array, policy);

        // 3. Unmarshal output
        return res.map([this](const script::ScriptEvalOutcome &outcome) {
            // Exception handling
            if (outcome.has_exception()) {
                throw script::ScriptEvaluateException(*outcome.exception);
            }

            // Extract value with type conversion
            auto object = outcome.result.get_object();
            auto result = object.at("result").get_object();
            auto value = result.at("value");

            return boost::json::value_to<Result>(value);  // ← Type-safe!
        });
    }
};
```

**Benefits:**
- ✅ Type-safe input and output
- ✅ Automatic exception handling
- ✅ Clear error messages with context
- ✅ Composable with `.map()`, `.recover()`, etc.

---

## Proposed Implementations

### 1. Generic Value Extraction Helper

**Location:** `include/bidi/script/extraction.hpp` (NEW FILE)

**Purpose:** Reusable helper for extracting typed values from BiDi responses

```cpp
#pragma once
/**
 * @file extraction.hpp
 * @brief Type-safe value extraction from BiDi script evaluation results
 *
 * Provides generic helpers for converting BiDi JSON responses to C++ types
 * with proper error handling and context preservation.
 */

#include <boost/json.hpp>
#include <boost/json/value_to.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bidi::script {

/**
 * @brief Extract typed value from BiDi evaluation result
 *
 * Handles the common pattern:
 * {
 *   "type": "...",
 *   "result": {
 *     "type": "...",
 *     "value": <actual_value>
 *   }
 * }
 *
 * @tparam T Target C++ type
 * @param result BiDi evaluation result object
 * @return Extracted value converted to type T
 * @throws std::runtime_error if extraction fails
 */
template <typename T>
[[nodiscard]] auto extract_value(const boost::json::object &result) -> T {
    try {
        // Navigate to the actual value
        if (!result.contains("result")) {
            throw std::runtime_error("Missing 'result' field in evaluation response");
        }

        auto result_obj = result.at("result");
        if (!result_obj.is_object()) {
            throw std::runtime_error("'result' field is not an object");
        }

        auto result_inner = result_obj.get_object();
        if (!result_inner.contains("value")) {
            throw std::runtime_error("Missing 'value' field in result object");
        }

        // Type-safe conversion using Boost.JSON
        return boost::json::value_to<T>(result_inner.at("value"));

    } catch (const std::exception &e) {
        throw std::runtime_error(
            std::string("Failed to extract value as ") + typeid(T).name() +
            ": " + e.what() +
            "; Result: " + boost::json::serialize(result));
    }
}

/**
 * @brief Extract optional typed value from BiDi result
 *
 * Returns std::nullopt if value is missing or extraction fails.
 * Useful for optional properties or fallback scenarios.
 *
 * @tparam T Target C++ type
 * @param result BiDi evaluation result object
 * @return std::optional<T> containing value or nullopt
 */
template <typename T>
[[nodiscard]] auto extract_value_opt(const boost::json::object &result)
    -> std::optional<T> {
    try {
        return extract_value<T>(result);
    } catch (...) {
        return std::nullopt;
    }
}

/**
 * @brief Extract value with fallback
 *
 * @tparam T Target C++ type
 * @param result BiDi evaluation result object
 * @param fallback Default value if extraction fails
 * @return Extracted value or fallback
 */
template <typename T>
[[nodiscard]] auto extract_value_or(const boost::json::object &result,
                                    T fallback) -> T {
    return extract_value_opt<T>(result).value_or(std::move(fallback));
}

/**
 * @brief Check if result contains a valid value
 *
 * @param result BiDi evaluation result object
 * @return true if result has extractable value
 */
[[nodiscard]] inline auto has_value(const boost::json::object &result) noexcept
    -> bool {
    try {
        return result.contains("result") &&
               result.at("result").is_object() &&
               result.at("result").as_object().contains("value");
    } catch (...) {
        return false;
    }
}

} // namespace bidi::script
```

---

### 2. Type-Safe `AutomationSession::evaluate_as<T>()`

**Location:** `include/bidi/automation_session.hpp` (ADD TO EXISTING)

**Implementation:**

```cpp
/**
 * @brief Type-safe JavaScript evaluation (ASYNC)
 *
 * Evaluates expression and automatically converts result to specified type.
 * Eliminates manual JSON parsing boilerplate.
 *
 * @tparam T Expected return type (must be Boost.JSON compatible)
 * @param expression JavaScript code to evaluate
 * @return Task<T> - Typed result (lazy, call () to execute)
 *
 * @throws std::runtime_error if conversion fails
 * @throws ScriptEvaluateException if script execution fails
 *
 * @example
 * @code
 * // Instead of manual parsing:
 * auto result = co_await session.evaluate("document.readyState")();
 * std::string state = result.at("value").as_string();  // Manual!
 *
 * // Type-safe alternative:
 * auto state = co_await session.evaluate_as<std::string>("document.readyState")();
 *
 * // Works with all JSON-compatible types:
 * auto count = co_await session.evaluate_as<int>("document.links.length")();
 * auto visible = co_await session.evaluate_as<bool>("document.hasFocus()")();
 * @endcode
 */
template <typename T>
[[nodiscard]] auto evaluate_as(std::string_view expression) -> Task<T> {
    return evaluate(expression)
        .map([expr = std::string(expression)](boost::json::object result) -> T {
            try {
                return bidi::script::extract_value<T>(result);
            } catch (const std::exception &e) {
                throw std::runtime_error(
                    std::string("evaluate_as failed for expression '") +
                    expr + "': " + e.what());
            }
        });
}

/**
 * @brief Type-safe evaluation with fallback (ASYNC)
 *
 * @tparam T Expected return type
 * @param expression JavaScript code to evaluate
 * @param fallback Default value if evaluation or conversion fails
 * @return Task<T> - Typed result or fallback
 *
 * @example
 * @code
 * auto title = co_await session.evaluate_as_or("document.title",
 *                                               std::string("Untitled"))();
 * @endcode
 */
template <typename T>
[[nodiscard]] auto evaluate_as_or(std::string_view expression,
                                   T fallback) -> Task<T> {
    return evaluate(expression)
        .map([fallback = std::move(fallback)](boost::json::object result) -> T {
            return bidi::script::extract_value_or(result, fallback);
        });
}
```

---

### 3. Refactor Existing Convenience Methods

**Current implementation** in `src/bidi_automation_session.cpp`:

```cpp
// DRY VIOLATION (duplicated logic)
auto AutomationSession::get_title() -> Task<std::string> {
    return evaluate("document.title")
        .map([](boost::json::object result) -> std::string {
            if (result.contains("value") && result.at("value").is_string()) {
                return std::string(result.at("value").as_string());
            }
            return "";
        });
}

auto AutomationSession::get_url() -> Task<std::string> {
    return evaluate("document.location.href")
        .map([](boost::json::object result) -> std::string {
            if (result.contains("value") && result.at("value").is_string()) {
                return std::string(result.at("value").as_string());
            }
            return "";
        });
}
```

**Refactored with type-safe helpers:**

```cpp
auto AutomationSession::get_title() -> Task<std::string> {
    return evaluate_as_or("document.title", std::string(""));
}

auto AutomationSession::get_url() -> Task<std::string> {
    return evaluate_as_or("document.location.href", std::string(""));
}
```

**Benefits:**
- ✅ Eliminates 90% of the code
- ✅ No DRY violation
- ✅ Consistent error handling
- ✅ Better error messages (includes expression in exception)

---

### 4. Additional Convenience Methods (Optional)

With type-safe helpers, adding new convenience methods becomes trivial:

```cpp
// In automation_session.hpp

/**
 * @brief Get document ready state (ASYNC)
 * @return Task<std::string> - "loading", "interactive", or "complete"
 */
[[nodiscard]] auto get_ready_state() -> Task<std::string> {
    return evaluate_as<std::string>("document.readyState");
}

/**
 * @brief Check if document has focus (ASYNC)
 * @return Task<bool> - true if document has focus
 */
[[nodiscard]] auto has_focus() -> Task<bool> {
    return evaluate_as<bool>("document.hasFocus()");
}

/**
 * @brief Get number of links in document (ASYNC)
 * @return Task<int> - Link count
 */
[[nodiscard]] auto get_link_count() -> Task<int> {
    return evaluate_as<int>("document.links.length");
}

/**
 * @brief Get page body text (ASYNC)
 * @return Task<std::string> - Body text content
 */
[[nodiscard]] auto get_body_text() -> Task<std::string> {
    return evaluate_as_or("document.body?.innerText", std::string(""));
}

/**
 * @brief Execute custom JavaScript and get typed result (ASYNC)
 *
 * Generic helper for common evaluation patterns.
 *
 * @tparam T Expected return type
 * @param expression JavaScript expression
 * @return Task<T> - Typed result
 */
template <typename T>
[[nodiscard]] auto exec(std::string_view expression) -> Task<T> {
    return evaluate_as<T>(expression);
}
```

**Usage becomes extremely clean:**

```cpp
return session.run([&]() -> asio::awaitable<int> {
    co_await session.navigate("https://example.com");

    // Type-safe, no manual parsing!
    auto title = co_await session.get_title();
    auto url = co_await session.get_url();
    auto ready = co_await session.get_ready_state();
    auto focused = co_await session.has_focus();
    auto links = co_await session.get_link_count();

    std::cout << "Title: " << title << "\n"
              << "URL: " << url << "\n"
              << "Ready: " << ready << "\n"
              << "Focused: " << std::boolalpha << focused << "\n"
              << "Links: " << links << "\n";

    co_return 0;
});
```

---

## Implementation Plan

### Phase 1: Core Infrastructure (P1)

1. **Create `include/bidi/script/extraction.hpp`**
   - Implement `extract_value<T>()`
   - Implement `extract_value_opt<T>()`
   - Implement `extract_value_or<T>()`
   - Add unit tests

2. **Add to `automation_session.hpp`**
   - Implement `evaluate_as<T>()`
   - Implement `evaluate_as_or<T>()`
   - Add Doxygen documentation with examples

### Phase 2: Refactor Existing Code (P1)

3. **Refactor `bidi_automation_session.cpp`**
   - Replace manual parsing in `get_title()`
   - Replace manual parsing in `get_url()`
   - Remove local helper (DRY violation)

### Phase 3: Expand Convenience API (P2 - Optional)

4. **Add more convenience methods** (if desired)
   - `get_ready_state()`
   - `has_focus()`
   - `get_link_count()`
   - `get_body_text()`
   - Document criteria for inclusion

---

## Criteria for Convenience Methods

Based on architectural review, establish clear criteria:

**Include in facade if:**
1. ✅ Used in >80% of automation scripts
2. ✅ Requires complex parsing (like `get_title`)
3. ✅ Standard DOM API (not application-specific)
4. ✅ Single expression (no multi-step logic)

**Exclude from facade if:**
1. ❌ Application-specific selector
2. ❌ Requires multiple BiDi calls
3. ❌ Can be trivially done with `evaluate_as<T>()`
4. ❌ Niche use case (<20% of scripts)

**Examples:**

✅ **Include:** `get_title()` - universal, complex result structure
✅ **Include:** `get_url()` - universal, used everywhere
✅ **Include:** `get_ready_state()` - standard DOM, common
❌ **Exclude:** `get_product_price()` - application-specific
❌ **Exclude:** `click_button(id)` - multi-step (find + click)
⚠️ **Maybe:** `get_body_text()` - borderline, but common enough

---

## Type Compatibility Reference

`boost::json::value_to<T>` supports:

**Primitive types:**
- `bool`, `int`, `int64_t`, `uint64_t`
- `float`, `double`
- `std::string`, `std::string_view`

**Containers:**
- `std::vector<T>`
- `std::map<std::string, T>`
- `std::optional<T>`

**JSON types:**
- `boost::json::object`
- `boost::json::array`
- `boost::json::value`

**Custom types:**
Add `tag_invoke` overload for custom conversions.

---

## Testing Strategy

### Unit Tests for Extraction Helpers

```cpp
// tests/script_extraction_test.cpp

TEST(ScriptExtraction, ExtractString) {
    boost::json::object result = {
        {"result", boost::json::object{
            {"type", "string"},
            {"value", "Hello, World!"}
        }}
    };

    auto str = bidi::script::extract_value<std::string>(result);
    EXPECT_EQ(str, "Hello, World!");
}

TEST(ScriptExtraction, ExtractInt) {
    boost::json::object result = {
        {"result", boost::json::object{
            {"type", "number"},
            {"value", 42}
        }}
    };

    auto num = bidi::script::extract_value<int>(result);
    EXPECT_EQ(num, 42);
}

TEST(ScriptExtraction, ExtractOptionalMissing) {
    boost::json::object result = {{"type", "undefined"}};

    auto opt = bidi::script::extract_value_opt<std::string>(result);
    EXPECT_FALSE(opt.has_value());
}

TEST(ScriptExtraction, ExtractWithFallback) {
    boost::json::object bad_result = {{"error", "failed"}};

    auto str = bidi::script::extract_value_or(bad_result, std::string("default"));
    EXPECT_EQ(str, "default");
}
```

### Integration Tests for AutomationSession

```cpp
// tests/automation_session_typesafe_test.cpp

TEST_F(AutomationSessionTest, EvaluateAsString) {
    auto session = AutomationSession::start();

    int exit_code = session.run([&]() -> asio::awaitable<int> {
        co_await session.navigate("https://example.com")();

        auto title = co_await session.evaluate_as<std::string>("document.title")();
        EXPECT_FALSE(title.empty());

        co_return 0;
    });

    EXPECT_EQ(exit_code, 0);
}

TEST_F(AutomationSessionTest, EvaluateAsInt) {
    auto session = AutomationSession::start();

    int exit_code = session.run([&]() -> asio::awaitable<int> {
        co_await session.navigate("https://example.com")();

        auto link_count = co_await session.evaluate_as<int>(
            "document.links.length")();
        EXPECT_GE(link_count, 0);

        co_return 0;
    });

    EXPECT_EQ(exit_code, 0);
}
```

---

## Performance Considerations

**Zero-cost abstractions maintained:**
- ✅ All helpers are inline templates
- ✅ No runtime polymorphism
- ✅ No virtual calls
- ✅ Compile-time type checking
- ✅ Perfect forwarding where applicable
- ✅ Move semantics throughout

**Benchmarks** (expected, needs verification):
```
evaluate() + manual parsing:        ~100ns overhead
evaluate_as<T>():                   ~100ns overhead (same!)
get_title() (before refactor):      ~100ns overhead
get_title() (after refactor):       ~100ns overhead (same!)
```

The abstraction cost should be zero or negligible.

---

## Migration Path

**Step-by-step process:**

1. ✅ Create `script/extraction.hpp` with helpers
2. ✅ Add `evaluate_as<T>()` to `AutomationSession`
3. ✅ Refactor existing convenience methods to use new helpers
4. ✅ Update examples to show type-safe patterns
5. ✅ Add comprehensive tests
6. ✅ Update CLAUDE.md with type-safe API documentation

**Breaking changes:** None (purely additive)

**Deprecation:** Not needed (old API remains functional)

---

## Documentation Updates

Add to `CLAUDE.md`:

```markdown
### Type-Safe Evaluation Helpers

**evaluate_as<T>()** - Type-safe evaluation:
```cpp
// Manual parsing (old way):
auto result = co_await session.evaluate("document.readyState")();
std::string state = result.at("value").as_string();  // Manual!

// Type-safe (new way):
auto state = co_await session.evaluate_as<std::string>("document.readyState")();
auto count = co_await session.evaluate_as<int>("document.links.length")();
auto focused = co_await session.evaluate_as<bool>("document.hasFocus()")();
```

**evaluate_as_or<T>()** - With fallback:
```cpp
auto title = co_await session.evaluate_as_or("document.title",
                                             std::string("Untitled"))();
```
```

---

## Summary

**Key benefits of this approach:**

1. ✅ **Consistency**: Same pattern as existing `function_wrapper.hpp`
2. ✅ **Type safety**: Compile-time checking, runtime validation
3. ✅ **Zero cost**: All inline, no runtime overhead
4. ✅ **DRY**: Eliminates duplicated parsing logic
5. ✅ **Usability**: 90% less boilerplate for users
6. ✅ **Extensibility**: Easy to add new convenience methods
7. ✅ **Maintainability**: Single source of truth for extraction logic

**Implementation priority:** High (P1) - addresses DRY violation and improves usability significantly.

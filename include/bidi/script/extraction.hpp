#pragma once
/**
 * @file extraction.hpp
 * @brief Type-safe value extraction from BiDi script evaluation results
 *
 * Provides generic helpers for converting BiDi JSON responses to C++ types
 * with proper error handling and context preservation.
 *
 * These helpers follow the same pattern as function_wrapper.hpp and
 * marshalling.hpp for consistent type-safe operations across the codebase.
 */

#include "bidi/script_eval.hpp"
#include <boost/json.hpp>
#include <boost/json/value_to.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>

// Forward declarations must be included
namespace bidi::script {
struct ScriptExceptionDetails;
struct ScriptEvalOutcome;
class ScriptEvaluateException;
} // namespace bidi::script

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
 *
 * @example
 * @code
 * boost::json::object result = get_eval_result();
 * auto title = extract_value<std::string>(result);
 * auto count = extract_value<int>(result);
 * @endcode
 */
template <typename T>
[[nodiscard]] auto extract_value(const boost::json::object &result) -> T {
    try {
        // Navigate to the actual value
        if (!result.contains("result")) {
            throw std::runtime_error(
                "Missing 'result' field in evaluation response");
        }

        const auto &result_obj = result.at("result");
        if (!result_obj.is_object()) {
            throw std::runtime_error("'result' field is not an object");
        }

        const auto &result_inner = result_obj.get_object();
        if (!result_inner.contains("value")) {
            throw std::runtime_error("Missing 'value' field in result object");
        }

        // Type-safe conversion using Boost.JSON
        return boost::json::value_to<T>(result_inner.at("value"));
    } catch (const std::exception &e) {
        throw std::runtime_error(std::format(
            "Failed to extract value as {}: {}; Result: {}", typeid(T).name(),
            e.what(), boost::json::serialize(result)));
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
 *
 * @example
 * @code
 * auto maybe_title = extract_value_opt<std::string>(result);
 * if (maybe_title) {
 *     std::cout << "Title: " << *maybe_title << "\n";
 * }
 * @endcode
 */
template <typename T>
[[nodiscard]] auto
extract_value_opt(const boost::json::object &result) -> std::optional<T> {
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
 *
 * @example
 * @code
 * auto title = extract_value_or(result, std::string("Untitled"));
 * auto count = extract_value_or(result, 0);
 * @endcode
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
 *
 * @example
 * @code
 * if (has_value(result)) {
 *     auto val = extract_value<std::string>(result);
 * }
 * @endcode
 */
[[nodiscard]] inline auto
has_value(const boost::json::object &result) noexcept -> bool {
    try {
        return result.contains("result") && result.at("result").is_object() &&
               result.at("result").as_object().contains("value");
    } catch (...) {
        return false;
    }
}

// ==================== ScriptEvalOutcome Extraction ====================

// Note: Implementations require script_eval.hpp to be included by the user
// These are forward-declared here to avoid circular dependency

/**
 * @brief Extract typed value from ScriptEvalOutcome
 *
 * Throws ScriptEvaluateException if outcome contains an exception.
 * Otherwise extracts the value with type conversion.
 *
 * @tparam T Target C++ type
 * @param outcome ScriptEvalOutcome from policy-aware evaluation
 * @return Extracted value converted to type T
 * @throws ScriptEvaluateException if outcome has exception
 * @throws std::runtime_error if extraction fails
 *
 * @note Requires including "bidi/script_eval.hpp" before use
 *
 * @example
 * @code
 * #include "bidi/script_eval.hpp"
 * auto outcome = co_await client->evaluate(expr, ctx, return_outcome);
 * auto title = extract_value_from_outcome<std::string>(outcome);
 * @endcode
 */
template <typename T>
[[nodiscard]] auto
extract_value_from_outcome(const ScriptEvalOutcome &outcome) -> T {
    // Check for script exception first
    if (outcome.has_exception()) {
        throw ScriptEvaluateException(*outcome.exception);
    }

    // Extract from result value
    try {
        if (!outcome.result.is_object()) {
            throw std::runtime_error("Result is not an object");
        }

        auto result_obj = outcome.result.as_object();
        return extract_value<T>(result_obj);

    } catch (const ScriptEvaluateException &) {
        throw; // Preserve script exceptions
    } catch (const std::exception &e) {
        throw std::runtime_error(
            std::string("Failed to extract value from ScriptEvalOutcome: ") +
            e.what());
    }
}

/**
 * @brief Extract optional typed value from ScriptEvalOutcome
 *
 * Returns nullopt if outcome has exception or extraction fails.
 *
 * @tparam T Target C++ type
 * @param outcome ScriptEvalOutcome from policy-aware evaluation
 * @return std::optional<T> containing value or nullopt
 *
 * @note Requires including "bidi/script_eval.hpp" before use
 */
template <typename T>
[[nodiscard]] auto extract_value_from_outcome_opt(
    const ScriptEvalOutcome &outcome) -> std::optional<T> {
    try {
        return extract_value_from_outcome<T>(outcome);
    } catch (...) {
        return std::nullopt;
    }
}

/**
 * @brief Extract value from ScriptEvalOutcome with fallback
 *
 * Returns fallback if outcome has exception or extraction fails.
 *
 * @tparam T Target C++ type
 * @param outcome ScriptEvalOutcome from policy-aware evaluation
 * @param fallback Default value
 * @return Extracted value or fallback
 *
 * @note Requires including "bidi/script_eval.hpp" before use
 */
template <typename T>
[[nodiscard]] auto
extract_value_from_outcome_or(const ScriptEvalOutcome &outcome,
                              T fallback) -> T {
    return extract_value_from_outcome_opt<T>(outcome).value_or(
        std::move(fallback));
}

} // namespace bidi::script

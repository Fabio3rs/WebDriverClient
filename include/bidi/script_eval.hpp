#pragma once
/**
 * @file script_eval.hpp
 * @brief Script evaluation result modeling and exception policy utilities.
 *
 * The BiDi `script.evaluate` command can either return a normal result or a
 * structured script exception (that may include a rich stack and details).
 * This header models both outcomes and provides policy-driven helpers used by
 * higher-level APIs to either throw an exception (`ScriptEvaluateException`)
 * or return a `ScriptEvalOutcome` that preserves both the normal value and
 * any exception details.
 *
 * Rationale:
 * - Tests and callers that need robust error handling may prefer to receive
 *   structured exception details instead of relying on textual messages.
 * - The `script_eval_policy` enum controls the behavior: either throw or
 *   return the outcome. This keeps the low-level parsing deterministic and
 *   keeps DX options for different call-sites.
 */

#include <boost/json.hpp>
#include <optional>
#include <string>
#include <vector>

// Minimal forward declaration of ParsedResponse to avoid including core.hpp
// in this header
namespace bidi::core {
struct ParsedResponse;
}

namespace bidi::script {

// Represents a script stack frame (when provided by the BiDi driver)
struct ScriptStackFrame {
    std::string url;                // may be empty
    std::string function_name;      // may be empty
    std::int64_t line_number{-1};   // -1 when absent
    std::int64_t column_number{-1}; // -1 when absent
};

// Full details of a script exception, preserving the raw sub-object
struct ScriptExceptionDetails {
    std::string exception_type; // normalmente "exception"
    std::string text;           // exceptionDetails.text
    std::string value;          // exceptionDetails.exception.value
    std::string name;           // exceptionDetails.exception.className
    std::string error_type;     // exceptionDetails.exception.type
    std::optional<std::int64_t> line_number;    // topo
    std::optional<std::int64_t> column_number;  // topo
    std::vector<ScriptStackFrame> stack_frames; // ordem conforme recebido
    boost::json::object
        raw; // snapshot de exceptionDetails (ou vazio se inexistente)
};

// Script evaluation policy for exception handling
enum class script_eval_policy : std::uint8_t {
    throw_on_script_exception,
    return_outcome
};

// Composite optional result (used when policy == return_outcome)
struct ScriptEvalOutcome {
    boost::json::value result; // normal value (if no exception)
    std::optional<ScriptExceptionDetails>
        exception;           // present if the script threw
    boost::json::object raw; // sempre: response.result original
    [[nodiscard]] auto has_exception() const noexcept -> bool {
        return exception.has_value();
    }
};

// Specialized exception containing full details
class ScriptEvaluateException : public std::runtime_error {
    ScriptExceptionDetails details_;

  public:
    explicit ScriptEvaluateException(ScriptExceptionDetails details)
        : std::runtime_error(details.text.empty() ? "script evaluate exception"
                                                  : details.text),
          details_(std::move(details)) {}
    [[nodiscard]] auto
    details() const noexcept -> const ScriptExceptionDetails & {
        return details_;
    }
};

// Internal API: detect whether result represents a script exception
[[nodiscard]] auto
is_script_exception_result(const boost::json::object &result) noexcept -> bool;

// Internal API: parse ScriptExceptionDetails from a result whose type ==
// "exception"
[[nodiscard]] auto parse_script_exception(
    const boost::json::object &result) noexcept -> ScriptExceptionDetails;

// Internal helper structure used by the evaluate overload to apply policy.
struct PolicyApplicationResult {
    enum class Action : std::uint8_t { fulfill, throw_exception };
    Action action{Action::fulfill};
    ScriptEvalOutcome outcome;        // valid when action == fulfill
    ScriptExceptionDetails exception; // valid when action == throw_exception
};

[[nodiscard]] auto
apply_policy(const core::ParsedResponse &response,
             script_eval_policy policy) noexcept -> PolicyApplicationResult;

} // namespace bidi::script

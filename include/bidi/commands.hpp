#pragma once
/**
 * @file commands.hpp
 * @brief Spec-aligned JSON builders for W3C WebDriver BiDi commands.
 *
 * Architectural rationale:
 * - W3C BiDi compliance: 1:1 mapping to WebDriver BiDi specification command
 *   shapes ensures wire format correctness and interoperability.
 * - Centralized construction: All command shape logic in one place keeps wire
 *   format consistent and makes unit testing straightforward.
 * - Type safety via enums: Enum parameters (CreateType, ReadinessState,
 *   ResultOwnership) prevent invalid string values at compile time.
 * - Integration with arena allocation: Builders return `boost::json::object`
 *   so callers can reuse serializer logic and arena allocation patterns from
 *   `memory_pool.hpp`, minimizing allocations on hot path.
 * - Minimal builders: Each builder allocates only the required JSON structure,
 *   avoiding unnecessary object construction and copying.
 * - Canonical names: Enum->string helpers eliminate scattered string literals
 *   and make refactoring safer.
 *
 * Performance characteristics:
 * - Builders construct minimal JSON objects (no unnecessary fields)
 * - String literals are compiled constants (zero runtime allocation)
 * - Integration with boost::json serializer for efficient wire encoding
 */

#include <boost/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace bidi::commands {

// ======================== Session Commands ========================

namespace session {

// Build session.subscribe params
[[nodiscard]] auto
subscribe(const std::vector<std::string> &events,
          const std::vector<std::string> &contexts = {}) -> boost::json::object;

// Build session.unsubscribe params
[[nodiscard]] auto unsubscribe(const std::vector<std::string> &events,
                               const std::vector<std::string> &contexts = {})
    -> boost::json::object;

// Build session.status params (empty)
[[nodiscard]] auto status() -> boost::json::object;

} // namespace session

// ======================== BrowsingContext Commands ========================

namespace browsing_context {

// Spec: browsingContext.create types
enum class CreateType { tab, window };

// Spec: browsingContext.navigate wait states
enum class ReadinessState { none, interactive, complete };

// Build browsingContext.create params
[[nodiscard]] auto
create(CreateType type,
       std::string_view reference_context = {}) -> boost::json::object;

// Build browsingContext.navigate params
[[nodiscard]] auto
navigate(std::string_view context, std::string_view url,
         ReadinessState wait = ReadinessState::complete) -> boost::json::object;

// Build browsingContext.close params
[[nodiscard]] auto close(std::string_view context) -> boost::json::object;

// Build browsingContext.getTree params
[[nodiscard]] auto get_tree(std::string_view root = {},
                            int max_depth = -1) -> boost::json::object;

// Build browsingContext.reload params
[[nodiscard]] auto
reload(std::string_view context, bool ignore_cache = false,
       ReadinessState wait = ReadinessState::complete) -> boost::json::object;

} // namespace browsing_context

// ======================== Script Commands ========================

namespace script {

// Spec: script.ResultOwnership
enum class ResultOwnership { root, none };

// Spec: script.Target types
struct Target {
    std::string_view context;
    std::string_view sandbox; // optional
};

// Build script.evaluate params
[[nodiscard]] auto evaluate(std::string_view expression, const Target &target,
                            bool await_promise = true,
                            ResultOwnership ownership = ResultOwnership::root)
    -> boost::json::object;

// Build script.callFunction params
[[nodiscard]] auto call_function(
    std::string_view function_declaration, const Target &target,
    const boost::json::array &arguments = {}, bool await_promise = true,
    ResultOwnership ownership = ResultOwnership::root) -> boost::json::object;

template <class... Args>
auto call_function(const Target &target, std::string_view function_declaration,
                   Args &&...args) -> boost::json::object {
    return call_function(function_declaration, target,
                         boost::json::array{std::forward<Args>(args)...});
}

// Build script.disown params (cleanup remote objects)
[[nodiscard]] auto disown(const boost::json::array &handles,
                          const Target &target) -> boost::json::object;

} // namespace script

// ======================== Log Commands ========================

namespace log {

// Spec: log.Level enum
enum class Level { debug, info, warn, error };

} // namespace log

// ======================== Network Commands ========================

namespace network {

// Build network.continueRequest params
[[nodiscard]] auto
continue_request(std::string_view request_id, std::string_view url = {},
                 std::string_view method = {},
                 const boost::json::object &headers = {},
                 std::string_view body = {}) -> boost::json::object;

// Build network.failRequest params
[[nodiscard]] auto
fail_request(std::string_view request_id) -> boost::json::object;

} // namespace network

// ======================== Utility Functions ========================

// Convert enum to string for JSON serialization
[[nodiscard]] auto
to_string(browsing_context::CreateType type) noexcept -> std::string_view;
[[nodiscard]] auto
to_string(browsing_context::ReadinessState state) noexcept -> std::string_view;
[[nodiscard]] auto
to_string(script::ResultOwnership ownership) noexcept -> std::string_view;
[[nodiscard]] auto to_string(log::Level level) noexcept -> std::string_view;

// Create BiDi LocalValue from C++ value
[[nodiscard]] auto
local_value_string(std::string_view value) -> boost::json::object;
[[nodiscard]] auto local_value_number(double value) -> boost::json::object;
[[nodiscard]] auto local_value_boolean(bool value) -> boost::json::object;
[[nodiscard]] auto local_value_null() -> boost::json::object;

} // namespace bidi::commands

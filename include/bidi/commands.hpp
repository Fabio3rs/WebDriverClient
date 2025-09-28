// bidi/commands.hpp — BiDi Command Builders (W3C spec-compliant)
#pragma once

#include <boost/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace bidi::commands {

// ======================== Session Commands ========================

namespace session {

// Build session.subscribe params
[[nodiscard]] boost::json::object
subscribe(const std::vector<std::string> &events,
          const std::vector<std::string> &contexts = {});

// Build session.unsubscribe params
[[nodiscard]] boost::json::object
unsubscribe(const std::vector<std::string> &events,
            const std::vector<std::string> &contexts = {});

// Build session.status params (empty)
[[nodiscard]] boost::json::object status();

} // namespace session

// ======================== BrowsingContext Commands ========================

namespace browsing_context {

// Spec: browsingContext.create types
enum class CreateType { tab, window };

// Spec: browsingContext.navigate wait states
enum class ReadinessState { none, interactive, complete };

// Build browsingContext.create params
[[nodiscard]] boost::json::object
create(CreateType type, std::string_view reference_context = {});

// Build browsingContext.navigate params
[[nodiscard]] boost::json::object
navigate(std::string_view context, std::string_view url,
         ReadinessState wait = ReadinessState::complete);

// Build browsingContext.close params
[[nodiscard]] boost::json::object close(std::string_view context);

// Build browsingContext.getTree params
[[nodiscard]] boost::json::object get_tree(std::string_view root = {},
                                           int max_depth = -1);

// Build browsingContext.reload params
[[nodiscard]] boost::json::object
reload(std::string_view context, bool ignore_cache = false,
    ReadinessState wait = ReadinessState::complete);

} // namespace browsing_context

// ======================== Script Commands ========================

namespace script {

// Spec: script.ResultOwnership
enum class ResultOwnership { root, none };

// Spec: script.Target types
struct Target {
    std::string_view context;
    std::string_view sandbox = {}; // optional
};

// Build script.evaluate params
[[nodiscard]] boost::json::object
evaluate(std::string_view expression, const Target &target,
         bool await_promise = true,
         ResultOwnership ownership = ResultOwnership::root);

// Build script.callFunction params
[[nodiscard]] boost::json::object
call_function(std::string_view function_declaration, const Target &target,
              const boost::json::array &arguments = {},
              bool await_promise = true,
              ResultOwnership ownership = ResultOwnership::root);

// Build script.disown params (cleanup remote objects)
[[nodiscard]] boost::json::object disown(const boost::json::array &handles,
                                         const Target &target);

} // namespace script

// ======================== Log Commands ========================

namespace log {

// Spec: log.Level enum
enum class Level { debug, info, warn, error };

} // namespace log

// ======================== Network Commands ========================

namespace network {

// Build network.continueRequest params
[[nodiscard]] boost::json::object
continue_request(std::string_view request_id, std::string_view url = {},
                 std::string_view method = {},
                 const boost::json::object &headers = {},
                 std::string_view body = {});

// Build network.failRequest params
[[nodiscard]] boost::json::object fail_request(std::string_view request_id);

} // namespace network

// ======================== Utility Functions ========================

// Convert enum to string for JSON serialization
[[nodiscard]] std::string_view to_string(browsing_context::CreateType type) noexcept;
[[nodiscard]] std::string_view to_string(browsing_context::ReadinessState state) noexcept;
[[nodiscard]] std::string_view to_string(script::ResultOwnership ownership) noexcept;
[[nodiscard]] std::string_view to_string(log::Level level) noexcept;

// Create BiDi LocalValue from C++ value
[[nodiscard]] boost::json::object local_value_string(std::string_view value);
[[nodiscard]] boost::json::object local_value_number(double value);
[[nodiscard]] boost::json::object local_value_boolean(bool value);
[[nodiscard]] boost::json::object local_value_null();

} // namespace bidi::commands

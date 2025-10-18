#pragma once
/**
 * @file script.hpp
 * @brief Script command builders for W3C WebDriver BiDi
 *
 * Commands for script evaluation and function calling
 */

#include <boost/json.hpp>
#include <string>
#include <string_view>

namespace bidi::commands::script {

/**
 * @brief Spec: script.ResultOwnership
 */
enum class ResultOwnership { root, none };

/**
 * @brief Spec: script.Target types
 */
struct Target {
    std::string_view context;
    std::string_view sandbox; // optional
};

/**
 * @brief Build script.evaluate params
 * @param expression JavaScript expression to evaluate
 * @param target Evaluation target (context + optional sandbox)
 * @param await_promise Whether to await promise resolution
 * @param ownership Result ownership mode
 * @return Params object for script.evaluate command
 */
[[nodiscard]] auto evaluate(std::string_view expression, const Target &target,
                            bool await_promise = true,
                            ResultOwnership ownership = ResultOwnership::root)
    -> boost::json::object;

/**
 * @brief Build script.callFunction params
 * @param function_declaration JavaScript function source
 * @param target Evaluation target (context + optional sandbox)
 * @param arguments Function arguments as BiDi LocalValues
 * @param await_promise Whether to await promise resolution
 * @param ownership Result ownership mode
 * @return Params object for script.callFunction command
 */
[[nodiscard]] auto
call_function(std::string_view function_declaration, const Target &target,
              boost::json::array arguments = {}, bool await_promise = true,
              ResultOwnership ownership = ResultOwnership::root)
    -> boost::json::object;

/**
 * @brief Build script.callFunction params (variadic convenience overload)
 * @tparam Args Argument types
 * @param target Evaluation target
 * @param function_declaration JavaScript function source
 * @param args Function arguments (will be converted to JSON array)
 * @return Params object for script.callFunction command
 */
template <class... Args>
auto call_function(const Target &target, std::string_view function_declaration,
                   Args &&...args) -> boost::json::object {
    return call_function(function_declaration, target,
                         boost::json::array{std::forward<Args>(args)...});
}

/**
 * @brief Build script.disown params (cleanup remote objects)
 * @param handles Array of handle objects to disown
 * @param target Target context
 * @return Params object for script.disown command
 */
[[nodiscard]] auto disown(const boost::json::array &handles,
                          const Target &target) -> boost::json::object;

/**
 * @brief Build script.addPreloadScript params
 * @param function_declaration JavaScript function source to run on realm
 * creation
 * @param arguments Optional array of ChannelValue arguments
 * @param sandbox Optional sandbox name
 * @return Params object for script.addPreloadScript command
 */
[[nodiscard]] auto add_preload_script(std::string_view function_declaration,
                                      boost::json::array arguments = {},
                                      std::string_view sandbox = {})
    -> boost::json::object;

/**
 * @brief Build script.removePreloadScript params
 * @param script Preload script identifier to remove
 * @return Params object for script.removePreloadScript command
 */
[[nodiscard]] auto remove_preload_script(std::string_view script)
    -> boost::json::object;

} // namespace bidi::commands::script

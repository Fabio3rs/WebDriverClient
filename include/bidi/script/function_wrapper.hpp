#pragma once
/**
 * @file function_wrapper.hpp
 * @brief High-level wrapper for BiDi script.callFunction
 *
 * Provides FunctionBidi<Result, Args...> template for type-safe JavaScript
 * function invocation with automatic C++ ↔ BiDi marshalling.
 */

#include "bidi/client.hpp"
#include "bidi/script/marshalling.hpp"
#include <boost/json/serialize.hpp>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bidi::script {

/**
 * @brief Type-safe JavaScript function caller
 *
 * Template wrapper that provides:
 * - Automatic C++ → BiDi argument marshalling
 * - Result type conversion (BiDi → C++)
 * - Script exception handling
 *
 * @tparam Result Expected C++ return type
 * @tparam Args C++ argument types
 *
 * @example
 * auto add = make_function_caller<int, int, int>(
 *     client, ctx_id, "function(a, b) { return a + b; }"
 * );
 * auto result = co_await add(2, 3)(); // returns Task<int>
 */
template <typename Result, typename... Args> struct FunctionBidi {
    std::shared_ptr<bidi::Client> client;
    std::string context_id;
    std::string function_declaration;
    script::script_eval_policy policy =
        script::script_eval_policy::throw_on_script_exception;

    /**
     * @brief Invoke JavaScript function with C++ arguments
     * @param args C++ values to pass as arguments
     * @return Task<Result> lazy operation
     */
    auto operator()(Args... args) const -> Task<Result> {
        auto args_array = make_args_array(args...);
        auto res = client->call_function(function_declaration, context_id,
                                         args_array, policy);

        return res.map([this](const script::ScriptEvalOutcome &outcome) {
            if (outcome.has_exception()) {
                throw script::ScriptEvaluateException(*outcome.exception);
            }

            if (!outcome.result.is_object()) {
                throw std::runtime_error(
                    std::format("Unexpected result type; Result: {}",
                                boost::json::serialize(outcome.result)));
            }

            auto object = outcome.result.get_object();
            auto result = object.at("result").get_object();

            try {
                auto value = result.at("value");
                return boost::json::value_to<Result>(value);
            } catch (const std::out_of_range &e) {
                throw std::runtime_error(
                    std::format("Result object for function {} missing "
                                "expected fields: {}; Result: {}",
                                function_declaration, e.what(),
                                boost::json::serialize(outcome.result)));
            } catch (const std::exception &e) {
                throw std::runtime_error(std::format(
                    "Failed to convert result of function {}: {}; Result: {}",
                    function_declaration, e.what(),
                    boost::json::serialize(outcome.result)));
            }
        });
    }
};

/**
 * @brief Factory function for FunctionBidi
 *
 * @tparam Result Expected C++ return type
 * @tparam Args C++ argument types
 * @param client BiDi client instance
 * @param context_id Browsing context ID
 * @param function_declaration JavaScript function source
 * @param policy Script exception handling policy
 * @return FunctionBidi callable object
 */
template <typename Result, typename... Args>
auto make_function_caller(
    std::shared_ptr<bidi::Client> client, std::string_view context_id,
    std::string function_declaration,
    script::script_eval_policy policy =
        script::script_eval_policy::throw_on_script_exception)
    -> FunctionBidi<Result, Args...> {
    return {client, std::string(context_id), std::move(function_declaration),
            policy};
}

} // namespace bidi::script

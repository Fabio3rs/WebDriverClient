#pragma once
/**
 * @file marshalling.hpp
 * @brief C++ to BiDi LocalValue type marshalling utilities
 *
 * Provides template-based automatic conversion from C++ types to BiDi
 * LocalValue JSON objects. Used for script.callFunction arguments and similar
 * scenarios where JavaScript values need to be constructed from C++.
 *
 * Performance: All functions are inline/constexpr for zero-cost abstractions.
 */

#include "bidi/commands.hpp" // for local_value_* primitives
#include <boost/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>

namespace bidi::script {

/**
 * @brief Maps C++ type to JavaScript type name
 * @tparam T C++ type to map
 * @return JavaScript type name as string_view
 */
template <typename T> constexpr auto js_typename_for() -> std::string_view {
    if constexpr (std::is_same_v<T, std::nullptr_t>) {
        return "null";
    } else if constexpr (std::is_same_v<T, bool>) {
        return "boolean";
    } else if constexpr (std::is_same_v<T, std::string> ||
                         std::is_same_v<T, std::string_view> ||
                         std::is_same_v<T, const char *>) {
        return "string";
    } else if constexpr (std::is_floating_point_v<T> ||
                         (std::is_integral_v<T> && !std::is_same_v<T, bool>)) {
        return "number";
    } else if constexpr (std::is_void_v<T>) {
        return "undefined";
    } else {
        return "object";
    }
}

/**
 * @brief Convert C++ value to BiDi LocalValue JSON object
 *
 * Overload set for all supported C++ → LocalValue conversions.
 * Uses bidi::commands::local_value_* primitives for construction.
 */

// Null
inline auto make_arg_json(std::nullptr_t) -> boost::json::object {
    return bidi::commands::local_value_null();
}

// Boolean
inline auto make_arg_json(bool value) -> boost::json::object {
    return bidi::commands::local_value_boolean(value);
}

// Number (double)
inline auto make_arg_json(double value) -> boost::json::object {
    return bidi::commands::local_value_number(value);
}

// Number (integral types)
template <typename Int>
inline auto make_arg_json(Int &&value)
    -> std::enable_if_t<std::is_integral_v<std::decay_t<Int>> &&
                            !std::is_same_v<std::decay_t<Int>, bool>,
                        boost::json::object> {
    return bidi::commands::local_value_number(
        static_cast<double>(std::forward<Int>(value)));
}

// String (string_view)
inline auto make_arg_json(std::string_view str) -> boost::json::object {
    return bidi::commands::local_value_string(str);
}

// String (std::string)
inline auto make_arg_json(const std::string &str) -> boost::json::object {
    return bidi::commands::local_value_string(str);
}

// String (const char*)
inline auto make_arg_json(const char *str) -> boost::json::object {
    if (str == nullptr) {
        return bidi::commands::local_value_null();
    }
    return bidi::commands::local_value_string(str);
}

// Already a LocalValue object
inline auto make_arg_json(const boost::json::object &obj)
    -> boost::json::object {
    return obj;
}

// Fallback for boost::json serializable types
template <typename T>
inline auto make_arg_json(T &&value)
    -> std::enable_if_t<!(std::is_integral_v<std::decay_t<T>> &&
                          !std::is_same_v<std::decay_t<T>, bool>),
                        boost::json::object> {
    try {
        auto val = boost::json::value_from(std::forward<T>(value));
        if (val.is_object()) {
            return val.as_object();
        }
        return boost::json::object{{"type", js_typename_for<std::decay_t<T>>()},
                                   {"value", std::move(val)}};
    } catch (...) {
        return boost::json::object{{"type", "object"}};
    }
}

/**
 * @brief Build BiDi arguments array from variadic C++ values
 * @param args C++ values to convert
 * @return boost::json::array of LocalValue objects
 */
template <typename... Args>
auto make_args_array(Args &&...args) -> boost::json::array {
    return boost::json::array{make_arg_json(std::forward<Args>(args))...};
}

} // namespace bidi::script

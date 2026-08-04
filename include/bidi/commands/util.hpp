#pragma once
/**
 * @file util.hpp
 * @brief Utility functions for command module types
 *
 * Enum to string converters and LocalValue builders
 */

#include "browsing_context.hpp"
#include "log.hpp"
#include "script.hpp"
#include <boost/json.hpp>
#include <string_view>

namespace bidi::commands {

// ======================== Enum Converters ========================

/**
 * @brief Convert browsing_context::CreateType to string
 */
[[nodiscard]] auto to_string(browsing_context::CreateType type) noexcept
    -> std::string_view;

/**
 * @brief Convert browsing_context::ReadinessState to string
 */
[[nodiscard]] auto to_string(browsing_context::ReadinessState state) noexcept
    -> std::string_view;

/**
 * @brief Convert script::ResultOwnership to string
 */
[[nodiscard]] auto to_string(script::ResultOwnership ownership) noexcept
    -> std::string_view;

/**
 * @brief Convert log::Level to string
 */
[[nodiscard]] auto to_string(log::Level level) noexcept -> std::string_view;

// ======================== LocalValue Builders ========================

/**
 * @brief Create BiDi LocalValue from string
 */
[[nodiscard]] auto local_value_string(std::string_view value)
    -> boost::json::object;

/**
 * @brief Create BiDi LocalValue from number
 */
[[nodiscard]] auto local_value_number(double value) -> boost::json::object;

/**
 * @brief Create BiDi LocalValue from boolean
 */
[[nodiscard]] auto local_value_boolean(bool value) -> boost::json::object;

/**
 * @brief Create BiDi LocalValue for null
 */
[[nodiscard]] auto local_value_null() -> boost::json::object;

} // namespace bidi::commands

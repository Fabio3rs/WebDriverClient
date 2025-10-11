#pragma once
/**
 * @file log.hpp
 * @brief Log module types for W3C WebDriver BiDi
 *
 * Enums and types for log events
 */

namespace bidi::commands::log {

/**
 * @brief Spec: log.Level enum
 */
enum class Level { debug, info, warn, error };

} // namespace bidi::commands::log

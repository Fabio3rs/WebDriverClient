#pragma once
/**
 * @file session.hpp
 * @brief Session command builders for W3C WebDriver BiDi
 *
 * Commands for session management: subscribe, unsubscribe, status
 */

#include <boost/json.hpp>
#include <string>
#include <vector>

namespace bidi::commands::session {

/**
 * @brief Build session.subscribe params
 * @param events List of event names to subscribe to
 * @param contexts Optional list of browsing context IDs to filter events
 * @return Params object for session.subscribe command
 */
[[nodiscard]] auto subscribe(const std::vector<std::string> &events,
                             const std::vector<std::string> &contexts = {})
    -> boost::json::object;

/**
 * @brief Build session.unsubscribe params
 * @param events List of event names to unsubscribe from
 * @param contexts Optional list of browsing context IDs
 * @return Params object for session.unsubscribe command
 */
[[nodiscard]] auto unsubscribe(const std::vector<std::string> &events,
                               const std::vector<std::string> &contexts = {})
    -> boost::json::object;

/**
 * @brief Build session.status params (empty)
 * @return Params object for session.status command
 */
[[nodiscard]] auto status() -> boost::json::object;

} // namespace bidi::commands::session

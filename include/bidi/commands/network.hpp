#pragma once
/**
 * @file network.hpp
 * @brief Network command builders for W3C WebDriver BiDi
 *
 * Commands for network interception and manipulation
 */

#include <boost/json.hpp>
#include <string>
#include <string_view>

namespace bidi::commands::network {

/**
 * @brief Build network.continueRequest params
 * @param request_id Request ID to continue
 * @param url Optional modified URL
 * @param method Optional modified method
 * @param headers Optional modified headers
 * @param body Optional modified body
 * @return Params object for network.continueRequest command
 */
[[nodiscard]] auto continue_request(std::string_view request_id,
                                    std::string_view url = {},
                                    std::string_view method = {},
                                    const boost::json::object &headers = {},
                                    std::string_view body = {})
    -> boost::json::object;

/**
 * @brief Build network.failRequest params
 * @param request_id Request ID to fail
 * @return Params object for network.failRequest command
 */
[[nodiscard]] auto fail_request(std::string_view request_id)
    -> boost::json::object;

} // namespace bidi::commands::network

#pragma once
/**
 * @file browsing_context.hpp
 * @brief BrowsingContext command builders for W3C WebDriver BiDi
 *
 * Commands for browsing context management: create, navigate, close, getTree,
 * reload
 */

#include <boost/json.hpp>
#include <string>
#include <string_view>

namespace bidi::commands::browsing_context {

/**
 * @brief Spec: browsingContext.create types
 */
enum class CreateType { tab, window };

/**
 * @brief Spec: browsingContext.navigate wait states
 */
enum class ReadinessState { none, interactive, complete };

/**
 * @brief Build browsingContext.create params
 * @param type Type of browsing context to create (tab or window)
 * @param reference_context Optional reference context ID
 * @return Params object for browsingContext.create command
 */
[[nodiscard]] auto create(CreateType type,
                          std::string_view reference_context = {})
    -> boost::json::object;

/**
 * @brief Build browsingContext.navigate params
 * @param context Browsing context ID
 * @param url URL to navigate to
 * @param wait Readiness state to wait for
 * @return Params object for browsingContext.navigate command
 */
[[nodiscard]] auto navigate(std::string_view context, std::string_view url,
                            ReadinessState wait = ReadinessState::complete)
    -> boost::json::object;

/**
 * @brief Build browsingContext.close params
 * @param context Browsing context ID to close
 * @return Params object for browsingContext.close command
 */
[[nodiscard]] auto close(std::string_view context) -> boost::json::object;

/**
 * @brief Build browsingContext.getTree params
 * @param root Optional root context ID
 * @param max_depth Maximum depth to traverse (-1 for unlimited)
 * @return Params object for browsingContext.getTree command
 */
[[nodiscard]] auto get_tree(std::string_view root = {}, int max_depth = -1)
    -> boost::json::object;

/**
 * @brief Build browsingContext.reload params
 * @param context Browsing context ID to reload
 * @param ignore_cache Whether to ignore cache
 * @param wait Readiness state to wait for
 * @return Params object for browsingContext.reload command
 */
[[nodiscard]] auto reload(std::string_view context, bool ignore_cache = false,
                          ReadinessState wait = ReadinessState::complete)
    -> boost::json::object;

/**
 * @brief Build browsingContext.handleUserPrompt params
 * @param context Browsing context ID
 * @param accept Whether to accept the prompt (default: true)
 * @param user_text Text to provide for prompt dialogs (default: empty)
 * @return Params object for browsingContext.handleUserPrompt command
 * @see
 * https://w3c.github.io/webdriver-bidi/#command-browsingContext-handleUserPrompt
 */
[[nodiscard]] auto
handle_user_prompt(std::string_view context,
                   std::optional<bool> accept = std::nullopt,
                   std::optional<std::string_view> user_text = std::nullopt)
    -> boost::json::object;

} // namespace bidi::commands::browsing_context

#pragma once
/**
 * @file types/all.hpp
 * @brief Convenience header for all W3C WebDriver BiDi types
 *
 * Includes all type module headers for easy access.
 * Use this header when you need types from multiple modules.
 *
 * @see https://w3c.github.io/webdriver-bidi/
 */

// Core types (no dependencies)
#include "bidi/types/core.hpp"

// Module types (ordered by dependencies)
#include "bidi/types/browsing_context.hpp"
#include "bidi/types/log.hpp"
#include "bidi/types/network.hpp"
#include "bidi/types/script.hpp"
#include "bidi/types/session.hpp"
#include "bidi/types/storage.hpp"

/**
 * @namespace bidi::types
 * @brief Root namespace for all W3C WebDriver BiDi strong types
 *
 * Type system organization:
 * - core: ErrorCode enum, message envelope types
 * - session: Capabilities, proxy, subscriptions
 * - browsing_context: Locators, navigation, screenshots
 * - script: RemoteValue, realms, handles
 * - network: Cookies, headers, request/response data
 * - storage: Cookie management, partitions
 * - log: Console logs, JavaScript exceptions
 */

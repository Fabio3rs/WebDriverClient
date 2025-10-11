#pragma once
/**
 * @file commands.hpp
 * @brief Umbrella header for W3C WebDriver BiDi command builders
 *
 * This header includes all command modules for convenience. For faster
 * compilation, consider including only the specific module headers you need:
 *
 * - commands/session.hpp - Session management commands
 * - commands/browsing_context.hpp - Browsing context commands
 * - commands/script.hpp - Script evaluation commands
 * - commands/log.hpp - Log types
 * - commands/network.hpp - Network interception commands
 * - commands/util.hpp - Enum converters and LocalValue builders
 *
 * Architectural benefits of module split:
 * - Reduced compilation dependencies (change to script doesn't recompile
 * session code)
 * - Clear separation of concerns (SF.1 compliance)
 * - Easier navigation and maintenance
 * - Better testability (can test modules independently)
 */

#include "commands/browsing_context.hpp"
#include "commands/log.hpp"
#include "commands/network.hpp"
#include "commands/script.hpp"
#include "commands/session.hpp"
#include "commands/util.hpp"

namespace bidi::commands {
// All command namespaces are now included via individual headers
// No additional declarations needed here
} // namespace bidi::commands

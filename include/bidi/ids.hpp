// This shim keeps backwards compatibility for older code that included
// `bidi/ids.hpp` and used uppercase constants. Prefer
// `include/bidi_methods.hpp` (provides `bidi::ids::methods::...` and
// `bidi::ids::events::...`).
#pragma once

#include "bidi_methods.hpp"

namespace bidi::ids {
// Re-export older uppercase names as aliases to the modern identifiers.
// This allows incremental migration without breaking existing includes.
inline constexpr auto BROWSING_CONTEXT_CREATE = bidi::ids::methods::bc_create;
inline constexpr auto BROWSING_CONTEXT_NAVIGATE =
    bidi::ids::methods::bc_navigate;
inline constexpr auto BROWSING_CONTEXT_CLOSE = bidi::ids::methods::bc_close;
inline constexpr auto BROWSING_CONTEXT_GET_TREE =
    bidi::ids::methods::bc_getTree;

inline constexpr auto SCRIPT_EVALUATE = bidi::ids::methods::script_evaluate;
inline constexpr auto SCRIPT_CALL_FUNCTION =
    bidi::ids::methods::script_callFunction;

inline constexpr auto SESSION_SUBSCRIBE = bidi::ids::methods::session_subscribe;
inline constexpr auto SESSION_UNSUBSCRIBE =
    bidi::ids::methods::session_unsubscribe;

// Events
inline constexpr auto EV_LOG_ENTRY_ADDED = bidi::ids::events::log_entryAdded;
inline constexpr auto EV_NETWORK_BEFORE_REQUEST_SENT =
    bidi::ids::events::net_beforeRequestSent;
inline constexpr auto EV_BROWSING_CONTEXT_CREATED =
    bidi::ids::events::bc_contextCreated;

} // namespace bidi::ids

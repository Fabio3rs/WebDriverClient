#pragma once
#include <string_view>

namespace bidi {
namespace ids {
namespace methods {
// session
inline constexpr std::string_view session_status = "session.status";
inline constexpr std::string_view session_new = "session.new";
inline constexpr std::string_view session_end = "session.end";
inline constexpr std::string_view session_subscribe = "session.subscribe";
inline constexpr std::string_view session_unsubscribe = "session.unsubscribe";
// browser
inline constexpr std::string_view browser_close = "browser.close";
inline constexpr std::string_view browser_createUserContext =
    "browser.createUserContext";
inline constexpr std::string_view browser_getClientWindows =
    "browser.getClientWindows";
inline constexpr std::string_view browser_getUserContexts =
    "browser.getUserContexts";
inline constexpr std::string_view browser_removeUserContext =
    "browser.removeUserContext";
// browsingContext
inline constexpr std::string_view bc_activate = "browsingContext.activate";
inline constexpr std::string_view bc_captureScreenshot =
    "browsingContext.captureScreenshot";
inline constexpr std::string_view bc_close = "browsingContext.close";
inline constexpr std::string_view bc_create = "browsingContext.create";
inline constexpr std::string_view bc_getTree = "browsingContext.getTree";
inline constexpr std::string_view bc_handleUserPrompt =
    "browsingContext.handleUserPrompt";
inline constexpr std::string_view bc_locateNodes =
    "browsingContext.locateNodes";
inline constexpr std::string_view bc_navigate = "browsingContext.navigate";
inline constexpr std::string_view bc_print = "browsingContext.print";
inline constexpr std::string_view bc_reload = "browsingContext.reload";
inline constexpr std::string_view bc_setViewport =
    "browsingContext.setViewport";
inline constexpr std::string_view bc_traverseHistory =
    "browsingContext.traverseHistory";
// script
inline constexpr std::string_view script_addPreloadScript =
    "script.addPreloadScript";
inline constexpr std::string_view script_removePreloadScript =
    "script.removePreloadScript";
inline constexpr std::string_view script_disown = "script.disown";
inline constexpr std::string_view script_callFunction = "script.callFunction";
inline constexpr std::string_view script_evaluate = "script.evaluate";
inline constexpr std::string_view script_getRealms = "script.getRealms";
// network (intercept e coleta)
inline constexpr std::string_view net_addIntercept = "network.addIntercept";
inline constexpr std::string_view net_removeIntercept =
    "network.removeIntercept";
inline constexpr std::string_view net_continueRequest =
    "network.continueRequest";
inline constexpr std::string_view net_continueResponse =
    "network.continueResponse";
inline constexpr std::string_view net_continueWithAuth =
    "network.continueWithAuth";
inline constexpr std::string_view net_failRequest = "network.failRequest";
inline constexpr std::string_view net_provideResponse =
    "network.provideResponse";
inline constexpr std::string_view net_setCacheBehavior =
    "network.setCacheBehavior";
inline constexpr std::string_view net_setExtraHeaders =
    "network.setExtraHeaders";
inline constexpr std::string_view net_addDataCollector =
    "network.addDataCollector";
inline constexpr std::string_view net_removeDataCollector =
    "network.removeDataCollector";
inline constexpr std::string_view net_getData = "network.getData";
// storage (cookies)
inline constexpr std::string_view storage_getCookies = "storage.getCookies";
inline constexpr std::string_view storage_setCookie = "storage.setCookie";
inline constexpr std::string_view storage_deleteCookies =
    "storage.deleteCookies";
// emulation (ambiente)
inline constexpr std::string_view emu_setLocaleOverride =
    "emulation.setLocaleOverride";
inline constexpr std::string_view emu_setTimezoneOverride =
    "emulation.setTimezoneOverride";
inline constexpr std::string_view emu_setUserAgentOverride =
    "emulation.setUserAgentOverride";
inline constexpr std::string_view emu_setGeolocationOverride =
    "emulation.setGeolocationOverride";
inline constexpr std::string_view emu_setScreenOrientationOverride =
    "emulation.setScreenOrientationOverride";
inline constexpr std::string_view emu_setScriptingEnabled =
    "emulation.setScriptingEnabled";
inline constexpr std::string_view emu_setForcedColorsModeThemeOverride =
    "emulation.setForcedColorsModeThemeOverride";
} // namespace methods

namespace events {
// browsingContext
inline constexpr std::string_view bc_contextCreated =
    "browsingContext.contextCreated";
inline constexpr std::string_view bc_contextDestroyed =
    "browsingContext.contextDestroyed";
inline constexpr std::string_view bc_navigationStarted =
    "browsingContext.navigationStarted";
inline constexpr std::string_view bc_fragmentNavigated =
    "browsingContext.fragmentNavigated";
inline constexpr std::string_view bc_historyUpdated =
    "browsingContext.historyUpdated";
inline constexpr std::string_view bc_domContentLoaded =
    "browsingContext.domContentLoaded";
inline constexpr std::string_view bc_load = "browsingContext.load";
inline constexpr std::string_view bc_downloadWillBegin =
    "browsingContext.downloadWillBegin";
inline constexpr std::string_view bc_downloadEnd =
    "browsingContext.downloadEnd";
inline constexpr std::string_view bc_navigationAborted =
    "browsingContext.navigationAborted";
inline constexpr std::string_view bc_navigationCommitted =
    "browsingContext.navigationCommitted";
inline constexpr std::string_view bc_navigationFailed =
    "browsingContext.navigationFailed";
inline constexpr std::string_view bc_userPromptOpened =
    "browsingContext.userPromptOpened";
inline constexpr std::string_view bc_userPromptClosed =
    "browsingContext.userPromptClosed";
// log
inline constexpr std::string_view log_entryAdded = "log.entryAdded";
// script
inline constexpr std::string_view script_message = "script.message";
inline constexpr std::string_view script_realmCreated = "script.realmCreated";
inline constexpr std::string_view script_realmDestroyed =
    "script.realmDestroyed";
// network (principais)
inline constexpr std::string_view net_beforeRequestSent =
    "network.beforeRequestSent";
inline constexpr std::string_view net_responseStarted =
    "network.responseStarted";
inline constexpr std::string_view net_authRequired = "network.authRequired";
inline constexpr std::string_view net_responseCompleted =
    "network.responseCompleted";
inline constexpr std::string_view net_fetchError = "network.fetchError";
} // namespace events

namespace values {
// Tipos/Enums stringados da spec
enum class Readiness {
    none,
    interactive,
    complete
}; // browsingContext.ReadinessState
enum class ResultOwnership { root, none }; // script.ResultOwnership
enum class CreateType { tab, window };     // browsingContext.CreateType
} // namespace values
} // namespace ids

namespace mod {  // typed layers per module
struct Session { /* status/new/end/subscribe/unsubscribe */
};
struct Browser { /* user contexts, client windows */
};
struct BrowsingContext { /* create/navigate/reload/locateNodes/... */
};
struct Script { /* evaluate/callFunction/addPreloadScript/... */
};
struct Log { /* apenas eventos */
};
struct Network { /* intercepts, continue/fail/provide, headers */
};
struct Storage { /* cookies */
};
struct Emulation { /* locale/timezone/UA/geo/orientation */
};
} // namespace mod

namespace dsl {
class Page { /* goto(), waitFor(), click(), ... */
};
} // namespace dsl
} // namespace bidi

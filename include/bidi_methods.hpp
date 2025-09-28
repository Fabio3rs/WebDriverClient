#pragma once

namespace bidi {
  namespace ids {
    namespace methods {
      // session
      inline constexpr auto session_status      = "session.status";
      inline constexpr auto session_new         = "session.new";
      inline constexpr auto session_end         = "session.end";
      inline constexpr auto session_subscribe   = "session.subscribe";
      inline constexpr auto session_unsubscribe = "session.unsubscribe";
      // browser
      inline constexpr auto browser_close               = "browser.close";
      inline constexpr auto browser_createUserContext   = "browser.createUserContext";
      inline constexpr auto browser_getClientWindows    = "browser.getClientWindows";
      inline constexpr auto browser_getUserContexts     = "browser.getUserContexts";
      inline constexpr auto browser_removeUserContext   = "browser.removeUserContext";
      // browsingContext
      inline constexpr auto bc_activate        = "browsingContext.activate";
      inline constexpr auto bc_captureScreenshot = "browsingContext.captureScreenshot";
      inline constexpr auto bc_close           = "browsingContext.close";
      inline constexpr auto bc_create          = "browsingContext.create";
      inline constexpr auto bc_getTree         = "browsingContext.getTree";
      inline constexpr auto bc_handleUserPrompt= "browsingContext.handleUserPrompt";
      inline constexpr auto bc_locateNodes     = "browsingContext.locateNodes";
      inline constexpr auto bc_navigate        = "browsingContext.navigate";
      inline constexpr auto bc_print           = "browsingContext.print";
      inline constexpr auto bc_reload          = "browsingContext.reload";
      inline constexpr auto bc_setViewport     = "browsingContext.setViewport";
      inline constexpr auto bc_traverseHistory = "browsingContext.traverseHistory";
      // script
      inline constexpr auto script_addPreloadScript = "script.addPreloadScript";
      inline constexpr auto script_removePreloadScript = "script.removePreloadScript";
      inline constexpr auto script_disown        = "script.disown";
      inline constexpr auto script_callFunction  = "script.callFunction";
      inline constexpr auto script_evaluate      = "script.evaluate";
      inline constexpr auto script_getRealms     = "script.getRealms";
      // network (intercept e coleta)
      inline constexpr auto net_addIntercept       = "network.addIntercept";
      inline constexpr auto net_removeIntercept    = "network.removeIntercept";
      inline constexpr auto net_continueRequest    = "network.continueRequest";
      inline constexpr auto net_continueResponse   = "network.continueResponse";
      inline constexpr auto net_continueWithAuth   = "network.continueWithAuth";
      inline constexpr auto net_failRequest        = "network.failRequest";
      inline constexpr auto net_provideResponse    = "network.provideResponse";
      inline constexpr auto net_setCacheBehavior   = "network.setCacheBehavior";
      inline constexpr auto net_setExtraHeaders    = "network.setExtraHeaders";
      inline constexpr auto net_addDataCollector   = "network.addDataCollector";
      inline constexpr auto net_removeDataCollector= "network.removeDataCollector";
      inline constexpr auto net_getData            = "network.getData";
      // storage (cookies)
      inline constexpr auto storage_getCookies     = "storage.getCookies";
      inline constexpr auto storage_setCookie      = "storage.setCookie";
      inline constexpr auto storage_deleteCookies  = "storage.deleteCookies";
      // emulation (ambiente)
      inline constexpr auto emu_setLocaleOverride        = "emulation.setLocaleOverride";
      inline constexpr auto emu_setTimezoneOverride      = "emulation.setTimezoneOverride";
      inline constexpr auto emu_setUserAgentOverride     = "emulation.setUserAgentOverride";
      inline constexpr auto emu_setGeolocationOverride   = "emulation.setGeolocationOverride";
      inline constexpr auto emu_setScreenOrientationOverride = "emulation.setScreenOrientationOverride";
      inline constexpr auto emu_setScriptingEnabled      = "emulation.setScriptingEnabled";
      inline constexpr auto emu_setForcedColorsModeThemeOverride = "emulation.setForcedColorsModeThemeOverride";
    }

    namespace events {
      // browsingContext
      inline constexpr auto bc_contextCreated     = "browsingContext.contextCreated";
      inline constexpr auto bc_contextDestroyed   = "browsingContext.contextDestroyed";
      inline constexpr auto bc_navigationStarted  = "browsingContext.navigationStarted";
      inline constexpr auto bc_fragmentNavigated  = "browsingContext.fragmentNavigated";
      inline constexpr auto bc_historyUpdated     = "browsingContext.historyUpdated";
      inline constexpr auto bc_domContentLoaded   = "browsingContext.domContentLoaded";
      inline constexpr auto bc_load               = "browsingContext.load";
      inline constexpr auto bc_downloadWillBegin  = "browsingContext.downloadWillBegin";
      inline constexpr auto bc_downloadEnd        = "browsingContext.downloadEnd";
      inline constexpr auto bc_navigationAborted  = "browsingContext.navigationAborted";
      inline constexpr auto bc_navigationCommitted= "browsingContext.navigationCommitted";
      inline constexpr auto bc_navigationFailed   = "browsingContext.navigationFailed";
      // log
      inline constexpr auto log_entryAdded        = "log.entryAdded";
      // script
      inline constexpr auto script_message        = "script.message";
      inline constexpr auto script_realmCreated   = "script.realmCreated";
      inline constexpr auto script_realmDestroyed = "script.realmDestroyed";
      // network (principais)
      inline constexpr auto net_beforeRequestSent = "network.beforeRequestSent";
      inline constexpr auto net_responseStarted   = "network.responseStarted";
      inline constexpr auto net_responseCompleted = "network.responseCompleted";
      inline constexpr auto net_fetchError        = "network.fetchError";
    }

    namespace values {
      // Tipos/Enums stringados da spec
      enum class Readiness { none, interactive, complete };      // browsingContext.ReadinessState
      enum class ResultOwnership { root, none };                  // script.ResultOwnership
      enum class CreateType { tab, window };                      // browsingContext.CreateType
    }
  }

  namespace mod {  // camadas tipadas por módulo
    struct Session { /* status/new/end/subscribe/unsubscribe */ };
    struct Browser { /* user contexts, client windows */ };
    struct BrowsingContext { /* create/navigate/reload/locateNodes/... */ };
    struct Script { /* evaluate/callFunction/addPreloadScript/... */ };
    struct Log { /* apenas eventos */ };
    struct Network { /* intercepts, continue/fail/provide, headers */ };
    struct Storage { /* cookies */ };
    struct Emulation { /* locale/timezone/UA/geo/orientation */ };
  }

  namespace dsl {
    class Page { /* goto(), waitFor(), click(), ... */ };
  }
}

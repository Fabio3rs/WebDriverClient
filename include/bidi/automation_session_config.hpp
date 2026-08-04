#pragma once

#include "bidi/commands/browsing_context.hpp"
#include "bidi/network_intercept_handler.hpp"
#include "bidi/resilience.hpp"
#include "bidi/script_eval.hpp"
#include "bidi/types/browsing_context.hpp"
#include "bidi/types/network.hpp"
#include "bidi/types/script.hpp"
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bidi {

/**
 * @brief Preload script configuration for automatic injection
 *
 * Preload scripts run automatically when a new realm (JavaScript execution
 * context) is created. Common use cases:
 * - Injecting test utilities
 * - Monkey-patching browser APIs
 * - Setting up global state
 *
 * @example
 * @code
 * PreloadScriptConfig{
 *     .function_declaration = "window.testHelpers = { ... };",
 *     .sandbox = "isolated"  // Optional isolation
 * };
 * @endcode
 */
struct PreloadScriptConfig {
    /**
     * @brief JavaScript function body to execute on realm creation
     *
     * Should be a valid JavaScript function declaration or expression.
     */
    std::string function_declaration;

    /**
     * @brief Optional arguments to pass to the preload script
     *
     * Arguments are marshalled from C++ to JavaScript using LocalValue.
     */
    std::optional<std::vector<types::script::LocalValue>> arguments;

    /**
     * @brief Optional sandbox name for script isolation
     *
     * If specified, the script runs in an isolated sandbox separate from
     * the page's main realm. Useful for avoiding conflicts with page scripts.
     */
    std::optional<std::string> sandbox;
};

/**
 * @brief Script module configuration (Priority: P0)
 *
 * Controls default behavior for JavaScript evaluation and preload scripts.
 *
 * @example
 * @code
 * ScriptConfiguration{
 *     .preload_scripts = {{
 *         .function_declaration = "console.log('Injected!');"
 *     }},
 *     .default_serialization = script::SerializationOptions{
 *         .maxDomDepth = 5,
 *         .maxObjectDepth = 5,
 *         .includeShadowTree = false
 *     },
 *     .default_ownership = script::ResultOwnership::None
 * };
 * @endcode
 */
struct ScriptConfiguration {
    /**
     * @brief Preload scripts to inject on realm creation
     *
     * These scripts run automatically whenever a new JavaScript execution
     * context is created (e.g., new page, iframe, or worker).
     */
    std::vector<PreloadScriptConfig> preload_scripts;

    /**
     * @brief Default serialization options for RemoteValue results
     *
     * Controls how JavaScript objects are serialized when returned from
     * evaluate() calls. Affects nested object depth and DOM tree traversal.
     */
    std::optional<types::script::SerializationOptions> default_serialization;

    /**
     * @brief Default ownership mode for script evaluation results
     *
     * - Root: Results are kept alive in browser (must be manually released)
     * - None: Results are automatically garbage collected
     *
     * Default: None (automatic cleanup)
     */
    types::script::ResultOwnership default_ownership{
        types::script::ResultOwnership::None};
};

/**
 * @brief Viewport dimensions configuration
 *
 * Defines the browser window's inner dimensions (excludes browser chrome).
 */
struct ViewportConfig {
    int width{1920};  ///< Viewport width in pixels
    int height{1080}; ///< Viewport height in pixels
};

/**
 * @brief Browsing context module configuration (Priority: P0)
 *
 * Controls default behavior for navigation, screenshots, and event
 * subscriptions.
 *
 * @example
 * @code
 * BrowsingContextConfiguration{
 *     .default_screenshot_format = types::browsing_context::ImageFormat{
 *         .type = "image/png",
 *         .quality = 0.9
 *     },
 *     .default_viewport = ViewportConfig{1920, 1080},
 *     .auto_subscribe_navigation_events = true
 * };
 * @endcode
 */
struct BrowsingContextConfiguration {
    /**
     * @brief Default screenshot format and quality
     *
     * Used by capture_screenshot() when format is not explicitly specified.
     * Supported formats: "image/png", "image/jpeg", "image/webp"
     */
    std::optional<types::browsing_context::ImageFormat>
        default_screenshot_format;

    /**
     * @brief Default viewport dimensions for new contexts
     *
     * Sets the initial viewport size when creating browsing contexts.
     * If not set, browser defaults are used (typically 800x600).
     */
    std::optional<ViewportConfig> default_viewport;

    /**
     * @brief Auto-subscribe to navigation lifecycle events
     *
     * When true, automatically subscribes to:
     * - browsingContext.navigationStarted
     * - browsingContext.domContentLoaded
     * - browsingContext.load
     * - browsingContext.fragmentNavigated
     *
     * Useful for monitoring page load progress without manual subscription.
     */
    bool auto_subscribe_navigation_events{false};

    /**
     * @brief Auto-subscribe to user prompt events
     *
     * When true, automatically subscribes to:
     * - browsingContext.userPromptOpened
     * - browsingContext.userPromptClosed
     *
     * Useful for detecting and handling alerts/confirms/prompts automatically.
     */
    bool auto_subscribe_user_prompts{false};
};

// ==================== P1: Essential Configuration ====================

/**
 * @brief Network module configuration (Priority: P1)
 *
 * Controls network interception, headers, cache behavior, and authentication.
 * Provides integration with NetworkInterceptHandler for request/response
 * modification.
 *
 * @example Basic interception
 * @code
 * NetworkConfiguration{
 *     .enable_network_intercept = true,
 *     .intercept_policy = NetworkInterceptPolicy::ContinueAll,
 *     .intercept_phases = {InterceptPhase::BeforeRequestSent}
 * };
 * @endcode
 *
 * @example Headers and auth
 * @code
 * NetworkConfiguration{
 *     .extra_headers = {
 *         {"Authorization", "Bearer token123"},
 *         {"X-Test-Mode", "true"}
 *     },
 *     .cache_behavior = NetworkConfiguration::CacheBehavior::bypass,
 *     .default_auth = types::network::AuthCredentials{
 *         .username = "user",
 *         .password = "pass"
 *     }
 * };
 * @endcode
 *
 * @example Custom intercept callback
 * @code
 * NetworkConfiguration{
 *     .enable_network_intercept = true,
 *     .intercept_policy = NetworkInterceptPolicy::Custom,
 *     .before_request_handler = [](const auto& params) {
 *         if (params.base.request.url.contains("tracking")) {
 *             return RequestResolution{.action = InterceptAction::Fail};
 *         }
 *         return RequestResolution{.action = InterceptAction::Continue};
 *     }
 * };
 * @endcode
 */
struct NetworkConfiguration {
    // ========== Interception Control ==========

    /**
     * @brief Enable automatic network interception
     *
     * When true, AutomationSession creates NetworkInterceptHandler during
     * initialization (Phase 1 in run()). The handler is automatically cleaned
     * up when the session is destroyed (RAII).
     *
     * Default: false (no interception)
     */
    bool enable_network_intercept{false};

    /**
     * @brief Intercept policy (ContinueAll/FailAll/Custom)
     *
     * - ContinueAll: All matching requests continue unchanged (monitoring)
     * - FailAll: All matching requests fail (testing error handling)
     * - Custom: Use custom callbacks for full control
     *
     * Default: ContinueAll
     */
    NetworkInterceptPolicy intercept_policy{
        NetworkInterceptPolicy::ContinueAll};

    /**
     * @brief Phases to intercept
     *
     * Possible values:
     * - BeforeRequestSent: Before request is sent to server
     * - ResponseStarted: After response headers received
     * - AuthRequired: When authentication challenge received
     *
     * Default: BeforeRequestSent only
     */
    std::vector<types::network::InterceptPhase> intercept_phases{
        types::network::InterceptPhase::BeforeRequestSent};

    /**
     * @brief URL patterns to intercept (empty = all URLs)
     *
     * Supports two pattern types:
     * - UrlPatternString: Simple string matching
     * - UrlPatternPattern: Advanced pattern matching (protocol, hostname, etc.)
     *
     * If empty, all requests are intercepted.
     */
    std::vector<types::network::UrlPattern> intercept_patterns;

    /**
     * @brief Custom callback for beforeRequestSent phase
     *
     * Called when a request is about to be sent. Return RequestResolution
     * to control request behavior (Continue/Fail/Modify).
     *
     * Only used when intercept_policy == Custom.
     *
     * @see BeforeRequestCallback for signature
     */
    std::optional<BeforeRequestCallback> before_request_handler;

    /**
     * @brief Custom callback for responseStarted phase
     *
     * Called when response headers are received. Return ResponseResolution
     * to control response behavior (Continue/Fail/Provide custom).
     *
     * Only used when intercept_policy == Custom.
     *
     * @see ResponseStartedCallback for signature
     */
    std::optional<ResponseStartedCallback> response_started_handler;

    /**
     * @brief Custom callback for authRequired phase
     *
     * Called when authentication challenge is received. Return AuthResolution
     * to provide credentials or cancel auth.
     *
     * Only used when intercept_policy == Custom.
     *
     * @see AuthRequiredCallback for signature
     */
    std::optional<AuthRequiredCallback> auth_required_handler;

    // ========== Headers & Cache ==========

    /**
     * @brief Extra headers to inject into all requests
     *
     * These headers are automatically added to every intercepted request.
     * Implemented via request modification in beforeRequestSent phase.
     *
     * Common use cases:
     * - Authorization headers for API testing
     * - Custom headers for tracking test runs
     * - User-Agent overrides
     *
     * **Note**: Requires enable_network_intercept = true
     */
    std::map<std::string, std::string> extra_headers;

    /**
     * @brief Cache behavior control
     */
    enum class CacheBehavior {
        default_cache, ///< Use normal browser cache
        bypass         ///< Bypass cache (fresh fetch every time)
    };

    /**
     * @brief Cache behavior setting
     *
     * - default_cache: Normal browser caching behavior
     * - bypass: Force fresh fetches (useful for testing)
     *
     * **Note**: Cache bypass requires enable_network_intercept = true
     *
     * Default: default_cache
     */
    CacheBehavior cache_behavior{CacheBehavior::default_cache};

    // ========== Authentication ==========

    /**
     * @brief Default authentication credentials
     *
     * When set, these credentials are automatically provided when
     * authentication challenges are encountered. Requires intercepting
     * AuthRequired phase.
     *
     * **Note**: Requires enable_network_intercept = true and
     *           AuthRequired in intercept_phases
     *
     * @example
     * @code
     * .default_auth = types::network::AuthCredentials{
     *     .type = "password",
     *     .username = "testuser",
     *     .password = "testpass"
     * }
     * @endcode
     */
    std::optional<types::network::AuthCredentials> default_auth;
};

/**
 * @brief Complete configuration for AutomationSession (Phase 1-2: P0+P1
 * Features)
 *
 * Uses C++20 designated initializers for self-documenting setup.
 * All fields have sensible defaults. Organized by W3C BiDi priority levels.
 *
 * **Phase 1 Focus**: Core script and browsing context configuration
 * **Future Phases**: Network, storage, emulation, observability
 *
 * @example Basic usage (all defaults)
 * @code
 * auto config = AutomationSessionConfig{
 *     .webdriver_url = "http://localhost:9515",
 *     .headless = true
 * };
 * auto session = AutomationSession::start(config);
 * @endcode
 *
 * @example Advanced script configuration
 * @code
 * auto config = AutomationSessionConfig{
 *     .script = {
 *         .preload_scripts = {{
 *             .function_declaration = "window.testHelpers = { ... };"
 *         }},
 *         .default_serialization = script::SerializationOptions{
 *             .maxDomDepth = 5,
 *             .maxObjectDepth = 5,
 *             .includeShadowTree = false
 *         }
 *     },
 *     .browsing_context = {
 *         .default_viewport = ViewportConfig{1366, 768},
 *         .auto_subscribe_navigation_events = true
 *     }
 * };
 * @endcode
 *
 * @example Viewport and screenshot configuration
 * @code
 * auto config = AutomationSessionConfig{
 *     .browsing_context = {
 *         .default_screenshot_format = types::browsing_context::ImageFormat{
 *             .type = "image/webp",
 *             .quality = 0.8
 *         },
 *         .default_viewport = ViewportConfig{1920, 1080}
 *     }
 * };
 * @endcode
 */
struct AutomationSessionConfig {
    // ========== Core Settings (Backward Compatible) ==========

    /**
     * @brief WebDriver server URL
     *
     * Typically "http://localhost:9515" for ChromeDriver.
     */
    std::string webdriver_url{"http://localhost:9515"};

    /**
     * @brief Enable headless mode (no GUI)
     *
     * When true, browser runs without visible window.
     * Useful for CI/CD environments and automated testing.
     */
    bool headless{true};

    /**
     * @brief Custom browser command-line arguments
     *
     * Common examples:
     * - "--disable-gpu" - Disable GPU acceleration
     * - "--no-sandbox" - Disable sandbox (required in some Docker environments)
     * - "--window-size=1920,1080" - Set window size
     * - "--user-agent=CustomBot" - Override user agent
     */
    std::vector<std::string> browser_args{};

    // ========== Operation Defaults ==========

    /**
     * @brief Default readiness state to wait for during navigation
     *
     * - none: Return immediately after navigation starts
     * - interactive: Wait for DOM ready (DOMContentLoaded)
     * - complete: Wait for full page load (window.onload)
     *
     * Individual navigate() calls can override this default.
     */
    commands::browsing_context::ReadinessState default_navigation_wait{
        commands::browsing_context::ReadinessState::complete};

    /**
     * @brief Default script exception handling policy
     *
     * - throw_on_script_exception: JavaScript errors throw C++ exceptions
     * - return_outcome: JavaScript errors captured in ScriptEvalOutcome
     *
     * Individual evaluate() calls can override this default.
     */
    script::script_eval_policy default_script_policy{
        script::script_eval_policy::throw_on_script_exception};

    /**
     * @brief Timeout for pending async operations during cleanup
     *
     * When AutomationSession is destroyed or encounters an error,
     * it waits up to this duration for pending operations to complete.
     *
     * Default: 5000ms (5 seconds)
     */
    std::chrono::milliseconds pending_operations_timeout{5000};

    /**
     * @brief Optional retry policy for failed operations
     *
     * When set, operations that fail with transient errors are automatically
     * retried according to the policy (exponential backoff, max attempts, etc).
     *
     * @see resilience::RetryPolicy::exponential() for common presets
     */
    std::optional<resilience::RetryPolicy> retry_policy;

    // ========== Module Configurations (P0: Critical) ==========

    /**
     * @brief Script module configuration
     *
     * Controls preload scripts, serialization options, and ownership.
     */
    ScriptConfiguration script{};

    /**
     * @brief Browsing context module configuration
     *
     * Controls viewport, screenshots, and event subscriptions.
     */
    BrowsingContextConfiguration browsing_context{};

    // ========== Module Configurations (P1: Essential) ==========

    /**
     * @brief Network module configuration
     *
     * Controls interception, headers, cache, and authentication.
     */
    NetworkConfiguration network{};

    // ========== Infrastructure ==========

    /**
     * @brief Optional external io_context to use
     *
     * When set, AutomationSession uses the provided io_context instead of
     * creating its own. Useful for sharing event loops across multiple
     * sessions.
     *
     * **CRITICAL**: User must ensure io_context outlives AutomationSession.
     * Use std::reference_wrapper to make ownership semantics explicit.
     *
     * @warning Experimental feature. Strand safety and lifecycle management
     *          need careful consideration.
     */
    std::optional<std::reference_wrapper<boost::asio::io_context>>
        external_io_context;
};

} // namespace bidi

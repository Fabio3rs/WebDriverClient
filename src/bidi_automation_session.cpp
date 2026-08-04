#include "bidi/automation_session.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/logging.hpp"
#include "bidi/script/marshalling.hpp"
#include <boost/json/object.hpp>
#include <boost/json/string.hpp>
#include <limits>
#include <stdexcept>
#include <utility>

namespace bidi {

// ==================== Network Configuration Helpers ====================

namespace {

constexpr std::string_view wait_for_element_function = R"js(
function(selector, selectorType, timeoutMs) {
    return new Promise((resolve, reject) => {
        let observer = null;
        let timeoutId = null;
        let domReadyHandler = null;

        const cleanup = () => {
            if (observer !== null) {
                observer.disconnect();
                observer = null;
            }
            if (timeoutId !== null) {
                clearTimeout(timeoutId);
                timeoutId = null;
            }
            if (domReadyHandler !== null) {
                document.removeEventListener("DOMContentLoaded", domReadyHandler);
                domReadyHandler = null;
            }
        };

        const findElement = () => {
            if (selectorType === "css") {
                return document.querySelector(selector);
            }
            return document.evaluate(
                selector,
                document,
                null,
                XPathResult.FIRST_ORDERED_NODE_TYPE,
                null
            ).singleNodeValue;
        };

        const check = () => {
            try {
                if (findElement() === null) {
                    return false;
                }
                cleanup();
                resolve(true);
                return true;
            } catch (error) {
                cleanup();
                reject(error);
                return true;
            }
        };

        const observe = () => {
            if (check()) {
                return;
            }

            const root = document.documentElement || document.body;
            if (root === null) {
                domReadyHandler = observe;
                document.addEventListener(
                    "DOMContentLoaded",
                    domReadyHandler,
                    {once: true}
                );
                return;
            }

            observer = new MutationObserver(check);
            observer.observe(root, {
                attributes: true,
                childList: true,
                subtree: true
            });
        };

        timeoutId = setTimeout(() => {
            if (!check()) {
                cleanup();
                resolve(false);
            }
        }, timeoutMs);
        observe();
    });
}
)js";

[[nodiscard]] constexpr auto
selector_type_name(ElementSelectorType type) -> std::string_view {
    switch (type) {
    case ElementSelectorType::css:
        return "css";
    case ElementSelectorType::xpath:
        return "xpath";
    }
    std::unreachable();
}

/**
 * @brief Apply extra headers to request resolution
 *
 * Merges extra_headers into the resolution's headers field.
 * Creates headers field if it doesn't exist.
 */
void apply_extra_headers(
    RequestResolution &resolution,
    const std::map<std::string, std::string> &extra_headers) {
    if (extra_headers.empty()) {
        return;
    }

    // Convert map to Header vector
    std::vector<types::network::Header> headers;
    headers.reserve(extra_headers.size());

    for (const auto &[name, value] : extra_headers) {
        headers.push_back(types::network::Header{
            .name = name,
            .value = types::network::StringBytes{.value = value}});
    }

    // Merge with existing headers (if any)
    if (resolution.headers) {
        resolution.headers->insert(resolution.headers->end(),
                                   std::make_move_iterator(headers.begin()),
                                   std::make_move_iterator(headers.end()));
    } else {
        resolution.headers = std::move(headers);
    }
}

/**
 * @brief Apply cache bypass to request resolution
 *
 * Adds Cache-Control: no-cache, no-store header to force fresh fetches.
 */
void apply_cache_bypass(RequestResolution &resolution) {
    if (!resolution.headers) {
        resolution.headers = std::vector<types::network::Header>{};
    }

    resolution.headers->push_back(types::network::Header{
        .name = "Cache-Control",
        .value = types::network::StringBytes{.value = "no-cache, no-store"}});
}

/**
 * @brief Wrap beforeRequestSent callback with convenience features
 *
 * Combines user callback (if provided) with extra_headers and cache bypass.
 * User callback is called first, then convenience features are applied.
 */
[[nodiscard]] auto
wrap_before_request_callback(const NetworkConfiguration &net_config)
    -> std::optional<BeforeRequestCallback> {

    // Check if we need any wrapping
    bool has_features = !net_config.extra_headers.empty() ||
                        net_config.cache_behavior ==
                            NetworkConfiguration::CacheBehavior::bypass;

    if (!has_features && !net_config.before_request_handler) {
        return std::nullopt;
    }

    // Create combined callback
    return
        [net_config](const types::network::BeforeRequestSentParameters &params)
            -> std::optional<RequestResolution> {
            // Step 1: Call user callback first (if provided)
            auto resolution = net_config.before_request_handler
                                  ? (*net_config.before_request_handler)(params)
                                  : std::make_optional<RequestResolution>();

            if (!resolution) {
                return std::nullopt; // User wants no action
            }

            // Step 2: Apply convenience features
            apply_extra_headers(*resolution, net_config.extra_headers);

            if (net_config.cache_behavior ==
                NetworkConfiguration::CacheBehavior::bypass) {
                apply_cache_bypass(*resolution);
            }

            return resolution;
        };
}

/**
 * @brief Wrap authRequired callback with default authentication
 *
 * Combines user callback (if provided) with default_auth credentials.
 * User callback takes precedence if provided.
 */
[[nodiscard]] auto
wrap_auth_required_callback(const NetworkConfiguration &net_config)
    -> std::optional<AuthRequiredCallback> {

    if (!net_config.default_auth && !net_config.auth_required_handler) {
        return std::nullopt;
    }

    return [net_config](const types::network::AuthRequiredParameters &params)
               -> std::optional<AuthResolution> {
        // Step 1: User callback takes precedence
        if (net_config.auth_required_handler) {
            return (*net_config.auth_required_handler)(params);
        }

        // Step 2: Use default_auth
        if (net_config.default_auth) {
            return AuthResolution{
                .action = InterceptAction::Continue,
                .auth_action = types::network::AuthAction::ProvideCredentials,
                .credentials = net_config.default_auth};
        }

        return std::nullopt;
    };
}

} // anonymous namespace

// ==================== Public Helper Functions ====================

/**
 * @brief Build NetworkInterceptConfig from NetworkConfiguration
 *
 * Converts AutomationSession's network configuration to NetworkInterceptHandler
 * configuration, applying convenience features and validation.
 *
 * @param net_config Source configuration from AutomationSessionConfig
 * @param context_id Browsing context ID to filter intercepts
 * @return Expected NetworkInterceptConfig or error string
 */
[[nodiscard]] auto
build_network_intercept_config(const NetworkConfiguration &net_config,
                               std::string_view context_id)
    -> std::expected<NetworkInterceptConfig, std::string> {

    // Validation (fail fast with clear error messages)
    if (!net_config.extra_headers.empty() &&
        !net_config.enable_network_intercept) {
        return std::unexpected(
            "extra_headers requires enable_network_intercept = true");
    }

    if (net_config.cache_behavior ==
            NetworkConfiguration::CacheBehavior::bypass &&
        !net_config.enable_network_intercept) {
        return std::unexpected(
            "cache bypass requires enable_network_intercept = true");
    }

    if (net_config.default_auth && !net_config.enable_network_intercept) {
        return std::unexpected(
            "default_auth requires enable_network_intercept = true");
    }

    // Determine effective policy
    auto effective_policy = net_config.intercept_policy;
    bool has_convenience = !net_config.extra_headers.empty() ||
                           net_config.cache_behavior ==
                               NetworkConfiguration::CacheBehavior::bypass ||
                           net_config.default_auth.has_value();

    if (has_convenience && effective_policy != NetworkInterceptPolicy::Custom) {
        // Auto-upgrade to Custom when convenience features are used
        logging::log_info(
            "Network configuration: auto-upgrading to Custom policy "
            "(convenience features detected)");
        effective_policy = NetworkInterceptPolicy::Custom;
    }

    // Build config with all fields explicitly initialized
    NetworkInterceptConfig intercept_config{
        .policy = effective_policy,
        .phases = net_config.intercept_phases,
        .contexts = std::vector<std::string>{std::string(context_id)},
        .url_patterns = net_config.intercept_patterns.empty()
                            ? std::nullopt
                            : std::make_optional(net_config.intercept_patterns),
        .before_request_handler = wrap_before_request_callback(net_config),
        .response_started_handler = net_config.response_started_handler,
        .auth_required_handler = wrap_auth_required_callback(net_config)};

    return intercept_config;
}

// ==================== AutomationSession Implementation ====================

// Phase 1 constructor: HTTP-only initialization (before WebSocket connect)
AutomationSession::AutomationSession(
    std::unique_ptr<IoContextRunner> runner, std::string websocket_url,
    std::shared_ptr<SessionGuard> session_guard, AutomationSessionConfig config)
    : runner_(std::move(runner)), client_guard_(nullptr), context_id_{},
      websocket_url_(std::move(websocket_url)),
      session_guard_(std::move(session_guard)), config_(std::move(config)) {}

// Phase 2 constructor: Full initialization (after WebSocket connect)
AutomationSession::AutomationSession(std::unique_ptr<IoContextRunner> runner,
                                     std::unique_ptr<ClientGuard> client_guard,
                                     std::string context_id,
                                     AutomationSessionConfig config)
    : runner_(std::move(runner)), client_guard_(std::move(client_guard)),
      context_id_(std::move(context_id)), session_guard_(nullptr),
      config_(std::move(config)) {}

// Static factory (blocking HTTP handshake only, WebSocket deferred to run())
// Backward-compatible version - delegates to config-based start()
auto AutomationSession::start(std::string_view webdriver_url,
                              bool headless) -> AutomationSession {
    // Create default config from parameters
    AutomationSessionConfig config{};
    config.webdriver_url = std::string(webdriver_url);
    config.headless = headless;

    // Delegate to config-based start()
    return start(config);
}

// Config-based static factory (blocking HTTP handshake only, WebSocket deferred
// to run())
auto AutomationSession::start(const AutomationSessionConfig &config)
    -> AutomationSession {

    // Phase 1: Start background io_context thread
    auto runner = std::make_unique<IoContextRunner>();

    // Phase 2: HTTP handshake only (blocking, synchronous, NO WebSocket yet)
    auto builder = connect_to(config.webdriver_url);

    // Apply basic configuration to connection builder
    if (config.headless) {
        builder = builder.headless();
    }

    // Apply browser args (including no-sandbox if not already present)
    bool has_no_sandbox = false;
    for (const auto &arg : config.browser_args) {
        if (arg == "--no-sandbox") {
            has_no_sandbox = true;
            break;
        }
    }

    if (!has_no_sandbox) {
        builder = builder.no_sandbox();
    }

    if (!config.browser_args.empty()) {
        builder = builder.with_args(config.browser_args);
    }

    // Get WebSocket URL via HTTP handshake (blocking)
    auto [ws_url, session_guard] = std::move(builder).get_websocket_url();

    bidi::logging::log_info("AutomationSession HTTP handshake complete: ws=" +
                            ws_url);

    // Return with deferred WebSocket connection
    // Phase 2 (WebSocket connect + context creation + config application) will
    // happen in run()
    return {std::move(runner), std::move(ws_url), std::move(session_guard),
            config};
}

// Navigate (async, returns lazy Task)
auto AutomationSession::navigate(
    std::string_view url, commands::browsing_context::ReadinessState wait,
    const std::source_location &loc) -> Task<std::string> {
    if (!client_guard_) {
        throw std::runtime_error(
            "Navigate called before run() - WebSocket not connected");
    }
    return client_guard_->client()->navigate(context_id_, url, wait, loc);
}

// Evaluate (async, returns lazy Task)
auto AutomationSession::evaluate(std::string_view expression,
                                 const std::source_location &loc)
    -> Task<boost::json::object> {
    if (!client_guard_) {
        throw std::runtime_error(
            "Evaluate called before run() - WebSocket not connected");
    }
    return client_guard_->client()->evaluate(expression, context_id_, true,
                                             loc);
}

// Get page title (convenience wrapper)
auto AutomationSession::get_title(const std::source_location &loc)
    -> Task<std::string> {
    return evaluate_as_or("document.title", std::string(""), loc);
}

// Get current URL (convenience wrapper)
auto AutomationSession::get_url(const std::source_location &loc)
    -> Task<std::string> {
    return evaluate_as_or("document.location.href", std::string(""), loc);
}

auto AutomationSession::wait_for_element(
    std::string_view selector, std::chrono::milliseconds timeout,
    ElementSelectorType type, const std::source_location &loc) -> Task<bool> {
    if (!client_guard_) {
        throw std::runtime_error(
            "wait_for_element called before run() - WebSocket not connected");
    }
    if (timeout.count() < 0 ||
        timeout.count() > std::numeric_limits<std::int32_t>::max()) {
        throw std::invalid_argument(
            "wait_for_element timeout must be between 0ms and 2147483647ms");
    }

    auto arguments = script::make_args_array(selector, selector_type_name(type),
                                             timeout.count());
    return client_guard_->client()
        ->call_function(
            wait_for_element_function, context_id_, std::move(arguments),
            script::script_eval_policy::throw_on_script_exception, true, loc)
        .map(
            [](const script::ScriptEvalOutcome &outcome) {
                return script::extract_value_from_outcome<bool>(outcome);
            },
            loc);
}

} // namespace bidi

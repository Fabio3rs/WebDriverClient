#include "bidi/automation_session.hpp"
#include "bidi/automation_session_builder.hpp"
#include <format>
#include <utility>

namespace bidi {

// ========== Core Settings ==========

auto AutomationSessionBuilder::webdriver_url(
    std::string url) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.webdriver_url = std::move(url);
    return copy;
}

auto AutomationSessionBuilder::webdriver_url(
    std::string url) && -> AutomationSessionBuilder {
    config_.webdriver_url = std::move(url);
    return std::move(*this);
}

auto AutomationSessionBuilder::headless(
    bool enable) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.headless = enable;
    return copy;
}

auto AutomationSessionBuilder::headless(
    bool enable) && -> AutomationSessionBuilder {
    config_.headless = enable;
    return std::move(*this);
}

auto AutomationSessionBuilder::with_browser_args(
    const std::vector<std::string> &args) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.browser_args = args;
    return copy;
}

auto AutomationSessionBuilder::with_browser_args(
    std::vector<std::string> &&args) && -> AutomationSessionBuilder {
    config_.browser_args = std::move(args);
    return std::move(*this);
}

auto AutomationSessionBuilder::append_browser_args(
    const std::vector<std::string> &args) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.browser_args.insert(copy.config_.browser_args.end(),
                                     args.begin(), args.end());
    return copy;
}

auto AutomationSessionBuilder::append_browser_args(
    std::vector<std::string> &&args) && -> AutomationSessionBuilder {
    config_.browser_args.insert(config_.browser_args.end(),
                                std::make_move_iterator(args.begin()),
                                std::make_move_iterator(args.end()));
    return std::move(*this);
}

auto AutomationSessionBuilder::no_sandbox()
    const & -> AutomationSessionBuilder {
    return append_browser_args({"--no-sandbox"});
}

auto AutomationSessionBuilder::no_sandbox() && -> AutomationSessionBuilder {
    return std::move(*this).append_browser_args({"--no-sandbox"});
}

auto AutomationSessionBuilder::disable_gpu()
    const & -> AutomationSessionBuilder {
    return append_browser_args({"--disable-gpu"});
}

auto AutomationSessionBuilder::disable_gpu() && -> AutomationSessionBuilder {
    return std::move(*this).append_browser_args({"--disable-gpu"});
}

auto AutomationSessionBuilder::window_size(
    int width, int height) const & -> AutomationSessionBuilder {
    return append_browser_args(
        {std::format("--window-size={},{}", width, height)});
}

auto AutomationSessionBuilder::window_size(
    int width, int height) && -> AutomationSessionBuilder {
    return std::move(*this).append_browser_args(
        {std::format("--window-size={},{}", width, height)});
}

auto AutomationSessionBuilder::user_agent(
    std::string_view agent) const & -> AutomationSessionBuilder {
    return append_browser_args({std::format("--user-agent={}", agent)});
}

auto AutomationSessionBuilder::user_agent(
    std::string_view agent) && -> AutomationSessionBuilder {
    return std::move(*this).append_browser_args(
        {std::format("--user-agent={}", agent)});
}

// ========== Operation Defaults ==========

auto AutomationSessionBuilder::with_default_navigation_wait(
    commands::browsing_context::ReadinessState state)
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.default_navigation_wait = state;
    return copy;
}

auto AutomationSessionBuilder::with_default_navigation_wait(
    commands::browsing_context::ReadinessState state)
    && -> AutomationSessionBuilder {
    config_.default_navigation_wait = state;
    return std::move(*this);
}

auto AutomationSessionBuilder::with_default_script_policy(
    script::script_eval_policy policy) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.default_script_policy = policy;
    return copy;
}

auto AutomationSessionBuilder::with_default_script_policy(
    script::script_eval_policy policy) && -> AutomationSessionBuilder {
    config_.default_script_policy = policy;
    return std::move(*this);
}

auto AutomationSessionBuilder::with_timeout(
    std::chrono::milliseconds timeout) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.pending_operations_timeout = timeout;
    return copy;
}

auto AutomationSessionBuilder::with_timeout(
    std::chrono::milliseconds timeout) && -> AutomationSessionBuilder {
    config_.pending_operations_timeout = timeout;
    return std::move(*this);
}

auto AutomationSessionBuilder::with_retry_policy(
    const resilience::RetryPolicy &policy) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.retry_policy = policy;
    return copy;
}

auto AutomationSessionBuilder::with_retry_policy(
    resilience::RetryPolicy &&policy) && -> AutomationSessionBuilder {
    config_.retry_policy = std::move(policy);
    return std::move(*this);
}

// ========== P0: Script Configuration ==========

auto AutomationSessionBuilder::with_preload_scripts(
    const std::vector<PreloadScriptConfig> &scripts)
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.script.preload_scripts = scripts;
    return copy;
}

auto AutomationSessionBuilder::with_preload_scripts(
    std::vector<PreloadScriptConfig> &&scripts) && -> AutomationSessionBuilder {
    config_.script.preload_scripts = std::move(scripts);
    return std::move(*this);
}

auto AutomationSessionBuilder::with_default_serialization(
    const types::script::SerializationOptions &options)
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.script.default_serialization = options;
    return copy;
}

auto AutomationSessionBuilder::with_default_serialization(
    types::script::SerializationOptions &&options)
    && -> AutomationSessionBuilder {
    config_.script.default_serialization = std::move(options);
    return std::move(*this);
}

auto AutomationSessionBuilder::with_default_ownership(
    types::script::ResultOwnership ownership)
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.script.default_ownership = ownership;
    return copy;
}

auto AutomationSessionBuilder::with_default_ownership(
    types::script::ResultOwnership ownership) && -> AutomationSessionBuilder {
    config_.script.default_ownership = ownership;
    return std::move(*this);
}

// ========== P0: Browsing Context Configuration ==========

auto AutomationSessionBuilder::with_viewport(
    int width, int height) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.browsing_context.default_viewport =
        ViewportConfig{width, height};
    return copy;
}

auto AutomationSessionBuilder::with_viewport(
    int width, int height) && -> AutomationSessionBuilder {
    config_.browsing_context.default_viewport = ViewportConfig{width, height};
    return std::move(*this);
}

auto AutomationSessionBuilder::auto_subscribe_to_navigation()
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.browsing_context.auto_subscribe_navigation_events = true;
    return copy;
}

auto AutomationSessionBuilder::auto_subscribe_to_navigation()
    && -> AutomationSessionBuilder {
    config_.browsing_context.auto_subscribe_navigation_events = true;
    return std::move(*this);
}

auto AutomationSessionBuilder::auto_subscribe_to_user_prompts()
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.browsing_context.auto_subscribe_user_prompts = true;
    return copy;
}

auto AutomationSessionBuilder::auto_subscribe_to_user_prompts()
    && -> AutomationSessionBuilder {
    config_.browsing_context.auto_subscribe_user_prompts = true;
    return std::move(*this);
}

auto AutomationSessionBuilder::with_screenshot_format(
    std::string_view type, double quality) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.browsing_context.default_screenshot_format =
        types::browsing_context::ImageFormat{.type = std::string(type),
                                             .quality = quality};
    return copy;
}

auto AutomationSessionBuilder::with_screenshot_format(
    std::string_view type, double quality) && -> AutomationSessionBuilder {
    config_.browsing_context.default_screenshot_format =
        types::browsing_context::ImageFormat{.type = std::string(type),
                                             .quality = quality};
    return std::move(*this);
}

// ========== P1: Network Configuration ==========

auto AutomationSessionBuilder::enable_network_intercept(
    NetworkInterceptPolicy policy,
    std::vector<types::network::InterceptPhase> phases)
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.network.enable_network_intercept = true;
    copy.config_.network.intercept_policy = policy;
    copy.config_.network.intercept_phases = std::move(phases);
    return copy;
}

auto AutomationSessionBuilder::enable_network_intercept(
    NetworkInterceptPolicy policy, std::vector<types::network::InterceptPhase>
                                       phases) && -> AutomationSessionBuilder {
    config_.network.enable_network_intercept = true;
    config_.network.intercept_policy = policy;
    config_.network.intercept_phases = std::move(phases);
    return std::move(*this);
}

auto AutomationSessionBuilder::with_intercept_patterns(
    const std::vector<types::network::UrlPattern> &patterns)
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.network.intercept_patterns = patterns;
    return copy;
}

auto AutomationSessionBuilder::with_intercept_patterns(
    std::vector<types::network::UrlPattern> &&patterns)
    && -> AutomationSessionBuilder {
    config_.network.intercept_patterns = std::move(patterns);
    return std::move(*this);
}

auto AutomationSessionBuilder::with_extra_headers(
    const std::map<std::string, std::string> &headers)
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.network.extra_headers = headers;
    // Automatically enable network interception
    copy.config_.network.enable_network_intercept = true;
    return copy;
}

auto AutomationSessionBuilder::with_extra_headers(
    std::map<std::string, std::string> &&headers)
    && -> AutomationSessionBuilder {
    config_.network.extra_headers = std::move(headers);
    // Automatically enable network interception
    config_.network.enable_network_intercept = true;
    return std::move(*this);
}

auto AutomationSessionBuilder::bypass_cache()
    const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.network.cache_behavior =
        NetworkConfiguration::CacheBehavior::bypass;
    // Automatically enable network interception
    copy.config_.network.enable_network_intercept = true;
    return copy;
}

auto AutomationSessionBuilder::bypass_cache() && -> AutomationSessionBuilder {
    config_.network.cache_behavior =
        NetworkConfiguration::CacheBehavior::bypass;
    // Automatically enable network interception
    config_.network.enable_network_intercept = true;
    return std::move(*this);
}

auto AutomationSessionBuilder::with_default_auth(
    std::string_view username,
    std::string_view password) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.network.default_auth =
        types::network::AuthCredentials{.type = "password",
                                        .username = std::string(username),
                                        .password = std::string(password)};
    // Automatically enable network interception and add AuthRequired phase
    copy.config_.network.enable_network_intercept = true;
    // Check if AuthRequired is already in phases
    bool has_auth_phase = false;
    for (const auto &phase : copy.config_.network.intercept_phases) {
        if (phase == types::network::InterceptPhase::AuthRequired) {
            has_auth_phase = true;
            break;
        }
    }
    if (!has_auth_phase) {
        copy.config_.network.intercept_phases.push_back(
            types::network::InterceptPhase::AuthRequired);
    }
    return copy;
}

auto AutomationSessionBuilder::with_default_auth(
    std::string_view username,
    std::string_view password) && -> AutomationSessionBuilder {
    config_.network.default_auth =
        types::network::AuthCredentials{.type = "password",
                                        .username = std::string(username),
                                        .password = std::string(password)};
    // Automatically enable network interception and add AuthRequired phase
    config_.network.enable_network_intercept = true;
    // Check if AuthRequired is already in phases
    bool has_auth_phase = false;
    for (const auto &phase : config_.network.intercept_phases) {
        if (phase == types::network::InterceptPhase::AuthRequired) {
            has_auth_phase = true;
            break;
        }
    }
    if (!has_auth_phase) {
        config_.network.intercept_phases.push_back(
            types::network::InterceptPhase::AuthRequired);
    }
    return std::move(*this);
}

// ========== Infrastructure ==========

auto AutomationSessionBuilder::with_io_context(
    boost::asio::io_context &ioc) const & -> AutomationSessionBuilder {
    AutomationSessionBuilder copy = *this;
    copy.config_.external_io_context = std::ref(ioc);
    return copy;
}

auto AutomationSessionBuilder::with_io_context(
    boost::asio::io_context &ioc) && -> AutomationSessionBuilder {
    config_.external_io_context = std::ref(ioc);
    return std::move(*this);
}

// ========== Terminal Operation ==========

auto AutomationSessionBuilder::start() && -> AutomationSession {
    // Use config-based AutomationSession::start()
    return AutomationSession::start(config_);
}

} // namespace bidi

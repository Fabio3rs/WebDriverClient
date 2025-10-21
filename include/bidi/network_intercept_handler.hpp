#pragma once
/**
 * @file network_intercept_handler.hpp
 * @brief RAII handler for network request/response interception
 *
 * Provides policy-based automatic handling of network intercepts following
 * W3C WebDriver BiDi specification.
 *
 * Architecture:
 * - RAII subscription management (C.21, C.31)
 * - Policy-based dispatch (zero-cost abstraction)
 * - Move-only semantics (prevents subscription leaks)
 * - Factory pattern for async construction (F.15, C.45)
 *
 * @see https://w3c.github.io/webdriver-bidi/#module-network
 */

#include "asyncx.hpp"
#include "bidi/types/network.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <vector>

namespace bidi {

// Forward declaration
class Client;
template <typename T> using Task = asyncx::Async<T>;

// ==================== Intercept Action Enums ====================

/**
 * @brief Action to take on intercepted request/response
 */
enum class InterceptAction : std::uint8_t {
    Continue, // Continue with original request/response
    Provide,  // Provide custom response
    Fail,     // Fail the request
    Ignore    // Don't handle (leave for manual handling)
};

// ==================== Resolution Structs ====================

/**
 * @brief Resolution for beforeRequestSent phase
 */
struct RequestResolution {
    InterceptAction action{InterceptAction::Continue};
    std::optional<types::network::BytesValue> body;
    std::optional<std::vector<types::network::CookieHeader>> cookies;
    std::optional<std::vector<types::network::Header>> headers;
    std::optional<std::string> method;
    std::optional<std::string> url;

    auto operator==(const RequestResolution &) const -> bool = default;
};

/**
 * @brief Resolution for responseStarted phase
 */
struct ResponseResolution {
    InterceptAction action{InterceptAction::Continue};
    std::optional<types::network::BytesValue> body;
    std::optional<std::vector<types::network::SetCookieHeader>> cookies;
    std::optional<types::network::AuthCredentials> credentials;
    std::optional<std::vector<types::network::Header>> headers;
    std::optional<std::string> reason_phrase;
    std::optional<std::uint64_t> status_code;

    auto operator==(const ResponseResolution &) const -> bool = default;
};

/**
 * @brief Resolution for authRequired phase
 */
struct AuthResolution {
    InterceptAction action{InterceptAction::Continue};
    types::network::AuthAction auth_action{types::network::AuthAction::Default};
    std::optional<types::network::AuthCredentials> credentials;

    auto operator==(const AuthResolution &) const -> bool = default;
};

// ==================== Policy System ====================

/**
 * @brief Predefined policies for network interception
 */
enum class NetworkInterceptPolicy : std::uint8_t {
    ContinueAll, // Continue all requests/responses unchanged
    FailAll,     // Fail all requests
    Custom       // Use custom callbacks
};

// ==================== Callback Concepts (C++20) ====================

/**
 * @brief Concept: Validates callback for beforeRequestSent phase
 *
 * Ensures callable accepts BeforeRequestSentParameters and returns
 * std::optional<RequestResolution>. Provides clear compile-time errors
 * when callback signature is incorrect.
 *
 * @tparam F Callable type to validate
 */
template <typename F>
concept BeforeRequestCallable =
    requires(F f, const types::network::BeforeRequestSentParameters &params) {
        { f(params) } -> std::convertible_to<std::optional<RequestResolution>>;
    };

/**
 * @brief Concept: Validates callback for responseStarted phase
 *
 * Ensures callable accepts ResponseStartedParameters and returns
 * std::optional<ResponseResolution>.
 *
 * @tparam F Callable type to validate
 */
template <typename F>
concept ResponseStartedCallable =
    requires(F f, const types::network::ResponseStartedParameters &params) {
        { f(params) } -> std::convertible_to<std::optional<ResponseResolution>>;
    };

/**
 * @brief Concept: Validates callback for authRequired phase
 *
 * Ensures callable accepts AuthRequiredParameters and returns
 * std::optional<AuthResolution>.
 *
 * @tparam F Callable type to validate
 */
template <typename F>
concept AuthRequiredCallable =
    requires(F f, const types::network::AuthRequiredParameters &params) {
        { f(params) } -> std::convertible_to<std::optional<AuthResolution>>;
    };

/**
 * @brief Callback signatures for custom intercept handling
 *
 * These type aliases accept any callable matching the respective concept.
 * Use concepts above for compile-time validation.
 */
using BeforeRequestCallback = std::function<std::optional<RequestResolution>(
    const types::network::BeforeRequestSentParameters &)>;

using ResponseStartedCallback = std::function<std::optional<ResponseResolution>(
    const types::network::ResponseStartedParameters &)>;

using AuthRequiredCallback = std::function<std::optional<AuthResolution>(
    const types::network::AuthRequiredParameters &)>;

/**
 * @brief Configuration for network intercept handler
 *
 * Factory methods provide ergonomic construction (C.45).
 */
struct NetworkInterceptConfig {
    NetworkInterceptPolicy policy{NetworkInterceptPolicy::ContinueAll};
    std::vector<types::network::InterceptPhase> phases;
    std::optional<std::vector<std::string>> contexts;
    std::optional<std::vector<types::network::UrlPattern>> url_patterns;

    // Custom callbacks (only used when policy == Custom)
    std::optional<BeforeRequestCallback> before_request_handler;
    std::optional<ResponseStartedCallback> response_started_handler;
    std::optional<AuthRequiredCallback> auth_required_handler;

    // Factory methods (C.45 - prefer factory functions)
    static auto continue_all(
        std::vector<types::network::InterceptPhase> phases_to_intercept =
            {types::network::InterceptPhase::BeforeRequestSent})
        -> NetworkInterceptConfig {
        return NetworkInterceptConfig{.policy =
                                          NetworkInterceptPolicy::ContinueAll,
                                      .phases = std::move(phases_to_intercept),
                                      .contexts = std::nullopt,
                                      .url_patterns = std::nullopt,
                                      .before_request_handler = std::nullopt,
                                      .response_started_handler = std::nullopt,
                                      .auth_required_handler = std::nullopt};
    }

    static auto
    fail_all(std::vector<types::network::InterceptPhase> phases_to_intercept =
                 {types::network::InterceptPhase::BeforeRequestSent})
        -> NetworkInterceptConfig {
        return NetworkInterceptConfig{.policy = NetworkInterceptPolicy::FailAll,
                                      .phases = std::move(phases_to_intercept),
                                      .contexts = std::nullopt,
                                      .url_patterns = std::nullopt,
                                      .before_request_handler = std::nullopt,
                                      .response_started_handler = std::nullopt,
                                      .auth_required_handler = std::nullopt};
    }

    /**
     * @brief Create custom policy configuration with validated callbacks
     *
     * @tparam BeforeReqFn Callable for beforeRequestSent (concept-validated)
     * @tparam RespStartedFn Callable for responseStarted (concept-validated)
     * @tparam AuthReqFn Callable for authRequired (concept-validated)
     *
     * Concepts ensure compile-time validation of callback signatures,
     * providing clear error messages if types don't match.
     */
    template <typename BeforeReqFn = BeforeRequestCallback,
              typename RespStartedFn = ResponseStartedCallback,
              typename AuthReqFn = AuthRequiredCallback>
        requires(std::same_as<BeforeReqFn, BeforeRequestCallback> ||
                 BeforeRequestCallable<BeforeReqFn>) &&
                    (std::same_as<RespStartedFn, ResponseStartedCallback> ||
                     ResponseStartedCallable<RespStartedFn>) &&
                    (std::same_as<AuthReqFn, AuthRequiredCallback> ||
                     AuthRequiredCallable<AuthReqFn>)
    static auto custom(
        const std::vector<types::network::InterceptPhase> &phases_to_intercept,
        BeforeReqFn before_request = {}, RespStartedFn response_started = {},
        AuthReqFn auth_required = {}) -> NetworkInterceptConfig {
        return NetworkInterceptConfig{
            .policy = NetworkInterceptPolicy::Custom,
            .phases = phases_to_intercept,
            .contexts = std::nullopt,
            .url_patterns = std::nullopt,
            .before_request_handler = std::move(before_request),
            .response_started_handler = std::move(response_started),
            .auth_required_handler = std::move(auth_required)};
    }
};

// ==================== RAII Handler ====================

/**
 * @brief RAII guard for network interception
 *
 * Automatically subscribes to network events, handles intercepted requests/
 * responses according to policy, and cleans up on destruction.
 *
 * Guidelines compliance:
 * - C.21: Move-only semantics (prevents subscription duplication)
 * - C.31: noexcept destructor (RAII cleanup)
 * - C.45: Factory method for async construction
 * - F.15: Return values over out-parameters
 *
 * Example:
 * @code
 * auto handler = co_await NetworkInterceptHandler::create(
 *     client,
 *     NetworkInterceptConfig::continue_all({
 *         InterceptPhase::BeforeRequestSent
 *     }));
 * // All matching requests automatically continued until handler destroyed
 * @endcode
 *
 * @see https://w3c.github.io/webdriver-bidi/#command-network-addIntercept
 */
class NetworkInterceptHandler {
  public:
    /**
     * @brief Factory: Create and initialize intercept handler
     *
     * @param client BiDi client instance
     * @param config Intercept configuration with policy
     * @param loc Source location for debugging (auto-captured)
     * @return Task resolving to handler instance
     */
    [[nodiscard]] static auto
    create(std::shared_ptr<Client> client, NetworkInterceptConfig config,
           const std::source_location &loc = std::source_location::current())
        -> Task<std::shared_ptr<NetworkInterceptHandler>>;

    /**
     * @brief Explicit asynchronous cleanup hook
     *
     * Removes the network intercept and surfaces any failures to the caller.
     * Callers should co_await this before letting the handler go out of scope
     * to guarantee deterministic teardown.
     */
    [[nodiscard]] auto
    cleanup(const std::source_location &loc = std::source_location::current())
        -> Task<void>;

    /**
     * @brief Destructor: Remove intercept and unsubscribe
     *
     * RAII cleanup - must be noexcept (C.31)
     */
    ~NetworkInterceptHandler() noexcept;

    // C.21: Move-only semantics
    NetworkInterceptHandler(NetworkInterceptHandler &&other) noexcept;
    auto operator=(NetworkInterceptHandler &&other) noexcept
        -> NetworkInterceptHandler &;
    NetworkInterceptHandler(const NetworkInterceptHandler &) = delete;
    auto operator=(const NetworkInterceptHandler &)
        -> NetworkInterceptHandler & = delete;

    /**
     * @brief Update policy at runtime
     *
     * @param new_config New configuration to apply
     */
    void set_config(NetworkInterceptConfig new_config) {
        config_ = std::move(new_config);
    }

    /**
     * @brief Check if handler is active
     *
     * @return true if intercept is registered and subscriptions active
     */
    [[nodiscard]] auto is_active() const noexcept -> bool {
        return !intercept_id_.empty();
    }

    /**
     * @brief Get intercept ID
     *
     * @return Intercept identifier from network.addIntercept
     */
    [[nodiscard]] auto intercept_id() const noexcept -> const std::string & {
        return intercept_id_;
    }

  private:
    // Private constructor - use factory method
    explicit NetworkInterceptHandler(std::shared_ptr<Client> client,
                                     NetworkInterceptConfig config,
                                     types::network::InterceptId intercept_id)
        : client_(std::move(client)), config_(std::move(config)),
          intercept_id_(std::move(intercept_id)) {}

    // Event handlers
    void handle_before_request(
        const types::network::BeforeRequestSentParameters &params);
    void handle_response_started(
        const types::network::ResponseStartedParameters &params);
    void
    handle_auth_required(const types::network::AuthRequiredParameters &params);

    std::shared_ptr<Client> client_;
    NetworkInterceptConfig config_;
    types::network::InterceptId intercept_id_;
    std::atomic<bool> cleanup_started_{false};
    std::atomic<bool> cleanup_succeeded_{false};
};

} // namespace bidi

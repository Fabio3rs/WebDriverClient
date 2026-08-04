#pragma once
/**
 * @file resilience.hpp
 * @brief Resilience patterns for BiDi operations
 *
 * Provides retry logic with exponential backoff and timeout operations with
 * fallback values. These patterns help build robust automation that can handle
 * transient failures and timing constraints.
 *
 * Architectural patterns:
 * - Retry: Iterative resilience for transient failures
 * - Timeout: Temporal constraints with graceful degradation
 */

#include "bidi/client.hpp"
#include <chrono>

namespace bidi::resilience {

struct RetryPolicy {
    int max_attempts = 3;
    std::chrono::milliseconds base_delay{100};
    static auto exponential(int attempts) -> RetryPolicy {
        RetryPolicy policy;
        policy.max_attempts = attempts;
        return policy;
    }
};

// timeout_or: run operation with timeout, on timeout recover with fallback
template <typename T, typename Fallback>
[[nodiscard]] auto timeout_or(bidi::Task<T> operation,
                              std::chrono::milliseconds timeout,
                              Fallback fallback) -> bidi::Task<T> {
    // Delegate to existing Async composition: timeout + recover
    return operation.timeout(timeout).recover(
        [fallback](auto /*ec*/) { return fallback; });
}

// retry: accept a factory that returns a fresh Task for each attempt
template <typename T, typename Factory>
[[nodiscard]] auto retry(Factory attempt_factory,
                         const RetryPolicy &policy = {}) -> bidi::Task<T> {
    auto ex = attempt_factory().get_executor();

    bidi::Task<T> current = attempt_factory();
    for (int i = 1; i < policy.max_attempts; ++i) {
        current = current.recover(
            [ex, i, policy, attempt_factory](auto /*ec*/) -> bidi::Task<T> {
                auto delay = std::chrono::milliseconds(
                    policy.base_delay.count() * (1 << (i - 1)));
                auto timer_async = bidi::Task<void>::make(ex);
                auto timer = std::make_shared<boost::asio::steady_timer>(ex);
                timer->expires_after(delay);
                timer->async_wait(
                    [timer, timer_async](
                        const boost::system::error_code &error_code) mutable {
                        if (!error_code) {
                            timer_async.fulfill();
                        } else {
                            timer_async.fail(error_code);
                        }
                    });

                return timer_async.and_then(
                    [attempt_factory]() { return attempt_factory(); });
            });
    }

    return current;
}

} // namespace bidi::resilience

// Legacy compatibility alias
namespace bidi {
namespace helpers = resilience;
} // namespace bidi

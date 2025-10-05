#pragma once
/**
 * @file retry_backoff.hpp
 * @brief Exponential backoff retry logic with zero busy-wait.
 *
 * Architectural rationale:
 * - Zero busy-wait: Retry delays use boost::asio::steady_timer for native
 *   kernel suspension (epoll/kqueue/IOCP). No sleep() or polling loops.
 * - Exponential backoff: Configurable multiplier and max delay prevent
 *   overwhelming failing services while allowing fast recovery.
 * - Jitter: Randomized delay prevents thundering herd when multiple clients
 *   retry simultaneously after shared service failure.
 * - Integration with asyncx: Lazy evaluation model - retry chain is built
 *   but not executed until terminal operation (.finally, co_await).
 * - Cooperative cancellation: Respects stop_token for clean shutdown during
 *   retry sequences without busy-wait polling.
 *
 * Performance characteristics:
 * - Native kernel suspension for retry delays (no CPU waste)
 * - Configurable retry policy per operation
 * - Optional retry hooks for logging/metrics
 * - Lazy evaluation enables optimization (chain flattening)
 *
 * Usage pattern:
 * @code
 * RetryPolicy policy{.max_tries = 5, .first_delay = 100ms, .multiplier = 2.0};
 * auto result = co_await retry_with_backoff<Result>(
 *     [&]() { return make_network_request(); },
 *     executor,
 *     policy
 * );
 * @endcode
 *
 * Integration with architecture:
 * - Used by BiDi client for transient network failures
 * - Works with pool-based allocation (minimal overhead per retry)
 * - Respects strand serialization when used within BiDi session
 */

#include "asyncx.hpp"
#include <algorithm>
#include <boost/asio.hpp>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <thread>
#include <type_traits>

namespace bidi {

namespace net = boost::asio;

constexpr int DEFAULT_MAX_TRIES = 3;
constexpr std::chrono::milliseconds DEFAULT_FIRST_DELAY{200};
constexpr double DEFAULT_MULTIPLIER = 2.0;
constexpr std::chrono::milliseconds DEFAULT_MAX_DELAY{3000};
constexpr double DEFAULT_JITTER = 0.2;

struct RetryPolicy {
    int max_tries{DEFAULT_MAX_TRIES};
    std::chrono::milliseconds first_delay{DEFAULT_FIRST_DELAY};
    double multiplier{DEFAULT_MULTIPLIER}; // exponential backoff
    std::chrono::milliseconds max_delay{DEFAULT_MAX_DELAY};
    double jitter{DEFAULT_JITTER}; // ±20%
    std::function<bool(const asyncx::EC &)> is_retryable{
        [](const asyncx::EC &) { return true; }};
    // optional hook invoked before scheduling a retry: (tries, next_delay, ec)
    std::function<void(int, std::chrono::milliseconds, const asyncx::EC &)>
        on_retry;
};

struct RetryState {
    int tries{0};
    std::shared_ptr<std::function<void()>> attempt_holder;
    std::mt19937_64 rng{std::random_device{}()};
    std::chrono::milliseconds delay{};
    std::optional<asyncx::Async<void>> current;
    bool cancelled{false};
};

// Move the definition of calculate_next_delay_with_jitter here
inline auto
calculate_next_delay_with_jitter(const RetryPolicy &policy,
                                 const std::shared_ptr<RetryState> &retry_state)
    -> std::chrono::milliseconds {
    const auto base_ms = static_cast<double>(retry_state->delay.count());
    std::uniform_real_distribution<double> dist{-policy.jitter, +policy.jitter};
    double jitter_factor = std::max(1.0 + dist(retry_state->rng), 0.0);

    auto next_ms = static_cast<long long>(base_ms * jitter_factor);
    next_ms = std::max(next_ms, 1LL);
    if (policy.max_delay.count() > 0) {
        next_ms =
            std::min(next_ms, static_cast<long long>(policy.max_delay.count()));
    }

    double grown = base_ms * policy.multiplier;
    auto grown_ll = static_cast<long long>(grown);
    if (policy.max_delay.count() > 0) {
        grown_ll = std::min(grown_ll,
                            static_cast<long long>(policy.max_delay.count()));
    } else {
        grown_ll = std::min(grown_ll, std::numeric_limits<long long>::max());
    }
    grown_ll = std::max(grown_ll, 1LL);
    retry_state->delay = std::chrono::milliseconds{grown_ll};

    return std::chrono::milliseconds{next_ms};
}

void schedule_retry(const std::shared_ptr<std::function<void()>> &retry_attempt,
                    const boost::asio::any_io_executor &executor,
                    std::chrono::milliseconds delay);

template <class T, class Factory>
auto retry_with_backoff(Factory make_async, net::any_io_executor executor,
                        const RetryPolicy &policy = {}) -> asyncx::Async<T> {
    using asyncx::Async;

    auto out = Async<T>::make(executor);

    auto retry_state = std::make_shared<RetryState>();
    if (policy.first_delay.count() <= 0) {
        retry_state->delay = std::chrono::milliseconds{1};
    } else {
        retry_state->delay = policy.first_delay;
    }

    auto retry_attempt = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak_retry_attempt = retry_attempt;

    if (auto token = out.get_stop_token(); token.stop_possible()) {
        auto weak_out = out.weak();
        auto reg = std::make_shared<std::stop_callback<std::function<void()>>>(
            token, [retry_state, weak_out]() mutable {
                retry_state->cancelled = true;
                try {
                    if (retry_state->current) {
                        retry_state->current->request_stop();
                    }
                } catch (...) { // NOLINT
                }
                try {
                    weak_out.try_fail(make_error_code(
                        boost::system::errc::operation_canceled));
                } catch (...) { // NOLINT
                }
            });

        if constexpr (std::is_void_v<T>) {
            out.finally([reg](std::optional<asyncx::EC> /*error_code*/,
                              const std::exception_ptr & /*exception_ptr*/) {});
        } else {
            out.finally([reg](std::optional<T> /*value*/,
                              std::optional<asyncx::EC> /*error_code*/,
                              const std::exception_ptr & /*exception_ptr*/) {});
        }
    }

    *retry_attempt = [out, executor, policy, retry_state, make_async,
                      weak_retry_attempt]() mutable {
        if (retry_state->cancelled || out.get_stop_token().stop_requested()) {
            out.fail(make_error_code(boost::system::errc::operation_canceled));
            return;
        }

        std::optional<Async<T>> created;
        try {
            created = make_async();
        } catch (...) {
            std::exception_ptr exception_ptr = std::current_exception();
            const bool can_retry = (retry_state->tries + 1 < policy.max_tries);
            if (!can_retry) {
                out.fail(exception_ptr);
                return;
            }
            retry_state->tries += 1;
            if (auto locked_attempt = weak_retry_attempt.lock()) {
                auto timer = std::make_shared<net::steady_timer>(
                    executor, std::chrono::milliseconds{1});
                timer->async_wait(
                    [sleeper_attempt = locked_attempt, timer](
                        const boost::system::error_code &error_code) mutable {
                        if (error_code != net::error::operation_aborted) {
                            try {
                                (*sleeper_attempt)();
                            } catch (const std::exception &e) {
                                std::cerr << "Exception in retry execution: "
                                          << e.what() << '\n';
                            } catch (...) {
                                std::cerr
                                    << "Unknown exception in retry execution\n";
                            }
                        }
                    });
            }
            return;
        }

        if (!created) {
            out.fail(make_error_code(boost::system::errc::operation_canceled));
            return;
        }

        retry_state->current = *created;

        if constexpr (std::is_void_v<T>) {
            created->finally([out, executor, policy, retry_state,
                              weak_retry_attempt](
                                 std::optional<asyncx::EC> error_code,
                                 std::exception_ptr exception_ptr) {
                retry_state->current.reset();
                if (exception_ptr) {
                    out.fail(exception_ptr);
                    return;
                }
                if (!error_code) {
                    out.fulfill();
                    return;
                }

                const bool can_retry =
                    policy.is_retryable(*error_code) &&
                    (retry_state->tries + 1 < policy.max_tries);
                if (!can_retry) {
                    out.fail(*error_code);
                    return;
                }

                retry_state->tries += 1;

                auto next_ms =
                    calculate_next_delay_with_jitter(policy, retry_state);

                if (policy.on_retry) {
                    try {
                        policy.on_retry(
                            retry_state->tries,
                            std::chrono::milliseconds{next_ms.count()},
                            *error_code);
                    } catch (...) { // NOLINT
                    }
                }

                if (retry_state->cancelled ||
                    out.get_stop_token().stop_requested()) {
                    out.fail(make_error_code(
                        boost::system::errc::operation_canceled));
                    return;
                }

                if (auto attempt = weak_retry_attempt.lock()) {
                    auto weak_retry_state =
                        std::weak_ptr<RetryState>(retry_state);

                    auto timer =
                        std::make_shared<net::steady_timer>(executor, next_ms);
                    timer->async_wait([sleeper_attempt = attempt,
                                       weak_retry_state,
                                       timer](const boost::system::error_code
                                                  &errc) mutable {
                        if (errc == net::error::operation_aborted) {
                            return;
                        }

                        if (auto s = weak_retry_state.lock()) {
                            if (s->cancelled) {
                                return;
                            }
                        }
                        try {
                            net::post(timer->get_executor(),
                                      [sleeper_attempt]() mutable {
                                          try {
                                              (*sleeper_attempt)();
                                          } catch (...) {
                                          }
                                      });
                        } catch (const std::exception &e) {
                            std::cerr
                                << "Exception in retry scheduling: " << e.what()
                                << '\n';
                        } catch (...) {
                            std::cerr
                                << "Unknown exception in retry scheduling\n";
                        }
                    });
                }
            });
        } else {
            created->finally([out, executor, policy, retry_state,
                              weak_retry_attempt](
                                 std::optional<T> value,
                                 std::optional<asyncx::EC> error_code,
                                 std::exception_ptr exception_ptr) {
                retry_state->current.reset();
                if (value) {
                    out.fulfill(std::move(*value));
                    return;
                }
                if (exception_ptr) {
                    out.fail(exception_ptr);
                    return;
                }

                const bool can_retry =
                    static_cast<bool>(error_code) &&
                    policy.is_retryable(*error_code) &&
                    (retry_state->tries + 1 < policy.max_tries);
                if (!can_retry) {
                    out.fail(error_code.value_or(asyncx::EC{}));
                    return;
                }

                retry_state->tries += 1;

                auto next_ms =
                    calculate_next_delay_with_jitter(policy, retry_state);

                if (policy.on_retry) {
                    try {
                        policy.on_retry(
                            retry_state->tries,
                            std::chrono::milliseconds{next_ms.count()},
                            *error_code);
                    } catch (...) { // NOLINT
                    }
                }

                if (retry_state->cancelled ||
                    out.get_stop_token().stop_requested()) {
                    out.fail(make_error_code(
                        boost::system::errc::operation_canceled));
                    return;
                }

                if (auto attempt = weak_retry_attempt.lock()) {
                    auto weak_retry_state =
                        std::weak_ptr<RetryState>(retry_state);

                    auto timer = std::make_shared<net::steady_timer>(executor);
                    timer->expires_after(next_ms);
                    timer->async_wait([sleeper_attempt = attempt,
                                       weak_retry_state, executor,
                                       timer](const boost::system::error_code
                                                  &errc) mutable {
                        if (errc == net::error::operation_aborted) {
                            return;
                        }

                        if (auto s = weak_retry_state.lock()) {
                            if (s->cancelled) {
                                return;
                            }
                        }

                        try {
                            net::post(executor, [sleeper_attempt]() mutable {
                                try {
                                    (*sleeper_attempt)();
                                } catch (...) { // NOLINT
                                }
                            });
                        } catch (const std::exception &e) {
                            std::cerr
                                << "Exception in retry scheduling: " << e.what()
                                << '\n';
                        } catch (...) {
                            std::cerr
                                << "Unknown exception in retry scheduling\n";
                        }
                    });
                }
            });
        }
    };

    if constexpr (std::is_void_v<T>) {
        out.finally(
            [retry_state](std::optional<asyncx::EC> /*error_code*/,
                          const std::exception_ptr & /*exception_ptr*/) {
                retry_state->attempt_holder.reset();
            });
    } else {
        out.finally(
            [retry_state](std::optional<T> /*value*/,
                          std::optional<asyncx::EC> /*error_code*/,
                          const std::exception_ptr & /*exception_ptr*/) {
                retry_state->attempt_holder.reset();
            });
    }

    retry_state->attempt_holder = retry_attempt;
    if (auto attempt = weak_retry_attempt.lock()) {
        (*attempt)();
    }

    return out;
}

void schedule_retry(const std::shared_ptr<std::function<void()>> &retry_attempt,
                    const boost::asio::any_io_executor &executor,
                    std::chrono::milliseconds delay) {
    auto timer = std::make_shared<net::steady_timer>(executor, delay);
    timer->async_wait([retry_attempt, timer](
                          const boost::system::error_code &error_code) mutable {
        if (error_code) {
            return;
        }

        try {
            (*retry_attempt)();
        } catch (const std::exception &e) {
            std::cerr << "Exception in retry scheduling: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "Unknown exception in retry scheduling\n";
        }
    });
}
} // namespace bidi

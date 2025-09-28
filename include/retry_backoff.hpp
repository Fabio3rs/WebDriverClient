#pragma once
#include "asyncx.hpp"
#include <boost/asio.hpp>
#include <limits>
#include <random>

namespace bidi {

namespace net = boost::asio;

struct RetryPolicy {
    int max_tries{3};
    std::chrono::milliseconds first_delay{200};
    double multiplier{2.0}; // backoff exponencial
    std::chrono::milliseconds max_delay{3000};
    double jitter{0.2}; // ±20%
    std::function<bool(const asyncx::EC &)> is_retryable{
        [](const asyncx::EC &) { return true; }};
};

template <class T, class Factory>
auto retry_with_backoff(Factory make_async, net::any_io_executor ex,
                        RetryPolicy pol = {}) -> asyncx::Async<T> {
    using asyncx::Async;

    auto out = Async<T>::make(ex);

    struct RetryState {
        std::shared_ptr<net::steady_timer> timer{};
        int tries{0};
        std::mt19937_64 rng{std::random_device{}()};
        std::chrono::milliseconds delay{};
    };

    auto state = std::make_shared<RetryState>();
    {
        if (pol.first_delay.count() <= 0) {
            state->delay = std::chrono::milliseconds{1};
        } else {
            state->delay = pol.first_delay;
        }
    }

    auto attempt = std::make_shared<std::function<void()>>();
    {
        *attempt = [out, ex, pol, state, make_async, attempt]() mutable {
            auto a = make_async(); // Async<T>
            a.finally([out, ex, pol, state, attempt](
                          std::optional<T> v, std::optional<asyncx::EC> ec,
                          std::exception_ptr ep) {
                if (v) {
                    out.fulfill(std::move(*v));
                    return;
                }
                if (ep) {
                    out.fail(ep);
                    return;
                }

                const bool can_retry = static_cast<bool>(ec) &&
                                       pol.is_retryable(*ec) &&
                                       (state->tries + 1 < pol.max_tries);
                if (!can_retry) {
                    out.fail(ec.value_or(asyncx::EC{}));
                    return;
                }

                state->tries += 1;

                // calcula próximo atraso com jitter
                const double base_ms =
                    static_cast<double>(state->delay.count());
                std::uniform_real_distribution<double> dist{-pol.jitter,
                                                            +pol.jitter};
                double jitter_factor = 1.0 + dist(state->rng);
                if (jitter_factor < 0.0) {
                    jitter_factor = 0.0;
                }

                long long next_ms =
                    static_cast<long long>(base_ms * jitter_factor);
                if (next_ms < 1) {
                    next_ms = 1;
                }
                if (pol.max_delay.count() > 0) {
                    next_ms =
                        std::min<long long>(next_ms, pol.max_delay.count());
                }

                // atualiza a janela de backoff (respeitando max_delay)
                {
                    double grown = base_ms * pol.multiplier;
                    long long grown_ll = static_cast<long long>(grown);
                    if (pol.max_delay.count() > 0) {
                        grown_ll = std::min<long long>(grown_ll,
                                                       pol.max_delay.count());
                    } else {
                        grown_ll = std::min<long long>(
                            grown_ll, std::numeric_limits<long long>::max());
                    }
                    if (grown_ll < 1) {
                        grown_ll = 1;
                    }
                    state->delay = std::chrono::milliseconds{grown_ll};
                }

                state->timer = std::make_shared<net::steady_timer>(ex);
                state->timer->expires_after(std::chrono::milliseconds{next_ms});
                state->timer->async_wait(
                    [attempt](const boost::system::error_code & /*ignored*/) {
                        (*attempt)();
                    });
            });
        };
    }

    (*attempt)();
    return out;
}

} // namespace bidi

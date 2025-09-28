#pragma once
#include "ThreadPool.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/system_executor.hpp> // system_executor
#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace asyncx {
namespace net = boost::asio;
using EC = boost::system::error_code;

// ---------- estado compartilhado ----------
template <class T> struct State {
    net::any_io_executor ex;
    std::mutex mx;
    bool done{false};
    std::variant<T, EC, std::exception_ptr> result;
    std::vector<std::function<void()>> conts;
    std::stop_source stop_src;
    explicit State(net::any_io_executor e) : ex(e) {}
};

template <> struct State<void> {
    net::any_io_executor ex;
    std::mutex mx;
    bool done{false};
    std::variant<EC, std::exception_ptr, std::monostate>
        result; // ok = monostate
    std::vector<std::function<void()>> conts;
    std::stop_source stop_src;
    explicit State(net::any_io_executor e) : ex(e) {}
};

// ---------- Async<T> ----------
template <class T = void> class Async {
    std::shared_ptr<State<T>> st_;
    explicit Async(std::shared_ptr<State<T>> st) : st_(std::move(st)) {}

  public:
    using value_type = T;

    Async() = default;

    static Async<T> make(net::any_io_executor ex) {
        return Async<T>(std::make_shared<State<T>>(ex));
    }

    net::any_io_executor get_executor() const { return st_->ex; }

    std::stop_token get_stop_token() const { return st_->stop_src.get_token(); }

    void request_stop() { st_->stop_src.request_stop(); }

    template <class Fn> Async<T> on_error(Fn fn) {
        auto &a = *this;
        auto ex = a.get_executor(); // assuma que você já expõe isso
        auto out = Async<T>::make(ex);

        a.finally([out, fn](std::optional<T> v, std::optional<EC> ec,
                            std::exception_ptr ep) mutable {
            if (v) {
                out.fulfill(std::move(*v));
                return;
            }
            // Tap (efeito colateral); se só houver exceção, passe EC{}.
            try {
                if (ec) {
                    fn(*ec);
                } else {
                    // fn(EC{});
                }
            } catch (...) {
                // nunca deixe exceção escapar do handler
            }
            if (ec) {
                out.fail(*ec);
            } else {
                out.fail(ep);
            }
        });
        return out;
    }

    // --- completar sucesso/erro (agora const) ---
    void fulfill(T v) const {
        std::vector<std::function<void()>> cs;
        assert(st_ != nullptr);
        {
            std::scoped_lock lk(st_->mx);
            if (st_->done) {
                return;
            }
            st_->done = true;
            st_->result = std::move(v);
            cs.swap(st_->conts);
        }
        for (auto &c : cs) {
            net::post(st_->ex, std::move(c));
        }
    }

    void fail(EC ec) const {
        std::vector<std::function<void()>> cs;
        {
            std::scoped_lock lk(st_->mx);
            if (st_->done) {
                return;
            }
            st_->done = true;
            st_->result = ec;
            cs.swap(st_->conts);
        }
        for (auto &c : cs) {
            net::post(st_->ex, std::move(c));
        }
    }

    void fail(std::exception_ptr eptr) const {
        std::vector<std::function<void()>> cs;
        {
            std::scoped_lock lk(st_->mx);
            if (st_->done) {
                return;
            }
            st_->done = true;
            st_->result = eptr;
            cs.swap(st_->conts);
        }
        for (auto &c : cs) {
            net::post(st_->ex, std::move(c));
        }
    }

    // --- map / and_then / recover / finally ---
    template <class F>
    auto map(F f) -> Async<std::invoke_result_t<F, const T &>> {
        using U = std::invoke_result_t<F, const T &>;
        auto next = Async<U>::make(st_->ex);
        auto cont = [st = st_, next, f = std::move(f)]() mutable {
            if (auto p = std::get_if<T>(&st->result)) {
                try {
                    next.fulfill(std::invoke(f, *p));
                } catch (...) {
                    next.fail(std::current_exception());
                }
            } else if (auto ec = std::get_if<EC>(&st->result)) {
                next.fail(*ec);
            } else {
                next.fail(std::get<std::exception_ptr>(st->result));
            }
        };
        attach_or_run(std::move(cont));
        return next;
    }

    template <class F>
    auto and_then(F f) -> decltype(std::invoke(f, std::declval<const T &>())) {
        using R = decltype(std::invoke(f, std::declval<const T &>()));
        using U = typename R::value_type;

        auto next = R::make(st_->ex);
        auto cont = [st = st_, next, f = std::move(f)]() mutable {
            if (auto p = std::get_if<T>(&st->result)) {
                try {
                    auto nxt = std::invoke(f, *p);
                    if constexpr (std::is_void_v<U>) {
                        nxt.finally([next](std::optional<EC> ec,
                                           std::exception_ptr ep) {
                            if (!ec && !ep) {
                                next.fulfill();
                            } else if (ec) {
                                next.fail(*ec);
                            } else {
                                next.fail(ep);
                            }
                        });
                    } else {
                        nxt.finally([next](std::optional<U> v,
                                           std::optional<EC> ec,
                                           std::exception_ptr ep) {
                            if (v) {
                                next.fulfill(*v);
                            } else if (ec) {
                                next.fail(*ec);
                            } else {
                                next.fail(ep);
                            }
                        });
                    }
                } catch (...) {
                    next.fail(std::current_exception());
                }
            } else if (auto ec = std::get_if<EC>(&st->result)) {
                next.fail(*ec);
            } else {
                next.fail(std::get<std::exception_ptr>(st->result));
            }
        };
        attach_or_run(std::move(cont));
        return next;
    }

    template <class F> Async<T> recover(F f) {
        auto next = Async<T>::make(st_->ex);
        auto cont = [st = st_, next, f = std::move(f)]() mutable {
            if (auto p = std::get_if<T>(&st->result)) {
                next.fulfill(std::move(*p));
                return;
            }
            try {
                if (auto ec = std::get_if<EC>(&st->result)) {
                    next.fulfill(std::invoke(f, *ec));
                } else {
                    next.fulfill(std::invoke(f, EC{}));
                }
            } catch (...) {
                next.fail(std::current_exception());
            }
        };
        attach_or_run(std::move(cont));
        return next;
    }

    template <class F> void finally(F f) const {
        auto cont = [st = st_, f = std::move(f)]() mutable {
            std::optional<T> v;
            std::optional<EC> ec;
            std::exception_ptr ep;

            if (auto pv = std::get_if<T>(&st->result)) {
                v = *pv;
            } else if (auto pe = std::get_if<EC>(&st->result)) {
                ec = *pe;
            } else {
                ep = std::get<std::exception_ptr>(st->result);
            }

            std::invoke(f, std::move(v), std::move(ec), ep);
        };
        attach_or_run(std::move(cont));
    }

    // awaiter opcional
    auto operator co_await() const {
        struct awaiter {
            std::shared_ptr<State<T>> st;
            bool await_ready() const noexcept { return st->done; }
            void await_suspend(std::coroutine_handle<> h) {
                std::scoped_lock lk(st->mx);
                st->conts.push_back([h] { h.resume(); });
            }
            T await_resume() {
                if (auto p = std::get_if<T>(&st->result)) {
                    return std::move(*p);
                }
                if (auto ec = std::get_if<EC>(&st->result)) {
                    throw boost::system::system_error(*ec);
                }
                std::rethrow_exception(
                    std::get<std::exception_ptr>(st->result));
            }
        };
        return awaiter{st_};
    }

  private:
    void attach_or_run(std::function<void()> c) const {
        bool run_now = false;
        {
            std::scoped_lock lk(st_->mx);
            if (!st_->done) {
                st_->conts.push_back(std::move(c));
                run_now = false;
            } else {
                run_now = true;
            }
        }
        if (run_now) {
            net::post(st_->ex, std::move(c));
        }
    }

  public:
    // ---------- fábricas ----------
    template <class U>
    static Async<U> from_future(net::any_io_executor ex, std::future<U> fut,
                                boost::asio::thread_pool *pool = nullptr) {
        auto a = Async<U>::make(ex);
        auto &target_pool = pool ? *pool : webdriver::global_thread_pool();
        boost::asio::post(target_pool, [a, f = std::move(fut)]() mutable {
            try {
                auto v = f.get();
                a.fulfill(std::move(v));
            } catch (const boost::system::system_error &se) {
                a.fail(se.code());
            } catch (...) {
                a.fail(std::current_exception());
            }
        });
        return a;
    }

    template <class Initiator>
    static Async<T> from_callback(net::any_io_executor ex, Initiator init) {
        auto a = Async<T>::make(ex);
        init(
            [a](EC ec, T v) {
                if (ec) {
                    a.fail(ec);
                } else {
                    a.fulfill(std::move(v));
                }
            },
            a.get_stop_token());
        return a;
    }
};

// ---------- Async<void> ----------
template <> class Async<void> {
    std::shared_ptr<State<void>> st_;
    explicit Async(std::shared_ptr<State<void>> st) : st_(std::move(st)) {}

  public:
    using value_type = void;

    static Async<void> make(net::any_io_executor ex) {
        return Async<void>(std::make_shared<State<void>>(ex));
    }

    net::any_io_executor get_executor() const { return st_->ex; }

    std::stop_token get_stop_token() const { return st_->stop_src.get_token(); }

    void request_stop() { st_->stop_src.request_stop(); }

    void fulfill() const {
        std::vector<std::function<void()>> cs;
        {
            std::scoped_lock lk(st_->mx);
            if (st_->done) {
                return;
            }
            st_->done = true;
            st_->result = std::monostate{};
            cs.swap(st_->conts);
        }
        for (auto &c : cs) {
            net::post(st_->ex, std::move(c));
        }
    }

    void fail(EC ec) const {
        std::vector<std::function<void()>> cs;
        {
            std::scoped_lock lk(st_->mx);
            if (st_->done) {
                return;
            }
            st_->done = true;
            st_->result = ec;
            cs.swap(st_->conts);
        }
        for (auto &c : cs) {
            net::post(st_->ex, std::move(c));
        }
    }

    void fail(std::exception_ptr ep) const {
        std::vector<std::function<void()>> cs;
        {
            std::scoped_lock lk(st_->mx);
            if (st_->done) {
                return;
            }
            st_->done = true;
            st_->result = ep;
            cs.swap(st_->conts);
        }
        for (auto &c : cs) {
            net::post(st_->ex, std::move(c));
        }
    }

    template <class F> auto map(F f) -> Async<std::invoke_result_t<F>> {
        using U = std::invoke_result_t<F>;
        auto next = Async<U>::make(st_->ex);
        auto cont = [st = st_, next, f = std::move(f)]() mutable {
            if (std::holds_alternative<std::monostate>(st->result)) {
                try {
                    next.fulfill(std::invoke(f));
                } catch (...) {
                    next.fail(std::current_exception());
                }
            } else if (auto ec = std::get_if<EC>(&st->result)) {
                next.fail(*ec);
            } else {
                next.fail(std::get<std::exception_ptr>(st->result));
            }
        };
        attach_or_run(std::move(cont));
        return next;
    }

    template <class F> auto and_then(F f) -> decltype(std::invoke(f)) {
        using R = decltype(std::invoke(f));
        using U = typename R::value_type;

        auto next = R::make(st_->ex);
        auto cont = [st = st_, next, f = std::move(f)]() mutable {
            if (std::holds_alternative<std::monostate>(st->result)) {
                try {
                    auto nxt = std::invoke(f);
                    if constexpr (std::is_void_v<U>) {
                        nxt.finally([next](std::optional<EC> ec,
                                           std::exception_ptr ep) {
                            if (!ec && !ep) {
                                next.fulfill();
                            } else if (ec) {
                                next.fail(*ec);
                            } else {
                                next.fail(ep);
                            }
                        });
                    } else {
                        nxt.finally([next](std::optional<U> v,
                                           std::optional<EC> ec,
                                           std::exception_ptr ep) {
                            if (v) {
                                next.fulfill(*v);
                            } else if (ec) {
                                next.fail(*ec);
                            } else {
                                next.fail(ep);
                            }
                        });
                    }
                } catch (...) {
                    next.fail(std::current_exception());
                }
            } else if (auto ec = std::get_if<EC>(&st->result)) {
                next.fail(*ec);
            } else {
                next.fail(std::get<std::exception_ptr>(st->result));
            }
        };
        attach_or_run(std::move(cont));
        return next;
    }

    template <class F> Async<void> recover(F f) {
        auto next = Async<void>::make(st_->ex);
        auto cont = [st = st_, next, f = std::move(f)]() mutable {
            if (std::holds_alternative<std::monostate>(st->result)) {
                next.fulfill();
                return;
            }
            try {
                (void)std::invoke(f);
                next.fulfill();
            } catch (...) {
                next.fail(std::current_exception());
            }
        };
        attach_or_run(std::move(cont));
        return next;
    }

    template <class F> void finally(F f) const {
        auto cont = [st = st_, f = std::move(f)]() mutable {
            std::optional<EC> ec;
            std::exception_ptr ep;

            if (auto pe = std::get_if<EC>(&st->result)) {
                ec = *pe;
            } else if (!std::holds_alternative<std::monostate>(st->result)) {
                ep = std::get<std::exception_ptr>(st->result);
            }
            std::invoke(f, std::move(ec), ep);
        };
        attach_or_run(std::move(cont));
    }

    auto operator co_await() const {
        struct awaiter {
            std::shared_ptr<State<void>> st;
            bool await_ready() const noexcept { return st->done; }
            void await_suspend(std::coroutine_handle<> h) {
                std::scoped_lock lk(st->mx);
                st->conts.push_back([h] { h.resume(); });
            }
            void await_resume() {
                if (auto ec = std::get_if<EC>(&st->result)) {
                    throw boost::system::system_error(*ec);
                }
                if (!std::holds_alternative<std::monostate>(st->result) &&
                    !std::holds_alternative<EC>(st->result)) {
                    std::rethrow_exception(
                        std::get<std::exception_ptr>(st->result));
                }
            }
        };
        return awaiter{st_};
    }

  private:
    void attach_or_run(std::function<void()> c) const {
        bool run_now = false;
        {
            std::scoped_lock lk(st_->mx);
            if (!st_->done) {
                st_->conts.push_back(std::move(c));
                run_now = false;
            } else {
                run_now = true;
            }
        }
        if (run_now) {
            net::post(st_->ex, std::move(c));
        }
    }

  public:
    template <class Initiator>
    static Async<void> from_callback(net::any_io_executor ex, Initiator init) {
        auto a = Async<void>::make(ex);
        init(
            [a](EC ec) {
                if (ec) {
                    a.fail(ec);
                } else {
                    a.fulfill();
                }
            },
            a.get_stop_token());
        return a;
    }
};

// ---------- combinadores livres: all / race / timeout ----------
template <class T>
Async<std::vector<T>> all(net::any_io_executor ex, std::vector<Async<T>> vs) {
    auto out = Async<std::vector<T>>::make(ex);
    auto res = std::make_shared<std::vector<std::optional<T>>>(vs.size());
    auto left = std::make_shared<std::atomic<int>>(static_cast<int>(vs.size()));

    if (vs.empty()) {
        out.fulfill({});
        return out;
    }

    for (std::size_t i = 0; i < vs.size(); ++i) {
        vs[i].finally([out, res, left, i](std::optional<T> v,
                                          std::optional<EC> ec,
                                          std::exception_ptr ep) {
            if (ec || ep) {
                out.fail(ec ? *ec : EC{});
                return;
            }
            (*res)[i] = std::move(*v);
            if (--(*left) == 0) {
                std::vector<T> vals;
                vals.reserve(res->size());
                for (auto &o : *res) {
                    vals.push_back(std::move(*o));
                }
                out.fulfill(std::move(vals));
            }
        });
    }

    return out;
}

template <class T>
Async<T> race(net::any_io_executor ex, std::vector<Async<T>> vs) {
    auto out = Async<T>::make(ex);
    auto done = std::make_shared<std::atomic_bool>(false);

    for (auto &a : vs) {
        a.finally([out, done](std::optional<T> v, std::optional<EC> ec,
                              std::exception_ptr ep) {
            if (done->exchange(true)) {
                return;
            }
            if (v) {
                out.fulfill(std::move(*v));
            } else if (ec) {
                out.fail(*ec);
            } else {
                out.fail(ep);
            }
        });
    }

    return out;
}

template <class T, class Rep, class Per>
Async<T> timeout(Async<T> a, net::any_io_executor ex,
                 std::chrono::duration<Rep, Per> d) {
    auto out = Async<T>::make(ex);
    auto done = std::make_shared<std::atomic_bool>(false);
    auto timer = std::make_shared<net::steady_timer>(ex);

    timer->expires_after(d);

    // Quando o timer dispara primeiro -> falha por timeout
    timer->async_wait([out, done](EC ec) {
        if (done->exchange(true)) {
            return;
        }
        if (!ec) {
            out.fail(make_error_code(boost::system::errc::timed_out));
        } else {
            // se foi cancelado por quem ganhou a corrida, ignoramos,
            // mas se for outro erro do timer, propaga
            if (ec != boost::system::errc::make_error_code(
                          boost::system::errc::operation_canceled)) {
                out.fail(ec);
            }
        }
    });

    // Quando 'a' completa primeiro -> cancela o timer e propaga o resultado
    a.finally([out, done, timer](std::optional<T> v, std::optional<EC> ec,
                                 std::exception_ptr ep) {
        if (done->exchange(true)) {
            return;
        }
        timer->cancel();
        if (v) {
            out.fulfill(std::move(*v));
        } else if (ec) {
            out.fail(*ec);
        } else {
            out.fail(ep);
        }
    });

    return out;
}

inline Async<void> value_on(boost::asio::any_io_executor ex) {
    auto a = Async<void>::make(ex);
    a.fulfill();
    return a;
}

inline Async<void> value() {
    // executa no system_executor; é imediato e não bloqueia o seu io_context
    auto a = Async<void>::make(boost::asio::system_executor{});
    a.fulfill();
    return a;
}

} // namespace asyncx

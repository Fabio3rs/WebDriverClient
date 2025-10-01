#pragma once
#include "ThreadPool.hpp"
#include "bidi/logging.hpp"
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
    explicit State(net::any_io_executor e) : ex(std::move(e)) {}
};

template <> struct State<void> {
    net::any_io_executor ex;
    std::mutex mx;
    bool done{false};
    std::variant<EC, std::exception_ptr, std::monostate>
        result; // ok = monostate
    std::vector<std::function<void()>> conts;
    std::stop_source stop_src;
    explicit State(net::any_io_executor e) : ex(std::move(e)) {}
};

// awaiter genérico para Async<T> (definido em namespace para permitir
// member templates como await_suspend). Possui especialização para void.
template <class T> struct async_awaiter {
    std::shared_ptr<State<T>> st;
    bool await_ready() const noexcept { return st->done; }
    template <class Promise>
    void await_suspend(std::coroutine_handle<Promise> h) {
        std::scoped_lock lk(st->mx);
        st->conts.push_back([h]() mutable { h.resume(); });
    }
    T await_resume() {
        if (auto p = std::get_if<T>(&st->result)) {
            return std::move(*p);
        }
        if (auto ec = std::get_if<EC>(&st->result)) {
            throw boost::system::system_error(*ec);
        }
        std::rethrow_exception(std::get<std::exception_ptr>(st->result));
    }
};

template <> struct async_awaiter<void> {
    std::shared_ptr<State<void>> st;
    bool await_ready() const noexcept { return st->done; }
    template <class Promise>
    void await_suspend(std::coroutine_handle<Promise> h) {
        std::scoped_lock lk(st->mx);
        st->conts.push_back([h]() mutable { h.resume(); });
    }
    void await_resume() {
        if (auto *ec = std::get_if<EC>(&st->result)) {
            throw boost::system::system_error(*ec);
        }
        if (!std::holds_alternative<std::monostate>(st->result) &&
            !std::holds_alternative<EC>(st->result)) {
            std::rethrow_exception(std::get<std::exception_ptr>(st->result));
        }
    }
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

    using type = T;

    // Conversão implícita para boost::asio::awaitable<void>
    operator net::awaitable<void>() const {
        auto self = *this;
        return [self]() -> net::awaitable<void> {
            co_await self;
            co_return;
        }();
    }

    // Conversão implícita para boost::asio::awaitable<T>
    operator net::awaitable<T>() const {
        auto self = *this;
        return [self]() -> net::awaitable<T> { co_return co_await self; }();
    }

    void cancel() { st_->stop_src.request_stop(); }

    // ============================
    // 1) Versão geral: aceita qualquer CompletionToken
    // ============================
    template <class CompletionToken> auto operator()(CompletionToken &&token) {
        // Assinatura da operação conforme T:
        //   - Se T != void: void(error_code, T)
        //   - Se T == void: void(error_code)
        using signature_t =
            std::conditional_t<std::is_void_v<T>,
                               void(boost::system::error_code),
                               void(boost::system::error_code, T)>;

        auto initiation = [this]<class Handler>(Handler &&handler) mutable {
            using H = std::decay_t<Handler>;

            // Preserva executor/allocator/cancel-slot associados ao handler
            auto ex = boost::asio::get_associated_executor(
                handler, boost::asio::system_executor());
            auto alloc = boost::asio::get_associated_allocator(handler);
            auto slot = boost::asio::get_associated_cancellation_slot(handler);

            // Move-only handler? Torna-o copiável para capturar em
            // lambdas/callbacks
            auto sp = std::make_shared<H>(std::forward<Handler>(handler));

            if (slot.is_connected()) {
                // Propaga cancelamento do chamador para tua Async
                slot.assign(
                    [this](boost::asio::cancellation_type) { this->cancel(); });
            }

            // Quando tua Async concluir, dispare o handler no executor
            // associado
            auto finallyCb = [sp, ex, alloc](
                                 std::optional<T> v,
                                 std::optional<boost::system::error_code> ec,
                                 const std::exception_ptr &) mutable {
                // Chama via dispatch e com allocator associado
                auto completionCallback = [sp, v = std::move(v), ec]() mutable {
                    if constexpr (std::is_void_v<T>) {
                        // T == void: apenas error_code
                        (*sp)(ec.value_or(boost::system::error_code{}));
                    } else {
                        if (v && !ec) {
                            (*sp)(boost::system::error_code{}, std::move(*v));
                        } else if (ec) {
                            (*sp)(*ec,
                                  T{}); // ajuste se tiver "valor nulo" melhor
                        } else {
                            // Sem valor e sem ec => considere como
                            // cancelado
                            (*sp)(boost::asio::error::operation_aborted, T{});
                        }
                    }
                };
                boost::asio::dispatch(
                    ex, boost::asio::bind_allocator(alloc, completionCallback));
            };
            this->finally(finallyCb);
        };

        // Converte token -> handler e retorna o tipo certo (awaitable, future,
        // void…)
        return boost::asio::async_initiate<CompletionToken, signature_t>(
            initiation, std::forward<CompletionToken>(token));
    }

    // ============================
    // 2) Versão "default": sem args, retorna awaitable<T>
    // ============================
    boost::asio::awaitable<T> operator()() {
        if constexpr (std::is_void_v<T>) {
            // para T == void, forneço um exemplo que retorna void
            co_await (*this)(boost::asio::use_awaitable);
            co_return;
        } else {
            co_return co_await (*this)(boost::asio::use_awaitable);
        }
    }

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

    // awaiter opcional (usa async_awaiter definido no namespace)
    auto operator co_await() const { return async_awaiter<T>{st_}; }
    auto operator co_await() { return async_awaiter<T>{st_}; }

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
            // If the state is already done, run the continuation inline to
            // ensure composed combinators observe completion immediately
            // and can cancel other operations deterministically.
            std::invoke(std::move(c));
        }
    }

  public:
    // ---------- fábricas ----------
    template <class U>
    static Async<U>
    from_future(net::any_io_executor ex, std::future<U> fut,
                std::shared_ptr<boost::asio::thread_pool> pool = nullptr) {
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

    // Adapt Async<void> into a boost::asio::awaitable<void>
    auto operator()() -> boost::asio::awaitable<void> {
        co_await boost::asio::async_initiate<
            decltype(boost::asio::use_awaitable),
            void(boost::system::error_code)>(
            [this](auto &&handler) mutable {
                using handler_t = std::decay_t<decltype(handler)>;
                auto sp = std::make_shared<handler_t>(
                    std::forward<decltype(handler)>(handler));
                this->finally([sp](std::optional<asyncx::EC> ec,
                                   const std::exception_ptr & /*ep*/) mutable {
                    if (!ec) {
                        (*sp)(boost::system::error_code{});
                    } else {
                        (*sp)(*ec);
                    }
                });
            },
            boost::asio::use_awaitable);
        co_return;
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

    auto operator co_await() const { return async_awaiter<void>{st_}; }

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
    auto ops = std::make_shared<std::vector<Async<T>>>(std::move(vs));
    auto res = std::make_shared<std::vector<std::optional<T>>>(ops->size());
    auto left = std::make_shared<std::atomic<int>>(static_cast<int>(ops->size()));

    if (ops->empty()) {
        out.fulfill({});
        return out;
    }

    // Propagate stop request on the composed Async to all children to make
    // the combinator cancel-safe. Keep the registration alive until `out`
    // completes by capturing the registration in an `out.finally` closure.
    if (ops->size() > 0) {
        auto token = out.get_stop_token();
        if (token.stop_possible()) {
            // allocate registration on heap so we can keep it alive across
            // asynchronous callbacks; the registration will be destroyed when
            // `out` completes.
            auto reg = std::make_shared<std::stop_callback<std::function<void()>>>(
                token, [ops]() {
                    for (auto &o : *ops) {
                        try {
                            o.request_stop();
                        } catch (...) {
                        }
                    }
                });
            // hold reg until out completes
            out.finally([reg](std::optional<std::vector<T>> /*v*/, std::optional<EC> /*ec*/, std::exception_ptr /*ep*/) {
                // reg goes out of scope and is destroyed here
            });
        }
    }

    for (std::size_t i = 0; i < ops->size(); ++i) {
        (*ops)[i].finally([out, res, left, ops, i](std::optional<T> v,
                                                  std::optional<EC> ec,
                                                  std::exception_ptr ep) {
            if (ec || ep) {
                // cancel remaining operations to avoid zombi work
                for (auto &o : *ops) {
                    try {
                        o.request_stop();
                    } catch (...) {
                    }
                }
                    std::fprintf(stderr, "DEBUG: all: child %zu failed, calling out.fail\n", i);
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
                    std::fprintf(stderr, "DEBUG: all: all children completed, fulfilling with %zu values\n", vals.size());
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
    auto ops = std::make_shared<std::vector<Async<T>>>(std::move(vs));

    // Propagate cancellation from composed Async to children
    if (ops->size() > 0) {
        auto token = out.get_stop_token();
        if (token.stop_possible()) {
            auto reg = std::make_shared<std::stop_callback<std::function<void()>>>(
                token, [ops]() {
                    for (auto &o : *ops) {
                        try {
                            o.request_stop();
                        } catch (...) {
                        }
                    }
                });
            out.finally([reg](std::optional<T> /*v*/, std::optional<EC> /*ec*/, std::exception_ptr /*ep*/) {
                // release registration
            });
        }
    }

    for (auto &a : *ops) {
        a.finally([out, done, ops](std::optional<T> v, std::optional<EC> ec,
                                  std::exception_ptr ep) {
            if (done->exchange(true)) {
                return;
            }
            // cancel all other operations to avoid extra work
            for (auto &o : *ops) {
                try {
                    o.request_stop();
                } catch (...) {
                }
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
    // Capture 'a' by value so we can request_stop() on it.
    // propagate cancellation from composed Async to the child and timer
    {
        auto token = out.get_stop_token();
        if (token.stop_possible()) {
            auto reg = std::make_shared<std::stop_callback<std::function<void()>>>(
                token, [a, timer]() mutable {
                    try {
                        a.request_stop();
                    } catch (...) {
                    }
                    try {
                        timer->cancel();
                    } catch (...) {
                    }
                });
            out.finally([reg](std::optional<T> /*v*/, std::optional<EC> /*ec*/, std::exception_ptr /*ep*/) {
                // release registration when out completes
            });
        }
    }

    timer->async_wait([out, done, a](EC ec) mutable {
        if (done->exchange(true)) {
            return;
        }
        if (!ec) {
            // Ask the operation to stop to avoid any residual work
            try {
                a.request_stop();
            } catch (...) {
            }
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

// NOTE: Avoid injecting into Boost.Asio internal namespaces (e.g.
// boost::asio::detail). That is undefined behaviour and fragile across
// Boost versions. Provide a public adapter instead so callers can write
// `co_await asyncx::as_awaitable(a)` when explicit conversion is needed.

namespace asyncx {
template <class T>
inline net::awaitable<T> as_awaitable(Async<T> val) {
    // Capture by value to extend the shared state lifetime while awaited.
    return std::move(val)(boost::asio::use_awaitable);
}
} // namespace asyncx

#pragma once
#include "ThreadPool.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/system_executor.hpp> // system_executor
#include <chrono>
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#if __has_include(<boost/outcome.hpp>)
#include <boost/outcome.hpp>
#endif

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
    [[nodiscard]] auto await_ready() const noexcept -> bool { return st->done; }
    template <class Promise>
    void await_suspend(std::coroutine_handle<Promise> h) {
        std::scoped_lock lk(st->mx);
        st->conts.push_back([h]() mutable { h.resume(); });
    }
    auto await_resume() -> T {
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
    [[nodiscard]] auto await_ready() const noexcept -> bool { return st->done; }
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

// ---------- Helper functions for CompletionToken handling ----------
namespace {

// Dispatches handler for void result type
template <class Handler>
void dispatch_void_completion(std::shared_ptr<Handler> sp,
                              std::optional<boost::system::error_code> ec) {
    (*sp)(ec.value_or(boost::system::error_code{}));
}

// Dispatches handler for non-void result type with success
template <class T, class Handler>
void dispatch_value_completion(std::shared_ptr<Handler> sp, std::optional<T> v,
                               std::optional<boost::system::error_code> ec) {

    // Early return: Success case
    if (v && !ec) {
        (*sp)(boost::system::error_code{}, std::move(*v));
        return;
    }

    // Early return: Error case
    if (ec) {
        (*sp)(*ec, T{});
        return;
    }

    // Default: Cancelled
    (*sp)(boost::asio::error::operation_aborted, T{});
}

// Creates completion callback for void result
template <class Handler>
auto make_void_completion_callback(
    std::shared_ptr<Handler> sp, std::optional<boost::system::error_code> ec) {
    return [sp, ec]() mutable { dispatch_void_completion(sp, ec); };
}

// Creates completion callback for non-void result
template <class T, class Handler>
auto make_value_completion_callback(
    std::shared_ptr<Handler> sp, std::optional<T> v,
    std::optional<boost::system::error_code> ec) {
    return [sp, v = std::move(v), ec]() mutable {
        dispatch_value_completion<T>(sp, std::move(v), ec);
    };
}

} // anonymous namespace

// ---------- Async<T> ----------
template <class T = void> class Async {
    std::shared_ptr<State<T>> st_;
    explicit Async(std::shared_ptr<State<T>> st) : st_(std::move(st)) {}

  public:
    using value_type = T;

    Async() = default;

    static auto make(net::any_io_executor ex) -> Async<T> {
        return Async<T>(std::make_shared<State<T>>(ex));
    }

    [[nodiscard]] auto get_executor() const -> net::any_io_executor {
        return st_->ex;
    }

    [[nodiscard]] auto get_stop_token() const -> std::stop_token {
        return st_->stop_src.get_token();
    }

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
    // CompletionToken support (refactored for reduced complexity)
    // ============================
    template <class CompletionToken> auto operator()(CompletionToken &&token) {
        // Signature depends on T: void(error_code) or void(error_code, T)
        using signature_t =
            std::conditional_t<std::is_void_v<T>,
                               void(boost::system::error_code),
                               void(boost::system::error_code, T)>;

        auto initiation = [self = *this]<class Handler>(Handler &&handler) {
            using handler_t = std::decay_t<Handler>;

            // Extract associated objects from handler
            auto executor = boost::asio::get_associated_executor(
                handler, boost::asio::system_executor());
            auto allocator = boost::asio::get_associated_allocator(handler);
            auto cancel_slot =
                boost::asio::get_associated_cancellation_slot(handler);

            // Wrap handler in shared_ptr for lambda capture
            auto handler_ptr =
                std::make_shared<handler_t>(std::forward<Handler>(handler));

            // Setup cancellation propagation
            self.setup_cancellation_propagation(cancel_slot);

            // Attach appropriate completion handler based on T
            if constexpr (std::is_void_v<T>) {
                self.attach_void_completion_handler(handler_ptr, executor,
                                                    allocator);
            } else {
                self.attach_value_completion_handler(handler_ptr, executor,
                                                     allocator);
            }
        };

        return boost::asio::async_initiate<CompletionToken, signature_t>(
            initiation, std::forward<CompletionToken>(token));
    }

    // ============================
    // Default: returns awaitable<T>
    // ============================
    auto operator()() -> boost::asio::awaitable<T> {
        if constexpr (std::is_void_v<T>) {
            // para T == void, forneço um exemplo que retorna void
            co_await (*this)(boost::asio::use_awaitable);
            co_return;
        } else {
            co_return co_await (*this)(boost::asio::use_awaitable);
        }
    }

    template <class Fn> auto on_error(Fn fn) -> Async<T> {
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
        try {
            attach_or_run(std::move(cont));
        } catch (...) {
            // EXCEPTION SAFETY FIX: If attach_or_run throws (e.g., bad_alloc),
            // fail next to prevent hang and notify caller of allocation
            // failure.
            next.fail(std::current_exception());
        }
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
        try {
            attach_or_run(std::move(cont));
        } catch (...) {
            // EXCEPTION SAFETY FIX: If attach_or_run throws (e.g., bad_alloc),
            // fail next to prevent hang and notify caller of allocation
            // failure.
            next.fail(std::current_exception());
        }
        return next;
    }

    template <class F> auto recover(F f) -> Async<T> {
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
        try {
            attach_or_run(std::move(cont));
        } catch (...) {
            // EXCEPTION SAFETY FIX: If attach_or_run throws (e.g., bad_alloc),
            // fail next to prevent hang and notify caller of allocation
            // failure.
            next.fail(std::current_exception());
        }
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
    // Lightweight non-owning handle to the shared state. Use this in
    // callbacks (e.g. stop callbacks) to avoid creating ownership cycles.
    struct Weak {
        std::weak_ptr<State<T>> wst;
        explicit Weak(std::weak_ptr<State<T>> w) : wst(std::move(w)) {}
        void try_request_stop() const noexcept {
            if (auto s = wst.lock()) {
                s->stop_src.request_stop();
            }
        }
        void try_fail(const EC &ec) const noexcept {
            if (auto s = wst.lock()) {
                std::vector<std::function<void()>> cs;
                {
                    std::scoped_lock lk(s->mx);
                    if (s->done) {
                        return;
                    }
                    s->done = true;
                    s->result = ec;
                    cs.swap(s->conts);
                }
                for (auto &c : cs) {
                    net::post(s->ex, std::move(c));
                }
            }
        }

        // remove todas as continuations sem completar (usado por timeout)
        void try_abandon() const noexcept {
            if (auto s = wst.lock()) {
                std::scoped_lock lk(s->mx);
                s->conts.clear();
            }
        }

        // try_fail overload for exception_ptr (non-void T)
        void try_fail(std::exception_ptr ep) const noexcept {
            if (auto s = wst.lock()) {
                std::vector<std::function<void()>> cs;
                {
                    std::scoped_lock lk(s->mx);
                    if (s->done) {
                        return;
                    }
                    s->done = true;
                    s->result = ep;
                    cs.swap(s->conts);
                }
                for (auto &c : cs) {
                    net::post(s->ex, std::move(c));
                }
            }
        }

        // try_fulfill for non-void T
        template <typename U = T>
        std::enable_if_t<!std::is_void_v<U>, void>
        try_fulfill(const T &v) const noexcept {
            if (auto s = wst.lock()) {
                std::vector<std::function<void()>> cs;
                {
                    std::scoped_lock lk(s->mx);
                    if (s->done) {
                        return;
                    }
                    s->done = true;
                    s->result = v;
                    cs.swap(s->conts);
                }
                for (auto &c : cs) {
                    net::post(s->ex, std::move(c));
                }
            }
        }

        // rvalue overload to avoid copy when possible
        template <typename U = T>
        std::enable_if_t<!std::is_void_v<U>, void>
        try_fulfill(T &&v) const noexcept {
            if (auto s = wst.lock()) {
                std::vector<std::function<void()>> cs;
                {
                    std::scoped_lock lk(s->mx);
                    if (s->done) {
                        return;
                    }
                    s->done = true;
                    s->result = std::move(v);
                    cs.swap(s->conts);
                }
                for (auto &c : cs) {
                    net::post(s->ex, std::move(c));
                }
            }
        }

        // try_fulfill for void
        template <typename U = T>
        std::enable_if_t<std::is_void_v<U>, void> try_fulfill() const noexcept {
            if (auto s = wst.lock()) {
                std::vector<std::function<void()>> cs;
                {
                    std::scoped_lock lk(s->mx);
                    if (s->done) {
                        return;
                    }
                    s->done = true;
                    s->result = std::monostate{};
                    cs.swap(s->conts);
                }
                for (auto &c : cs) {
                    net::post(s->ex, std::move(c));
                }
            }
        }
    };

    auto weak() const noexcept -> Weak {
        return Weak{std::weak_ptr<State<T>>(st_)};
    }

    void release() noexcept { st_ = {}; }

  private:
    // ========== Private helpers for operator()(CompletionToken) ==========

    // Setup cancellation propagation from handler's slot to this Async
    template <class CancellationSlot>
    void setup_cancellation_propagation(CancellationSlot &slot) const {
        if (!slot.is_connected()) {
            return;
        }

        slot.assign([weak = this->weak()](boost::asio::cancellation_type) {
            weak.try_request_stop();
        });
    }

    // Create and attach the finally callback for void result type
    template <class Handler>
    void attach_void_completion_handler(
        std::shared_ptr<Handler> handler_ptr, net::any_io_executor executor,
        boost::asio::associated_allocator_t<Handler> allocator) const {

        auto finally_cb = [sp = handler_ptr, ex = executor, alloc = allocator](
                              std::optional<boost::system::error_code> ec,
                              const std::exception_ptr &) mutable {
            auto completion = make_void_completion_callback(sp, ec);
            boost::asio::dispatch(
                ex, boost::asio::bind_allocator(alloc, std::move(completion)));
        };

        this->finally(std::move(finally_cb));
    }

    // Create and attach the finally callback for non-void result type
    template <class Handler>
    void attach_value_completion_handler(
        std::shared_ptr<Handler> handler_ptr, net::any_io_executor executor,
        boost::asio::associated_allocator_t<Handler> allocator) const {

        auto finally_cb =
            [sp = handler_ptr, ex = executor, alloc = allocator](
                std::optional<T> value,
                std::optional<boost::system::error_code> error_code,
                const std::exception_ptr &) mutable {
                auto completion = make_value_completion_callback<T>(
                    sp, std::move(value), error_code);
                boost::asio::dispatch(ex, boost::asio::bind_allocator(
                                              alloc, std::move(completion)));
            };

        this->finally(std::move(finally_cb));
    }

    void attach_or_run(std::function<void()> c) const {
        std::optional<std::function<void()>> run_inline;
        {
            std::scoped_lock lk(st_->mx);
            if (!st_->done) {
                st_->conts.push_back(std::move(c));
            } else {
                // CRITICAL FIX: Move continuation INSIDE the lock to prevent
                // race where fulfill() could capture it from vector while we
                // try to invoke it, causing double-execution.
                run_inline = std::move(c);
            }
        }
        if (run_inline) {
            // If the state is already done, run the continuation inline to
            // ensure composed combinators observe completion immediately
            // and can cancel other operations deterministically.
            std::invoke(std::move(*run_inline));
        }
    }

  public:
    // ---------- fábricas ----------
    template <class U>
    static auto
    from_future(net::any_io_executor ex, std::future<U> fut,
                const std::shared_ptr<boost::asio::thread_pool> &pool = nullptr)
        -> Async<U> {
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
    static auto from_callback(net::any_io_executor ex,
                              Initiator init) -> Async<T> {
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

    static auto make(const net::any_io_executor &ex) -> Async<void> {
        return Async<void>(std::make_shared<State<void>>(ex));
    }

    [[nodiscard]] auto get_executor() const -> net::any_io_executor {
        return st_->ex;
    }

    [[nodiscard]] auto get_stop_token() const -> std::stop_token {
        return st_->stop_src.get_token();
    }

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

    void fail(const std::exception_ptr &ep) const {
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

    // Lightweight non-owning handle to the shared state for void
    // specialization.
    struct Weak {
        std::weak_ptr<State<void>> wst;
        explicit Weak(std::weak_ptr<State<void>> w) : wst(std::move(w)) {}
        void try_request_stop() const noexcept {
            if (auto s = wst.lock()) {
                s->stop_src.request_stop();
            }
        }
        void try_fail(const EC &ec) const noexcept {
            try {
                if (auto s = wst.lock()) {
                    std::vector<std::function<void()>> cs;
                    {
                        std::scoped_lock lk(s->mx);
                        if (s->done) {
                            return;
                        }
                        s->done = true;
                        s->result = ec;
                        cs.swap(s->conts);
                    }
                    for (auto &c : cs) {
                        net::post(s->ex, std::move(c));
                    }
                }
            } catch (...) { // NOLINT
                // EXCEPTION SAFETY FIX: Silently ignore exceptions in noexcept
                // context. This is a "best-effort" operation (Weak handle), so
                // if allocation or posting fails, there's no owner to notify
                // anyway.
            }
        }
        // remove todas as continuations sem completar (usado por timeout)
        void try_abandon() const noexcept {
            try {
                if (auto s = wst.lock()) {
                    std::scoped_lock lk(s->mx);
                    s->conts.clear();
                }
            } catch (...) { // NOLINT
            }
        }
        // try_fail overload for exception_ptr (void specialization)
        void try_fail(std::exception_ptr ep) const noexcept {
            try {
                if (auto s = wst.lock()) {
                    std::vector<std::function<void()>> cs;
                    {
                        std::scoped_lock lk(s->mx);
                        if (s->done) {
                            return;
                        }
                        s->done = true;
                        s->result = ep;
                        cs.swap(s->conts);
                    }
                    for (auto &c : cs) {
                        net::post(s->ex, std::move(c));
                    }
                }
            } catch (...) { // NOLINT
                // Best-effort ignore
            }
        }
        void try_fulfill() const noexcept {
            try {
                if (auto s = wst.lock()) {
                    std::vector<std::function<void()>> cs;
                    {
                        std::scoped_lock lk(s->mx);
                        if (s->done) {
                            return;
                        }
                        s->done = true;
                        s->result = std::monostate{};
                        cs.swap(s->conts);
                    }
                    for (auto &c : cs) {
                        net::post(s->ex, std::move(c));
                    }
                }
            } catch (...) { // NOLINT
                // EXCEPTION SAFETY FIX: Silently ignore exceptions in noexcept
                // context. This is a "best-effort" operation (Weak handle), so
                // if allocation or posting fails, there's no owner to notify
                // anyway.
            }
        }
    };

    [[nodiscard]] auto weak() const noexcept -> Weak {
        return Weak{std::weak_ptr<State<void>>(st_)};
    }

    // Adapt Async<void> into a boost::asio::awaitable<void>
    auto operator()() -> boost::asio::awaitable<void> {
        auto asyncHandler = [this](auto &&handler) mutable {
            using handler_t = std::decay_t<decltype(handler)>;
            auto sp = std::make_shared<handler_t>(
                std::forward<decltype(handler)>(handler));
            this->finally([sp](std::optional<asyncx::EC> erc,
                               const std::exception_ptr & /*ep*/) mutable {
                if (!erc) {
                    (*sp)(boost::system::error_code{});
                } else {
                    (*sp)(*erc);
                }
            });
        };

        co_await boost::asio::async_initiate<
            decltype(boost::asio::use_awaitable),
            void(boost::system::error_code)>(asyncHandler,
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
            } else if (auto *ec = std::get_if<EC>(&st->result)) {
                next.fail(*ec);
            } else {
                next.fail(std::get<std::exception_ptr>(st->result));
            }
        };
        try {
            attach_or_run(std::move(cont));
        } catch (...) {
            // EXCEPTION SAFETY FIX: If attach_or_run throws (e.g., bad_alloc),
            // fail next to prevent hang and notify caller of allocation
            // failure.
            next.fail(std::current_exception());
        }
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
            } else if (auto *ec = std::get_if<EC>(&st->result)) {
                next.fail(*ec);
            } else {
                next.fail(std::get<std::exception_ptr>(st->result));
            }
        };
        try {
            attach_or_run(std::move(cont));
        } catch (...) {
            // EXCEPTION SAFETY FIX: If attach_or_run throws (e.g., bad_alloc),
            // fail next to prevent hang and notify caller of allocation
            // failure.
            next.fail(std::current_exception());
        }
        return next;
    }

    template <class F> auto recover(F f) -> Async<void> {
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
        try {
            attach_or_run(std::move(cont));
        } catch (...) {
            // EXCEPTION SAFETY FIX: If attach_or_run throws (e.g., bad_alloc),
            // fail next to prevent hang and notify caller of allocation
            // failure.
            next.fail(std::current_exception());
        }
        return next;
    }

    template <class F> void finally(F f) const {
        auto cont = [st = st_, f = std::move(f)]() mutable {
            std::optional<EC> ec;
            std::exception_ptr ep;

            if (auto *pe = std::get_if<EC>(&st->result)) {
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
        std::optional<std::function<void()>> run_inline;
        {
            std::scoped_lock lk(st_->mx);
            if (!st_->done) {
                st_->conts.push_back(std::move(c));
            } else {
                // CRITICAL FIX: Move continuation INSIDE the lock to prevent
                // race where fulfill() could capture it from vector while we
                // try to invoke it, causing double-execution.
                run_inline = std::move(c);
            }
        }
        if (run_inline) {
            // If the state is already done, run the continuation inline to
            // ensure composed combinators observe completion immediately
            // and can cancel other operations deterministically.
            std::invoke(std::move(*run_inline));
        }
    }

  public:
    template <class Initiator>
    static auto from_callback(const net::any_io_executor &ex,
                              Initiator init) -> Async<void> {
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
auto all(net::any_io_executor ex,
         std::vector<Async<T>> vs) -> Async<std::vector<T>> {
    auto out = Async<std::vector<T>>::make(ex);
    auto ops = std::make_shared<std::vector<Async<T>>>(std::move(vs));
    auto res = std::make_shared<std::vector<std::optional<T>>>(ops->size());
    auto left =
        std::make_shared<std::atomic<int>>(static_cast<int>(ops->size()));

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
            auto reg =
                std::make_shared<std::stop_callback<std::function<void()>>>(
                    token, [ops]() {
                        for (auto &o : *ops) {
                            try {
                                o.request_stop();
                            } catch (...) { // NOLINT
                                // Best-effort cancellation: if request_stop()
                                // fails, continue trying to cancel other
                                // operations. Failure here is non-critical
                                // (operation may already be done).
                            }
                        }
                    });
            // hold reg until out completes
            out.finally([reg](std::optional<std::vector<T>> /*v*/,
                              std::optional<EC> /*ec*/,
                              const std::exception_ptr & /*ep*/) {
                // reg goes out of scope and is destroyed here
            });
        }
    }

    for (std::size_t i = 0; i < ops->size(); ++i) {
        (*ops)[i].finally(
            [out, res, left, ops, i](std::optional<T> v, std::optional<EC> ec,
                                     const std::exception_ptr &ep) {
                if (ec || ep) {
                    // cancel remaining operations to avoid zombi work
                    for (auto &o : *ops) {
                        try {
                            o.request_stop();
                        } catch (...) { // NOLINT
                            // Best-effort cancellation: if request_stop()
                            // fails, continue trying to cancel other
                            // operations. Failure here is non-critical
                            // (operation may already be done).
                        }
                    }
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
auto race(net::any_io_executor ex, std::vector<Async<T>> vs) -> Async<T> {
    auto out = Async<T>::make(ex);
    auto done = std::make_shared<std::atomic_bool>(false);
    auto ops = std::make_shared<std::vector<Async<T>>>(std::move(vs));

    // Propagate cancellation from composed Async to children
    if (ops->size() > 0) {
        auto token = out.get_stop_token();
        if (token.stop_possible()) {
            auto reg =
                std::make_shared<std::stop_callback<std::function<void()>>>(
                    token, [ops]() {
                        for (auto &o : *ops) {
                            try {
                                o.request_stop();
                            } catch (...) { // NOLINT
                                // Best-effort cancellation: if request_stop()
                                // fails, continue trying to cancel other
                                // operations. Failure here is non-critical
                                // (operation may already be done).
                            }
                        }
                    });
            out.finally([reg](std::optional<T> /*v*/, std::optional<EC> /*ec*/,
                              const std::exception_ptr & /*ep*/) {
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
                } catch (...) { // NOLINT
                    // Best-effort cancellation: if request_stop() fails,
                    // continue trying to cancel other operations. Failure
                    // here is non-critical (operation may already be done).
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
auto timeout(Async<T> inA, net::any_io_executor ex,
             std::chrono::duration<Rep, Per> d) -> Async<T> {
    auto out = Async<T>::make(ex);
    auto done = std::make_shared<std::atomic_bool>(false);
    auto timer = std::make_shared<net::steady_timer>(ex);
    timer->expires_after(d);
    // Weak handles to avoid prolonging lifetime unnecessarily
    auto weak_in = inA.weak();
    auto weak_out = out.weak();

    // Quando o timer dispara primeiro -> falha por timeout
    // Capture 'a' by value so we can request_stop() on it.
    // propagate cancellation from composed Async to the child and timer
    if (auto token = out.get_stop_token(); token.stop_possible()) {
        auto reg = std::make_shared<std::stop_callback<std::function<void()>>>(
            token, [weak_in, timer]() mutable {
                try {
                    weak_in.try_request_stop();
                } catch (...) {
                }
                try {
                    timer->cancel();
                } catch (...) {
                }
            });
        out.finally([reg, timer](std::optional<T> /*v*/,
                                 std::optional<EC> /*ec*/,
                                 const std::exception_ptr & /*ep*/) {
            // allow timer to be released
            (void)reg;
        });
    }

    timer->async_wait([weak_out, weak_in, done, timer](EC ec) mutable {
        if (done->exchange(true)) {
            return;
        }
        if (!ec) {
            // Signal stop to underlying op first
            weak_in.try_request_stop();
            // Abandon any remaining continuations of the underlying op to break
            // potential retain cycles
            weak_in.try_abandon();
            // Fail composed op if still alive
            weak_out.try_fail(make_error_code(boost::system::errc::timed_out));
        } else {
            // se foi cancelado por quem ganhou a corrida, ignoramos,
            // mas se for outro erro do timer, propaga
            if (ec != boost::system::errc::make_error_code(
                          boost::system::errc::operation_canceled)) {
                weak_out.try_fail(ec);
            }
        }
    });

    // Quando 'a' completa primeiro -> cancela o timer e propaga o resultado
    inA.finally([weak_out, done, timer](std::optional<T> v,
                                        std::optional<EC> ec,
                                        std::exception_ptr ep) {
        if (done->exchange(true)) {
            return;
        }
        timer->cancel();
        if (v) {
            weak_out.try_fulfill(*v);
        } else if (ec) {
            weak_out.try_fail(*ec);
        } else {
            weak_out.try_fail(ep);
        }
    });

    return out;
}

inline auto value_on(const boost::asio::any_io_executor &ex) -> Async<void> {
    auto a = Async<void>::make(ex);
    a.fulfill();
    return a;
}

inline auto value() -> Async<void> {
    // executa no system_executor; é imediato e não bloqueia o seu io_context
    auto a = Async<void>::make(boost::asio::system_executor{});
    a.fulfill();
    return a;
}

} // namespace asyncx

// =============================
// Pipe Operator Extension (asyncx)
// =============================
namespace asyncx {
template <class T> struct is_async : std::false_type {};
template <class T> struct is_async<Async<T>> : std::true_type {};
template <class T>
inline constexpr bool is_async_v = is_async<std::decay_t<T>>::value;

template <class F> struct Pipeable {
    F callable_fn;
    template <class AsyncLike>
        requires is_async_v<AsyncLike>
    auto operator()(AsyncLike async_value) const
        -> std::invoke_result_t<F, AsyncLike> {
        return std::invoke(callable_fn, std::move(async_value));
    }
};

template <class AsyncLike, class F>
    requires is_async_v<AsyncLike>
auto operator|(AsyncLike async_value, const Pipeable<F> &pipe_obj)
    -> decltype(pipe_obj(std::move(async_value))) {
    return pipe_obj(std::move(async_value));
}

template <class AsyncLike, class F>
    requires is_async_v<AsyncLike> && std::invocable<F, AsyncLike>
auto operator|(AsyncLike async_value, F &&fn_callable)
    -> decltype(std::invoke(std::forward<F>(fn_callable),
                            std::move(async_value))) {
    return std::invoke(std::forward<F>(fn_callable), std::move(async_value));
}

template <class F> auto map_p(F pipe_fn) {
    return Pipeable{[captured_fn = std::move(pipe_fn)](auto async_value) {
        return async_value.map(captured_fn);
    }};
}

template <class F> auto and_then_p(F pipe_fn) {
    return Pipeable{[captured_fn = std::move(pipe_fn)](auto async_value) {
        return async_value.and_then(captured_fn);
    }};
}

template <class F> auto recover_p(F pipe_fn) {
    return Pipeable{[captured_fn = std::move(pipe_fn)](auto async_value) {
        return async_value.recover(captured_fn);
    }};
}

template <class F> auto on_error_p(F pipe_fn) {
    return Pipeable{[captured_fn = std::move(pipe_fn)](auto async_value) {
        return async_value.on_error(captured_fn);
    }};
}

template <class Rep, class Per>
auto timeout_p(std::chrono::duration<Rep, Per> duration) {
    return Pipeable{[dur = duration](auto async_value) {
        using T = typename std::decay_t<decltype(async_value)>::value_type;
        return timeout<T>(std::move(async_value), async_value.get_executor(),
                          dur);
    }};
}

// Tap functor at namespace scope to allow member template operator()
template <class F> struct TapFunctorNS {
    F fn;
    template <class AsyncLike>
        requires is_async_v<AsyncLike>
    auto operator()(AsyncLike async_value) const {
        using T = typename std::decay_t<AsyncLike>::value_type;
        if constexpr (std::is_void_v<T>) {
            static_assert(
                std::is_invocable_v<F>,
                "tap for Async<void> requires callable with no arguments");
            return async_value.map([fn = fn] { fn(); });
        } else {
            static_assert(std::is_invocable_v<F, const T &>,
                          "tap requires callable accepting const T&");
            return async_value.map([fn = fn](const T &value_ref) {
                fn(value_ref);
                return value_ref;
            });
        }
    }
};

template <class F> auto tap(F side_effect_fn) {
    return Pipeable{TapFunctorNS<F>{std::move(side_effect_fn)}};
}

template <class F> auto filter(F predicate) {
    return Pipeable{[pred = std::move(predicate)](auto async_value) {
        using T = typename std::decay_t<decltype(async_value)>::value_type;
        static_assert(!std::is_void_v<T>,
                      "filter not supported for Async<void>");
        return async_value.map([pred](const T &value_ref) -> std::optional<T> {
            return pred(value_ref) ? std::optional<T>(value_ref) : std::nullopt;
        });
    }};
}

template <class F> auto filter_map(F transform_fn) {
    return Pipeable{[captured_fn = std::move(transform_fn)](auto async_value) {
        using T = typename std::decay_t<decltype(async_value)>::value_type;
        static_assert(!std::is_void_v<T>,
                      "filter_map not supported for Async<void>");
        return async_value.map([captured_fn](const T &value_ref) {
            return captured_fn(value_ref);
        });
    }};
}

template <class T, class U>
auto zip(Async<T> first_async,
         Async<U> second_async) -> Async<std::tuple<T, U>> {
    struct ZipState {
        std::mutex mx;
        std::optional<T> v1;
        std::optional<U> v2;
        bool done = false;
    };
    auto ex = first_async.get_executor();
    auto out = Async<std::tuple<T, U>>::make(ex);
    auto state = std::make_shared<ZipState>();
    // Propagate cancellation from composed Async to children (keep reg alive
    // until out completes)
    {
        auto token = out.get_stop_token();
        if (token.stop_possible()) {
            auto reg =
                std::make_shared<std::stop_callback<std::function<void()>>>(
                    token, [first_async, second_async]() mutable {
                        // Best-effort cancellation: ignore errors
                        try {
                            first_async.request_stop();
                        } catch (...) { // NOLINT
                            // intentionally ignored
                        }
                        try {
                            second_async.request_stop();
                        } catch (...) { // NOLINT
                            // intentionally ignored
                        }
                    });
            out.finally([reg](auto..., auto..., auto...) { /* keep alive */ });
        }
    }
    first_async.finally([out, state, second_async](auto value_opt,
                                                   auto error_code_opt,
                                                   auto exception_ptr) mutable {
        std::scoped_lock lock(state->mx);
        if (error_code_opt || exception_ptr) {
            // cancel the other operation to avoid wasted work
            try {
                second_async.request_stop();
            } catch (...) { // NOLINT
            }
            out.fail(error_code_opt ? *error_code_opt : EC{});
            return;
        }
        state->v1 = std::move(*value_opt);
        if (state->v2 && !state->done) {
            state->done = true;
            out.fulfill(std::make_tuple(*state->v1, *state->v2));
        }
    });
    second_async.finally([out, state, first_async](auto value_opt,
                                                   auto error_code_opt,
                                                   auto exception_ptr) mutable {
        std::scoped_lock lock(state->mx);
        if (error_code_opt || exception_ptr) {
            try {
                first_async.request_stop();
            } catch (...) { // NOLINT
            }
            out.fail(error_code_opt ? *error_code_opt : EC{});
            return;
        }
        state->v2 = std::move(*value_opt);
        if (state->v1 && !state->done) {
            state->done = true;
            out.fulfill(std::make_tuple(*state->v1, *state->v2));
        }
    });
    return out;
}

} // namespace asyncx
namespace asyncx {
template <class T> inline auto as_awaitable(Async<T> val) -> net::awaitable<T> {
    // Capture by value to extend the shared state lifetime while awaited.
    if constexpr (std::is_void_v<T>) {
        return std::move(val)();
    } else {
        return std::move(val)(boost::asio::use_awaitable);
    }
}
} // namespace asyncx

namespace asyncx {
// as_expected: adapt Async<T> -> awaitable<std::expected<T, EC>>
template <class T>
inline auto as_expected(Async<T> op) -> net::awaitable<std::expected<T, EC>> {
    if constexpr (std::is_void_v<T>) {
        auto tup =
            co_await op(boost::asio::as_tuple(boost::asio::use_awaitable));
        EC ec = std::get<0>(tup);
        if (ec) {
            co_return std::unexpected(ec);
        }
        co_return std::expected<void, EC>{};
    } else {
        auto tup =
            co_await op(boost::asio::as_tuple(boost::asio::use_awaitable));
        EC ec = std::get<0>(tup);
        auto val = std::get<1>(tup);
        if (ec) {
            co_return std::unexpected(ec);
        }
        co_return std::expected<T, EC>{std::in_place, std::move(val)};
    }
}

#if __has_include(<boost/outcome.hpp>)
#if defined(BOOST_OUTCOME_V2_NAMESPACE)
namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;
// as_result: adapt Async<T> -> awaitable<outcome::result<T, EC>>
template <class T>
inline auto as_result(Async<T> op) -> net::awaitable<outcome::result<T, EC>> {
    if constexpr (std::is_void_v<T>) {
        auto tup =
            co_await op(boost::asio::as_tuple(boost::asio::use_awaitable));
        EC ec = std::get<0>(tup);
        if (ec) {
            co_return outcome::failure(ec);
        }
        co_return outcome::success();
    } else {
        auto tup =
            co_await op(boost::asio::as_tuple(boost::asio::use_awaitable));
        EC ec = std::get<0>(tup);
        auto val = std::get<1>(tup);
        if (ec) {
            co_return outcome::failure<T>(ec);
        }
        co_return outcome::success<T>(std::move(val));
    }
}
#endif
#endif

} // namespace asyncx

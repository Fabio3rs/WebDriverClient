/**
 * @file asyncx.hpp
 * @brief Executor-agnostic async primitives for cross-executor composition
 *
 * @section Architecture
 *
 * This library provides generic asynchronous primitives that work across
 * multiple executors and io_contexts. Unlike BiDi components that use
 * strand-based serialization within a single io_context, asyncx supports
 * cross-executor composition where operations can span different execution
 * contexts.
 *
 * @section Lazy_Evaluation
 *
 * **Lazy evaluation model:**
 *
 * - Operations are lazy: Async<T> objects are lightweight and do not start
 *   execution when created.
 * - Composition operators (.map, .and_then, .on_error, .timeout, .retry) build
 *   up a chain of transformations without executing anything.
 * - Terminal operations materialize the chain: .finally(), co_await are the
 *   only triggers that start actual async work.
 * - This design enables optimization (chain flattening) and clean error
 * handling.
 *
 * @section Synchronization
 *
 * **Why std::mutex instead of strand?**
 *
 * - asyncx::Async<T> is executor-agnostic and supports cross-executor
 * composition
 * - Operations may complete on different io_contexts (e.g., all(), race(),
 * zip())
 * - Strand serialization assumes single executor topology
 * - Mutex provides correct synchronization across arbitrary executors
 *
 * **Contrast with BiDi architecture:**
 *
 * - BiDi components: Single io_context + strand → no mutex needed
 * - asyncx library: Multi-executor composition → mutex required
 *
 * Both patterns are correct for their respective use cases.
 *
 * @section Performance
 *
 * Mutexes in asyncx have negligible overhead because:
 * - Critical sections are minimal (flag check + vector swap)
 * - Contention is rare (completion happens once)
 * - Lock-free fast path when already completed
 *
 * @see bidi::core::ThreadingContext for BiDi's strand-based architecture
 */
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
#include <source_location>
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

/**
 * @brief Shared state for async operations with cross-executor safety
 *
 * @tparam T Result value type (or void for no-value operations)
 *
 * This structure holds the shared state for Async<T> futures and provides
 * thread-safe access to completion state across multiple executors.
 *
 * @section Thread Safety
 *
 * All member access is protected by `mx` mutex because:
 * - Completion may occur on executor A (e.g., io_context thread 1)
 * - Continuation attachment may occur on executor B (e.g., io_context thread 2)
 * - No single strand can serialize access across different executors
 *
 * @section Design Rationale
 *
 * The mutex protects:
 * - `done` flag: Completion state checked from multiple threads
 * - `result` variant: May be written by completer, read by awaiter
 * - `conts` vector: Continuations attached from arbitrary contexts
 *
 * Lock-free alternative would require complex ABA handling and is not
 * worth the complexity for operations that complete once.
 *
 * @note This differs from BiDi's strand-based model where all state access
 *       occurs on a single strand within one io_context.
 */
template <class T> struct State {
    net::any_io_executor ex; ///< Executor for posting continuations
    std::mutex mx;           ///< Protects done, result, conts (cross-executor)
    bool done{false};        ///< Completion flag
    std::variant<T, EC, std::exception_ptr> result; ///< Result value or error
    std::vector<std::function<void()>> conts;       ///< Pending continuations
    std::stop_source stop_src; ///< Cooperative cancellation source
    std::source_location loc;  ///< Source location for diagnostics

    /**
     * @brief Construct shared state with executor
     * @param e Executor for posting continuations when operation completes
     */
    explicit State(net::any_io_executor exec) : ex(std::move(exec)) {}
    explicit State(net::any_io_executor exec, std::source_location where)
        : ex(std::move(exec)), loc(where) {}
};

/**
 * @brief Shared state specialization for void operations
 *
 * Identical to State<T> except result variant uses std::monostate
 * for successful completion (no value to return).
 *
 * @see State<T> for detailed thread-safety documentation
 */
template <> struct State<void> {
    net::any_io_executor ex; ///< Executor for posting continuations
    std::mutex mx;           ///< Protects done, result, conts (cross-executor)
    bool done{false};        ///< Completion flag
    std::variant<EC, std::exception_ptr, std::monostate>
        result;                               ///< Error or monostate (success)
    std::vector<std::function<void()>> conts; ///< Pending continuations
    std::stop_source stop_src; ///< Cooperative cancellation source
    std::source_location loc;  ///< Source location for diagnostics

    /**
     * @brief Construct shared state with executor
     * @param e Executor for posting continuations when operation completes
     */
    explicit State(net::any_io_executor e) : ex(std::move(e)) {}
    explicit State(net::any_io_executor e, std::source_location where)
        : ex(std::move(e)), loc(where) {}
};

// Generic awaiter for Async<T> (defined in namespace scope to allow
// member templates like await_suspend). Specialized for void.
template <class T> struct async_awaiter {
    std::shared_ptr<State<T>> st;
    /**
     * @brief Fast-path check for completion
     *
     * await_ready returns true when the shared state already holds a
     * completion result. This enables the coroutine to continue without
     * suspension when the operation finished before the co_await point.
     */
    [[nodiscard]] auto await_ready() const noexcept -> bool { return st->done; }
    template <class Promise>
    void await_suspend(std::coroutine_handle<Promise> h) {
        std::scoped_lock lk(st->mx);
        // Store a continuation that will resume the awaiting coroutine.
        // The continuation is posted by the fulfiller via net::post so the
        // resumption happens on the executor stored in the shared state.
        st->conts.push_back([h]() mutable { h.resume(); });
    }
    auto await_resume() -> T {
        // On resume inspect the shared result variant. Order matters:
        // 1) If a value is present, return it.
        // 2) If an error_code is present, translate to system_error.
        // 3) Otherwise the stored exception_ptr is rethrown preserving the
        //    original domain exception type (this is how producers can
        //    communicate typed exceptions across executors).
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

/**
 * @brief Executor-agnostic async operation with lazy evaluation
 *
 * @tparam T Result type (or void for side-effect operations)
 *
 * Async<T> represents a lazy asynchronous operation that can be composed
 * across multiple executors and io_contexts. Operations are only materialized
 * when a terminal operator is invoked (.finally, co_await, operator()).
 *
 * @section Cross-Executor Safety
 *
 * Unlike BiDi components that serialize state via strand within a single
 * io_context, Async<T> uses std::mutex for shared state protection because:
 *
 * - Operations can span multiple io_contexts (e.g., all(), race(), zip())
 * - Completion may occur on executor A while continuation attaches on executor
 * B
 * - No assumption about executor topology (may be different io_services
 * entirely)
 *
 * @section Usage Examples
 *
 * @code
 * // Single executor (similar to BiDi usage)
 * auto result = co_await Async<int>::make(my_executor)
 *     .map([](int x) { return x * 2; })
 *     .and_then([](int x) { return fetch_data(x); });
 *
 * // Cross-executor composition (requires mutex synchronization)
 * auto combined = asyncx::zip(
 *     Async<int>::make(executor_A),  // May complete on io_context A
 *     Async<std::string>::make(executor_B)  // May complete on io_context B
 * );
 * @endcode
 *
 * @note This class is move-only and uses shared_ptr for shared state
 * management.
 *
 * @see State<T> for mutex synchronization details
 * @see bidi::core::ThreadingContext for strand-based alternative (single
 * executor)
 */
template <class T = void> class Async {
    std::shared_ptr<State<T>> st_;
    explicit Async(std::shared_ptr<State<T>> st) : st_(std::move(st)) {}

  public:
    using value_type = T;

    /// @brief Default constructor creates invalid async (use make() instead)
    Async() = default;

    /**
     * @brief Factory method to create Async with associated executor
     * @param ex Executor for posting continuations when operation completes
     * @return New Async<T> with shared state allocated on heap
     */
    static auto make(net::any_io_executor ex,
                     std::source_location loc = std::source_location::current())
        -> Async<T> {
        return Async<T>(std::make_shared<State<T>>(ex, loc));
    }

    [[nodiscard]] auto get_executor() const -> net::any_io_executor {
        return st_->ex;
    }

    [[nodiscard]] auto get_stop_token() const -> std::stop_token {
        return st_->stop_src.get_token();
    }

    void request_stop() { st_->stop_src.request_stop(); }

    /**
     * @brief Notes on cancellation and stop_token
     *
     * Cancellation is cooperative: callers may invoke `request_stop()` or use
     * the `stop_token` returned by `get_stop_token()` to observe cancellation.
     * The library posts cancellation requests and it's the responsibility of
     * producers to honor the stop token. This design keeps the Async primitive
     * lightweight and allows different producers to implement their own
     * cancellation semantics.
     */

    using type = T;

    // Implicit conversion to boost::asio::awaitable<void>
    operator net::awaitable<void>() const {
        auto self = *this;
        return (self)();
    }

    // Implicit conversion to boost::asio::awaitable<T>
    operator net::awaitable<T>() const {
        auto self = *this;
        return (self)();
    }

    /**
     * @note The implicit conversions above capture `*this` by value to keep
     * the shared state alive for the duration of the awaitable. This avoids
     * use-after-move or lifetime issues when an Async is materialized inside
     * a temporary expression in a coroutine.
     */

    void cancel() { st_->stop_src.request_stop(); }

    auto as_boost_future() { return (*this)(boost::asio::use_future); }

    auto get() { return as_boost_future().get(); }

    using signature_t =
        std::conditional_t<std::is_void_v<T>, void(boost::system::error_code),
                           void(boost::system::error_code, T)>;

#if defined(BOOST_ASIO_HAS_CONCEPTS)
    template <boost::asio::completion_token_for<signature_t> CompletionToken>
#else
    template <BOOST_ASIO_COMPLETION_TOKEN_FOR(signature_t) CompletionToken>
#endif
    auto operator()(CompletionToken token) const {
        // Signature depends on T: void(error_code) or void(error_code, T)

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
            initiation, token);
    }

    /**
     * @brief Adapt Async<T> into a Boost.Asio awaitable
     *
     * This adapter converts a lazy `Async<T>` into a
     * `boost::asio::awaitable<T>` so callers can `co_await` the operation
     * inside a Boost.Asio coroutine. It uses the CompletionToken path
     * (`use_awaitable`) internally to materialize the operation.
     *
     * Behavior:
     * - Success: the awaitable returns the contained value (or returns
     *   normally for `void`).
     * - Failure with stored exception: if the shared state holds a
     *   `std::exception_ptr` (for example a domain exception like
     *   `ScriptEvaluateException`), the adapter will rethrow that exception
     *   so the awaiting coroutine receives the original, typed exception.
     * - Failure without stored exception: if a `boost::system::system_error`
     *   occurred (e.g. transport or cancellation error) it will be propagated
     *   unchanged.
     *
     * Note: producers may call `fail(std::exception_ptr)` to record domain
     * exceptions. This adapter ensures those exceptions are preserved for
     * `co_await` consumers rather than being masked as a generic
     * `system_error`.
     *
     * @throws Any exception stored in the Async's state (rethrows the
     *         original exception type) or `boost::system::system_error`
     *         for transport/cancellation errors.
     *
     * Usage patterns:
     *
     * **Pattern 1 (Preferred): Direct co_await on temporaries**
     * @code
     * // Most common: co_await directly on function return (rvalue)
     * auto result = co_await client->navigate(ctx, url);
     * @endcode
     *
     * **Pattern 2: Parallel composition (when needed)**
     * @code
     * // Launch multiple operations, await later
     * auto task1 = client->operation1();  // Start async op 1
     * auto task2 = client->operation2();  // Start async op 2
     *
     * // ... do other work ...
     *
     * // Await results (requires () or std::move for lvalues)
     * auto result1 = co_await task1();  // Convert lvalue to awaitable
     * auto result2 = co_await std::move(task2);  // Or use std::move
     * @endcode
     *
     * **Pattern 3: Exception handling**
     * @code
     * try {
     *   auto value = co_await client->evaluate(expr, ctx);
     * } catch (const ScriptEvaluateException& e) {
     *   // Domain-specific exceptions preserved
     *   std::cerr << e.reason << ", line " << e.line_number << "\n";
     * } catch (const boost::system::system_error& se) {
     *   // Transport/timeout errors
     *   std::cerr << se.what() << "\n";
     * }
     * @endcode
     */
    auto operator()() -> boost::asio::awaitable<T> {
        if constexpr (std::is_void_v<T>) {
            try {
                co_await (*this)(boost::asio::use_awaitable);
                co_return;
            } catch (const boost::system::system_error & /*se*/) {
                std::exception_ptr eptr;
                {
                    std::scoped_lock lk(st_->mx);
                    if (auto p =
                            std::get_if<std::exception_ptr>(&st_->result)) {
                        eptr = *p;
                    }
                }
                if (eptr) {
                    std::rethrow_exception(eptr);
                }
                throw;
            }
        } else {
            try {
                co_return co_await (*this)(boost::asio::use_awaitable);
            } catch (const boost::system::system_error & /*se*/) {
                std::exception_ptr eptr;
                {
                    std::scoped_lock lk(st_->mx);
                    if (auto p =
                            std::get_if<std::exception_ptr>(&st_->result)) {
                        eptr = *p;
                    }
                }
                if (eptr) {
                    std::rethrow_exception(eptr);
                }
                throw;
            }
        }
    }

    template <class Fn>
    auto on_error(Fn fn,
                  std::source_location loc = std::source_location::current())
        -> Async<T> {
        auto &a = *this;
        // Capture the executor from this Async. The executor is used to
        // create downstream Async objects and to post continuations.
        auto ex = a.get_executor();
        auto out = Async<T>::make(ex, loc);

        a.finally([out, fn, loc](std::optional<T> v, std::optional<EC> ec,
                                 std::exception_ptr ep) mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
            if (v) {
                out.fulfill(std::move(*v));
                return;
            }
            // Tap (side-effect); if only an exception is present, pass EC{}.
            try {
                if (ec) {
                    fn(*ec);
                } else {
                    // fn(EC{});
                }
            } catch (...) { // NOLINT
                // Never let exceptions escape the handler; swallow to avoid
                // terminating the process from user-provided handlers.
            }
            if (ec) {
                out.fail(*ec);
            } else {
                out.fail(ep);
            }
        });
        return out;
    }

    // --- complete success/error (now const) ---
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
        // Post continuations to the executor stored in the shared state.
        // Posting (instead of calling inline) ensures continuations run on
        // the intended executor and avoids unexpectedly re-entering user
        // code on the fulfiller's thread which could violate assumptions
        // about executor affinity.
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
        // See note in fulfill(): post continuations to preserve executor
        // affinity and avoid surprising inline invocation of user handlers.
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
        // Rethrow semantics: producers store exception_ptr here. Consumers
        // expecting typed domain exceptions will receive the original type
        // when awaiting (see async_awaiter::await_resume()).
        for (auto &c : cs) {
            net::post(st_->ex, std::move(c));
        }
    }

    // --- map / and_then / recover / finally ---
    template <class F>
    auto map(F f,
             const std::source_location &loc = std::source_location::current())
        -> Async<std::invoke_result_t<F, const T &>> {
        using U = std::invoke_result_t<F, const T &>;
        auto next = Async<U>::make(st_->ex, loc);
        auto cont = [st = st_, next, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
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
    auto
    and_then(F f,
             const std::source_location &loc = std::source_location::current())
        -> decltype(std::invoke(f, std::declval<T &&>())) {
        using R = decltype(std::invoke(f, std::declval<T &&>()));
        using U = typename R::value_type;

        auto next = R::make(st_->ex);
        auto cont = [st = st_, next, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
            if (auto p = std::get_if<T>(&st->result)) {
                try {
                    auto nxt = std::invoke(f, std::move(*p));
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

    template <class F>
    auto recover(F f,
                 std::source_location loc = std::source_location::current())
        -> Async<T> {
        auto next = Async<T>::make(st_->ex, loc);
        auto cont = [st = st_, next, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
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

    template <class F>
    void finally(F f, const std::source_location &loc =
                          std::source_location::current()) const {
        auto cont = [st = st_, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
            std::optional<T> v;
            std::optional<EC> ec;
            std::exception_ptr ep;

            if (auto pv = std::get_if<T>(&st->result)) {
                v = std::move(*pv); // Move for move-only types
            } else if (auto pe = std::get_if<EC>(&st->result)) {
                ec = *pe;
            } else {
                ep = std::get<std::exception_ptr>(st->result);
            }

            std::invoke(f, std::move(v), std::move(ec), ep);
        };
        attach_or_run(std::move(cont));
    }

    auto
    await(const std::source_location &loc = std::source_location::current())
        -> auto & {
        attach_or_run([loc]() {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
        });

        return *this;
    }

    template <class F>
    void await_error(F f, const std::source_location &loc =
                              std::source_location::current()) const {
        auto cont = [st = st_, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
            std::optional<EC> ec;
            std::exception_ptr ep;
            if (auto pe = std::get_if<EC>(&st->result)) {
                ec = *pe;
            } else {
                ep = std::get<std::exception_ptr>(st->result);
            }
            if (ec) {
                std::invoke(f, *ec);
            }
        };
        attach_or_run(std::move(cont));
    }

    template <class F>
    void await(F f, const std::source_location &loc =
                        std::source_location::current()) const {
        auto cont = [st = st_, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
            std::optional<T &> v;
            std::optional<EC> ec;
            std::exception_ptr ep;

            if (auto pv = std::get_if<T>(&st->result)) {
                v.emplace(*pv);
            } else if (auto pe = std::get_if<EC>(&st->result)) {
                ec = *pe;
            } else {
                ep = std::get<std::exception_ptr>(st->result);
            }

            std::invoke(f, std::move(v), std::move(ec), ep);
        };
        attach_or_run(std::move(cont));
    }

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

        // remove all continuations without completing (used by timeout)
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

        auto finally_cb = [sp = handler_ptr, ex = std::move(executor),
                           alloc = allocator](
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
            [sp = handler_ptr, ex = std::move(executor), alloc = allocator](
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
    // ---------- factories ----------
    template <class U>
    static auto
    from_future(net::any_io_executor ex, std::future<U> fut,
                const std::shared_ptr<boost::asio::thread_pool> &pool = nullptr,
                std::source_location loc = std::source_location::current())
        -> Async<U> {
        auto a = Async<U>::make(ex, loc);
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
    static auto
    from_callback(net::any_io_executor ex, Initiator init,
                  std::source_location loc = std::source_location::current())
        -> Async<T> {
        auto a = Async<T>::make(ex, loc);
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

    static auto make(const net::any_io_executor &ex,
                     std::source_location loc = std::source_location::current())
        -> Async<void> {
        return Async<void>(std::make_shared<State<void>>(ex, loc));
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
        // remove all continuations without completing (used by timeout)
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
        void try_fail(const std::exception_ptr &ep) const noexcept {
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
    auto operator()(std::source_location loc = std::source_location::current())
        -> boost::asio::awaitable<void> {
        auto asyncHandler = [this, loc](auto &&handler) mutable {
            using handler_t = std::decay_t<decltype(handler)>;
            auto sp = std::make_shared<handler_t>(
                std::forward<decltype(handler)>(handler));
            this->finally(
                [sp, loc](std::optional<asyncx::EC> erc,
                          const std::exception_ptr & /*ep*/) mutable {
                    (void)loc; // Captured for GDB inspection (see CLAUDE.md
                               // debugging section)
                    if (!erc) {
                        (*sp)(boost::system::error_code{});
                    } else {
                        (*sp)(*erc);
                    }
                },
                loc);
        };

        co_await boost::asio::async_initiate<
            decltype(boost::asio::use_awaitable),
            void(boost::system::error_code)>(asyncHandler,
                                             boost::asio::use_awaitable);
        co_return;
    }

    template <class F>
    auto map(F f, std::source_location loc = std::source_location::current())
        -> Async<std::invoke_result_t<F>> {
        using U = std::invoke_result_t<F>;
        auto next = Async<U>::make(st_->ex, loc);
        auto cont = [st = st_, next, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
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

    template <class F>
    auto and_then(F f,
                  std::source_location loc = std::source_location::current())
        -> decltype(std::invoke(f)) {
        using R = decltype(std::invoke(f));
        using U = typename R::value_type;

        auto next = R::make(st_->ex, loc);
        auto cont = [st = st_, next, f = std::move(f), loc]() mutable {
            if (std::holds_alternative<std::monostate>(st->result)) {
                try {
                    auto nxt = std::invoke(f);
                    if constexpr (std::is_void_v<U>) {
                        nxt.finally([next, loc](std::optional<EC> ec,
                                                std::exception_ptr ep) {
                            (void)loc; // Captured for GDB inspection (see
                                       // CLAUDE.md debugging section)
                            if (!ec && !ep) {
                                next.fulfill();
                            } else if (ec) {
                                next.fail(*ec);
                            } else {
                                next.fail(ep);
                            }
                        });
                    } else {
                        nxt.finally([next, loc](std::optional<U> v,
                                                std::optional<EC> ec,
                                                std::exception_ptr ep) {
                            (void)loc; // Captured for GDB inspection (see
                                       // CLAUDE.md debugging section)
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

    template <class F>
    auto recover(F f,
                 std::source_location loc = std::source_location::current())
        -> Async<void> {
        auto next = Async<void>::make(st_->ex, loc);
        auto cont = [st = st_, next, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
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

    template <class F>
    void
    finally(F f,
            std::source_location loc = std::source_location::current()) const {
        auto cont = [st = st_, f = std::move(f), loc]() mutable {
            (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                       // section)
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
    static auto
    from_callback(const net::any_io_executor &ex, Initiator init,
                  std::source_location loc = std::source_location::current())
        -> Async<void> {
        auto a = Async<void>::make(ex, loc);
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
auto all(net::any_io_executor ex, std::vector<Async<T>> vs,
         std::source_location loc = std::source_location::current())
    -> Async<std::vector<T>> {
    auto out = Async<std::vector<T>>::make(ex, loc);
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
            out.await();
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
auto race(net::any_io_executor ex, std::vector<Async<T>> vs,
          std::source_location loc = std::source_location::current())
    -> Async<T> {
    auto out = Async<T>::make(ex, loc);
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
             std::chrono::duration<Rep, Per> d,
             std::source_location loc = std::source_location::current())
    -> Async<T> {
    auto out = Async<T>::make(ex, loc);
    auto done = std::make_shared<std::atomic_bool>(false);
    auto timer = std::make_shared<net::steady_timer>(ex);
    timer->expires_after(d);
    // Weak handles to avoid prolonging lifetime unnecessarily
    auto weak_in = inA.weak();
    auto weak_out = out.weak();

    // When the timer fires first -> fail with timeout
    // Capture 'a' by value so we can request_stop() on it.
    // propagate cancellation from composed Async to the child and timer
    if (auto token = out.get_stop_token(); token.stop_possible()) {
        auto reg = std::make_shared<std::stop_callback<std::function<void()>>>(
            token, [weak_in, timer]() mutable {
                try {
                    weak_in.try_request_stop();
                } catch (...) { // NOLINT
                }
                try {
                    timer->cancel();
                } catch (...) { // NOLINT
                }
            });
        out.finally([reg, timer](std::optional<T> /*v*/,
                                 std::optional<EC> /*ec*/,
                                 const std::exception_ptr & /*ep*/) {
            // allow timer to be released
            (void)reg;
        });
    }

    timer->async_wait([weak_out, weak_in, done, timer, loc](EC ec) mutable {
        (void)loc; // Captured for GDB inspection (see CLAUDE.md debugging
                   // section)
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

inline auto value_on(const boost::asio::any_io_executor &ex,
                     std::source_location loc = std::source_location::current())
    -> Async<void> {
    auto a = Async<void>::make(ex, loc);
    a.fulfill();
    return a;
}

inline auto value(std::source_location loc = std::source_location::current())
    -> Async<void> {
    // Execute on the system_executor; this is immediate and does not block
    // the caller's io_context.
    auto a = Async<void>::make(boost::asio::system_executor{}, loc);
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
        auto executor = async_value.get_executor();
        return timeout<T>(std::move(async_value), executor, dur);
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
            return async_value.map([fn = fn]() -> void {
                static_assert(std::is_void_v<std::invoke_result_t<F>>,
                              "tap functor for Async<void> must return void");
                fn();
                return; // explicit
            });
        } else {
            static_assert(std::is_invocable_v<F, const T &>,
                          "tap requires callable accepting const T&");
            return async_value.map([fn = fn](const T &value_ref) -> T {
                fn(value_ref);    // side effect
                return value_ref; // explicit copy (or elided for trivially
                                  // copyable)
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

/**
 * @brief Combine two async operations into a tuple (cross-executor safe)
 *
 * @tparam T Type of first async operation
 * @tparam U Type of second async operation
 * @param first_async First operation (may complete on executor A)
 * @param second_async Second operation (may complete on executor B)
 * @return Async<std::tuple<T, U>> that completes when both operations finish
 *
 * @section Cross-Executor Synchronization
 *
 * **Why mutex in ZipState?**
 *
 * This combinator allows operations on different executors to coordinate:
 *
 * @code
 * auto op1 = fetch_from_database(db_executor);  // Completes on DB thread pool
 * auto op2 = fetch_from_network(io_executor);   // Completes on network
 * io_context auto combined = zip(op1, op2);  // Coordinates across executors
 * @endcode
 *
 * The mutex protects ZipState members because:
 * - `v1` may be written by completion on executor A
 * - `v2` may be written by completion on executor B
 * - Both completions check `done` flag and may write result simultaneously
 * - No single strand can serialize access across different executors
 *
 * @section Failure Semantics
 *
 * If either operation fails, the other is cancelled and the error propagates.
 *
 * @note For single-executor scenarios, BiDi components use strand instead.
 * @see State<T> for additional synchronization rationale
 */
template <class T, class U>
auto zip(Async<T> first_async, Async<U> second_async,
         std::source_location loc = std::source_location::current())
    -> Async<std::tuple<T, U>> {
    /**
     * @brief Shared state for zip coordination with cross-executor safety
     *
     * Mutex protects simultaneous writes from different executor contexts.
     */
    struct ZipState {
        std::mutex mx;       ///< Protects v1, v2, done (cross-executor)
        std::optional<T> v1; ///< Result from first operation
        std::optional<U> v2; ///< Result from second operation
        bool done = false;   ///< Completion flag
    };
    auto ex = first_async.get_executor();
    auto out = Async<std::tuple<T, U>>::make(ex, loc);
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
            out.await(); // keep reg alive until out completes
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
        state->v1 = *value_opt; // keep local copy until both ready
        if (state->v2 && !state->done) {
            state->done = true;
            out.fulfill(
                std::make_tuple(std::move(*state->v1), std::move(*state->v2)));
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
        state->v2 = *value_opt;
        if (state->v1 && !state->done) {
            state->done = true;
            out.fulfill(
                std::make_tuple(std::move(*state->v1), std::move(*state->v2)));
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

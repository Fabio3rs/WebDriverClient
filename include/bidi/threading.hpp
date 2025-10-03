// include/bidi/threading.hpp — Threading context for zero busy-wait
#pragma once

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <memory>
#include <thread>
#include <vector>

namespace bidi::core {

/**
 * @brief Thread management for BiDi client using Boost.Asio primitives
 *
 * Architecture:
 * - io_context: WebSocket I/O + timers (suspends on epoll/kqueue/IOCP)
 * - thread_pool: CPU-bound work (JSON parsing, transformations)
 * - strand: WebSocket state serialization (no mutex needed)
 *
 * Zero busy-wait guarantee: All threads suspend via native kernel primitives
 */
class ThreadingContext {
  public:
    // NOTE FOR CONTRIBUTORS:
    // This component relies on real OS thread primitives and kernel wait
    // primitives (io_context, thread_pool, steady_timer and std::jthread).
    // Design rules to follow when integrating with ThreadingContext:
    // - Use `post_io`, `post_cpu` and `post_ws` to schedule work instead of
    //   creating raw threads or performing blocking waits inside handlers.
    // - Do not call blocking primitives (e.g. future.get(), promise wait,
    //   blocking I/O) from code that runs on `ws_strand_` or inside the
    //   io_context threads; prefer the awaitable/callback variants.
    // - Timers created via `make_timer` are tied to the strand and use
    //   kernel-native wait facilities (no busy-wait). Cancel timers to
    //   interrupt waits.
    // - This class is intended to provide native suspension primitives;
    //   callers must preserve the strand/serialisation guarantees and avoid
    //   blocking the event loop thread.

    explicit ThreadingContext(
        std::size_t io_threads = 1,
        std::size_t cpu_threads = std::thread::hardware_concurrency());

    ~ThreadingContext();

    // Non-copyable, non-movable
    ThreadingContext(const ThreadingContext &) = delete;
    ThreadingContext &operator=(const ThreadingContext &) = delete;
    ThreadingContext(ThreadingContext &&) = delete;
    ThreadingContext &operator=(ThreadingContext &&) = delete;

    // Get executors for different workloads
    [[nodiscard]] auto
    get_io_executor() noexcept -> boost::asio::io_context::executor_type {
        return io_context_->get_executor();
    }

    [[nodiscard]] auto
    get_cpu_executor() noexcept -> boost::asio::thread_pool::executor_type {
        return cpu_pool_->get_executor();
    }

    [[nodiscard]] auto get_ws_strand() noexcept
        -> boost::asio::strand<boost::asio::io_context::executor_type> {
        return ws_strand_;
    }

    // Post work to specific executors (native suspension)
    template <typename CompletionToken> void post_io(CompletionToken &&token) {
        boost::asio::post(*io_context_, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken> void post_cpu(CompletionToken &&token) {
        boost::asio::post(*cpu_pool_, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken> void post_ws(CompletionToken &&token) {
        boost::asio::post(ws_strand_, std::forward<CompletionToken>(token));
    }

    // Create timers (native timeout via kernel)
    [[nodiscard]] auto make_timer(std::chrono::milliseconds duration)
        -> std::shared_ptr<boost::asio::steady_timer> {
        return std::make_shared<boost::asio::steady_timer>(ws_strand_,
                                                           duration);
    }

    // Graceful shutdown
    void stop();

  private:
    // I/O context for WebSocket and timers
    std::unique_ptr<boost::asio::io_context> io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        io_work_guard_;

    // CPU thread pool for heavy work
    std::unique_ptr<boost::asio::thread_pool> cpu_pool_;

    // WebSocket strand for serialization (eliminates mutex)
    boost::asio::strand<boost::asio::io_context::executor_type> ws_strand_;

    // I/O threads (suspend on kernel I/O primitives)
    std::vector<std::jthread> io_threads_;
};

} // namespace bidi::core

#pragma once

#include "client.hpp"
#include <boost/asio.hpp>
#include <memory>
#include <thread>

namespace bidi {

// IoContextRunner: RAII helper that starts an io_context in a background
// thread.
// - Keeps io_context alive via work_guard
// - Exposes executor for creating tasks
// - Joins thread on destruction
class IoContextRunner {
  public:
    IoContextRunner()
        : ioc_(std::make_shared<boost::asio::io_context>()),
          work_guard_(boost::asio::make_work_guard(*ioc_)),
          thread_([ioc = ioc_]() mutable {
              try {
                  ioc->run();
              } catch (const std::exception &ex) {
                  // Logging is intentionally minimal to avoid header dependency
              }
          }) {}

    explicit IoContextRunner(std::size_t threads) : IoContextRunner() {
        // If threads > 1, spawn additional worker threads (not joinable here)
        for (std::size_t i = 1; i < threads; ++i) {
            workers_.emplace_back([ioc = ioc_]() mutable { ioc->run(); });
        }
    }

    ~IoContextRunner() {
        work_guard_.reset();
        if (ioc_) {
            ioc_->stop();
        }
        if (thread_.joinable())
            thread_.join();
        for (auto &t : workers_) {
            if (t.joinable())
                t.join();
        }
    }

    // Non-copyable
    IoContextRunner(const IoContextRunner &) = delete;
    IoContextRunner &operator=(const IoContextRunner &) = delete;

    // Non-movable: boost::asio::executor_work_guard may be non-assignable on
    // some Asio/standalone Asio implementations. Keep IoContextRunner
    // non-copyable and non-movable to avoid attempting to assign the
    // work_guard_ member.
    IoContextRunner(IoContextRunner &&) noexcept = delete;
    IoContextRunner &operator=(IoContextRunner &&) noexcept = delete;

    boost::asio::io_context &get() const { return *ioc_; }
    boost::asio::io_context::executor_type get_executor() const {
        return ioc_->get_executor();
    }

  private:
    std::shared_ptr<boost::asio::io_context> ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard_;
    std::thread thread_;
    std::vector<std::thread> workers_;
};

// SyncClient: minimal sync facade that hides io_context and exposes a simple
// blocking connect helper. This class is intentionally small; it uses
// bidi::Client::connect(...).get() under the hood when available (delegates to
// existing Async futures).
class SyncClient {
  public:
    explicit SyncClient(std::size_t threads = 1) : runner_(threads) {}

    template <typename... Args> auto connect(Args &&...args) {
        // Forward to bidi::Client::connect using runner_.get() as io_context.
        // Return the future/result from co_spawn + use_future pattern.
        return bidi::Client::connect(runner_.get(),
                                     std::forward<Args>(args)...);
    }

    boost::asio::io_context &ioc() { return runner_.get(); }

  private:
    IoContextRunner runner_;
};

} // namespace bidi

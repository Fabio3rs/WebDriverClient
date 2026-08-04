#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace bidi {

// IoContextRunner: RAII helper that starts an io_context in a background
// thread.
// - Keeps io_context alive via work_guard
// - Exposes executor for creating tasks
// - Joins thread on destruction
// - Provides guard control for AutomationSession::run() workflow
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
        try {
            // If threads > 1, spawn additional worker threads
            for (std::size_t i = 1; i < threads; ++i) {
                workers_.emplace_back([ioc = ioc_]() mutable { ioc->run(); });
            }
        } catch (...) {
            // Exception safety: cleanup on failure
            work_guard_.reset();
            ioc_->stop();
            for (auto &worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            throw;
        }
    }

    ~IoContextRunner() {
        work_guard_.reset();
        if (ioc_) {
            ioc_->stop();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        for (auto &worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
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

    [[nodiscard]] auto get() const -> boost::asio::io_context & {
        return *ioc_;
    }
    [[nodiscard]] auto get_executor() const
        -> boost::asio::io_context::executor_type {
        return ioc_->get_executor();
    }

    // Guard control for AutomationSession::run() workflow
    void arm_work() {
        if (!work_guard_) {
            work_guard_.emplace(boost::asio::make_work_guard(*ioc_));
        }
    }

    void release_work() { work_guard_.reset(); }

    void stop() {
        if (ioc_) {
            ioc_->stop();
        }
    }

    void restart() {
        if (ioc_) {
            ioc_->restart();
        }
    }

  private:
    std::shared_ptr<boost::asio::io_context> ioc_;
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_guard_;
    std::thread thread_;
    std::vector<std::thread> workers_;
};

} // namespace bidi

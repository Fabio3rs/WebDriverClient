#pragma once
/**
 * @file guards.hpp
 * @brief Collection of small RAII helpers used throughout tests and examples.
 *
 * Rationale:
 * - Tests and examples create many resources that must be reliably cleaned up
 *   (sessions, timers, io threads). These helpers codify the recommended
 *   cleanup order used by the project: unsubscribe -> disconnect -> drain ->
 *   stop -> join. Using RAII here reduces boilerplate in examples and makes
 *   tests deterministic (important for ASan leak checking).
 * - TimerGuard cancels timers explicitly in destructors to avoid dangling
 *   callbacks that could be observed by sanitizers as indirect leaks when
 *   stress tests create thousands of timers.
 * - IoContextGuard ensures io_context is stopped and the runner thread is
 *   joined to avoid races during teardown.
 */

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/core.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <expected>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace bidi {

/**
 * @brief RAII guard for WebDriver HTTP session lifecycle
 *
 * Guarantees session cleanup on destruction, preventing resource leaks.
 * Thread-safe, move-only class.
 *
 * Example:
 * @code
 * auto session = bidi::SessionGuard("http://localhost:9515");
 * auto ws_url = session.connect(args, "chrome", true);
 * // Session automatically closed on scope exit
 * @endcode
 */
class SessionGuard {
  private:
    WebDriver driver_;
    std::string session_id_;
    bool connected_{false};

  public:
    explicit SessionGuard(std::string url) {
        driver_.webDriverUrl = std::move(url);
    }

    ~SessionGuard() noexcept {
        if (connected_ && !session_id_.empty()) {
            try {
                driver_.quit();
            } catch (const std::exception &exception) {
                logging::log_error(std::string("Failed to cleanup session: ") +
                                   exception.what());
            } catch (...) {
                logging::log_error("Unknown error during session cleanup");
            }
        }
    }

    SessionGuard(const SessionGuard &) = delete;
    auto operator=(const SessionGuard &) -> SessionGuard & = delete;
    SessionGuard(SessionGuard &&) noexcept = default;
    auto operator=(SessionGuard &&) noexcept -> SessionGuard & = default;

    auto connect(const WebDriver::json &args, const std::string &browser_name,
                 bool headless) -> std::expected<std::string, std::string> {
        try {
            auto response = driver_.connect(args, browser_name, headless);
            session_id_ = driver_.sessionId;
            connected_ = true;

            if (!response.contains("capabilities") ||
                !response["capabilities"].contains("webSocketUrl")) {
                return std::unexpected(
                    "Missing webSocketUrl in session capabilities");
            }

            return response["capabilities"]["webSocketUrl"].get<std::string>();
        } catch (const std::exception &exception) {
            return std::unexpected(std::string("Connection failed: ") +
                                   exception.what());
        }
    }

    // Create a session using a pre-built capabilities payload
    auto connect_with_payload(const WebDriver::json &payload)
        -> std::expected<std::string, std::string> {
        try {
            auto response = driver_.connect_with_payload(payload);
            session_id_ = driver_.sessionId;
            connected_ = true;

            if (!response.contains("capabilities") ||
                !response["capabilities"].contains("webSocketUrl")) {
                return std::unexpected(
                    "Missing webSocketUrl in session capabilities");
            }

            return response["capabilities"]["webSocketUrl"].get<std::string>();
        } catch (const std::exception &exception) {
            return std::unexpected(std::string("Connection failed: ") +
                                   exception.what());
        }
    }

    [[nodiscard]] auto session_id() const noexcept -> const std::string & {
        return session_id_;
    }

    [[nodiscard]] auto is_connected() const noexcept -> bool {
        return connected_;
    }
};

/**
 * @brief RAII guard for Boost.Asio steady_timer (watchdog)
 *
 * Automatically cancels timer on destruction, preventing dangling callbacks.
 * Captures timer by shared_ptr to extend lifetime beyond scope.
 *
 * Example:
 * @code
 * std::atomic<bool> flag{false};
 * {
 *     bidi::TimerGuard watchdog(executor, 30s, flag);
 *     // ... do work ...
 * } // Timer automatically cancelled here
 * @endcode
 */
class TimerGuard {
  private:
    std::shared_ptr<boost::asio::steady_timer> timer_;
    [[maybe_unused]] std::atomic<bool> &flag_;

  public:
    TimerGuard(const boost::asio::any_io_executor &executor,
               std::chrono::seconds timeout, std::atomic<bool> &flag)
        : timer_(std::make_shared<boost::asio::steady_timer>(executor)),
          flag_(flag) {

        timer_->expires_after(timeout);
        timer_->async_wait([timer = timer_, &flag](
                               const boost::system::error_code &error_code) {
            if (error_code == boost::asio::error::operation_aborted) {
                return;
            }

            if (!flag.load(std::memory_order_acquire)) {
                logging::log_error(
                    "Watchdog timeout reached, operation exceeded deadline");
            }
        });
    }

    ~TimerGuard() noexcept {
        boost::system::error_code error_code;
        timer_->cancel(error_code);
        if (error_code) {
            logging::log_error(
                std::string("Failed to cancel watchdog timer: ") +
                error_code.message());
        }
    }

    TimerGuard(const TimerGuard &) = delete;
    auto operator=(const TimerGuard &) -> TimerGuard & = delete;
    TimerGuard(TimerGuard &&) = delete;
    auto operator=(TimerGuard &&) -> TimerGuard & = delete;

    void disarm() noexcept {
        boost::system::error_code error_code;
        timer_->cancel(error_code);
    }
};

/**
 * @brief RAII guard for BiDi client and subscriptions
 *
 * Manages BiDi client lifecycle with proper cleanup order:
 * 1. Unsubscribe all events
 * 2. Disconnect session
 * 3. Reset client
 *
 * Example:
 * @code
 * auto client = co_await bidi::Client::connect(io, url)();
 * bidi::ClientGuard guard(client);
 * guard.add_subscription(sub1);
 * // Automatic cleanup on scope exit
 * @endcode
 */
class ClientGuard {
  private:
    std::shared_ptr<bidi::Client> client_;
    std::vector<std::shared_ptr<bidi::core::BiDiSession::Subscription>>
        subscriptions_;

  public:
    explicit ClientGuard(std::shared_ptr<bidi::Client> client)
        : client_(std::move(client)) {}

    ~ClientGuard() noexcept {
        try {
            subscriptions_.clear();

            if (client_ && client_->session()) {
                try {
                    client_->session()->disconnect();
                } catch (const std::exception &exception) {
                    logging::log_error(
                        std::string("Failed to disconnect session: ") +
                        exception.what());
                } catch (...) {
                    logging::log_error(
                        "Unknown error during session disconnect");
                }
            }

            client_.reset();
        } catch (const std::exception &exception) {
            logging::log_error(std::string("ClientGuard cleanup failed: ") +
                               exception.what());
        } catch (...) {
            logging::log_error("Unknown error in ClientGuard cleanup");
        }
    }

    ClientGuard(const ClientGuard &) = delete;
    auto operator=(const ClientGuard &) -> ClientGuard & = delete;
    ClientGuard(ClientGuard &&) noexcept = default;
    auto operator=(ClientGuard &&) noexcept -> ClientGuard & = default;

    [[nodiscard]] auto client() noexcept -> std::shared_ptr<bidi::Client> & {
        return client_;
    }

    [[nodiscard]] auto client() const noexcept
        -> const std::shared_ptr<bidi::Client> & {
        return client_;
    }

    void add_subscription(
        std::shared_ptr<bidi::core::BiDiSession::Subscription> subscription) {
        subscriptions_.push_back(std::move(subscription));
    }

    [[nodiscard]] auto subscription_count() const noexcept -> size_t {
        return subscriptions_.size();
    }
};

/**
 * @brief RAII guard for std::future with timeout support
 *
 * Prevents indefinite blocking on future.get() by enforcing timeout.
 * Provides std::expected interface for error handling.
 *
 * Example:
 * @code
 * auto coro = co_spawn(io, my_coroutine(), use_future);
 * bidi::FutureGuard scoped(std::move(coro), 30s, "my_operation");
 * auto result = scoped.get(); // Returns expected<T, string>
 * @endcode
 */
template <typename T> class FutureGuard {
  private:
    std::future<T> future_;
    std::chrono::seconds timeout_;
    std::string operation_name_;

  public:
    FutureGuard(std::future<T> future, std::chrono::seconds timeout,
                std::string name)
        : future_(std::move(future)), timeout_(timeout),
          operation_name_(std::move(name)) {}

    auto get() -> std::expected<T, std::string> {
        if (future_.wait_for(timeout_) == std::future_status::timeout) {
            return std::unexpected(std::string("Timeout waiting for ") +
                                   operation_name_ + " (>" +
                                   std::to_string(timeout_.count()) + "s)");
        }

        try {
            if constexpr (std::is_void_v<T>) {
                future_.get();
                return {};
            } else {
                return future_.get();
            }
        } catch (const std::exception &exception) {
            return std::unexpected(std::string("Exception in ") +
                                   operation_name_ + ": " + exception.what());
        } catch (...) {
            return std::unexpected(std::string("Unknown exception in ") +
                                   operation_name_);
        }
    }

    ~FutureGuard() noexcept {
        if (future_.valid()) {
            using namespace std::chrono_literals;
            if (future_.wait_for(100ms) != std::future_status::ready) {
                logging::log_error(std::string("FutureGuard destructor: "
                                               "operation '") +
                                   operation_name_ + "' still pending");
            }
        }
    }

    FutureGuard(const FutureGuard &) = delete;
    auto operator=(const FutureGuard &) -> FutureGuard & = delete;
    FutureGuard(FutureGuard &&) noexcept = default;
    auto operator=(FutureGuard &&) noexcept -> FutureGuard & = default;
};

/**
 * @brief RAII guard for io_context thread management
 *
 * Runs io_context in dedicated thread with automatic join on destruction.
 * Prevents "forgot to join thread" bugs.
 *
 * Example:
 * @code
 * boost::asio::io_context io;
 * std::atomic<bool> flag{false};
 * {
 *     bidi::IoContextGuard runner(io, flag);
 *     // ... post work to io_context ...
 * } // Automatically stops and joins thread here
 * @endcode
 */
class IoContextGuard {
  private:
    boost::asio::io_context &io_context_;
    std::atomic<bool> &stop_flag_;
    std::thread runner_thread_;

  public:
    IoContextGuard(boost::asio::io_context &io_context, std::atomic<bool> &flag)
        : io_context_(io_context), stop_flag_(flag) {
        runner_thread_ = std::thread([this]() {
            try {
                io_context_.run();
                stop_flag_.store(true, std::memory_order_release);
            } catch (const std::exception &exception) {
                logging::log_error(
                    std::string("io_context.run() threw exception: ") +
                    exception.what());
                stop_flag_.store(true, std::memory_order_release);
            } catch (...) {
                logging::log_error("io_context.run() threw unknown exception");
                stop_flag_.store(true, std::memory_order_release);
            }
        });
    }

    ~IoContextGuard() noexcept {
        try {
            if (!stop_flag_.load(std::memory_order_acquire)) {
                io_context_.stop();
            }

            if (runner_thread_.joinable()) {
                runner_thread_.join();
            }
        } catch (const std::exception &exception) {
            logging::log_error(std::string("IoContextGuard cleanup failed: ") +
                               exception.what());
        } catch (...) {
            logging::log_error("Unknown error in IoContextGuard cleanup");
        }
    }

    IoContextGuard(const IoContextGuard &) = delete;
    auto operator=(const IoContextGuard &) -> IoContextGuard & = delete;
    IoContextGuard(IoContextGuard &&) = delete;
    auto operator=(IoContextGuard &&) -> IoContextGuard & = delete;

    void stop() noexcept {
        io_context_.stop();
        stop_flag_.store(true, std::memory_order_release);
    }
};

} // namespace bidi

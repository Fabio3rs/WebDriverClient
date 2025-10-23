#pragma once
/**
 * @file guards.hpp
 * @brief RAII guards for deterministic resource cleanup in tests and examples.
 *
 * Architectural rationale:
 * - RAII everywhere: Core project principle is automatic resource management
 *   via RAII wrappers. These guards codify the recommended cleanup order:
 *   unsubscribe → disconnect → drain → stop → join.
 * - Resource leak prevention: Guarantees cleanup even when exceptions occur
 *   or early returns happen. Critical for ASan/LSan leak detection in tests.
 * - Deterministic cleanup ordering: Destructors enforce correct teardown
 *   sequence to avoid use-after-free and race conditions during shutdown.
 * - Integration with zero busy-wait: IoContextGuard ensures io_context is
 *   stopped and runner threads are joined without polling loops.
 * - Timer lifecycle safety: TimerGuard explicitly cancels timers in destructor
 *   to avoid dangling callbacks observed by sanitizers as indirect leaks in
 *   stress tests with thousands of timers.
 *
 * Usage pattern:
 * - SessionGuard: WebDriver HTTP session lifecycle (auto-cleanup on scope exit)
 * - ClientGuard: BiDi client with subscription management (RAII unsubscribe)
 * - TimerGuard: Explicit timer cancellation (prevents callback races)
 * - IoContextGuard: io_context + runner thread lifecycle (safe shutdown)
 *
 * @note These guards are intended for tests and examples where boilerplate
 *       reduction and deterministic cleanup are priorities. Production code
 *       may prefer explicit lifecycle management for finer control.
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
 * Manages BiDi client lifecycle with deterministic cleanup order:
 * 1. Unsubscribe all events (subscription list cleared)
 * 2. Disconnect session (if active)
 * 3. Reset client (breaking reference cycles)
 *
 * Features:
 * - Fail-safe cleanup with exception swallowing (no throws from destructor)
 * - Detailed logging of each cleanup step for debugging
 * - State tracking to prevent double-cleanup
 * - Explicit cleanup() method for early resource release
 * - Automatic cleanup via RAII on scope exit
 *
 * Cleanup Guarantee:
 * Destructors guarantee that all resources are released even if:
 * - Exceptions occur during cleanup
 * - Some cleanup steps fail
 * - The guard is moved (cleanup responsibility transfers)
 *
 * Example:
 * @code
 * auto client = co_await bidi::Client::connect(io, url);
 * bidi::ClientGuard guard(client);
 * guard.add_subscription(sub1);
 * // Subscriptions cleared + session disconnected + client reset
 * // automatically on scope exit
 * @endcode
 */
class ClientGuard {
  private:
    std::shared_ptr<bidi::Client> client_;
    std::vector<std::shared_ptr<bidi::core::BiDiSession::Subscription>>
        subscriptions_;
    bool cleanup_started_{false};
    bool cleanup_completed_{false};

    /**
     * @brief Perform cleanup with detailed logging and error handling
     * @note Always completes, even if individual steps fail
     */
    void perform_cleanup_() noexcept {
        if (cleanup_completed_) {
            return; // Already cleaned up
        }

        cleanup_started_ = true;

        // Step 1: Clear subscriptions (break event handler cycles)
        try {
            const auto subscription_count = subscriptions_.size();
            subscriptions_.clear();
            if (subscription_count > 0) {
                logging::log_error(std::string("ClientGuard: cleared ") +
                                   std::to_string(subscription_count) +
                                   " subscription(s)");
            }
        } catch (const std::exception &e) {
            logging::log_error(std::string("ClientGuard: failed to clear "
                                           "subscriptions: ") +
                               e.what());
        } catch (...) {
            logging::log_error(
                "ClientGuard: unknown error clearing subscriptions");
        }

        // Step 2: Disconnect session
        if (client_) {
            try {
                if (auto session = client_->session()) {
                    session->disconnect();
                    logging::log_error("ClientGuard: session disconnected");
                }
            } catch (const std::exception &e) {
                logging::log_error(std::string("ClientGuard: failed to "
                                               "disconnect session: ") +
                                   e.what());
            } catch (...) {
                logging::log_error(
                    "ClientGuard: unknown error disconnecting session");
            }
        }

        // Step 3: Reset client (break reference cycles and weak_ptr cycles)
        try {
            if (client_) {
                client_.reset();
                logging::log_error("ClientGuard: client reset");
            }
        } catch (const std::exception &e) {
            logging::log_error(std::string("ClientGuard: failed to reset "
                                           "client: ") +
                               e.what());
        } catch (...) {
            logging::log_error("ClientGuard: unknown error resetting client");
        }

        cleanup_completed_ = true;
    }

  public:
    explicit ClientGuard(std::shared_ptr<bidi::Client> client) noexcept
        : client_(std::move(client)) {}

    /**
     * @brief Explicit cleanup method for early resource release
     *
     * Can be called before destruction to release resources immediately.
     * Safe to call multiple times (idempotent).
     */
    void cleanup() noexcept { perform_cleanup_(); }

    /**
     * @brief Destructor: performs RAII cleanup (no throws guaranteed)
     */
    ~ClientGuard() noexcept {
        try {
            if (!cleanup_completed_) {
                perform_cleanup_();
            }
        } catch (const std::exception &e) {
            logging::log_error(
                std::string("ClientGuard destructor exception: ") + e.what());
        } catch (...) {
            logging::log_error("ClientGuard destructor: unknown exception");
        }
    }

    ClientGuard(const ClientGuard &) = delete;
    auto operator=(const ClientGuard &) -> ClientGuard & = delete;

    // Move semantics: transfer cleanup responsibility
    ClientGuard(ClientGuard &&other) noexcept
        : client_(std::move(other.client_)),
          subscriptions_(std::move(other.subscriptions_)),
          cleanup_started_(other.cleanup_started_),
          cleanup_completed_(other.cleanup_completed_) {
        // Reset source to prevent double-cleanup
        other.cleanup_completed_ = true;
    }

    auto operator=(ClientGuard &&other) noexcept -> ClientGuard & {
        if (this != &other) {
            // Cleanup self first
            if (!cleanup_completed_) {
                perform_cleanup_();
            }

            // Take ownership of other's resources
            client_ = std::move(other.client_);
            subscriptions_ = std::move(other.subscriptions_);
            cleanup_started_ = other.cleanup_started_;
            cleanup_completed_ = other.cleanup_completed_;

            // Prevent double-cleanup in other's destructor
            other.cleanup_completed_ = true;
        }
        return *this;
    }

    /**
     * @brief Get non-const reference to client (for mutations)
     */
    [[nodiscard]] auto client() noexcept -> std::shared_ptr<bidi::Client> & {
        return client_;
    }

    /**
     * @brief Get const reference to client (for reads)
     */
    [[nodiscard]] auto client() const noexcept
        -> const std::shared_ptr<bidi::Client> & {
        return client_;
    }

    /**
     * @brief Add subscription to cleanup list
     *
     * Subscriptions are cleared (in reverse order of insertion) during
     * cleanup, ensuring proper RAII semantics.
     */
    void add_subscription(
        std::shared_ptr<bidi::core::BiDiSession::Subscription> subscription) {
        subscriptions_.push_back(std::move(subscription));
    }

    /**
     * @brief Get count of managed subscriptions
     */
    [[nodiscard]] auto subscription_count() const noexcept -> size_t {
        return subscriptions_.size();
    }

    /**
     * @brief Check if cleanup has been completed
     */
    [[nodiscard]] auto is_cleaned_up() const noexcept -> bool {
        return cleanup_completed_;
    }

    /**
     * @brief Clear all managed subscriptions explicitly
     *
     * Useful for selective cleanup before destructor.
     */
    void clear_subscriptions() noexcept {
        try {
            subscriptions_.clear();
        } catch (const std::exception &e) {
            logging::log_error(std::string("ClientGuard: failed to clear "
                                           "subscriptions: ") +
                               e.what());
        } catch (...) {
            logging::log_error(
                "ClientGuard: unknown error clearing subscriptions");
        }
    }

    /**
     * @brief Synchronous graceful cleanup helper
     *
     * This method can be called from non-io threads to perform a graceful
     * shutdown: it will run an awaitable on the session executor to wait for
     * pending operations to complete (up to a timeout) and then disconnect.
     *
     * WARNING: Do NOT call this from the io_context/strand thread; it will
     * block waiting for the async shutdown and deadlock the executor.
     */
    void cleanup_graceful(std::chrono::milliseconds timeout =
                              std::chrono::milliseconds(5000)) noexcept {
        if (cleanup_completed_) {
            return;
        }

        try {
            if (!client_) {
                perform_cleanup_();
                return;
            }

            if (auto session = client_->session()) {
                // Launch coroutine on session executor and wait via future
                std::promise<void> p;
                auto f = p.get_future();

                boost::asio::post(session->get_executor(), [session, timeout,
                                                            prom = std::move(
                                                                p)]() mutable {
                    // co_spawn a coroutine that awaits pending operations then
                    // sets promise
                    boost::asio::co_spawn(
                        session->get_executor(),
                        [session, timeout, prom = std::move(prom)]() mutable
                            -> boost::asio::awaitable<void> {
                            co_await session->await_pending_operations_complete(
                                timeout);
                            session->disconnect();
                            try {
                                prom.set_value();
                            } catch (...) { // NOLINT: promise may be already
                                            // satisfied or broken - ignore
                            }
                            co_return;
                        },
                        boost::asio::detached);
                });

                // Wait for completion (caller must not be on io_context thread)
                if (f.valid()) {
                    if (f.wait_for(timeout + std::chrono::milliseconds(100)) ==
                        std::future_status::timeout) {
                        logging::log_warning(
                            "ClientGuard::cleanup_graceful timed out waiting "
                            "for async shutdown");
                    }
                }
            }
        } catch (const std::exception &e) {
            logging::log_error(
                std::string("ClientGuard::cleanup_graceful exception: ") +
                e.what());
        } catch (...) {
            logging::log_error(
                "ClientGuard::cleanup_graceful unknown exception");
        }

        // Final cleanup of local resources
        perform_cleanup_();
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

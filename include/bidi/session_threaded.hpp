// include/bidi/session_threaded.hpp — BiDi session with native suspension
#pragma once

#include "bidi/core.hpp"
#include "bidi/threading.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace bidi::core {

// Pending entry with native promise (no condition_variable needed)
struct PendingEntry {
    std::string method{};
    boost::json::object params{};
    std::chrono::steady_clock::time_point created{
        std::chrono::steady_clock::now()};

    // Native promise for co_await (suspends thread via kernel)
    std::shared_ptr<std::promise<boost::json::object>> promise{};
    std::shared_ptr<boost::asio::steady_timer> timer{};
    // Optional completion callback invoked on strand when response arrives
    // Signature: (success, result, error_code, error_message)
    std::function<void(bool, const boost::json::object &, const std::string &,
                       const std::string &)>
        on_complete{};
};

/**
 * @brief BiDi session with zero busy-wait architecture
 *
 * Threading model:
 * - WebSocket I/O: async_read/write suspend on epoll/kqueue/IOCP
 * - JSON parsing: posted to CPU thread pool
 * - State management: serialized via strand (no mutex)
 * - Timeouts: steady_timer with native cancellation
 * - Awaiting: std::future suspends thread until promise completion
 */
class ThreadedBiDiSession
    : public std::enable_shared_from_this<ThreadedBiDiSession> {
  public:
    explicit ThreadedBiDiSession(std::shared_ptr<ThreadingContext> threading);
    ~ThreadedBiDiSession() noexcept;

    // Non-copyable, non-movable
    ThreadedBiDiSession(const ThreadedBiDiSession &) = delete;
    ThreadedBiDiSession &operator=(const ThreadedBiDiSession &) = delete;
    ThreadedBiDiSession(ThreadedBiDiSession &&) = delete;
    ThreadedBiDiSession &operator=(ThreadedBiDiSession &&) = delete;

    // Connect with native async (no polling)
    [[nodiscard]] auto
    async_connect(const std::string &ws_url) -> std::future<bool>;

    // Send command with native await (thread suspends until response)
    [[nodiscard]] auto send_command_await(
        const std::string &method, const boost::json::object &params = {},
        std::chrono::milliseconds timeout = std::chrono::milliseconds{
            30000}) -> boost::json::object;

    // Awaitable version for coroutines
    [[nodiscard]] auto send_command_awaitable(std::string method,
                                              boost::json::object params = {})
        -> boost::asio::awaitable<boost::json::object>;

    // Event subscription with callback
    using EventHandler =
        std::function<void(const std::string &, const boost::json::object &)>;
    void subscribe_event(const std::string &method, EventHandler handler);

    void disconnect();

    // ===================== Refcounted subscribe/unsubscribe (RAII)
    // =====================
    struct Subscription {
        std::weak_ptr<ThreadedBiDiSession> session;
        std::string method;
        std::optional<std::string> context; // empty => global
        bool active{false};

        void cancel() noexcept;
        void release() noexcept { active = false; }
        ~Subscription() noexcept;

        Subscription() = default;
        Subscription(const Subscription &) = delete;
        Subscription &operator=(const Subscription &) = delete;
        Subscription(Subscription &&other) noexcept
            : session(std::move(other.session)),
              method(std::move(other.method)),
              context(std::move(other.context)), active(other.active) {
            other.active = false;
        }
        Subscription &operator=(Subscription &&other) noexcept {
            if (this != &other) {
                cancel();
                session = std::move(other.session);
                method = std::move(other.method);
                context = std::move(other.context);
                active = other.active;
                other.active = false;
            }
            return *this;
        }
    };

    // Subscribe to one event (optionally scoped by context). Returns RAII
    // handle.
    [[nodiscard]] Subscription
    subscribe(const std::string &method,
              const std::optional<std::string> &context, EventHandler handler);

    // Batch subscribe helper: returns N handles.
    [[nodiscard]] std::vector<Subscription>
    subscribe_many(const std::vector<std::string> &methods,
                   std::optional<std::vector<std::string>> contexts,
                   const EventHandler &handler);

  private:
    void start_read_loop();
    void on_read_ok();
    void on_read_error(const boost::system::error_code &err);
    void start_write_loop();
    void process_message_on_cpu(const std::string &message);
    void complete_pending_on_strand(id_type request_id, bool success,
                                    const boost::json::object &result,
                                    const std::string &error_code = {},
                                    const std::string &error_message = {});
    void setup_timeout(id_type request_id, std::chrono::milliseconds duration);
    void publish_event(const std::string &method,
                       const boost::json::object &params);

    // Non-blocking send: registers pending and invokes on_complete on strand
    void send_command_async(
        const std::string &method, const boost::json::object &params,
        std::chrono::milliseconds timeout,
        std::function<void(bool, const boost::json::object &,
                           const std::string &, const std::string &)>
            on_complete);

    // Internal subscribe/unsubscribe (strand-only). Returns whether wire call
    // was issued.
    void incr_subscription_on_strand(const std::string &method,
                                     const std::optional<std::string> &context,
                                     EventHandler handler);
    void decr_subscription_on_strand(const std::string &method,
                                     const std::optional<std::string> &context);
    // methods is taken by value so callers can move temporaries in and we
    // avoid extra copies when capturing into async callbacks.
    void send_subscribe_wire(std::vector<std::string> methods,
                             const std::vector<std::string> *contexts_opt);

    // Helpers extracted from send_subscribe_wire to reduce complexity and
    // make the async callback body small. These run on the strand.
    void
    apply_subscribe_success_on_strand(const std::vector<std::string> &methods);
    void revert_subscribe_failure_on_strand(
        const std::vector<std::string> &methods,
        const std::vector<std::string> &captured_contexts,
        const std::string &err_code, const std::string &err_msg);

    // Further small helpers used by the revert path to reduce function
    // complexity and make intent explicit.
    void revert_context_refcounts_on_strand(
        const std::vector<std::string> &methods,
        const std::vector<std::string> &captured_contexts);
    void
    revert_global_refcounts_on_strand(const std::vector<std::string> &methods);
    void
    clear_pending_handlers_on_strand(const std::vector<std::string> &methods);

    // Default timeout used for subscribe/unsubscribe commands. Inline so it
    // has internal linkage and can be used in translation units without ODR
    // issues.
    inline static constexpr std::chrono::milliseconds kSubscribeTimeout{5000};
    void send_unsubscribe_wire(const std::vector<std::string> &methods,
                               const std::vector<std::string> *contexts_opt);

    // Threading context
    std::shared_ptr<ThreadingContext> threading_;

    // Optional core session object to which subscribe logic is delegated
    std::shared_ptr<BiDiSession> core_;

    // Attach core after construction so core can use this transport
    void attach_core(std::shared_ptr<BiDiSession> core);

    // WebSocket stream
    std::unique_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>>
        ws_stream_;

    // Connection state
    std::atomic<bool> connected_{false};

    // I/O buffers
    boost::beast::flat_buffer read_buffer_{};
    std::deque<std::string> write_queue_{};
    std::atomic<bool> is_writing_{false};

    // Pending requests (accessed only on strand - thread safe)
    std::unordered_map<id_type, std::shared_ptr<PendingEntry>> pending_map_{};
    std::atomic<id_type> next_id_{1};

    // Pool for PendingEntry to reduce allocations in high-throughput
    std::vector<std::shared_ptr<PendingEntry>> pending_entry_pool_;
    std::mutex pending_entry_pool_mutex_;
    std::shared_ptr<PendingEntry> acquire_pending_entry();
    void recycle_pending_entry(std::shared_ptr<PendingEntry> entry);

    // Event handlers (accessed only on strand)
    std::unordered_map<std::string, std::vector<EventHandler>> event_handlers_;
    // Pending handlers waiting for wire-level subscribe confirmation
    std::unordered_map<std::string, std::vector<EventHandler>>
        pending_event_handlers_;

    // Refcounts (accessed only on strand)
    std::unordered_map<std::string, std::size_t> global_refcount_;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::size_t>>
        context_refcount_; // context -> (method -> count)
};

} // namespace bidi::core

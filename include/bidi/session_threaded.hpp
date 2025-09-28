// include/bidi/session_threaded.hpp — BiDi session with native suspension
#pragma once

#include "bidi/core.hpp"
#include "bidi/threading.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <future>
#include <memory>
#include <unordered_map>

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
    ~ThreadedBiDiSession();

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
    [[nodiscard]] auto
    send_command_awaitable(const std::string &method,
                           const boost::json::object &params = {})
        -> boost::asio::awaitable<boost::json::object>;

    // Event subscription with callback
    using EventHandler =
        std::function<void(const std::string &, const boost::json::object &)>;
    void subscribe_event(const std::string &method, EventHandler handler);

    void disconnect();

  private:
    void start_read_loop();
    void start_write_loop();
    void process_message_on_cpu(const std::string &message);
    void complete_pending_on_strand(id_type request_id, bool success,
                                    const boost::json::object &result,
                                    const std::string &error_code = {},
                                    const std::string &error_message = {});
    void setup_timeout(id_type request_id, std::chrono::milliseconds duration);
    void publish_event(const std::string &method,
                       const boost::json::object &params);

    // Threading context
    std::shared_ptr<ThreadingContext> threading_;

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

    // Event handlers (accessed only on strand)
    std::unordered_map<std::string, std::vector<EventHandler>> event_handlers_;
};

} // namespace bidi::core

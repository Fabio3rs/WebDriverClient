#pragma once
/**
 * @file core.hpp
 * @brief Core types and low-level BiDi protocol primitives.
 *
 * This header defines the minimal, spec-aligned core for the BiDi client:
 * - id_type and MAX_SAFE_ID (compatibility with IEEE-754 JavaScript safe
 *   integer range)
 * - fast-path message kind detection used by the router to avoid full JSON
 *   materialization on the hot path
 * - helpers to build commands and parse responses/events
 *
 * Architectural rationale:
 * - Fast-path routing: scans incoming frames to decide Response vs Event with
 *   a light-weight scan instead of allocating/parsing the whole DOM. This is
 *   a key hot-path optimization to reduce allocations and latency.
 * - ID safety: internal storage uses a 64-bit counter (`id_type`) for reuse
 *   and monotonicity; when serializing to wire code must consider
 *   MAX_SAFE_ID to remain interoperable with JS runtimes.
 * - Pending map & timer race: requests are registered in the pending map and
 *   raced with a steady_timer (deterministic timeouts via kernel primitives).
 *   The PendingEntry::timer_generation exists to prevent stale timer callbacks
 *   from affecting newer requests that reuse the same entry object.
 * - Zero busy-wait: All async operations use native kernel suspension via
 *   Boost.Asio primitives (epoll/kqueue/IOCP). No polling loops or sleep.
 * - Write queue serialization: Beast best practice ensures only one
 *   async_write active at a time, preventing overlapping writes.
 * - Arena allocation: JSON parsing uses boost::json::monotonic_resource for
 *   bulk allocation/deallocation. CRITICAL SAFETY RULE: No string_view or
 *   pointer from arena memory may escape the message handler scope; consumers
 *   must materialize copies if values need to outlive the parse operation.
 * - Pool-based allocation: Memory/buffer/timer/pending_entry pools reduce
 *   allocation overhead and improve cache locality under high load.
 *
 * Threading model expectations:
 * - Classes in this header assume they are used from a strand-serialized
 *   context (see `threading.hpp` for helpers). Public methods that can be
 *   called from other threads will document required synchronization.
 * - All WebSocket state transitions occur on the strand to eliminate mutexes.
 */

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "asyncx.hpp"
#include "bidi/types/core.hpp"

namespace bidi::core {

// Spec: BiDi message ID type (safe integer range 2^53-1)
using id_type = std::uint64_t;
static constexpr std::uint64_t MAX_SAFE_ID = 9007199254740991ULL; // 2^53-1

// Spec: Message envelope types
enum class MessageKind {
    Command,  // {id, method, params}
    Response, // {id, result} or {id, error, message}
    Event,    // {type: "event", method, params}
    Unknown
};

// Spec: Fast message kind detection (no full JSON parse)
[[nodiscard]] auto detect_message_kind(std::string_view payload) noexcept
    -> MessageKind;

// Spec: Check if ID is within safe integer range
[[nodiscard]] constexpr auto is_id_safe(std::uint64_t id) noexcept -> bool {
    return id <= MAX_SAFE_ID;
}

// Spec: Build command message {id, method, params}
[[nodiscard]] auto build_command(id_type id_value, std::string_view method,
                                 const boost::json::object &params = {})
    -> std::string;

// Spec: Parsed response structure
struct ParsedResponse {
    id_type id{};
    bool is_success{};
    boost::json::object result;
    std::optional<bidi::ErrorCode>
        error_code;             // Strong-typed W3C BiDi error code
    std::string error_code_raw; // Raw error string for unknown/future codes
    std::string error_message;
    std::string stacktrace; // W3C BiDi: optional, execution stack on errors
    // Additional fields for traceability/telemetry
    std::string method;   // original command method
    std::string trace_id; // locally generated trace id for correlation
    std::string raw_json; // raw received payload
    std::chrono::steady_clock::duration
        latency{};               // duration between send and response
    bool timeout_expired{false}; // true if locally constructed due to timeout
};

[[nodiscard]] auto parse_response(std::string_view payload)
    -> std::optional<ParsedResponse>;

// Spec: Parsed event structure
struct ParsedEvent {
    std::string method;
    boost::json::object params;
};

[[nodiscard]] auto parse_event(std::string_view payload)
    -> std::optional<ParsedEvent>;

// ======================== WebSocket Transport ========================

namespace net = boost::asio;
namespace beast = boost::beast;
namespace ws = beast::websocket;

/**
 * @brief WebSocket client with strand-serialized state for BiDi protocol
 *
 * @section Thread Safety
 * ALL public methods except get_executor() must be called from strand context.
 * The strand is available via get_executor().
 *
 * Member access rules:
 * - write_queue_, is_writing_, handlers: STRAND-ONLY
 * - Connection state (host_, port_, target_): Set once during connect
 * - pending_responses_, event_handlers_, event_refcount_: STRAND-ONLY
 */
class WebSocketClient : public std::enable_shared_from_this<WebSocketClient> {
  public:
    using MessageHandler = std::function<void(std::string)>;
    using ErrorHandler = std::function<void(boost::system::error_code)>;
    using ConnectHandler = std::function<void(boost::system::error_code)>;

    explicit WebSocketClient(net::io_context &ioc);

    // Parse WebSocket URL and connect
    template <class CompletionToken>
    auto async_connect(std::string_view url, CompletionToken &&token);

    // Send message with write queue serialization
    template <class CompletionToken>
    auto async_send(std::string message, CompletionToken &&token);

    // Set handlers
    void set_message_handler(MessageHandler handler) {
        on_message_ = std::move(handler);
    }
    void set_error_handler(ErrorHandler handler) {
        on_error_ = std::move(handler);
    }

    // Start continuous read loop
    void start_read_loop();

    // Close the websocket (best-effort)
    void close();

    // Get executor for posting callbacks
    /**
     * @brief Get executor for async operations
     * @return Executor by value (safe for capture in lambdas)
     */
    auto get_executor() const -> net::any_io_executor {
        return strand_.get_inner_executor();
    }

  private:
    void do_read();
    void do_write();
    void on_resolve(const boost::system::error_code &error_code,
                    const net::ip::tcp::resolver::results_type &results);
    void on_connect(const boost::system::error_code &error_code,
                    const net::ip::tcp::endpoint &endpoint);
    void on_handshake(const boost::system::error_code &error_code);

    net::strand<net::io_context::executor_type> strand_;
    net::ip::tcp::resolver resolver_;
    ws::stream<beast::tcp_stream> ws_;
    beast::flat_buffer read_buffer_;

    // Connection state
    std::string host_;
    std::string port_;
    std::string target_;
    ConnectHandler connect_handler_;

    // Write queue (Beast best practice: serialize writes)
    // Each queued entry stores the message payload and an optional
    // completion handler which will be invoked when the underlying
    // async write completes. This ensures callers are notified of the
    // real transport result instead of a fire-and-forget acknowledge.
    using WriteCompletionHandler =
        std::function<void(const boost::system::error_code &)>;
    std::deque<std::pair<std::string, WriteCompletionHandler>> write_queue_;
    bool is_writing_{false};

    // Event handlers
    MessageHandler on_message_;
    ErrorHandler on_error_;
};

// ======================== BiDi Session ========================

// BiDi session manager: handles pending requests and events
class BiDiSession : public std::enable_shared_from_this<BiDiSession> {
  public:
    using ResponseHandler = std::function<void(ParsedResponse)>;
    using EventHandler = std::function<void(ParsedEvent)>;

    // RAII subscription handle (auto-unsubscribes on destruction)
    class Subscription {
      public:
        Subscription() = default;
        ~Subscription() noexcept;
        Subscription(const Subscription &) = delete;
        auto operator=(const Subscription &) -> Subscription & = delete;
        Subscription(Subscription &&other) noexcept;
        auto operator=(Subscription &&other) noexcept -> Subscription &;

        void cancel() noexcept;
        // Detach: prevents refcount decrement on destruction
        void release() noexcept { active_ = false; }
        [[nodiscard]] auto is_active() const noexcept -> bool {
            return active_;
        }
        [[nodiscard]] auto subscription_id() const noexcept
            -> const std::string & {
            return subscription_id_;
        }

      private:
        friend class BiDiSession;
        std::weak_ptr<BiDiSession> session_;
        std::string method_;
        // nullopt => global; otherwise specific contexts
        std::optional<std::vector<std::string>> contexts_;
        // identify specific handler to remove
        std::shared_ptr<EventHandler> handler_ptr_;
        std::string subscription_id_;
        bool active_{false};
    };

    explicit BiDiSession(std::shared_ptr<WebSocketClient> websocket);
    // Default constructible when transport will be injected later
    BiDiSession() = default;

    // Send command and register response handler
    void
    send_command(std::string_view method, const boost::json::object &params,
                 ResponseHandler handler,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds{
                     kDefaultTimeout});

    // Awaitable version for coroutines
    [[nodiscard]] auto send_command_awaitable(
        std::string_view method, boost::json::object params,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{
            kDefaultTimeout}) -> boost::asio::awaitable<ParsedResponse>;

    // default timeout used across BiDi core for request operations
    static constexpr std::chrono::milliseconds kDefaultTimeout{5000};

    // Transport sender injection for async non-blocking sends used by core
    using TransportSender = std::function<void(
        const std::string &method, const boost::json::object &params,
        std::chrono::milliseconds timeout,
        std::function<void(bool, const boost::json::object &,
                           const std::string &, const std::string &)>
            on_complete)>;

    void set_transport_sender(TransportSender sender) {
        transport_sender_ = std::move(sender);
    }

    // Subscribe to events (global scope). Returns RAII handle.
    [[nodiscard]] auto subscribe_event(std::string_view event_method,
                                       EventHandler handler)
        -> asyncx::Async<std::shared_ptr<Subscription>>;
    [[nodiscard]] auto subscribe_event_awaitable(std::string event_method,
                                                 EventHandler handler)
        -> boost::asio::awaitable<Subscription>;
    [[nodiscard]] auto subscribe_event_async(std::string event_method,
                                             EventHandler handler)
        -> asyncx::Async<std::shared_ptr<Subscription>>;
    void unsubscribe_event(const std::string &event_method);

    // Subscribe to events scoped by contexts (deduped by refcount per context)
    [[nodiscard]] auto
    subscribe_event_scoped(const std::string &event_method,
                           const std::vector<std::string> &contexts,
                           EventHandler handler) -> Subscription;
    void unsubscribe_event_scoped(const std::string &event_method,
                                  const std::vector<std::string> &contexts);

    // Get executor for async operations
    auto get_executor() const -> net::any_io_executor;

    // Remove handler from event list (MUST be called from strand context for
    // thread-safety) WARNING: This is a low-level method - only call from
    // within strand execution context
    void remove_handler_from_event_list_unsafe(
        const std::string &method,
        const std::shared_ptr<EventHandler> &handler_ptr);

    // Start session (connect WebSocket and begin message processing)
    template <class CompletionToken>
    auto async_start(std::string_view websocket_url, CompletionToken &&token);

    // Graceful disconnect/close of the underlying WebSocket (non-blocking)
    void disconnect();

    // Pending requests: id -> response handler
    struct PendingEntry {
        ResponseHandler handler;
        std::string method;
        std::string trace_id; // propagated for logging correlation
        net::steady_timer timer;

        /// Generation counter for timer lifecycle (prevents stale timer
        /// callbacks)
        ///
        /// @invariant Lifecycle states:
        /// - 0: Timer never armed (initial state after construction)
        /// - >=1: Valid timer generation (incremented on each timer arm)
        ///
        /// @pattern On timer arm: increment generation, capture value, register
        /// async_wait
        /// @pattern In timer callback: compare captured generation with current
        /// value
        ///
        /// @rationale Prevents race condition where timer fires after entry is:
        /// - Completed and reused for different request
        /// - Cancelled and timer is rearmed
        /// - Entry removed and ID recycled (though atomic counter prevents
        /// this)
        ///
        /// @note Overflow behavior: uint64_t provides 2^64 distinct values.
        /// At 1 billion increments/second, overflow takes ~584 years.
        /// Even if overflow occurs, comparison by equality (captured_gen ==
        /// current_gen) remains correct because we only compare within the
        /// lifetime of a single PendingEntry. Once entry is erased and
        /// recreated, generation resets to 0. Therefore, overflow is not a
        /// practical concern and does not require special handling.
        ///
        /// @see ThreadedBiDiSession::PendingEntry for threaded session
        /// equivalent
        std::uint64_t timer_generation{0};
        std::chrono::steady_clock::time_point start;
    };

  private:
    void on_message(const std::string &payload);
    void on_error(const boost::system::error_code &error_code);

    // Helper to satisfy and clear stored subscribe promises for an event
    void satisfy_subscribe_promises(const std::string &event_method,
                                    bool success);

    // Revert global event refcount for an event on failure
    void
    revert_global_event_refcount_on_failure(const std::string &event_method);

    // Revert context-scoped refcounts for a list of contexts on failure
    void revert_context_refcounts_on_failure(
        const std::string &event_method,
        const std::vector<std::string> &contexts);

    // Send subscribe over the wire for a global event (0->1 transition)
    void send_subscribe_wire_global(const std::string &event_method);

    // Send subscribe over the wire for a scoped event (contexts list moved)
    void send_subscribe_wire_scoped(const std::string &event_method,
                                    std::vector<std::string> contexts);

    std::shared_ptr<WebSocketClient> ws_;
    std::atomic<id_type> next_id_{1ULL};

    std::unordered_map<id_type, PendingEntry> pending_responses_;

    // Event subscriptions: method -> handlers (multiple subscribers supported)
    std::unordered_map<std::string, std::vector<std::shared_ptr<EventHandler>>>
        event_handlers_;
    // Global refcount per method to dedupe wire subscribe/unsubscribe
    std::unordered_map<std::string, std::size_t> event_refcount_;
    // Context-scoped refcounts: context -> (method -> count)
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::size_t>>
        context_event_refcount_;
    // Pending subscribe confirmation promises per event method
    std::unordered_map<std::string,
                       std::vector<std::shared_ptr<std::promise<bool>>>>
        subscribe_promises_;
    // Exposed shared futures map cache (optional)
    std::unordered_map<std::string, std::shared_future<bool>>
        subscribe_futures_;
    // Callback-based subscribers for confirmation (non-blocking resume)
    std::unordered_map<std::string, std::vector<std::function<void(bool)>>>
        subscribe_callbacks_;

    // Active subscription IDs per event method (for unsubscribe operations)
    std::unordered_map<std::string, std::string> active_subscriptions_;

    // Optional transport sender used by core to send async commands via
    // the underlying transport implementation (e.g., ThreadedBiDiSession).
    TransportSender transport_sender_;

#ifdef BIDI_TESTING
  public:
    /**
     * @brief TEST ONLY: Inject message bypassing WebSocket
     *
     * @warning Must be called from strand context for thread safety.
     *          Only for unit tests - do not use in production code.
     *
     * @param payload JSON message to inject
     */
    void test_inject_message(std::string payload) {
        on_message(std::move(payload));
    }

    /**
     * @brief TEST ONLY: Inject error bypassing WebSocket
     * @warning Must be called from strand context.
     * @param error_code Error to inject
     */
    void test_inject_error(const boost::system::error_code &error_code) {
        on_error(error_code);
    }

    /**
     * @brief TEST ONLY: Get pending responses count
     * @return Number of pending requests
     */
    [[nodiscard]] std::size_t test_pending_size() const {
        return pending_responses_.size();
    }

    /**
     * @brief TEST ONLY: Reserve capacity in pending_responses_ map
     *
     * @warning Must be called from strand context for thread safety.
     *          Only for stress tests to reduce allocations under high load.
     *
     * @param capacity Number of entries to reserve capacity for
     *
     * @rationale In stress tests with 1000+ concurrent requests, pre-allocating
     *            map capacity avoids multiple rehashing operations and improves
     *            performance. Not needed in production where load is typically
     *            lower and gradual growth is acceptable.
     */
    void test_reserve_pending(std::size_t capacity) {
        pending_responses_.reserve(capacity);
    }
#endif
};

// ======================== Template Implementations ========================

template <class CompletionToken>
auto WebSocketClient::async_connect(std::string_view url,
                                    CompletionToken &&token) {
    auto wrapper = [this, url = std::string(url)](auto &&handler) {
        // Parse WebSocket URL: ws://host:port/path
        connect_handler_ = std::forward<decltype(handler)>(handler);

        std::string url_str = url.starts_with("ws://")
                                  ? std::string(url.substr(5))
                                  : std::string(url);
        auto slash_pos = url_str.find('/');
        auto host_port = (slash_pos != std::string::npos)
                             ? url_str.substr(0, slash_pos)
                             : url_str;
        target_ =
            (slash_pos != std::string::npos) ? url_str.substr(slash_pos) : "/";

        auto colon_pos = host_port.find(':');
        if (colon_pos != std::string::npos) {
            host_ = host_port.substr(0, colon_pos);
            port_ = host_port.substr(colon_pos + 1);
        } else {
            host_ = host_port;
            port_ = "80";
        }

        resolver_.async_resolve(
            host_, port_, [self = shared_from_this()](auto ec, auto results) {
                self->on_resolve(ec, results);
            });
    };

    return net::async_initiate<CompletionToken,
                               void(boost::system::error_code)>(wrapper, token);
}

template <class CompletionToken>
auto WebSocketClient::async_send(std::string message, CompletionToken &&token) {
    auto wrapper = [this,
                    message = std::move(message)](auto &&handler) mutable {
        net::post(strand_, [this, message = std::move(message),
                            handler = std::forward<decltype(handler)>(
                                handler)]() mutable {
            // Store the message and its completion handler together so the
            // handler can be invoked with the real transport result once
            // the async write completes.
            write_queue_.emplace_back(
                std::move(message),
                WriteCompletionHandler{
                    [h = std::move(handler)](
                        const boost::system::error_code &ec) mutable {
                        h(ec);
                    }});

            if (!is_writing_) {
                do_write();
            }
        });
    };

    return net::async_initiate<CompletionToken,
                               void(boost::system::error_code)>(wrapper, token);
}

template <class CompletionToken>
auto BiDiSession::async_start(std::string_view websocket_url,
                              CompletionToken &&token) {
    auto wrapper = [this, websocket_url = std::string(websocket_url)](
                       auto &&handler) mutable {
        ws_->set_message_handler(
            [this](const std::string &msg) { on_message(msg); });
        ws_->set_error_handler([this](auto ec) { on_error(ec); });

        ws_->async_connect(websocket_url,
                           [handler = std::forward<decltype(handler)>(handler)](
                               auto ec) mutable {
                               if (!ec) {
                                   // Connection successful, start read loop
                                   // Note: ws_ is now managed by this
                                   // BiDiSession
                               }
                               handler(ec);
                           });
    };

    return net::async_initiate<CompletionToken,
                               void(boost::system::error_code)>(wrapper, token);
}

} // namespace bidi::core

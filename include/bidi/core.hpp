// bidi/core.hpp — WebDriver BiDi Core (W3C spec-compliant)
#pragma once

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
[[nodiscard]] MessageKind
detect_message_kind(std::string_view payload) noexcept;

// Spec: Check if ID is within safe integer range
[[nodiscard]] constexpr bool is_id_safe(std::uint64_t id) noexcept {
    return id <= MAX_SAFE_ID;
}

// Spec: Build command message {id, method, params}
[[nodiscard]] std::string build_command(id_type id_value,
                                        std::string_view method,
                                        const boost::json::object &params = {});

// Spec: Parsed response structure
struct ParsedResponse {
    id_type id{};
    bool is_success{};
    boost::json::object result{};
    std::string error_code{};
    std::string error_message{};
    // Campos adicionais para rastreabilidade/telemetria
    std::string method{};   // método original do comando
    std::string trace_id{}; // trace id gerado localmente para correlação
    std::string raw_json{}; // payload bruto recebido
    std::chrono::steady_clock::duration
        latency{};               // duração entre envio e resposta
    bool timeout_expired{false}; // true se construído localmente por timeout
};

[[nodiscard]] std::optional<ParsedResponse>
parse_response(std::string_view payload);

// Spec: Parsed event structure
struct ParsedEvent {
    std::string method{};
    boost::json::object params{};
};

[[nodiscard]] std::optional<ParsedEvent> parse_event(std::string_view payload);

// ======================== WebSocket Transport ========================

namespace net = boost::asio;
namespace beast = boost::beast;
namespace ws = beast::websocket;

// WebSocket client for BiDi protocol (Beast/Asio based)
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
    net::any_io_executor get_executor() const {
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
    std::deque<std::string> write_queue_;
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
        Subscription &operator=(const Subscription &) = delete;
        Subscription(Subscription &&other) noexcept;
        Subscription &operator=(Subscription &&other) noexcept;

        void cancel() noexcept;
        // Detach: prevents refcount decrement on destruction
        void release() noexcept { active_ = false; }
        [[nodiscard]] bool is_active() const noexcept { return active_; }
        // Blocking wait for confirmation from the remote server. Returns
        // std::nullopt if no confirmation future is available or the wait
        // timed out; otherwise returns the boolean result of the subscribe
        // (true == subscribed successfully).
        [[nodiscard]] std::optional<bool>
        wait_confirmed(std::chrono::milliseconds timeout) const noexcept;

      private:
        friend class BiDiSession;
        std::weak_ptr<BiDiSession> session_;
        std::string method_;
        // nullopt => global; otherwise specific contexts
        std::optional<std::vector<std::string>> contexts_;
        // identify specific handler to remove
        std::shared_ptr<EventHandler> handler_ptr_;
        bool active_{false};
        // Future que é satisfeito quando o servidor confirma o subscribe
        std::shared_future<bool> confirmed_;
        [[nodiscard]] std::shared_future<bool> confirmed() const noexcept {
            return confirmed_;
        }
        // Coroutine support: co_await subscription to wait for confirmation
        struct Awaiter {
            std::shared_future<bool> fut;
            std::weak_ptr<BiDiSession> session;
            std::atomic<bool> result{false};

            bool await_ready() const noexcept {
                if (!fut.valid())
                    return true; // no confirmation available
                return fut.wait_for(std::chrono::seconds{0}) ==
                       std::future_status::ready;
            }

            void await_suspend(std::coroutine_handle<> h) {
                // Wait on a background thread and resume on the session
                // executor
                std::thread([fut = this->fut, session = this->session, h,
                             self = this]() mutable {
                    try {
                        fut.wait();
                        self->result.store(fut.get(),
                                           std::memory_order_relaxed);
                    } catch (...) {
                        self->result.store(false, std::memory_order_relaxed);
                    }
                    if (auto s = session.lock()) {
                        net::post(s->get_executor(),
                                  [h]() mutable { h.resume(); });
                    } else {
                        h.resume();
                    }
                }).detach();
            }

            bool await_resume() const noexcept {
                return result.load(std::memory_order_relaxed);
            }
        };

        [[nodiscard]] Awaiter operator co_await() const noexcept {
            return Awaiter{confirmed_, session_};
        }
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

    // default timeout used across BiDi core for request operations
    static inline constexpr std::chrono::milliseconds kDefaultTimeout{5000};

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
    [[nodiscard]] Subscription subscribe_event(const std::string &event_method,
                                               EventHandler handler);
    void unsubscribe_event(const std::string &event_method);

    // Subscribe to events scoped by contexts (deduped by refcount per context)
    [[nodiscard]] Subscription
    subscribe_event_scoped(const std::string &event_method,
                           const std::vector<std::string> &contexts,
                           EventHandler handler);
    void unsubscribe_event_scoped(const std::string &event_method,
                                  const std::vector<std::string> &contexts);

    // Get executor for async operations
    net::any_io_executor get_executor() const;

    // Start session (connect WebSocket and begin message processing)
    template <class CompletionToken>
    auto async_start(std::string_view websocket_url, CompletionToken &&token);

    // Graceful disconnect/close of the underlying WebSocket (non-blocking)
    void disconnect();

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

    // Pending requests: id -> response handler
    struct PendingEntry {
        ResponseHandler handler;
        std::string method;
        std::string trace_id; // propagated for logging correlation
        net::steady_timer timer;
        std::chrono::steady_clock::time_point start;
    };
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

    // Optional transport sender used by core to send async commands via
    // the underlying transport implementation (e.g., ThreadedBiDiSession).
    TransportSender transport_sender_{};

#ifdef BIDI_TESTING
  public:
    // Hooks expostos somente em builds de teste para injeção controlada.
    void test_inject_message(std::string payload) {
        on_message(std::move(payload));
    }
    void test_inject_error(const boost::system::error_code &error_code) {
        on_error(error_code);
    }
    std::size_t test_pending_size() const { return pending_responses_.size(); }
#endif
};

// ======================== Template Implementations ========================

template <class CompletionToken>
auto WebSocketClient::async_connect(std::string_view url,
                                    CompletionToken &&token) {
    auto wrapper = [this, url = std::string(url)](auto &&handler) {
        // Parse WebSocket URL: ws://host:port/path
        connect_handler_ = std::move(handler);

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
                            handler = std::move(handler)]() mutable {
            write_queue_.emplace_back(std::move(message));
            if (!is_writing_) {
                do_write();
            }
            // For simplicity, call handler immediately (fire-and-forget)
            // In production, you'd track completion per message
            net::post(strand_, [handler = std::move(handler)]() mutable {
                handler(boost::system::error_code{});
            });
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
            [this](std::string msg) { on_message(std::move(msg)); });
        ws_->set_error_handler([this](auto ec) { on_error(ec); });

        ws_->async_connect(websocket_url,
                           [handler = std::move(handler)](auto ec) mutable {
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

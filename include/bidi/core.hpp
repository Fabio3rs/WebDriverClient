// bidi/core.hpp — WebDriver BiDi Core (W3C spec-compliant)
#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
[[nodiscard]] std::string build_command(id_type id, std::string_view method,
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

    explicit BiDiSession(std::shared_ptr<WebSocketClient> ws);

    // Send command and register response handler
    void send_command(
        std::string_view method, const boost::json::object &params,
        ResponseHandler handler,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

    // Subscribe to events
    void subscribe_event(const std::string &event_method, EventHandler handler);
    void unsubscribe_event(const std::string &event_method);

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

    // Event subscriptions: method -> handler
    std::unordered_map<std::string, EventHandler> event_handlers_;

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

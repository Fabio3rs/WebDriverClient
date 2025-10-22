/// @file bidi_core.cpp
/// @brief W3C WebDriver BiDi protocol implementation
/// @note Thread-safe async operations using Boost.Asio strand pattern
/// @see https://w3c.github.io/webdriver-bidi/
#include "asyncx.hpp"
#include "bidi/core.hpp"
#include "bidi/logging.hpp"
#include <algorithm>
#include <boost/json/serialize.hpp>
#include <charconv>
#include <format>
#include <memory>

namespace bidi::core {

/// Configuration constants
namespace {
/// @brief Maximum allowed WebSocket message payload (DoS protection)
/// @note Set to 10 MB to prevent memory exhaustion attacks
constexpr std::size_t MAX_PAYLOAD_SIZE = 10ULL * 1024ULL * 1024ULL;
} // anonymous namespace

/// @brief Parse command/response ID from JSON object
/// @param obj Parsed JSON object containing "id" field
/// @return ID value if valid (int64 or numeric string), nullopt on error
/// @note Supports both integer and string ID formats per W3C BiDi spec
/// @note Validates non-negative constraint and JavaScript safe integer range
static auto parse_id_from_object(const boost::json::object &obj) noexcept
    -> std::optional<id_type> {
    const auto *it_id = obj.find("id");
    if (it_id == obj.end()) {
        return std::nullopt;
    }
    const auto &val = it_id->value();

    id_type parsed_id = 0;

    if (val.is_int64()) {
        auto int_val = val.as_int64();
        /// @note W3C BiDi Spec: IDs must be non-negative integers
        if (int_val < 0) {
            bidi::logging::log_error("Received negative ID in BiDi message");
            return std::nullopt;
        }
        parsed_id = static_cast<id_type>(int_val);
    } else if (val.is_string()) {
        const auto str = val.as_string();
        unsigned long long tmp = 0ULL;
        auto parse_res = std::from_chars(str.begin(), str.end(), tmp);
        if (parse_res.ec != std::errc() || parse_res.ptr != str.end()) {
            bidi::logging::log_error(
                std::format("Failed to parse ID from string: {}", str.c_str()));
            return std::nullopt;
        }
        parsed_id = static_cast<id_type>(tmp);
    } else {
        bidi::logging::log_error(
            "ID field has invalid type (not int64 or string)");
        return std::nullopt;
    }

    /// @note W3C BiDi Spec: Validate ID is within JavaScript safe integer range
    /// (Number.MAX_SAFE_INTEGER = 2^53-1)
    if (!is_id_safe(parsed_id)) {
        bidi::logging::log_warning(std::format(
            "Received ID {} exceeds MAX_SAFE_ID ({})", parsed_id, MAX_SAFE_ID));
        // Accept but log warning for out-of-range IDs
    }

    return parsed_id;
}

/// @brief Fill ParsedResponse structure for success-type responses
/// @param out Output ParsedResponse structure to populate
/// @param obj Parsed JSON response object
/// @note Sets is_success=true and extracts "result" object (or empty object if
/// missing)
static void fill_response_success(ParsedResponse &out,
                                  const boost::json::object &obj) noexcept {
    out.is_success = true;
    const auto *result_it = obj.find("result");
    out.result = (result_it != obj.end() && result_it->value().is_object())
                     ? result_it->value().as_object()
                     : boost::json::object{};
}

/**
 * @brief Fill ParsedResponse structure for error-type responses.
 * @param out Output ParsedResponse structure to populate.
 * @param obj Parsed JSON response object.
 * @note Maintains backward compatibility with existing tests while logging
 * warnings for missing required fields.
 */
static void fill_response_error(ParsedResponse &out,
                                const boost::json::object &obj) noexcept {
    out.is_success = false;

    /// Required field per W3C BiDi Spec: error code string
    const auto *error_it = obj.find("error");
    if (error_it != obj.end() && error_it->value().is_string()) {
        auto error_str = error_it->value().as_string();
        out.error_code_raw = std::string(error_str.c_str());
        // Parse to ErrorCode enum for type safety
        out.error_code = bidi::types::core::parse_error_code(error_str);
    } else {
        // Maintains backward compatibility with existing tests but logs warning
        bidi::logging::log_warning(
            "Error response missing required 'error' field");
        out.error_code_raw = std::string{};
        out.error_code = std::nullopt;
    }

    /// Required field per W3C BiDi Spec: human-readable error message
    const auto *message_it = obj.find("message");
    if (message_it != obj.end() && message_it->value().is_string()) {
        out.error_message =
            std::string(message_it->value().as_string().c_str());
    } else {
        // Maintains backward compatibility with existing tests but logs warning
        bidi::logging::log_warning(
            "Error response missing required 'message' field");
        out.error_message = std::string{};
    }

    /// Optional field per W3C BiDi Spec: stack trace string
    const auto *stack_it = obj.find("stacktrace");
    if (stack_it != obj.end() && stack_it->value().is_string()) {
        out.stacktrace = std::string(stack_it->value().as_string().c_str());
    }
}

/// @brief Fast message kind detection using W3C BiDi spec patterns
/// @param payload JSON string to classify
/// @return MessageKind enum: Event, Response, Command, or Unknown
/// @note Only parses root-level fields to avoid false positives from nested
/// structures
auto detect_message_kind(std::string_view payload) noexcept -> MessageKind {
    try {
        // Parse only root level to avoid false positives from nested JSON
        auto parsed = boost::json::parse(payload);
        if (!parsed.is_object()) {
            return MessageKind::Unknown;
        }

        const auto &obj = parsed.as_object();

        // Check "type" field at root level
        const auto *type_it = obj.find("type");
        const bool has_type =
            (type_it != obj.end() && type_it->value().is_string());
        const auto *id_it = obj.find("id");
        const bool has_id = (id_it != obj.end());
        const auto *method_it = obj.find("method");
        const bool has_method = (method_it != obj.end());

        /// W3C BiDi Spec Pattern 1: Event messages have type="event" and method
        /// field
        if (has_type && type_it->value().as_string() == "event") {
            if (!has_method) {
                bidi::logging::log_warning(
                    "Event message missing 'method' field");
            }
            return MessageKind::Event;
        }

        /// W3C BiDi Spec Pattern 2: Response messages have id +
        /// type="success"|"error"
        if (has_id && has_type) {
            auto type_str = type_it->value().as_string();
            if (type_str == "success" || type_str == "error") {
                return MessageKind::Response;
            }
        }

        /// W3C BiDi Spec Pattern 3: Command messages have id + method WITHOUT
        /// type field
        if (has_id && has_method && !has_type) {
            return MessageKind::Command;
        }

        // Log unclassified messages for debugging protocol violations
        bidi::logging::log_warning(std::format(
            "Unable to classify message kind: has_id={}, has_type={}, "
            "has_method={}",
            has_id, has_type, has_method));

        return MessageKind::Unknown;

    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::format("detect_message_kind parse exception: {}", e.what()));
        return MessageKind::Unknown;
    } catch (...) {
        bidi::logging::log_error("detect_message_kind unknown exception");
        return MessageKind::Unknown;
    }
}

/// @brief Build W3C BiDi command message with proper ID serialization
/// @param id_value Unique command identifier
/// @param method BiDi method name (e.g., "browsingContext.create")
/// @param params JSON object with method parameters
/// @return Serialized JSON string ready for transmission
/// @note IDs exceeding JavaScript MAX_SAFE_INTEGER are serialized as strings
/// for IEEE-754 compatibility
auto build_command(id_type id_value, std::string_view method,
                   const boost::json::object &params) -> std::string {
    boost::json::object cmd;
    if (is_id_safe(id_value)) {
        cmd["id"] = static_cast<std::int64_t>(id_value);
    } else {
        /// Serialize as string to preserve interoperability with IEEE-754
        /// based consumers (e.g., JavaScript) when id exceeds MAX_SAFE_ID
        cmd["id"] = std::to_string(id_value);
    }
    cmd["method"] = method;
    cmd["params"] = params;
    return boost::json::serialize(cmd);
}

/// @brief Parse W3C BiDi response message from JSON object
/// @param obj Parsed JSON object
/// @return ParsedResponse if valid success/error response, nullopt otherwise
/// @note Validates required fields per W3C BiDi specification
static auto parse_response_from_object(const boost::json::object &obj)
    -> std::optional<ParsedResponse> {
    auto id_opt = parse_id_from_object(obj);
    if (!id_opt.has_value()) {
        return std::nullopt;
    }

    ParsedResponse response;
    response.id = *id_opt;

    const auto *type_it = obj.find("type");
    if (type_it == obj.end() || !type_it->value().is_string()) {
        return std::nullopt;
    }
    auto type_str = type_it->value().as_string();

    if (type_str == "success") {
        fill_response_success(response, obj);
        return response;
    }

    if (type_str == "error") {
        fill_response_error(response, obj);
        return response;
    }

    return std::nullopt;
}

/// @brief Parse W3C BiDi response from JSON payload string
/// @param payload Raw JSON string received from WebSocket
/// @return ParsedResponse with id, type, and result/error fields, or nullopt on
/// parse failure
/// @note Thread-safe, no side effects except logging
auto parse_response(std::string_view payload) -> std::optional<ParsedResponse> {
    try {
        auto parsed = boost::json::parse(payload);
        if (!parsed.is_object()) {
            return std::nullopt;
        }
        return parse_response_from_object(parsed.as_object());
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::format("parse_response exception: {}", e.what()));
        return std::nullopt;
    } catch (...) {
        bidi::logging::log_error("parse_response unknown exception");
        return std::nullopt;
    }
}

/// @brief Parse W3C BiDi event message from JSON payload
/// @param payload Raw JSON string (type="event", method="...", params=...)
/// @return ParsedEvent with method name and optional params, or nullopt if
/// invalid
/// @note Events are server-initiated notifications and must have type="event" +
/// method field
auto parse_event(std::string_view payload) -> std::optional<ParsedEvent> {
    try {
        auto parsed = boost::json::parse(payload);
        if (!parsed.is_object()) {
            return std::nullopt;
        }

        auto &obj = parsed.as_object();

        /// W3C BiDi Spec: Events must have type="event"
        auto *type_it = obj.find("type");
        if (type_it == obj.end() || !type_it->value().is_string() ||
            type_it->value().as_string() != "event") {
            return std::nullopt;
        }

        /// W3C BiDi Spec: Events must have method field
        auto *method_it = obj.find("method");
        if (method_it == obj.end() || !method_it->value().is_string()) {
            return std::nullopt;
        }

        ParsedEvent event;
        event.method = method_it->value().as_string().c_str();

        /// Params are optional per spec but usually present
        auto *params_it = obj.find("params");
        if (params_it != obj.end() && params_it->value().is_object()) {
            event.params = params_it->value().as_object();
        }

        return event;
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::format("parse_event exception: {}", e.what()));
        return std::nullopt;
    } catch (...) {
        bidi::logging::log_error("parse_event unknown exception");
        return std::nullopt;
    }
}

// ============================================================================
// WebSocketClient Implementation - Thread-safe async WebSocket operations
// ============================================================================

/// @brief Construct WebSocketClient with strand for thread-safe async
/// operations
/// @param ioc Boost.Asio io_context for async operations
/// @note All operations are serialized through strand to prevent data races
WebSocketClient::WebSocketClient(net::io_context &ioc)
    : strand_(net::make_strand(ioc)), resolver_(strand_), ws_(strand_) {}

void WebSocketClient::start_read_loop() { do_read(); }

void WebSocketClient::do_read() {
    auto readCb = [self = shared_from_this()](
                      const boost::system::error_code &error_code,
                      std::size_t bytes_transferred) {
        if (error_code) {
            if (self->on_error_) {
                self->on_error_(error_code);
            }
            return;
        }

        /// Validate payload size (DoS protection per security guidelines)
        if (bytes_transferred > MAX_PAYLOAD_SIZE) {
            bidi::logging::log_error(
                std::format("Received payload too large: {} bytes (max: {})",
                            bytes_transferred, MAX_PAYLOAD_SIZE));
            if (self->on_error_) {
                self->on_error_(make_error_code(std::errc::message_size));
            }
            return;
        }

        /// Extract complete message from buffer
        auto data = beast::buffers_to_string(self->read_buffer_.data());
        self->read_buffer_.consume(self->read_buffer_.size());

        /// Dispatch to registered message handler (strand-serialized)
        if (self->on_message_) {
            self->on_message_(std::move(data));
        }

        /// Continue read loop (no busy-wait, async event-driven)
        self->do_read();
    };
    ws_.async_read(read_buffer_, readCb);
}

void WebSocketClient::do_write() {
    if (write_queue_.empty() || is_writing_) {
        return;
    }

    is_writing_ = true;

    // The front of the queue stores the payload and an optional
    // completion handler to be invoked once the underlying async write
    // completes. Move the payload out for the write operation and keep
    // the handler to call afterward.
    auto [payload, completion_handler] = std::move(write_queue_.front());

    auto writeCompletionHandler =
        [self = shared_from_this(),
         on_write_complete = std::move(completion_handler)](
            const boost::system::error_code &error_code, std::size_t) mutable {
            self->is_writing_ = false;

            // Invoke stored completion handler with real result
            if (on_write_complete) {
                // Call handler on the strand to keep ordering semantics
                net::post(
                    self->strand_,
                    [write_completion_cb = std::move(on_write_complete),
                     ec = error_code]() mutable { write_completion_cb(ec); });
            }

            if (error_code) {
                if (self->on_error_) {
                    self->on_error_(error_code);
                }
                return;
            }

            // Pop the entry we consumed
            self->write_queue_.pop_front();

            // Continue writing if queue not empty
            if (!self->write_queue_.empty()) {
                self->do_write();
            }
        };
    ws_.async_write(net::buffer(payload), writeCompletionHandler);
}

void WebSocketClient::on_resolve(
    const boost::system::error_code &error_code,
    const net::ip::tcp::resolver::results_type &results) {
    if (error_code) {
        if (connect_handler_) {
            connect_handler_(error_code);
        }
        // Propagate transport error to on_error_ so BiDiSession can handle
        // clearing pending responses and failing promises/futures.
        if (on_error_) {
            on_error_(error_code);
        }
        return;
    }

    beast::get_lowest_layer(ws_).async_connect(
        results,
        [self = shared_from_this()](auto connect_ec, const auto &endpoint) {
            if (connect_ec) {
                // ensure on_connect is informed and propagate via on_error_
                if (self->connect_handler_) {
                    self->connect_handler_(connect_ec);
                }
                if (self->on_error_) {
                    self->on_error_(connect_ec);
                }
                return;
            }
            self->on_connect(connect_ec, endpoint);
        });
}

void WebSocketClient::on_connect(const boost::system::error_code &error_code,
                                 const net::ip::tcp::endpoint /*unused*/ &) {
    if (error_code) {
        if (connect_handler_) {
            connect_handler_(error_code);
        }
        if (on_error_) {
            on_error_(error_code);
        }
        return;
    }

    // Set WebSocket options
    ws_.set_option(
        ws::stream_base::timeout::suggested(beast::role_type::client));
    ws_.set_option(ws::stream_base::decorator([](ws::request_type &req) {
        req.set(beast::http::field::user_agent, "bidi-core/1.0");
    }));

    // Perform WebSocket handshake
    ws_.async_handshake(host_, target_,
                        [self = shared_from_this()](auto handshake_ec) {
                            if (handshake_ec) {
                                if (self->connect_handler_) {
                                    self->connect_handler_(handshake_ec);
                                }
                                if (self->on_error_) {
                                    self->on_error_(handshake_ec);
                                }
                                return;
                            }
                            self->on_handshake(handshake_ec);
                        });
}

void WebSocketClient::on_handshake(
    const boost::system::error_code &error_code) {
    if (connect_handler_) {
        connect_handler_(error_code);
    }

    if (!error_code) {
        start_read_loop();
    }
}

// Close the websocket gracefully (best-effort)
void WebSocketClient::close() {
    try {
        // post to strand to ensure thread-safety
        auto cleanupPendingOps = [self = shared_from_this()]() {
            // best-effort: cancel pending operations and clear queues/handlers
            // to break cycles
            try {
                // cancel any pending timers or operations by closing the
                // websocket
                boost::system::error_code close_error;
                self->ws_.close(ws::close_code::normal, close_error);
                if (close_error) {
                    bidi::logging::log_error("WebSocketClient::close error",
                                             close_error, nullptr, "");
                }

                // clear write queue
                std::deque<std::pair<std::string,
                                     WebSocketClient::WriteCompletionHandler>>
                    empty;
                self->write_queue_.swap(empty);

                // clear callbacks
                self->on_message_ = {};
                self->on_error_ = {};
                self->connect_handler_ = {};
            } catch (const std::exception &e) {
                bidi::logging::log_error(
                    "WebSocketClient::close inner exception", std::error_code{},
                    std::current_exception(), "");
            }
        };
        net::post(strand_, cleanupPendingOps);
    } catch (const std::exception &e) {
        bidi::logging::log_error("WebSocketClient::close exception",
                                 std::error_code{}, std::current_exception(),
                                 "");
    }
}

// ============================================================================
// BiDiSession Implementation - W3C WebDriver BiDi protocol session management
// ============================================================================

/// @brief Construct BiDiSession with WebSocket transport
/// @param websocket Shared pointer to configured WebSocketClient
/// @note Session manages command-response correlation and event subscriptions
BiDiSession::BiDiSession(std::shared_ptr<WebSocketClient> websocket)
    : ws_(std::move(websocket)) {}

/// @brief Send BiDi command with callback-based async completion
/// @param method W3C BiDi method name (e.g., "browsingContext.create")
/// @param params JSON object with method-specific parameters
/// @param handler Callback invoked with ParsedResponse on completion/timeout
/// @param timeout Maximum wait time before triggering timeout response
/// @note Thread-safe via strand serialization; auto-generates trace_id for
/// correlation
void BiDiSession::send_command(std::string_view method,
                               const boost::json::object &params,
                               ResponseHandler handler,
                               std::chrono::milliseconds timeout,
                               const std::source_location &loc) {
    if (!ws_) {
        // WebSocket not connected, invoke handler with transport error
        ParsedResponse error_resp;
        error_resp.id = 0; // ID unknown since command not sent
        error_resp.is_success = false;
        error_resp.error_code = bidi::ErrorCode::UnknownError;
        error_resp.error_code_raw = "transport";
        error_resp.error_message = "WebSocket not connected";
        error_resp.method = std::string(method);
        error_resp.trace_id = bidi::logging::make_trace_id();
        error_resp.trace_location = loc;
        error_resp.timeout_expired = false;
        handler(std::move(error_resp));
        return;
    }

    auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
    auto start_tp = std::chrono::steady_clock::now();
    /// Generate trace_id for distributed tracing and log correlation
    auto trace_id = bidi::logging::make_trace_id();
    // Create the pending entry WITHOUT moving handler yet
    // This allows us to notify caller if emplace() fails (defense-in-depth)
    PendingEntry entry{.handler =
                           ResponseHandler{}, // empty handler placeholder
                       .method = std::string(method),
                       .trace_id = trace_id,
                       .timer = net::steady_timer(ws_->get_executor()),
                       .trace_location = loc,
                       /*timer_generation=*/.timer_generation = 0,
                       .start = start_tp};

    // CRITICAL: Emplace BEFORE arming timer to avoid race window where timer
    // fires before entry exists in map
    auto [it, inserted] = pending_responses_.emplace(id, std::move(entry));

    // R.3/CP.2: Handle emplace failure gracefully (bad_alloc or unexpected
    // collision) In production, assert may be compiled out, so we need explicit
    // check
    if (!inserted) {
        // Log critical failure with trace correlation
        bidi::logging::log_error("Failed to register pending entry (allocation "
                                 "failure or ID collision)",
                                 std::error_code{}, nullptr, trace_id);

        // FIXED: Handler was NOT moved yet, so we CAN notify caller
        // This branch should be unreachable in practice:
        // - ID collision is impossible (atomic counter guarantees uniqueness)
        // - bad_alloc would propagate as exception (not return false from
        // emplace)
        //
        // However, if we reach here (e.g., low memory edge case), notify
        // handler with error response so caller is aware of failure
        ParsedResponse error_resp;
        error_resp.id = id;
        error_resp.is_success = false;
        error_resp.error_code = bidi::ErrorCode::UnknownError;
        error_resp.error_code_raw = "internal_error";
        error_resp.error_message = "Failed to register pending entry";
        error_resp.method = std::string(method);
        error_resp.trace_id = trace_id;
        error_resp.trace_location = loc;
        error_resp.timeout_expired = false;

        // Invoke handler with error (handler is still valid, not moved)
        handler(std::move(error_resp));

        // In debug builds, assert will still catch impossible scenarios
        assert(false &&
               "ID collision should be impossible with atomic counter");
        return;
    }

    // FIXED: Move handler to entry AFTER successful emplace
    // This ensures handler is only moved if entry was successfully inserted
    it->second.handler = std::move(handler);

    // Bump generation and arm timer AFTER entry is visible in map
    ++(it->second.timer_generation);
    const auto captured_gen = it->second.timer_generation;
    it->second.timer.expires_after(timeout);
    it->second.timer.async_wait(
        [self = shared_from_this(), id, captured_gen,
         loc](const boost::system::error_code &error_code) {
            if (error_code) {
                return;
            } // timer cancelled
            auto it_timeout = self->pending_responses_.find(id);
            if (it_timeout == self->pending_responses_.end()) {
                return; // Should never happen after reordering fix
            }
            // Ignore stale timer if generation doesn't match
            if (it_timeout->second.timer_generation != captured_gen) {
                return;
            }
            ParsedResponse timeout_resp;
            timeout_resp.id = id;
            timeout_resp.is_success = false;
            timeout_resp.error_code = bidi::ErrorCode::UnknownError;
            timeout_resp.error_code_raw = "timeout";
            timeout_resp.error_message = "operation timed out";
            timeout_resp.method = it_timeout->second.method;
            timeout_resp.trace_id = it_timeout->second.trace_id;
            timeout_resp.trace_location = loc;
            timeout_resp.timeout_expired = true;
            timeout_resp.latency =
                std::chrono::steady_clock::now() - it_timeout->second.start;
            // log timeout with correlation
            bidi::logging::log_error("BiDi command timeout", std::error_code{},
                                     nullptr, timeout_resp.trace_id);
            it_timeout->second.handler(std::move(timeout_resp));
            self->pending_responses_.erase(it_timeout);
        });

    auto message = build_command(id, method, params);
    auto logTransportErr = [self = shared_from_this(), id,
                            loc](const boost::system::error_code &error_code) {
        if (!error_code) {
            return;
        }

        auto it_transport = self->pending_responses_.find(id);
        if (it_transport == self->pending_responses_.end()) {
            return;
        }

        ParsedResponse resp;
        resp.id = id;
        resp.is_success = false;
        resp.error_code = bidi::ErrorCode::UnknownError;
        resp.error_code_raw = "transport";
        resp.error_message = error_code.message();
        resp.method = it_transport->second.method;
        resp.trace_id = it_transport->second.trace_id;
        resp.trace_location = loc;
        resp.latency =
            std::chrono::steady_clock::now() - it_transport->second.start;
        // Log transport send failure with trace_id
        bidi::logging::log_error("BiDi send transport error", error_code,
                                 nullptr, resp.trace_id);
        it_transport->second.handler(std::move(resp));
        self->pending_responses_.erase(it_transport);
    };
    ws_->async_send(std::move(message), logTransportErr);
}

/// @brief Send BiDi command with C++20 coroutine-based async completion
/// @param method W3C BiDi method name
/// @param params JSON parameters (passed by value to avoid lifetime issues
/// across suspension points)
/// @param timeout Maximum wait time before timeout error
/// @return awaitable<ParsedResponse> for use with co_await
/// @note CRITICAL: params passed by value to prevent dangling reference after
/// coroutine suspension
/// @note Uses Async<T> for zero busy-wait suspension (event-driven wakeup)
/// @see NASA P10 Rule: No pointer/reference lifetime assumptions across async
/// boundaries
auto BiDiSession::send_command_awaitable(
    std::string_view method, boost::json::object params,
    std::chrono::milliseconds timeout,
    std::source_location loc) -> boost::asio::awaitable<ParsedResponse> {
    auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
    auto start_tp = std::chrono::steady_clock::now();
    auto trace_id = bidi::logging::make_trace_id();

    /// Create Async<T> for zero-busy-wait completion signaling
    auto response_async =
        asyncx::Async<ParsedResponse>::make(ws_->get_executor());

    // Create the pending entry with generation 0 (consistent with header
    // default)
    PendingEntry entry{.handler =
                           [response_async](const ParsedResponse &resp) {
                               response_async.fulfill(resp);
                           },
                       .method = std::string(method),
                       .trace_id = trace_id,
                       .timer = net::steady_timer(ws_->get_executor()),
                       .trace_location = loc,
                       /*timer_generation=*/.timer_generation = 0,
                       .start = start_tp};

    // CRITICAL: Emplace BEFORE arming timer to avoid race window
    auto [it2, inserted2] = pending_responses_.emplace(id, std::move(entry));

    // R.3/CP.2: Handle emplace failure gracefully (bad_alloc or unexpected
    // collision)
    if (!inserted2) {
        // Log critical failure with trace correlation
        bidi::logging::log_error("Failed to register pending entry (allocation "
                                 "failure or ID collision)",
                                 std::error_code{}, nullptr, trace_id);

        // Notify awaiting coroutine with internal error via Async<T>
        ParsedResponse resp;
        resp.id = id;
        resp.is_success = false;
        resp.error_code = bidi::ErrorCode::UnknownError;
        resp.error_code_raw = "internal";
        resp.error_message = "failed to allocate pending entry";
        resp.trace_id = trace_id;
        resp.trace_location = loc;
        resp.method = std::string(method);
        resp.latency = std::chrono::steady_clock::now() - start_tp;

        // Fulfill the async result with error (coroutine will resume with
        // error)
        response_async.fulfill(std::move(resp));

        // Return the error via use_awaitable adapter (converts Async<T> to
        // awaitable<T>)
        co_return co_await response_async(boost::asio::use_awaitable);
    }

    // Bump generation and arm timer AFTER entry is visible in map
    ++(it2->second.timer_generation);
    const auto captured_gen2 = it2->second.timer_generation;
    it2->second.timer.expires_after(timeout);
    it2->second.timer.async_wait(
        [self = shared_from_this(), id, captured_gen2,
         loc](const boost::system::error_code &error_code) {
            if (error_code) {
                return; // timer cancelled
            }
            auto it_timeout = self->pending_responses_.find(id);
            if (it_timeout == self->pending_responses_.end()) {
                return;
            }
            if (it_timeout->second.timer_generation != captured_gen2) {
                return;
            }
            ParsedResponse timeout_resp;
            timeout_resp.id = id;
            timeout_resp.is_success = false;
            timeout_resp.error_code = bidi::ErrorCode::UnknownError;
            timeout_resp.error_code_raw = "timeout";
            timeout_resp.error_message = "operation timed out";
            timeout_resp.method = it_timeout->second.method;
            timeout_resp.trace_id = it_timeout->second.trace_id;
            timeout_resp.trace_location = loc;
            timeout_resp.timeout_expired = true;
            timeout_resp.latency =
                std::chrono::steady_clock::now() - it_timeout->second.start;
            bidi::logging::log_error("BiDi command timeout", std::error_code{},
                                     nullptr, timeout_resp.trace_id);
            it_timeout->second.handler(std::move(timeout_resp));
            self->pending_responses_.erase(it_timeout);
        });

    auto message = build_command(id, method, params);
    ws_->async_send(
        std::move(message), [self = shared_from_this(), id,
                             loc](const boost::system::error_code &error_code) {
            if (!error_code) {
                return;
            }
            auto it_transport = self->pending_responses_.find(id);
            if (it_transport == self->pending_responses_.end()) {
                return;
            }
            ParsedResponse resp;
            resp.id = id;
            resp.is_success = false;
            resp.error_code = bidi::ErrorCode::UnknownError;
            resp.error_code_raw = "transport";
            resp.error_message = error_code.message();
            resp.method = it_transport->second.method;
            resp.trace_id = it_transport->second.trace_id;
            resp.trace_location = loc;
            resp.latency =
                std::chrono::steady_clock::now() - it_transport->second.start;
            bidi::logging::log_error("BiDi send transport error", error_code,
                                     nullptr, resp.trace_id);
            it_transport->second.handler(std::move(resp));
            self->pending_responses_.erase(it_transport);
        });

    /// Zero busy-wait: coroutine suspends until response_async is fulfilled
    /// Event-driven wakeup via Async<T> operator() returning awaitable<T>
    co_return co_await response_async(boost::asio::use_awaitable);
}

/// @brief Subscribe to W3C BiDi event with lazy async pattern (Async<T>)
/// @param event_method BiDi event name (e.g., "log.entryAdded")
/// @param handler Callback invoked when event is received
/// @return Async<Subscription> that resolves when server confirms subscription
/// @note Uses reference counting: only sends session.subscribe on first
/// subscriber
/// @note Thread-safe: all operations serialized via strand
/// @see Guia.md: Lazy evaluation pattern for composed async operations
auto BiDiSession::subscribe_event(std::string_view event_method,
                                  EventHandler handler,
                                  std::source_location loc)
    -> asyncx::Async<std::shared_ptr<BiDiSession::Subscription>> {
    auto sub = std::make_shared<Subscription>();
    sub->session_ = weak_from_this();
    sub->method_ = event_method;
    sub->contexts_ = std::nullopt;
    sub->handler_ptr_ = std::make_shared<EventHandler>(std::move(handler));
    sub->active_ = true;
    sub->set_source_location(loc);

    /// Register handler locally before sending wire protocol message
    event_handlers_[sub->method_].push_back(sub->handler_ptr_);

    /// Reference counting: only send session.subscribe when transitioning 0→1
    auto &event_ref_counter = event_refcount_[sub->method_];
    const bool need_wire = (event_ref_counter == 0);
    ++event_ref_counter;

    if (need_wire) {
        boost::json::object params;
        params["events"] = boost::json::array({sub->method_});

        // Create Async for the response
        auto response_async =
            asyncx::Async<ParsedResponse>::make(get_executor());

        // Send command with callback
        send_command(
            "session.subscribe", params,
            [response_async](const ParsedResponse &resp) {
                if (resp.is_success) {
                    response_async.fulfill(resp);
                } else {
                    // For failure, fail the async
                    response_async.fail(boost::system::error_code{});
                }
            },
            kDefaultTimeout, loc);

        // Use and_then for composition
        return response_async.and_then(
            [this, sub](const ParsedResponse &response)
                -> asyncx::Async<std::shared_ptr<Subscription>> {
                if (!response.is_success) {
                    // Revert refcount on failure
                    auto &event_ref_counter_inner =
                        event_refcount_[sub->method_];
                    --event_ref_counter_inner;
                    if (event_ref_counter_inner == 0) {
                        event_refcount_.erase(sub->method_);
                    }
                    // Remove handler on failure
                    event_handlers_[sub->method_].pop_back();

                    throw std::runtime_error(
                        std::format("session.subscribe failed: {}",
                                    response.error_message));
                }

                // Extract subscription ID from response per W3C BiDi spec
                if (const auto *it = response.result.find("subscription");
                    it != nullptr) {
                    sub->subscription_id_ =
                        std::string(it->value().get_string());
                    active_subscriptions_[sub->method_] = sub->subscription_id_;
                } else {
                    // Spec violation: server should return subscription ID
                    bidi::logging::log_warning(
                        "Server did not return subscription ID in "
                        "session.subscribe response");
                }

                bidi::logging::log_info(
                    std::format("session.subscribe ok for {} with id {}",
                                sub->method_, sub->subscription_id_));

                // Return fulfilled Async
                auto result_async =
                    asyncx::Async<std::shared_ptr<Subscription>>::make(
                        get_executor());
                result_async.fulfill(sub);
                return result_async;
            });
    }

    // If we already have a subscription for this event, copy the existing
    // subscription ID
    if (auto it = active_subscriptions_.find(sub->method_);
        it != active_subscriptions_.end()) {
        sub->subscription_id_ = it->second;
    } else {
        // This shouldn't happen, but log a warning
        bidi::logging::log_warning(std::format(
            "Multiple subscriptions to {} but no subscription ID found",
            sub->method_));
    }

    // Return fulfilled Async
    auto result_async =
        asyncx::Async<std::shared_ptr<Subscription>>::make(get_executor());
    result_async.fulfill(sub);
    return result_async;
}

/// @brief Subscribe to W3C BiDi event with C++20 coroutine pattern
/// @param event_method BiDi event name (passed by value to avoid lifetime
/// issues)
/// @param handler Callback invoked when event is received
/// @return awaitable<Subscription> for use with co_await
/// @note Uses reference counting: only sends session.subscribe on first
/// subscriber
/// @note RAII: Subscription destructor auto-unsubscribes and removes handler
/// @throws std::runtime_error if session.subscribe command fails
auto BiDiSession::subscribe_event_awaitable(std::string event_method,
                                            EventHandler handler,
                                            std::source_location loc)
    -> boost::asio::awaitable<BiDiSession::Subscription> {
    Subscription sub;
    sub.session_ = weak_from_this();
    sub.method_ = event_method;
    sub.contexts_ = std::nullopt;
    sub.handler_ptr_ = std::make_shared<EventHandler>(std::move(handler));
    sub.active_ = true;
    sub.set_source_location(loc);

    /// Register handler locally before sending wire protocol message
    event_handlers_[event_method].push_back(sub.handler_ptr_);

    /// Reference counting: only send session.subscribe when transitioning 0→1
    auto &event_ref_counter2 = event_refcount_[event_method];
    const bool need_wire = (event_ref_counter2 == 0);
    ++event_ref_counter2;

    if (need_wire) {
        boost::json::object params;
        params["events"] = boost::json::array({event_method});

        // Send command and co_await the response
        auto response = co_await send_command_awaitable(
            "session.subscribe", params, kDefaultTimeout, loc);

        if (!response.is_success) {
            // Revert refcount on failure
            --event_ref_counter2;
            if (event_ref_counter2 == 0) {
                event_refcount_.erase(event_method);
            }
            // Remove handler on failure
            event_handlers_[event_method].pop_back();

            throw std::runtime_error(std::format("session.subscribe failed: {}",
                                                 response.error_message));
        }

        // Extract subscription ID from response per W3C BiDi spec
        if (const auto *it = response.result.find("subscription");
            it != nullptr) {
            sub.subscription_id_ = std::string(it->value().get_string());
            active_subscriptions_[event_method] = sub.subscription_id_;
        } else {
            // Spec violation: server should return subscription ID
            bidi::logging::log_warning("Server did not return subscription ID "
                                       "in session.subscribe response");
        }

        bidi::logging::log_info(
            std::format("session.subscribe ok for {} with id {}", event_method,
                        sub.subscription_id_));
    } else {
        // If we already have a subscription for this event, copy the existing
        // subscription ID
        if (auto it = active_subscriptions_.find(event_method);
            it != active_subscriptions_.end()) {
            sub.subscription_id_ = it->second;
        } else {
            // This shouldn't happen, but log a warning
            bidi::logging::log_warning(std::format(
                "Multiple subscriptions to {} but no subscription ID found",
                event_method));
        }
    }

    co_return sub;
}

auto BiDiSession::subscribe_event_async(std::string event_method,
                                        EventHandler handler,
                                        std::source_location loc)
    -> asyncx::Async<std::shared_ptr<BiDiSession::Subscription>> {
    auto sub_ptr = std::make_shared<Subscription>();
    sub_ptr->session_ = weak_from_this();
    sub_ptr->method_ = event_method;
    sub_ptr->contexts_ = std::nullopt;
    sub_ptr->handler_ptr_ = std::make_shared<EventHandler>(std::move(handler));
    sub_ptr->active_ = true;
    sub_ptr->set_source_location(loc);

    // Register locally
    event_handlers_[event_method].push_back(sub_ptr->handler_ptr_);

    // Increase refcount and only send session.subscribe when transitioning 0->1
    auto &event_ref_counter3 = event_refcount_[event_method];
    const bool need_wire = (event_ref_counter3 == 0);
    ++event_ref_counter3;

    if (need_wire) {
        boost::json::object params;
        params["events"] = boost::json::array({event_method});

        // Create Async for the response
        auto response_async =
            asyncx::Async<ParsedResponse>::make(get_executor());

        // Send command with callback
        send_command(
            "session.subscribe", params,
            [response_async](const ParsedResponse &resp) {
                if (resp.is_success) {
                    response_async.fulfill(resp);
                } else {
                    // For failure, fail the async
                    response_async.fail(boost::system::error_code{});
                }
            },
            kDefaultTimeout, loc);

        // Use and_then for composition
        return response_async.and_then(
            [this, sub_ptr, event_method](const ParsedResponse &response)
                -> asyncx::Async<std::shared_ptr<Subscription>> {
                if (!response.is_success) {
                    // Revert refcount on failure
                    auto &event_ref_counter_inner2 =
                        event_refcount_[event_method];
                    --event_ref_counter_inner2;
                    if (event_ref_counter_inner2 == 0) {
                        event_refcount_.erase(event_method);
                    }
                    // Remove handler on failure
                    event_handlers_[event_method].pop_back();

                    throw std::runtime_error(
                        std::format("session.subscribe failed: {}",
                                    response.error_message));
                }

                // Extract subscription ID from response per W3C BiDi spec
                if (const auto *it = response.result.find("subscription");
                    it != nullptr) {
                    sub_ptr->subscription_id_ =
                        std::string(it->value().get_string());
                    active_subscriptions_[event_method] =
                        sub_ptr->subscription_id_;
                } else {
                    // Spec violation: server should return subscription ID
                    bidi::logging::log_warning(
                        "Server did not return subscription ID in "
                        "session.subscribe response");
                }

                bidi::logging::log_info(
                    std::format("session.subscribe ok for {} with id {}",
                                event_method, sub_ptr->subscription_id_));

                // Return fulfilled Async
                auto result_async =
                    asyncx::Async<std::shared_ptr<Subscription>>::make(
                        get_executor());
                result_async.fulfill(sub_ptr);
                return result_async;
            });
    }

    // If we already have a subscription for this event, copy the existing
    // subscription ID
    if (auto it = active_subscriptions_.find(event_method);
        it != active_subscriptions_.end()) {
        sub_ptr->subscription_id_ = it->second;
    } else {
        // This shouldn't happen, but log a warning
        bidi::logging::log_warning(std::format(
            "Multiple subscriptions to {} but no subscription ID found",
            event_method));
    }

    // Return fulfilled Async
    auto result_async =
        asyncx::Async<std::shared_ptr<Subscription>>::make(get_executor());
    result_async.fulfill(sub_ptr);
    return result_async;
}

void BiDiSession::unsubscribe_event(const std::string &event_method,
                                    std::source_location loc) {
    // Decrease refcount and only send session.unsubscribe at last drop
    auto it = event_refcount_.find(event_method);
    if (it == event_refcount_.end()) {
        // CRITICAL FIX: Validate event_handlers_ before erase to prevent
        // removing active handlers
        auto h_it = event_handlers_.find(event_method);
        if (h_it != event_handlers_.end()) {
            if (!h_it->second.empty()) {
                // If handlers remain but refcount is 0, this indicates
                // inconsistency Do NOT erase handlers - preserve them for
                // safety
                bidi::logging::log_warning(std::format(
                    "unsubscribe_event({}): refcount=0 but {} handlers remain "
                    "(possible race or leaked refcount)",
                    event_method, h_it->second.size()));
                return;
            }
            event_handlers_.erase(h_it);
        }
        return;
    }
    if (it->second > 0) {
        --(it->second);
    }
    const bool last = (it->second == 0);
    if (!last) {
        return;
    }

    event_refcount_.erase(it);
    boost::json::object params;
    params["events"] = boost::json::array({event_method});
    send_command(
        "session.unsubscribe", params,
        [](const ParsedResponse &resp) {
            if (!resp.is_success) {
                bidi::logging::log_error(std::format(
                    "session.unsubscribe failed: {}", resp.error_message));
            }
        },
        kDefaultTimeout, loc);
    event_handlers_.erase(event_method);
}

auto BiDiSession::subscribe_event_scoped(
    const std::string &event_method, const std::vector<std::string> &contexts,
    EventHandler handler, std::source_location loc) -> Subscription {
    Subscription sub;
    sub.session_ = weak_from_this();
    sub.method_ = event_method;
    sub.contexts_ = contexts;
    sub.handler_ptr_ = std::make_shared<EventHandler>(std::move(handler));
    sub.active_ = true;
    sub.set_source_location(loc);

    // Register local handler
    event_handlers_[event_method].push_back(sub.handler_ptr_);

    /// Accumulate per-context refcounts and collect contexts that need wire
    /// Pre-allocate to avoid reallocations during loop
    std::vector<std::string> need_wire_contexts;
    need_wire_contexts.reserve(contexts.size());
    for (const auto &ctx : contexts) {
        auto &method_map = context_event_refcount_[ctx];
        auto &count = method_map[event_method];
        const bool need_wire = (count == 0);
        ++count;
        if (need_wire) {
            need_wire_contexts.push_back(ctx);
        }
    }

    if (need_wire_contexts.empty()) {
        return sub;
    }

    boost::json::object params;
    params["events"] = boost::json::array({event_method});
    boost::json::array ctxs;
    for (const auto &ctx : need_wire_contexts) {
        ctxs.emplace_back(ctx);
    }
    params["contexts"] = std::move(ctxs);

    // CRITICAL FIX: Add error callback to rollback refcounts on failure
    auto handler_weak = std::weak_ptr<void>(sub.handler_ptr_);
    auto eventSubscriptionHandler = [self = shared_from_this(), event_method,
                                     need_wire = std::move(need_wire_contexts),
                                     handler_weak = std::move(handler_weak)](
                                        const ParsedResponse &resp) mutable {
        if (!resp.is_success) {
            // Revert refcounts on failure
            self->revert_context_refcounts_on_failure(event_method, need_wire);

            // Remove handler if Subscription still exists
            if (!handler_weak.expired()) {
                auto h_it = self->event_handlers_.find(event_method);
                if (h_it != self->event_handlers_.end()) {
                    auto &vec = h_it->second;
                    auto isHandlerOwnedBySame =
                        [&handler_weak](const auto &handl) {
                            // Compare weak_ptr ownership
                            auto h_weak = std::weak_ptr<void>(handl);
                            return !h_weak.owner_before(handler_weak) &&
                                   !handler_weak.owner_before(h_weak);
                        };
                    vec.erase(std::remove_if(vec.begin(), vec.end(),
                                             isHandlerOwnedBySame),
                              vec.end());
                    if (vec.empty()) {
                        self->event_handlers_.erase(h_it);
                    }
                }
            }

            bidi::logging::log_error(
                std::format("session.subscribe (scoped) failed: {} - "
                            "Subscription invalidated, refcounts reverted",
                            resp.error_message));
        } else {
            // Extract subscription ID from response per W3C BiDi spec
            if (const auto *it = resp.result.find("subscription");
                it != nullptr) {
                const auto sub_id = std::string(it->value().get_string());
                bidi::logging::log_info(
                    std::format("session.subscribe (scoped) ok for {} with "
                                "id {} (contexts: {})",
                                event_method, sub_id, need_wire.size()));
            } else {
                bidi::logging::log_info(std::format(
                    "session.subscribe (scoped) ok for {} (contexts: {})",
                    event_method, need_wire.size()));
            }
        }
    };
    send_command("session.subscribe", params, eventSubscriptionHandler,
                 kDefaultTimeout, loc);

    return sub;
}

void BiDiSession::unsubscribe_event_scoped(
    const std::string &event_method, const std::vector<std::string> &contexts,
    std::source_location loc) {
    /// Pre-allocate to avoid reallocations during loop
    std::vector<std::string> need_wire_contexts;
    need_wire_contexts.reserve(contexts.size());
    for (const auto &ctx : contexts) {
        auto it_ctx = context_event_refcount_.find(ctx);
        if (it_ctx == context_event_refcount_.end()) {
            continue;
        }
        auto &method_map = it_ctx->second;
        auto it_m = method_map.find(event_method);
        if (it_m == method_map.end()) {
            continue;
        }
        if (it_m->second > 0) {
            --(it_m->second);
            if (it_m->second == 0) {
                need_wire_contexts.push_back(ctx);
                method_map.erase(it_m);
            }
        }
        if (method_map.empty()) {
            context_event_refcount_.erase(it_ctx);
        }
    }

    if (need_wire_contexts.empty()) {
        return;
    }

    boost::json::object params;
    params["events"] = boost::json::array({event_method});
    boost::json::array ctxs;
    for (const auto &ctx : need_wire_contexts) {
        ctxs.emplace_back(ctx);
    }
    params["contexts"] = std::move(ctxs);
    send_command(
        "session.unsubscribe", params,
        [](const ParsedResponse &resp) {
            if (!resp.is_success) {
                bidi::logging::log_error(
                    std::format("session.unsubscribe (scoped) failed: {}",
                                resp.error_message));
            }
        },
        kDefaultTimeout, loc);
}

// Helper implementations
void BiDiSession::satisfy_subscribe_promises(const std::string &event_method,
                                             bool success) {
    auto it_prom = subscribe_promises_.find(event_method);
    if (it_prom == subscribe_promises_.end()) {
        return;
    }
    // Call any registered non-blocking callbacks first
    auto it_cb = subscribe_callbacks_.find(event_method);
    if (it_cb != subscribe_callbacks_.end()) {
        for (auto &cb : it_cb->second) {
            try {
                cb(success);
            } catch (const std::exception &e) {
                bidi::logging::log_error(
                    std::format("subscribe callback exception for {}: {}",
                                event_method, e.what()));
            } catch (...) {
                bidi::logging::log_error(
                    std::format("subscribe callback unknown exception for {}",
                                event_method));
            }
        }
        // clear callbacks for this event
        subscribe_callbacks_.erase(it_cb);
    }
    for (auto &promise_ptr : it_prom->second) {
        try {
            promise_ptr->set_value(success);
        } catch (const std::exception &ex) {
            bidi::logging::log_error(std::format(
                "satisfy_subscribe_promises exception: {}", ex.what()));
        } catch (...) {
            bidi::logging::log_error(
                "satisfy_subscribe_promises unknown exception");
        }
    }
    subscribe_promises_.erase(it_prom);
    subscribe_futures_.erase(event_method);
}

void BiDiSession::revert_global_event_refcount_on_failure(
    const std::string &event_method) {
    auto it = event_refcount_.find(event_method);
    if (it != event_refcount_.end() && it->second > 0) {
        --(it->second);
        if (it->second == 0) {
            event_refcount_.erase(it);
        }
    }
}

void BiDiSession::revert_context_refcounts_on_failure(
    const std::string &event_method, const std::vector<std::string> &contexts) {
    for (const auto &ctx : contexts) {
        auto it_ctx = this->context_event_refcount_.find(ctx);
        if (it_ctx == this->context_event_refcount_.end()) {
            continue;
        }
        auto &method_map = it_ctx->second;
        auto it_m = method_map.find(event_method);
        if (it_m == method_map.end()) {
            continue;
        }
        if (it_m->second > 0) {
            --(it_m->second);
            if (it_m->second == 0) {
                method_map.erase(it_m);
            }
        }
        if (method_map.empty()) {
            this->context_event_refcount_.erase(it_ctx);
        }
    }
}

/// @brief Remove handler from event list (thread-safe via strand)
/// @param method BiDi event method name
/// @param handler_ptr Shared pointer to handler being removed
/// @note CRITICAL: Must be called from strand context for thread-safety
/// @note Removes empty method entries to avoid memory leaks
void BiDiSession::remove_handler_from_event_list_unsafe(
    const std::string &method,
    const std::shared_ptr<EventHandler> &handler_ptr) {
    auto it = event_handlers_.find(method);
    if (it != event_handlers_.end()) {
        auto &vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), handler_ptr), vec.end());
        if (vec.empty()) {
            event_handlers_.erase(it);
        }
    }
}

void BiDiSession::send_subscribe_wire_global(const std::string &event_method) {
    boost::json::object params;
    params["events"] = boost::json::array({event_method});

    // Transport sender (if provided) accepts a completion with (success,
    // result, err_code, err_msg)
    if (transport_sender_) {
        auto self = shared_from_this();
        auto on_complete = [self, event_method](
                               bool success, const boost::json::object &result,
                               const std::string &err_code,
                               const std::string &err_msg) {
            self->satisfy_subscribe_promises(event_method, success);
            if (!success) {
                self->revert_global_event_refcount_on_failure(event_method);
                bidi::logging::log_error(
                    std::format("session.subscribe failed for {}: {} {}",
                                event_method, err_code, err_msg));
            } else {
                // Extract and store subscription ID if present
                if (const auto *it = result.find("subscription");
                    it != nullptr) {
                    self->active_subscriptions_[event_method] =
                        std::string(it->value().get_string());
                }
                bidi::logging::log_info(
                    std::format("session.subscribe ok for {}", event_method));
            }
        };

        transport_sender_("session.subscribe", params, kDefaultTimeout,
                          std::move(on_complete));
        return;
    }

    // Fallback: use send_command which will call our response handler
    send_command("session.subscribe", params,
                 [this, event_method](const ParsedResponse &resp) {
                     const bool success = resp.is_success;
                     satisfy_subscribe_promises(event_method, success);
                     if (!success) {
                         revert_global_event_refcount_on_failure(event_method);
                         bidi::logging::log_error(
                             std::format("session.subscribe failed for {}: {}",
                                         event_method, resp.error_message));
                     } else {
                         // Extract and store subscription ID if present
                         if (const auto *it = resp.result.find("subscription");
                             it != nullptr) {
                             active_subscriptions_[event_method] =
                                 std::string(it->value().get_string());
                         }
                         bidi::logging::log_info(std::format(
                             "session.subscribe ok for {}", event_method));
                     }
                 });
}

void BiDiSession::send_subscribe_wire_scoped(
    const std::string &event_method, std::vector<std::string> contexts) {
    boost::json::object params;
    params["events"] = boost::json::array({event_method});
    boost::json::array ctxs;
    for (const auto &ctx : contexts) {
        ctxs.emplace_back(ctx);
    }
    params["contexts"] = std::move(ctxs);

    // Use send_command and translate response into revert helpers
    auto self = shared_from_this();
    send_command("session.subscribe", params,
                 [self, event_method, contexts = std::move(contexts)](
                     const ParsedResponse &resp) mutable {
                     if (!resp.is_success) {
                         self->revert_context_refcounts_on_failure(event_method,
                                                                   contexts);
                         bidi::logging::log_error(std::format(
                             "session.subscribe (scoped) failed: {}",
                             resp.error_message));
                         return;
                     }
                     bidi::logging::log_info(
                         std::string("session.subscribe (scoped) ok"));
                 });
}

// ================= Subscription RAII =================

auto BiDiSession::Subscription::operator=(Subscription &&other) noexcept
    -> Subscription & {
    if (this != &other) {
        cancel();
        session_ = std::move(other.session_);
        method_ = std::move(other.method_);
        contexts_ = std::move(other.contexts_);
        handler_ptr_ = std::move(other.handler_ptr_);
        subscription_id_ = std::move(other.subscription_id_);
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

BiDiSession::Subscription::Subscription(Subscription &&other) noexcept
    : session_(std::move(other.session_)), method_(std::move(other.method_)),
      contexts_(std::move(other.contexts_)),
      handler_ptr_(std::move(other.handler_ptr_)),
      subscription_id_(std::move(other.subscription_id_)), loc_(other.loc_),
      active_(other.active_) {
    other.active_ = false;
}

BiDiSession::Subscription::~Subscription() noexcept {
    try {
        cancel();
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::format("~BiDiSession::Subscription exception: {}", e.what()));
    } catch (...) {
        bidi::logging::log_error(
            "~BiDiSession::Subscription unknown exception");
    }
}

void BiDiSession::Subscription::cancel() noexcept {
    try {
        if (!active_) {
            return;
        }
        active_ = false;

        if (auto session_shared = session_.lock()) {
            auto method = method_;
            auto handler_ptr = handler_ptr_;
            auto contexts = contexts_;
            try {
                net::post(
                    session_shared->get_executor(),
                    [session_shared, method = std::move(method),
                     handler_ptr = std::move(handler_ptr),
                     contexts = std::move(contexts)]() mutable {
                        // CRITICAL FIX: Use strand-safe method
                        session_shared->remove_handler_from_event_list_unsafe(
                            method, handler_ptr);

                        if (!contexts.has_value()) {
                            session_shared->unsubscribe_event(method);
                            return;
                        }
                        session_shared->unsubscribe_event_scoped(method,
                                                                 *contexts);
                    });
            } catch (const std::exception &e) {
                bidi::logging::log_error(
                    std::string(
                        "BiDiSession::Subscription::cancel post exception: ") +
                    e.what());
            } catch (...) {
                bidi::logging::log_error(
                    "BiDiSession::Subscription::cancel post unknown exception");
            }
        }
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format(
            "BiDiSession::Subscription::cancel exception: {}", e.what()));
    } catch (...) {
        bidi::logging::log_error(
            "BiDiSession::Subscription::cancel unknown exception");
    }
}

namespace {

// Helper function to process a response message
void process_response_message(
    const std::string &payload,
    std::unordered_map<bidi::core::id_type, BiDiSession::PendingEntry>
        &pending_responses) {
    auto response = parse_response(payload);
    if (response) {
        auto it = pending_responses.find(response->id);
        if (it != pending_responses.end()) {
            response->method = it->second.method;
            response->trace_id = it->second.trace_id;
            response->trace_location = it->second.trace_location;
            response->raw_json = std::string(payload);
            response->latency =
                std::chrono::steady_clock::now() - it->second.start;
            boost::system::error_code ignore_ec;
            it->second.timer.cancel(ignore_ec);
            auto handler = std::move(it->second.handler);
            pending_responses.erase(it);
            bidi::logging::log_info(
                std::format("[OK] Response processed: id={} method={}; {}; "
                            "Origin loc: {}:{}:{}",
                            response->id, response->method, payload,
                            response->trace_location.file_name(),
                            response->trace_location.line(),
                            response->trace_location.column()));
            handler(*response);
        }
    }
}

// Helper function to process an event message
void process_event_message(
    const std::string &payload,
    std::unordered_map<std::string,
                       std::vector<std::shared_ptr<BiDiSession::EventHandler>>>
        &event_handlers) {
    bidi::logging::log_info("[EVENT] Detected event message");
    auto event = parse_event(payload);
    if (!event) {
        return;
    }
    bidi::logging::log_info(
        std::format("[EVENT] Event detected: {}", event->method));
    auto it = event_handlers.find(event->method);
    if (it == event_handlers.end()) {
        bidi::logging::log_debug(
            std::format("No handler registered for event: {}", event->method));
        return;
    }

    size_t handler_count = 0;
    for (auto &cb_ptr : it->second) {
        if (cb_ptr && *cb_ptr) {
            try {
                (*cb_ptr)(*event);
                handler_count++;
            } catch (const std::exception &e) {
                bidi::logging::log_error(
                    std::format("Event handler exception for {}: {}",
                                event->method, e.what()));
            } catch (...) {
                bidi::logging::log_error(std::format(
                    "Event handler unknown exception for {}", event->method));
            }
        }
    }

    bidi::logging::log_info(std::format("Dispatched event {} to {} handler(s)",
                                        event->method, handler_count));
}

} // anonymous namespace

void BiDiSession::on_message(const std::string &payload) {
    bidi::logging::log_info(
        std::format("[RECV] Received message: {}", payload));
    auto kind = detect_message_kind(payload);

    switch (kind) {
    case MessageKind::Response: {
        process_response_message(payload, pending_responses_);
        break;
    }

    case MessageKind::Event: {
        process_event_message(payload, event_handlers_);
        break;
    }

    case MessageKind::Command:
    case MessageKind::Unknown:
        bidi::logging::log_error("[ERROR] Unknown or unsupported message type");
        break;
    }
}

void BiDiSession::on_error(const boost::system::error_code &error_code) {
    // Structured logging for transport error
    bidi::logging::log_error("BiDi WebSocket error", error_code, nullptr);

    // Clear pending responses with error and cancel timers
    std::size_t before = pending_responses_.size();
    for (auto it = pending_responses_.begin();
         it != pending_responses_.end();) {
        auto id = it->first;
        auto &entry = it->second;
        // cancel timer
        boost::system::error_code t_error;
        entry.timer.cancel(t_error);

        ParsedResponse error_response;
        error_response.id = id;
        error_response.is_success = false;
        // Use websocket_error to reflect transport-level websocket failures
        error_response.error_code = bidi::ErrorCode::UnknownError;
        error_response.error_code_raw = "websocket_error";
        error_response.error_message = error_code.message();
        error_response.method = entry.method;
        error_response.trace_id = entry.trace_id;
        error_response.trace_location = entry.trace_location;
        // move handler out before erase
        auto handler = std::move(entry.handler);
        it = pending_responses_.erase(it);
        if (handler) {
            // Log pending cleared due to websocket error with trace_id
            bidi::logging::log_error("BiDi pending cleared by websocket error",
                                     error_code, nullptr,
                                     error_response.trace_id);
            handler(std::move(error_response));
        }
    }
    bidi::logging::log_info("BiDiSession::on_error cleared pending_responses",
                            "");
    bidi::logging::log_info(
        std::format("BiDiSession::on_error cleared pending_responses "
                    "(before={}, after={})",
                    before, pending_responses_.size()),
        "");
}

void BiDiSession::disconnect() {
    std::size_t before = pending_responses_.size();
    // Clear pending responses and cancel their timers
    for (auto it = pending_responses_.begin();
         it != pending_responses_.end();) {
        auto id = it->first;
        auto &entry = it->second;
        boost::system::error_code t_ec;
        entry.timer.cancel(t_ec);

        ParsedResponse resp;
        resp.id = id;
        resp.is_success = false;
        resp.error_code = bidi::ErrorCode::UnknownError;
        resp.error_code_raw = "shutdown";
        resp.error_message = "session disconnecting";
        resp.trace_id = entry.trace_id;
        resp.trace_location = entry.trace_location;
        auto handler = std::move(entry.handler);
        it = pending_responses_.erase(it);
        if (handler) {
            bidi::logging::log_info("BiDiSession::disconnect pending cleared",
                                    resp.trace_id);
            handler(std::move(resp));
        }
    }
    bidi::logging::log_info("BiDiSession::disconnect cleared pending_responses",
                            "");
    bidi::logging::log_info(
        std::format("BiDiSession::disconnect cleared pending_responses "
                    "(before={}, after={})",
                    before, pending_responses_.size()),
        "");

    // best-effort: close and reset underlying websocket pointer to break cycles
    if (ws_) {
        ws_->close();
        ws_.reset();
    }
}

auto BiDiSession::get_executor() const -> net::any_io_executor {
    return ws_->get_executor();
}

} // namespace bidi::core

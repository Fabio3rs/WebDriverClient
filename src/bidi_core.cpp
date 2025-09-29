// src/bidi_core.cpp — BiDi Core implementation
#include "bidi/core.hpp"
#include "bidi/logging.hpp"
#include <charconv>
#include <iostream> // kept only for debug builds; clang-tidy flags may remove in release

namespace bidi::core {

// Fast message kind detection using W3C BiDi spec patterns
auto detect_message_kind(std::string_view payload) noexcept -> MessageKind {
    // Ordem importa para evitar classificar evento como resposta só por conter
    // "type". 1) Evento: type:"event"
    if (payload.find(R"("type":"event")") != std::string_view::npos ||
        payload.find(R"("type": "event")") != std::string_view::npos) {
        // Evento não deve ter "id" obrigatoriamente; spec: eventos não carregam
        // id.
        return MessageKind::Event;
    }
    // 2) Resposta: id + type:(success|error)
    const bool has_id = payload.find("\"id\"") != std::string_view::npos;
    if (has_id &&
        (payload.find(R"("type":"success")") != std::string_view::npos ||
         payload.find(R"("type": "success")") != std::string_view::npos ||
         payload.find(R"("type":"error")") != std::string_view::npos ||
         payload.find(R"("type": "error")") != std::string_view::npos)) {
        return MessageKind::Response;
    }
    // 3) Comando: id + method e sem campo type
    if (has_id && payload.find("\"method\"") != std::string_view::npos &&
        payload.find("\"type\"") == std::string_view::npos) {
        return MessageKind::Command;
    }
    return MessageKind::Unknown;
}

// Build command message according to BiDi spec
auto build_command(id_type id, std::string_view method,
                   const boost::json::object &params) -> std::string {
    boost::json::object cmd;
    if (is_id_safe(id)) {
        cmd["id"] = static_cast<std::int64_t>(id);
    } else {
        // Serialize as string to preserve interoperability with IEEE-754
        // based consumers (e.g., JavaScript) when id exceeds MAX_SAFE_ID.
        cmd["id"] = std::to_string(id);
    }
    cmd["method"] = method;
    cmd["params"] = params;
    return boost::json::serialize(cmd);
}

// Parse response message according to W3C BiDi spec
auto parse_response(std::string_view payload) -> std::optional<ParsedResponse> {
    try {
        auto parsed = boost::json::parse(payload);
        if (!parsed.is_object()) {
            return std::nullopt;
        }

        auto &obj = parsed.as_object();

        // W3C BiDi Spec: Response MUST have id. Accept either numeric id or
        // string-encoded id (the latter used when id > MAX_SAFE_ID).
        auto *id_it = obj.find("id");
        if (id_it == obj.end()) {
            return std::nullopt;
        }

        ParsedResponse response;
        // Numeric id (int64)
        if (id_it->value().is_int64()) {
            response.id = static_cast<id_type>(id_it->value().as_int64());
        } else if (id_it->value().is_string()) {
            // parse string to uint64_t safely
            const auto s = id_it->value().as_string();
            id_type parsed_id = 0;
            // use std::from_chars for fast, locale-independent parsing
            auto str = std::string_view{s.data(), s.size()};
            unsigned long long tmp = 0ULL;
            // Attempt manual parse using from_chars
            auto res =
                std::from_chars(str.data(), str.data() + str.size(), tmp);
            if (res.ec != std::errc() || res.ptr != (str.data() + str.size())) {
                return std::nullopt; // invalid numeric string
            }
            parsed_id = static_cast<id_type>(tmp);
            response.id = parsed_id;
        } else {
            return std::nullopt;
        }

        // W3C BiDi Spec: Response MUST have "type" field
        auto *type_it = obj.find("type");
        if (type_it == obj.end() || !type_it->value().is_string()) {
            return std::nullopt;
        }

        auto type_str = type_it->value().as_string();

        if (type_str == "success") {
            // Accept success even if result field is missing/off-spec; default
            // to empty object so callers can handle absent payloads.
            response.is_success = true;
            auto *result_it = obj.find("result");
            if (result_it != obj.end() && result_it->value().is_object()) {
                response.result = result_it->value().as_object();
            } else {
                response.result = {};
            }
            return response;
        }
        if (type_str == "error") {
            // Be lenient: some implementations may omit "error"/"message".
            response.is_success = false;
            auto *error_it = obj.find("error");
            auto *message_it = obj.find("message");
            if (error_it != obj.end() && error_it->value().is_string()) {
                response.error_code = error_it->value().as_string().c_str();
            } else {
                response.error_code.clear();
            }
            if (message_it != obj.end() && message_it->value().is_string()) {
                response.error_message =
                    message_it->value().as_string().c_str();
            } else {
                response.error_message.clear();
            }
            return response;
        }

        return std::nullopt; // Invalid type
    } catch (...) {
        return std::nullopt;
    }
}

// Parse event message
auto parse_event(std::string_view payload) -> std::optional<ParsedEvent> {
    try {
        auto parsed = boost::json::parse(payload);
        if (!parsed.is_object()) {
            return std::nullopt;
        }

        auto &obj = parsed.as_object();

        // Must be type: "event"
        auto *type_it = obj.find("type");
        if (type_it == obj.end() || !type_it->value().is_string() ||
            type_it->value().as_string() != "event") {
            return std::nullopt;
        }

        // Must have method
        auto *method_it = obj.find("method");
        if (method_it == obj.end() || !method_it->value().is_string()) {
            return std::nullopt;
        }

        ParsedEvent event;
        event.method = method_it->value().as_string().c_str();

        // Params are optional but usually present
        auto *params_it = obj.find("params");
        if (params_it != obj.end() && params_it->value().is_object()) {
            event.params = params_it->value().as_object();
        }

        return event;
    } catch (...) {
        return std::nullopt;
    }
}

// ======================== WebSocketClient Implementation
// ========================

WebSocketClient::WebSocketClient(net::io_context &ioc)
    : strand_(net::make_strand(ioc)), resolver_(strand_), ws_(strand_) {}

void WebSocketClient::start_read_loop() { do_read(); }

void WebSocketClient::do_read() {
    ws_.async_read(
        read_buffer_,
        [self = shared_from_this()](const boost::system::error_code &error_code,
                                    std::size_t) {
            if (error_code) {
                if (self->on_error_) {
                    self->on_error_(error_code);
                }
                return;
            }

            // Extract message
            auto data = beast::buffers_to_string(self->read_buffer_.data());
            self->read_buffer_.consume(self->read_buffer_.size());

            // Dispatch to message handler
            if (self->on_message_) {
                self->on_message_(std::move(data));
            }

            // Continue reading
            self->do_read();
        });
}

void WebSocketClient::do_write() {
    if (write_queue_.empty() || is_writing_) {
        return;
    }

    is_writing_ = true;

    ws_.async_write(
        net::buffer(write_queue_.front()),
        [self = shared_from_this()](const boost::system::error_code &error_code,
                                    std::size_t) {
            self->is_writing_ = false;

            if (error_code) {
                if (self->on_error_) {
                    self->on_error_(error_code);
                }
                return;
            }

            self->write_queue_.pop_front();

            // Continue writing if queue not empty
            if (!self->write_queue_.empty()) {
                self->do_write();
            }
        });
}

void WebSocketClient::on_resolve(
    const boost::system::error_code &error_code,
    const net::ip::tcp::resolver::results_type &results) {
    if (error_code) {
        if (connect_handler_) {
            connect_handler_(error_code);
        }
        return;
    }

    beast::get_lowest_layer(ws_).async_connect(
        results, [self = shared_from_this()](auto connect_ec, auto endpoint) {
            self->on_connect(connect_ec, endpoint);
        });
}

void WebSocketClient::on_connect(const boost::system::error_code &error_code,
                                 const net::ip::tcp::endpoint /*unused*/ &) {
    if (error_code) {
        if (connect_handler_) {
            connect_handler_(error_code);
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
        net::post(strand_, [self = shared_from_this()]() {
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
                std::deque<std::string> empty;
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
        });
    } catch (const std::exception &e) {
        bidi::logging::log_error("WebSocketClient::close exception",
                                 std::error_code{}, std::current_exception(),
                                 "");
    }
}

// ======================== BiDiSession Implementation ========================

BiDiSession::BiDiSession(std::shared_ptr<WebSocketClient> ws)
    : ws_(std::move(ws)) {}

void BiDiSession::send_command(std::string_view method,
                               const boost::json::object &params,
                               ResponseHandler handler,
                               std::chrono::milliseconds timeout) {
    auto id = next_id_++;
    auto start_tp = std::chrono::steady_clock::now();
    // generate trace id for this request for correlation
    auto trace_id = bidi::logging::make_trace_id();
    PendingEntry entry{std::move(handler), std::string(method), trace_id,
                       net::steady_timer(ws_->get_executor()), start_tp};
    entry.timer.expires_after(timeout);
    entry.timer.async_wait(
        [self = shared_from_this(), id](const boost::system::error_code &ec) {
            if (ec) {
                return;
            } // timer cancelado
            auto it = self->pending_responses_.find(id);
            if (it == self->pending_responses_.end()) {
                return;
            }
            ParsedResponse timeout_resp;
            timeout_resp.id = id;
            timeout_resp.is_success = false;
            timeout_resp.error_code = "timeout";
            timeout_resp.error_message = "operation timed out";
            timeout_resp.method = it->second.method;
            timeout_resp.trace_id = it->second.trace_id;
            timeout_resp.timeout_expired = true;
            timeout_resp.latency =
                std::chrono::steady_clock::now() - it->second.start;
            // log timeout with correlation
            bidi::logging::log_error("BiDi command timeout", std::error_code{},
                                     nullptr, timeout_resp.trace_id);
            it->second.handler(std::move(timeout_resp));
            self->pending_responses_.erase(it);
        });
    pending_responses_.emplace(id, std::move(entry));

    auto message = build_command(id, method, params);
    ws_->async_send(
        std::move(message), [self = shared_from_this(), id](auto ec) {
            if (ec) {
                auto it = self->pending_responses_.find(id);
                if (it != self->pending_responses_.end()) {
                    ParsedResponse resp;
                    resp.id = id;
                    resp.is_success = false;
                    resp.error_code = "transport";
                    resp.error_message = ec.message();
                    resp.method = it->second.method;
                    resp.trace_id = it->second.trace_id;
                    resp.latency =
                        std::chrono::steady_clock::now() - it->second.start;
                    // Log transport send failure with trace_id
                    bidi::logging::log_error("BiDi send transport error", ec,
                                             nullptr, resp.trace_id);
                    it->second.handler(std::move(resp));
                    self->pending_responses_.erase(it);
                }
            }
        });
}

void BiDiSession::subscribe_event(const std::string &event_method,
                                  EventHandler handler) {
    event_handlers_[event_method] = std::move(handler);

    // Enviar comando para registrar o evento no lado remoto
    boost::json::object params;
    params["events"] = boost::json::array({event_method});

    send_command("session.subscribe", params,
                 [event_method](const ParsedResponse &response) {
                     if (response.is_success) {
                         bidi::logging::log_info(
                             "✅ Evento registrado com sucesso: " +
                             event_method);
                     } else {
                         bidi::logging::log_error(
                             "❌ Falha ao registrar evento: " + event_method +
                             ", erro: " + response.error_message);
                     }
                 });
}

void BiDiSession::unsubscribe_event(const std::string &event_method) {
    event_handlers_.erase(event_method);
}

void BiDiSession::on_message(const std::string &payload) {
    bidi::logging::log_info("🔍 Received message: " + payload);
    auto kind = detect_message_kind(payload);

    switch (kind) {
    case MessageKind::Response: {
        auto response = parse_response(payload);
        if (response) {
            auto it = pending_responses_.find(response->id);
            if (it != pending_responses_.end()) {
                response->method = it->second.method;
                response->trace_id = it->second.trace_id;
                response->raw_json = std::string(payload);
                response->latency =
                    std::chrono::steady_clock::now() - it->second.start;
                boost::system::error_code ignore_ec;
                it->second.timer.cancel(ignore_ec);
                auto handler = std::move(it->second.handler);
                pending_responses_.erase(it);
                bidi::logging::log_info("✅ Response processed: id=" +
                                        std::to_string(response->id) +
                                        " method=" + response->method);
                handler(*response);
            }
        }
        break;
    }

    case MessageKind::Event: {
        bidi::logging::log_info("🔔 Detected event message");
        auto event = parse_event(payload);
        if (event) {
            bidi::logging::log_info("🎯 Event detected: " + event->method);
            auto it = event_handlers_.find(event->method);
            if (it != event_handlers_.end()) {
                bidi::logging::log_info("📤 Dispatching event to handler: " +
                                        event->method);
                it->second(*event);
            } else {
                bidi::logging::log_error("❌ No handler found for event: " +
                                         event->method);
            }
        } else {
            bidi::logging::log_error("❌ Failed to parse event message");
        }
        break;
    }

    case MessageKind::Command:
    case MessageKind::Unknown:
        bidi::logging::log_error("❌ Unknown or unsupported message type");
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
        error_response.error_code = "websocket_error";
        error_response.error_message = error_code.message();
        error_response.method = entry.method;
        error_response.trace_id = entry.trace_id;
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
        std::string(
            "BiDiSession::on_error cleared pending_responses (before=") +
            std::to_string(before) +
            ", after=" + std::to_string(pending_responses_.size()),
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
        resp.error_code = "shutdown";
        resp.error_message = "session disconnecting";
        resp.trace_id = entry.trace_id;
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
        std::string(
            "BiDiSession::disconnect cleared pending_responses (before=") +
            std::to_string(before) +
            ", after=" + std::to_string(pending_responses_.size()),
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

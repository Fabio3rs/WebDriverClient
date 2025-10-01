// src/bidi_core.cpp — BiDi Core implementation
#include "bidi/core.hpp"
#include "bidi/logging.hpp"
#include <charconv>
#include <format>
#include <iostream> // kept only for debug builds; clang-tidy flags may remove in release
#include <memory>

namespace bidi::core {

// Helper: parse id from a JSON object (int64 or numeric string)
static auto parse_id_from_object(const boost::json::object &obj) noexcept
    -> std::optional<id_type> {
    const auto *it_id = obj.find("id");
    if (it_id == obj.end()) {
        return std::nullopt;
    }
    const auto &val = it_id->value();
    if (val.is_int64()) {
        return static_cast<id_type>(val.as_int64());
    }
    if (val.is_string()) {
        const auto str = val.as_string();
        unsigned long long tmp = 0ULL;
        auto parse_res = std::from_chars(str.begin(), str.end(), tmp);
        if (parse_res.ec != std::errc() || parse_res.ptr != str.end()) {
            return std::nullopt;
        }
        return static_cast<id_type>(tmp);
    }
    return std::nullopt;
}

// Helper: fill ParsedResponse for success type
static void fill_response_success(ParsedResponse &out,
                                  const boost::json::object &obj) noexcept {
    out.is_success = true;
    const auto *result_it = obj.find("result");
    out.result = (result_it != obj.end() && result_it->value().is_object())
                     ? result_it->value().as_object()
                     : boost::json::object{};
}

// Helper: fill ParsedResponse for error type
static void fill_response_error(ParsedResponse &out,
                                const boost::json::object &obj) noexcept {
    out.is_success = false;
    const auto *error_it = obj.find("error");
    const auto *message_it = obj.find("message");
    out.error_code = (error_it != obj.end() && error_it->value().is_string())
                         ? std::string(error_it->value().as_string().c_str())
                         : std::string();
    out.error_message =
        (message_it != obj.end() && message_it->value().is_string())
            ? std::string(message_it->value().as_string().c_str())
            : std::string();
}

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
auto build_command(id_type id_value, std::string_view method,
                   const boost::json::object &params) -> std::string {
    boost::json::object cmd;
    if (is_id_safe(id_value)) {
        cmd["id"] = static_cast<std::int64_t>(id_value);
    } else {
        // Serialize as string to preserve interoperability with IEEE-754
        // based consumers (e.g., JavaScript) when id exceeds MAX_SAFE_ID.
        cmd["id"] = std::to_string(id_value);
    }
    cmd["method"] = method;
    cmd["params"] = params;
    return boost::json::serialize(cmd);
}

// Parse response message according to W3C BiDi spec
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

auto parse_response(std::string_view payload) -> std::optional<ParsedResponse> {
    try {
        auto parsed = boost::json::parse(payload);
        if (!parsed.is_object()) {
            return std::nullopt;
        }
        return parse_response_from_object(parsed.as_object());
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("parse_response exception: ") +
                                 e.what());
        return std::nullopt;
    } catch (...) {
        bidi::logging::log_error("parse_response unknown exception");
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
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("parse_event exception: ") +
                                 e.what());
        return std::nullopt;
    } catch (...) {
        bidi::logging::log_error("parse_event unknown exception");
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
        results,
        [self = shared_from_this()](auto connect_ec, const auto &endpoint) {
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

BiDiSession::BiDiSession(std::shared_ptr<WebSocketClient> websocket)
    : ws_(std::move(websocket)) {}

void BiDiSession::send_command(std::string_view method,
                               const boost::json::object &params,
                               ResponseHandler handler,
                               std::chrono::milliseconds timeout) {
    auto id = next_id_++;
    auto start_tp = std::chrono::steady_clock::now();
    // generate trace id for this request for correlation
    auto trace_id = bidi::logging::make_trace_id();
    PendingEntry entry{.handler = std::move(handler),
                       .method = std::string(method),
                       .trace_id = trace_id,
                       .timer = net::steady_timer(ws_->get_executor()),
                       .start = start_tp};
    entry.timer.expires_after(timeout);
    entry.timer.async_wait([self = shared_from_this(),
                            id](const boost::system::error_code &error_code) {
        if (error_code) {
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
        std::move(message), [self = shared_from_this(),
                             id](const boost::system::error_code &error_code) {
            if (!error_code) {
                return;
            }

            auto it = self->pending_responses_.find(id);
            if (it == self->pending_responses_.end()) {
                return;
            }

            ParsedResponse resp;
            resp.id = id;
            resp.is_success = false;
            resp.error_code = "transport";
            resp.error_message = error_code.message();
            resp.method = it->second.method;
            resp.trace_id = it->second.trace_id;
            resp.latency = std::chrono::steady_clock::now() - it->second.start;
            // Log transport send failure with trace_id
            bidi::logging::log_error("BiDi send transport error", error_code,
                                     nullptr, resp.trace_id);
            it->second.handler(std::move(resp));
            self->pending_responses_.erase(it);
        });
}

auto BiDiSession::subscribe_event(const std::string &event_method,
                                  EventHandler handler) -> Subscription {
    Subscription sub;
    sub.session_ = weak_from_this();
    sub.method_ = event_method;
    sub.contexts_ = std::nullopt;
    sub.handler_ptr_ = std::make_shared<EventHandler>(std::move(handler));
    sub.active_ = true;

    // Register locally
    event_handlers_[event_method].push_back(sub.handler_ptr_);

    // Create promise/future that will be satisfied when server confirms
    // subscribe
    auto prom = std::make_shared<std::promise<bool>>();
    auto fut = prom->get_future().share();
    // store promise so callback can satisfy it
    subscribe_promises_[event_method].push_back(prom);
    // cache shared_future for quick access (optional)
    subscribe_futures_[event_method] = fut;
    sub.confirmed_ = fut;

    // Increase refcount and only send session.subscribe when transitioning 0->1
    auto &count = event_refcount_[event_method];
    const bool need_wire = (count == 0);
    ++count;

    if (need_wire) {
        send_subscribe_wire_global(event_method);
    }

    return sub;
}

void BiDiSession::unsubscribe_event(const std::string &event_method) {
    // Decrease refcount and only send session.unsubscribe at last drop
    auto it = event_refcount_.find(event_method);
    if (it == event_refcount_.end()) {
        // Nothing to do locally
        event_handlers_.erase(event_method);
        return;
    }
    if (it->second > 0) {
        --(it->second);
    }
    const bool last = (it->second == 0);
    if (last) {
        event_refcount_.erase(it);
        boost::json::object params;
        params["events"] = boost::json::array({event_method});
        send_command("session.unsubscribe", params,
                     [](const ParsedResponse &resp) {
                         if (!resp.is_success) {
                             bidi::logging::log_error(
                                 std::string("session.unsubscribe failed: ") +
                                 resp.error_message);
                         }
                     });
        event_handlers_.erase(event_method);
    }
}

auto BiDiSession::subscribe_event_scoped(
    const std::string &event_method, const std::vector<std::string> &contexts,
    EventHandler handler) -> Subscription {
    Subscription sub;
    sub.session_ = weak_from_this();
    sub.method_ = event_method;
    sub.contexts_ = contexts;
    sub.handler_ptr_ = std::make_shared<EventHandler>(std::move(handler));
    sub.active_ = true;

    // Register local handler
    event_handlers_[event_method].push_back(sub.handler_ptr_);

    // Accumulate per-context refcounts and collect contexts that need wire
    std::vector<std::string> need_wire_contexts;
    for (const auto &ctx : contexts) {
        auto &method_map = context_event_refcount_[ctx];
        auto &count = method_map[event_method];
        const bool need_wire = (count == 0);
        ++count;
        if (need_wire) {
            need_wire_contexts.push_back(ctx);
        }
    }

    if (!need_wire_contexts.empty()) {
        boost::json::object params;
        params["events"] = boost::json::array({event_method});
        boost::json::array ctxs;
        for (const auto &ctx : need_wire_contexts) {
            ctxs.emplace_back(ctx);
        }
        params["contexts"] = std::move(ctxs);
        // Move contexts into helper to avoid copying and reduce lambda
        send_subscribe_wire_scoped(event_method, std::move(need_wire_contexts));
    }

    return sub;
}

void BiDiSession::unsubscribe_event_scoped(
    const std::string &event_method, const std::vector<std::string> &contexts) {
    std::vector<std::string> need_wire_contexts;
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

    if (!need_wire_contexts.empty()) {
        boost::json::object params;
        params["events"] = boost::json::array({event_method});
        boost::json::array ctxs;
        for (const auto &ctx : need_wire_contexts) {
            ctxs.emplace_back(ctx);
        }
        params["contexts"] = std::move(ctxs);
        send_command(
            "session.unsubscribe", params, [](const ParsedResponse &resp) {
                if (!resp.is_success) {
                    bidi::logging::log_error(
                        std::format("session.unsubscribe (scoped) failed: {}",
                                    resp.error_message));
                }
            });
    }
}

// Helper implementations
void BiDiSession::satisfy_subscribe_promises(const std::string &event_method,
                                             bool success) {
    auto it_prom = subscribe_promises_.find(event_method);
    if (it_prom == subscribe_promises_.end()) {
        return;
    }
    for (auto &promise_ptr : it_prom->second) {
        try {
            promise_ptr->set_value(success);
        } catch (const std::exception &ex) {
            bidi::logging::log_error(
                std::string("satisfy_subscribe_promises exception: ") +
                ex.what());
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

void BiDiSession::send_subscribe_wire_global(const std::string &event_method) {
    boost::json::object params;
    params["events"] = boost::json::array({event_method});

    // Transport sender (if provided) accepts a completion with (success,
    // result, err_code, err_msg)
    if (transport_sender_) {
        auto self = shared_from_this();
        auto on_complete = [self, event_method](
                               bool success,
                               const boost::json::object & /*result*/,
                               const std::string &err_code,
                               const std::string &err_msg) {
            self->satisfy_subscribe_promises(event_method, success);
            if (!success) {
                self->revert_global_event_refcount_on_failure(event_method);
                bidi::logging::log_error(
                    std::format("session.subscribe failed for {}: {} {}",
                                event_method, err_code, err_msg));
            } else {
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
        confirmed_ = std::move(other.confirmed_);
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

BiDiSession::Subscription::Subscription(Subscription &&other) noexcept {
    *this = std::move(other);
}

BiDiSession::Subscription::~Subscription() noexcept {
    try {
        cancel();
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::string("~BiDiSession::Subscription exception: ") + e.what());
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
            // Remove handler from local list
            auto it = session_shared->event_handlers_.find(method_);
            if (it != session_shared->event_handlers_.end()) {
                auto &vec = it->second;
                // remove handler_ptr_ entries
                std::erase_if(vec, [&handler_ptr = handler_ptr_](
                                       const auto &handler_candidate) {
                    return handler_candidate == handler_ptr;
                });
                if (vec.empty()) {
                    session_shared->event_handlers_.erase(it);
                }
            }

            if (!contexts_.has_value()) {
                session_shared->unsubscribe_event(method_);
                return;
            }
            session_shared->unsubscribe_event_scoped(method_, *contexts_);
        }
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::string("BiDiSession::Subscription::cancel exception: ") +
            e.what());
    } catch (...) {
        bidi::logging::log_error(
            "BiDiSession::Subscription::cancel unknown exception");
    }
}

auto BiDiSession::Subscription::wait_confirmed(
    std::chrono::milliseconds timeout) const noexcept -> std::optional<bool> {
    try {
        if (confirmed_.valid()) {
            auto status = confirmed_.wait_for(timeout);
            if (status == std::future_status::ready) {
                return confirmed_.get();
            }
            return std::nullopt; // timeout
        }
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("wait_confirmed exception: ") +
                                 e.what());
        return std::nullopt;
    } catch (...) {
        bidi::logging::log_error("wait_confirmed unknown exception");
        return std::nullopt;
    }
    return std::nullopt;
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
                for (auto &cb_ptr : it->second) {
                    if (cb_ptr && *cb_ptr) {
                        (*cb_ptr)(*event);
                    }
                }
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

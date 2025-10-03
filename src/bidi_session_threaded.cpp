// src/bidi_session_threaded.cpp — Implementation of zero busy-wait BiDi session
#include "bidi/logging.hpp"
#include "bidi/session_threaded.hpp"

namespace bidi::core {

ThreadedBiDiSession::ThreadedBiDiSession(
    std::shared_ptr<ThreadingContext> threading)
    : threading_{std::move(threading)},
      ws_stream_{std::make_unique<
          boost::beast::websocket::stream<boost::beast::tcp_stream>>(
          threading_->get_io_executor())} {
    bidi::logging::log_info(
        "Created threaded BiDi session with zero busy-wait");
}

auto ThreadedBiDiSession::acquire_pending_entry()
    -> std::shared_ptr<PendingEntry> {
    std::lock_guard<std::mutex> lk(pending_entry_pool_mutex_);
    if (!pending_entry_pool_.empty()) {
        auto e = pending_entry_pool_.back();
        pending_entry_pool_.pop_back();
        // reset fields
        e->method.clear();
        e->params = {};
        e->promise.reset();
        e->timer.reset();
        e->on_complete = nullptr;
        return e;
    }
    return std::make_shared<PendingEntry>();
}

void ThreadedBiDiSession::recycle_pending_entry(
    std::shared_ptr<PendingEntry> entry) {
    if (!entry) {
        return;
    }
    // clear heavy members
    entry->method.clear();
    entry->params = {};
    entry->promise.reset();
    entry->timer.reset();
    entry->on_complete = nullptr;
    std::lock_guard<std::mutex> lk(pending_entry_pool_mutex_);
    pending_entry_pool_.push_back(std::move(entry));
}

void ThreadedBiDiSession::attach_core(std::shared_ptr<BiDiSession> core) {
    core_ = std::move(core);
    if (!core_) {
        return;
    }

    // Provide transport sender implementation: forward to send_command_async
    core_->set_transport_sender(
        [self = shared_from_this()](
            const std::string &method, const boost::json::object &params,
            std::chrono::milliseconds timeout,
            std::function<void(bool, const boost::json::object &,
                               const std::string &, const std::string &)>
                on_complete) {
            // call send_command_async on this transport
            self->send_command_async(method, params, timeout,
                                     std::move(on_complete));
        });
}

ThreadedBiDiSession::~ThreadedBiDiSession() noexcept {
    try {
        disconnect();
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::string("~ThreadedBiDiSession exception: ") + e.what());
    } catch (...) {
        bidi::logging::log_error("~ThreadedBiDiSession unknown exception");
    }
}

auto ThreadedBiDiSession::async_connect(const std::string &ws_url)
    -> std::future<bool> {
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    // Parse WebSocket URL
    std::string url_str =
        ws_url.starts_with("ws://") ? ws_url.substr(5) : ws_url;
    auto slash_pos = url_str.find('/');
    auto host_port = (slash_pos != std::string::npos)
                         ? url_str.substr(0, slash_pos)
                         : url_str;
    std::string target =
        (slash_pos != std::string::npos) ? url_str.substr(slash_pos) : "/";

    auto colon_pos = host_port.find(':');
    std::string host = (colon_pos != std::string::npos)
                           ? host_port.substr(0, colon_pos)
                           : host_port;
    std::string port = (colon_pos != std::string::npos)
                           ? host_port.substr(colon_pos + 1)
                           : "80";

    // Async resolve (thread suspends on kernel DNS resolution)
    auto resolver = std::make_shared<boost::asio::ip::tcp::resolver>(
        threading_->get_io_executor());

    resolver->async_resolve(
        host, port,
        [this, self = shared_from_this(), promise, target, resolver](
            boost::system::error_code resolve_ec,
            const boost::asio::ip::tcp::resolver::results_type &results) {
            if (resolve_ec) {
                promise->set_value(false);
                return;
            }

            // Async connect (thread suspends on kernel TCP connect)
            boost::beast::get_lowest_layer(*ws_stream_)
                .async_connect(
                    results, [this, self, promise, target](
                                 boost::system::error_code connect_ec,
                                 const boost::asio::ip::tcp::resolver::
                                     results_type::endpoint_type &endpoint) {
                        if (connect_ec) {
                            promise->set_value(false);
                            return;
                        }

                        // Set WebSocket options
                        ws_stream_->set_option(
                            boost::beast::websocket::stream_base::timeout::
                                suggested(boost::beast::role_type::client));

                        ws_stream_->set_option(
                            boost::beast::websocket::stream_base::decorator(
                                [](boost::beast::websocket::request_type &req) {
                                    req.set(
                                        boost::beast::http::field::user_agent,
                                        "BiDi-Client/1.0");
                                }));

                        // Async WebSocket handshake (thread suspends on kernel
                        // I/O)
                        std::string handshake_host =
                            endpoint.address().to_string() + ":" +
                            std::to_string(endpoint.port());
                        ws_stream_->async_handshake(
                            handshake_host, target,
                            [this, self,
                             promise](boost::system::error_code err) {
                                if (err) {
                                    promise->set_value(false);
                                    return;
                                }

                                connected_ = true;

                                // Start read loop (continuous suspension on
                                // async_read)
                                start_read_loop();

                                promise->set_value(true);
                            });
                    });
        });

    return future;
}

auto ThreadedBiDiSession::send_command_await(
    const std::string &method, const boost::json::object &params,
    std::chrono::milliseconds timeout) -> boost::json::object {

    auto id = next_id_.fetch_add(1);
    auto entry = acquire_pending_entry();
    entry->method = method;
    entry->params = params;
    entry->promise = std::make_shared<std::promise<boost::json::object>>();

    auto future = entry->promise->get_future();

    // Post to strand for thread-safe state management
    threading_->post_ws(
        [this, self = shared_from_this(), id, entry, timeout]() {
            // Register pending (on strand - no mutex needed)
            pending_map_[id] = entry;

            // Setup timeout using native timer (suspends on kernel timer)
            setup_timeout(id, timeout);

            // Build and queue message
            boost::json::object command{{"id", static_cast<std::int64_t>(id)},
                                        {"method", entry->method},
                                        {"params", entry->params}};

            std::string message = boost::json::serialize(command);
            write_queue_.push_back(std::move(message));

            // Start write if not already writing
            if (!is_writing_.exchange(true)) {
                start_write_loop();
            }
        });

    // NATIVE AWAIT: Thread suspends here until promise completion (no
    // busy-wait!)
    return future.get();
}

// (send_command_awaitable is implemented above)

void ThreadedBiDiSession::start_read_loop() {
    if (!connected_) {
        return;
    }

    auto on_read = [self = shared_from_this()](boost::system::error_code err,
                                               std::size_t) mutable {
        if (err) {
            self->on_read_error(err);
            return;
        }
        self->on_read_ok();
        self->start_read_loop();
    };

    ws_stream_->async_read(read_buffer_, std::move(on_read));
}

void ThreadedBiDiSession::on_read_ok() {
    auto message = boost::beast::buffers_to_string(read_buffer_.data());
    read_buffer_.clear();
    threading_->post_cpu(
        [self = shared_from_this(), msg = std::move(message)]() mutable {
            self->process_message_on_cpu(msg);
        });
}

void ThreadedBiDiSession::on_read_error(const boost::system::error_code &err) {
    connected_ = false;
    bidi::logging::log_error(std::string("Read error: ") + err.message());
}

void ThreadedBiDiSession::start_write_loop() {
    threading_->post_ws([this, self = shared_from_this()]() {
        if (write_queue_.empty()) {
            is_writing_ = false;
            return;
        }

        auto &message = write_queue_.front();

        // Async write suspends thread on kernel I/O
        ws_stream_->async_write(
            boost::asio::buffer(message),
            [this, self](boost::system::error_code err,
                         std::size_t /* bytes_transferred */) {
                threading_->post_ws([this, self, err]() {
                    if (!err && !write_queue_.empty()) {
                        write_queue_.pop_front();
                    }

                    if (!write_queue_.empty()) {
                        start_write_loop(); // Continue writing
                    } else {
                        is_writing_ = false;
                    }
                });
            });
    });
}

void ThreadedBiDiSession::process_message_on_cpu(const std::string &message) {
    try {
        // Heavy JSON parsing on CPU thread (not blocking I/O thread)
        auto kind = detect_message_kind(message);

        if (kind == MessageKind::Response) {
            auto response_opt = parse_response(message);
            if (response_opt) {
                auto &response = *response_opt;

                // Post back to strand for thread-safe completion
                threading_->post_ws(
                    [this, self = shared_from_this(), request_id = response.id,
                     success = response.is_success,
                     result = std::move(response.result),
                     error_code = std::move(response.error_code),
                     error_message = std::move(response.error_message)]() {
                        complete_pending_on_strand(request_id, success, result,
                                                   error_code, error_message);
                    });
            }
        } else if (kind == MessageKind::Event) {
            auto event_opt = parse_event(message);
            if (event_opt) {
                publish_event(event_opt->method, event_opt->params);
            }
        }
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Message processing error: ") +
                                 e.what());
    }
}

void ThreadedBiDiSession::complete_pending_on_strand(
    id_type request_id, bool success, const boost::json::object &result,
    const std::string &error_code, const std::string &error_message) {

    // Execute on strand - thread safe
    auto it = pending_map_.find(request_id);
    if (it != pending_map_.end()) {
        auto entry = std::move(it->second);
        pending_map_.erase(it);

        // Cancel timeout (native cancellation)
        if (entry->timer) {
            entry->timer->cancel();
        }

        // Invoke optional completion callback (non-blocking path)
        try {
            if (entry->on_complete) {
                entry->on_complete(success, result, error_code, error_message);
            }
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::string("on_complete callback threw: ") + e.what());
        } catch (...) {
            // Log unknown exception to aid diagnostics; avoid throwing from
            // strand callbacks.
            bidi::logging::log_error(
                "complete_pending_on_strand unknown exception");
        }

        // Complete promise (wakes awaiting thread via kernel notification)
        try {
            if (entry->promise) {
                if (success) {
                    entry->promise->set_value(result);
                } else {
                    entry->promise->set_exception(std::make_exception_ptr(
                        std::runtime_error{error_code + ": " + error_message}));
                }
            }
        } catch (...) {
            // Promise already set or other issue
        }
        // Recycle entry back to pool to reduce allocations
        recycle_pending_entry(std::move(entry));
    }
}

void ThreadedBiDiSession::setup_timeout(id_type request_id,
                                        std::chrono::milliseconds duration) {
    // Create timer that suspends on kernel timer primitives
    auto timer = threading_->make_timer(duration);

    if (auto it = pending_map_.find(request_id); it != pending_map_.end()) {
        it->second->timer = timer;
    }

    // Async wait suspends thread until timeout or cancellation
    timer->async_wait([this, self = shared_from_this(),
                       request_id](boost::system::error_code err) {
        if (!err) { // Timeout fired (not cancelled)
            // Post to strand to access pending_map_ safely
            threading_->post_ws([this, self, request_id]() {
                complete_pending_on_strand(request_id, false, {}, "timeout",
                                           "Request timed out");
            });
        }
        // If ec == operation_aborted, timer was cancelled (response arrived)
    });
}

void ThreadedBiDiSession::subscribe_event(const std::string &method,
                                          EventHandler handler) {
    threading_->post_ws(
        [self = shared_from_this(), method, handler = std::move(handler)]() {
            self->event_handlers_[method].push_back(handler);
        });
}

void ThreadedBiDiSession::publish_event(const std::string &method,
                                        const boost::json::object &params) {
    threading_->post_ws([self = shared_from_this(), method, params]() {
        auto it = self->event_handlers_.find(method);
        if (it != self->event_handlers_.end()) {
            for (auto &handler : it->second) {
                try {
                    handler(method, params);
                } catch (const std::exception &e) {
                    bidi::logging::log_error(
                        std::string("Event handler error: ") + e.what());
                }
            }
        }
    });
}

void ThreadedBiDiSession::disconnect() {
    if (connected_.exchange(false)) {
        try {
            if (ws_stream_ && ws_stream_->is_open()) {
                ws_stream_->close(boost::beast::websocket::close_code::normal);
            }
        } catch (const std::exception &e) {
            bidi::logging::log_error(std::string("disconnect close error: ") +
                                     e.what());
        } catch (...) {
            bidi::logging::log_error("disconnect close unknown exception");
        }

        // Cancel all pending requests
        threading_->post_ws([self = shared_from_this()]() {
            for (auto &[id, entry] : self->pending_map_) {
                if (entry->timer) {
                    entry->timer->cancel();
                }
                try {
                    entry->promise->set_exception(std::make_exception_ptr(
                        std::runtime_error{"Connection closed"}));
                } catch (const std::exception &e) {
                    bidi::logging::log_error(
                        std::string(
                            "complete_pending_on_strand promise error: ") +
                        e.what());
                } catch (...) {
                    bidi::logging::log_error(
                        "complete_pending_on_strand promise unknown exception");
                }
                // recycle
                self->recycle_pending_entry(std::move(entry));
            }
            self->pending_map_.clear();
        });
    }
}

// ===================== Refcounted subscribe/unsubscribe (RAII)
// =====================

auto ThreadedBiDiSession::subscribe(const std::string &method,
                                    const std::optional<std::string> &context,
                                    EventHandler handler) -> Subscription {
    Subscription sub;
    sub.session = weak_from_this();
    sub.method = method;
    sub.context = context;
    sub.active = true;

    threading_->post_ws([self = shared_from_this(), method, context,
                         handler = std::move(handler)]() mutable {
        self->incr_subscription_on_strand(method, context, std::move(handler));
    });

    return sub;
}

auto ThreadedBiDiSession::subscribe_many(
    const std::vector<std::string> &methods,
    std::optional<std::vector<std::string>> contexts,
    const EventHandler &handler) -> std::vector<Subscription> {
    std::vector<Subscription> subs;
    if (!contexts || contexts->empty()) {
        // Global subs
        subs.reserve(methods.size());
        for (const auto &m : methods) {
            subs.emplace_back(subscribe(m, std::nullopt, handler));
        }
        return subs;
    }

    // Context-scoped subs
    subs.reserve(methods.size() * contexts->size());
    for (const auto &ctx : *contexts) {
        for (const auto &m : methods) {
            subs.emplace_back(subscribe(m, ctx, handler));
        }
    }
    return subs;
}

void ThreadedBiDiSession::incr_subscription_on_strand(
    const std::string &method, const std::optional<std::string> &context,
    EventHandler handler) {
    // Must run on strand
    if (context.has_value()) {
        auto &ctx_map = context_refcount_[*context];
        auto &count = ctx_map[method];
        const bool first = (count == 0);
        ++count;
        // Defer installing handler until wire-confirmation. Keep pending.
        pending_event_handlers_[method].push_back(std::move(handler));
        if (first) {
            std::vector<std::string> methods{method};
            std::vector<std::string> contexts{*context};
            send_subscribe_wire(methods, &contexts);
        }
        return;
    }

    // Global
    auto &count = global_refcount_[method];
    const bool first = (count == 0);
    ++count;
    pending_event_handlers_[method].push_back(std::move(handler));
    if (first) {
        std::vector<std::string> methods{method};
        send_subscribe_wire(methods, nullptr);
    }
}

void ThreadedBiDiSession::decr_subscription_on_strand(
    const std::string &method, const std::optional<std::string> &context) {
    // Must run on strand
    if (context.has_value()) {
        auto it_ctx = context_refcount_.find(*context);
        if (it_ctx == context_refcount_.end()) {
            return;
        }
        auto &ctx_map = it_ctx->second;
        auto it = ctx_map.find(method);
        if (it == ctx_map.end()) {
            return;
        }
        if (it->second > 0) {
            --(it->second);
            if (it->second == 0) {
                // last — emit unsubscribe
                std::vector<std::string> methods{method};
                std::vector<std::string> contexts{*context};
                send_unsubscribe_wire(methods, &contexts);
                ctx_map.erase(it);
            }
        }
        if (ctx_map.empty()) {
            context_refcount_.erase(it_ctx);
        }
        return;
    }

    // Global
    auto it = global_refcount_.find(method);
    if (it == global_refcount_.end()) {
        return;
    }
    if (it->second > 0) {
        --(it->second);
        if (it->second == 0) {
            std::vector<std::string> methods{method};
            send_unsubscribe_wire(methods, nullptr);
            global_refcount_.erase(it);
        }
    }
}

void ThreadedBiDiSession::send_subscribe_wire(
    std::vector<std::string> methods,
    const std::vector<std::string> *contexts_opt) {
    // Build params with minimal temporaries, reusing methods vector for
    // iteration. contexts_opt may be null; avoid copying unless needed.
    boost::json::object params;
    boost::json::array evs;
    evs.reserve(methods.size());
    for (const auto &method : methods) {
        evs.emplace_back(method);
    }
    params["events"] = std::move(evs);

    std::vector<std::string> captured_contexts;
    if ((contexts_opt != nullptr) && !contexts_opt->empty()) {
        boost::json::array ctxs;
        ctxs.reserve(contexts_opt->size());
        for (const auto &ctx : *contexts_opt) {
            ctxs.emplace_back(ctx);
        }
        params["contexts"] = std::move(ctxs);
        // capture contexts for potential revert path
        captured_contexts = *contexts_opt;
    }

    // Move methods into callback to avoid copies; captured_contexts is
    // moved as well when possible. The callback posts a small handler to
    // the strand which contains the heavier logic extracted to helpers.
    auto subscription_callback =
        [this, self = shared_from_this(), methods = std::move(methods),
         captured_contexts = std::move(captured_contexts)](
            bool success, const boost::json::object &,
            const std::string &err_code, const std::string &err_msg) mutable {
            // Post a minimal closure to the strand that delegates to helpers.
            if (!success) {
                this->threading_->post_ws(
                    [this, methods = std::move(methods),
                     captured_contexts = std::move(captured_contexts), err_code,
                     err_msg]() mutable {
                        this->revert_subscribe_failure_on_strand(
                            methods, captured_contexts, err_code, err_msg);
                    });
                return;
            }

            this->threading_->post_ws(
                [this, methods = std::move(methods)]() mutable {
                    this->apply_subscribe_success_on_strand(methods);
                });
        };

    send_command_async("session.subscribe", params, kSubscribeTimeout,
                       subscription_callback);
}

void ThreadedBiDiSession::send_unsubscribe_wire(
    const std::vector<std::string> &methods,
    const std::vector<std::string> *contexts_opt) {
    boost::json::object params;
    boost::json::array evs;
    for (const auto &method : methods) {
        evs.emplace_back(method);
    }
    params["events"] = std::move(evs);
    if ((contexts_opt != nullptr) && !contexts_opt->empty()) {
        boost::json::array ctxs;
        for (const auto &ctx : *contexts_opt) {
            ctxs.emplace_back(ctx);
        }
        params["contexts"] = std::move(ctxs);
    }

    // Non-blocking: register pending and return immediately.
    auto unsubscribe_handler = [this, self = shared_from_this(),
                                methods](bool success,
                                         const boost::json::object & /*result*/,
                                         const std::string &err_code,
                                         const std::string &err_msg) mutable {
        // Ensure we run the handler logic on the strand
        auto unsubscribe_session_handler = [this, success, methods, err_code,
                                            err_msg]() {
            if (!success) {
                bidi::logging::log_error(
                    std::string("session.unsubscribe failed: ") + err_code +
                    " - " + err_msg);
                return;
            }

            // On success remove handlers (both pending and active)
            for (const auto &method_name : methods) {
                auto it = event_handlers_.find(method_name);
                if (it != event_handlers_.end()) {
                    event_handlers_.erase(it);
                }
                pending_event_handlers_.erase(method_name);
            }

            bidi::logging::log_info(std::string("session.unsubscribe ok"));
        };
        this->threading_->post_ws(unsubscribe_session_handler);
    };
    send_command_async("session.unsubscribe", params, kSubscribeTimeout,
                       unsubscribe_handler);
}

void ThreadedBiDiSession::apply_subscribe_success_on_strand(
    const std::vector<std::string> &methods) {
    for (const auto &method_name : methods) {
        auto it = pending_event_handlers_.find(method_name);
        if (it != pending_event_handlers_.end()) {
            auto &vec = event_handlers_[method_name];
            // move handlers
            for (auto &handler : it->second) {
                vec.push_back(std::move(handler));
            }
            pending_event_handlers_.erase(it);
        }
    }
    bidi::logging::log_info(std::string("session.subscribe ok"));
}

void ThreadedBiDiSession::revert_context_refcounts_on_strand(
    const std::vector<std::string> &methods,
    const std::vector<std::string> &captured_contexts) {
    for (const auto &context_id : captured_contexts) {
        auto it_ctx = this->context_refcount_.find(context_id);
        if (it_ctx == this->context_refcount_.end()) {
            continue;
        }
        auto &method_map = it_ctx->second;
        for (const auto &method_name : methods) {
            auto it_m = method_map.find(method_name);
            if (it_m == method_map.end()) {
                continue;
            }
            if (it_m->second > 0) {
                --(it_m->second);
                if (it_m->second == 0) {
                    method_map.erase(it_m);
                }
            }
        }
        if (method_map.empty()) {
            this->context_refcount_.erase(it_ctx);
        }
    }
}

void ThreadedBiDiSession::revert_global_refcounts_on_strand(
    const std::vector<std::string> &methods) {
    for (const auto &method_name : methods) {
        auto it = this->global_refcount_.find(method_name);
        if (it == this->global_refcount_.end()) {
            continue;
        }
        if (it->second > 0) {
            --(it->second);
            if (it->second == 0) {
                this->global_refcount_.erase(it);
            }
        }
    }
}

void ThreadedBiDiSession::clear_pending_handlers_on_strand(
    const std::vector<std::string> &methods) {
    for (const auto &method_name : methods) {
        pending_event_handlers_.erase(method_name);
    }
}

void ThreadedBiDiSession::revert_subscribe_failure_on_strand(
    const std::vector<std::string> &methods,
    const std::vector<std::string> &captured_contexts,
    const std::string &err_code, const std::string &err_msg) {
    // revert context refcounts
    if (!captured_contexts.empty()) {
        revert_context_refcounts_on_strand(methods, captured_contexts);
    } else {
        // no contexts => revert globals
        revert_global_refcounts_on_strand(methods);
    }

    // clear pending handlers for these methods
    clear_pending_handlers_on_strand(methods);

    bidi::logging::log_error(std::string("session.subscribe failed: ") +
                             err_code + " - " + err_msg);
}

void ThreadedBiDiSession::send_command_async(
    const std::string &method, const boost::json::object &params,
    std::chrono::milliseconds timeout,
    std::function<void(bool, const boost::json::object &, const std::string &,
                       const std::string &)>
        on_complete) {

    // Allocate a pending entry and register it on the strand without blocking.
    auto id = next_id_.fetch_add(1);
    auto entry = acquire_pending_entry();
    entry->method = method;
    entry->params = params;
    entry->on_complete = std::move(on_complete);

    threading_->post_ws(
        [this, self = shared_from_this(), id, entry, timeout]() {
            // register pending
            pending_map_[id] = entry;

            // setup timeout
            setup_timeout(id, timeout);

            // build message and queue
            boost::json::object command{{"id", static_cast<std::int64_t>(id)},
                                        {"method", entry->method},
                                        {"params", entry->params}};
            std::string message = boost::json::serialize(command);
            write_queue_.push_back(std::move(message));

            if (!is_writing_.exchange(true)) {
                start_write_loop();
            }
        });
}

auto ThreadedBiDiSession::send_command_awaitable(std::string method,
                                                 boost::json::object params)
    -> boost::asio::awaitable<boost::json::object> {

    // Run the blocking send_command_await on the CPU executor so the WS
    // strand is never blocked. The CPU executor has threads that can
    // suspend while waiting on the promise.
    // Use a named coroutine helper (no capturing of enclosing lambda) so the
    // closure lifetime is explicit and clang-tidy won't warn about
    // coroutine-lambda captures causing use-after-free.
    // File-scope helper to perform blocking send_command_await on CPU executor
    auto self_shared = shared_from_this();
    // The coroutine lambda captures a shared_ptr (self_shared) by value to
    // guarantee lifetime while executing on the CPU executor. This is safe and
    // intentional; suppress the clang-tidy warning with justification.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    auto runner = [self_shared, method = std::move(method),
                   params = std::move(params),
                   cpu = threading_->get_cpu_executor()]()
        -> boost::asio::awaitable<boost::json::object> {
        co_return self_shared->send_command_await(method, params);
    };

    auto result_obj = co_await boost::asio::co_spawn(
        threading_->get_cpu_executor(), std::move(runner),
        boost::asio::use_awaitable);
    co_return result_obj;
}

void ThreadedBiDiSession::Subscription::cancel() noexcept {
    try {
        if (!active) {
            return;
        }
        active = false;
        if (auto session_ptr = session.lock()) {
            auto method_copy = method;
            auto context_copy = context;
            session_ptr->threading_->post_ws(
                [session_ptr, method_copy, context_copy]() {
                    session_ptr->decr_subscription_on_strand(method_copy,
                                                             context_copy);
                });
        }
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::string(
                "ThreadedBiDiSession::Subscription::cancel exception: ") +
            e.what());
    } catch (...) {
        bidi::logging::log_error(
            "ThreadedBiDiSession::Subscription::cancel unknown exception");
    }
}

ThreadedBiDiSession::Subscription::~Subscription() noexcept { cancel(); }

} // namespace bidi::core

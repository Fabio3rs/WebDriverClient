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

ThreadedBiDiSession::~ThreadedBiDiSession() { disconnect(); }

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
                             promise](boost::system::error_code ec) {
                                if (ec) {
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
    auto entry = std::make_shared<PendingEntry>();
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

auto ThreadedBiDiSession::send_command_awaitable(
    const std::string &method, const boost::json::object &params)
    -> boost::asio::awaitable<boost::json::object> {

    // Convert to awaitable using Asio's native suspension
    auto result = co_await boost::asio::co_spawn(
        threading_->get_ws_strand(),
        [this, method,
         params]() -> boost::asio::awaitable<boost::json::object> {
            // This will suspend the coroutine until completion
            auto future_result = send_command_await(method, params);
            co_return future_result;
        },
        boost::asio::use_awaitable);

    co_return result;
}

void ThreadedBiDiSession::start_read_loop() {
    if (!connected_) {
        return;
    }

    // Async read suspends thread on kernel I/O (epoll/kqueue/IOCP)
    ws_stream_->async_read(
        read_buffer_,
        [this, self = shared_from_this()](boost::system::error_code ec,
                                          std::size_t /* bytes_transferred */) {
            if (ec) {
                connected_ = false;
                bidi::logging::log_error(std::string("Read error: ") +
                                         ec.message());
                return;
            }

            // Extract message
            std::string message =
                boost::beast::buffers_to_string(read_buffer_.data());
            read_buffer_.clear();

            // Process on CPU thread pool (heavy JSON parsing)
            threading_->post_cpu([this, self, message = std::move(message)]() {
                process_message_on_cpu(message);
            });

            // Continue reading (recursive async - no polling!)
            start_read_loop();
        });
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
            [this, self](boost::system::error_code ec,
                         std::size_t /* bytes_transferred */) {
                threading_->post_ws([this, self, ec]() {
                    if (!ec && !write_queue_.empty()) {
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

        // Complete promise (wakes awaiting thread via kernel notification)
        try {
            if (success) {
                entry->promise->set_value(result);
            } else {
                entry->promise->set_exception(std::make_exception_ptr(
                    std::runtime_error{error_code + ": " + error_message}));
            }
        } catch (...) {
            // Promise already set
        }
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
            complete_pending_on_strand(request_id, false, {}, "timeout",
                                       "Request timed out");
        }
        // If ec == operation_aborted, timer was cancelled (response
        // arrived)
    });
}

void ThreadedBiDiSession::subscribe_event(const std::string &method,
                                          EventHandler handler) {
    threading_->post_ws([this, method, handler = std::move(handler)]() {
        event_handlers_[method].push_back(handler);
    });
}

void ThreadedBiDiSession::publish_event(const std::string &method,
                                        const boost::json::object &params) {
    threading_->post_ws([this, method, params]() {
        auto it = event_handlers_.find(method);
        if (it != event_handlers_.end()) {
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
        } catch (...) {
            // Ignore close errors
        }

        // Cancel all pending requests
        threading_->post_ws([this]() {
            for (auto &[id, entry] : pending_map_) {
                if (entry->timer) {
                    entry->timer->cancel();
                }
                try {
                    entry->promise->set_exception(std::make_exception_ptr(
                        std::runtime_error{"Connection closed"}));
                } catch (...) {
                    // Promise already set
                }
            }
            pending_map_.clear();
        });
    }
}

} // namespace bidi::core

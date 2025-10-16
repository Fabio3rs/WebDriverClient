// Example: End-to-end BiDi flow (session + context + navigate + evaluate)
// Origin: migrated from demo_integration.cpp (renamed for naming consistency)
// Focus: coroutine-based full workflow using legacy WebDriver session to obtain
// webSocketUrl

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/commands.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <memory>
#include <string>

namespace asio = boost::asio;

class EndToEndFlowDemo {
  private:
    asio::io_context io_context_;
    // RAII session and client handled via guards in run()
    std::string websocket_url_;
    std::atomic<bool> finished{false};
    std::shared_ptr<asio::steady_timer> watchdog_timer_;

  public:
    EndToEndFlowDemo() = default;

    ~EndToEndFlowDemo() { cleanup(); }

    EndToEndFlowDemo(const EndToEndFlowDemo &) = delete;
    auto operator=(const EndToEndFlowDemo &) -> EndToEndFlowDemo & = delete;
    EndToEndFlowDemo(EndToEndFlowDemo &&) = delete;
    auto operator=(EndToEndFlowDemo &&) -> EndToEndFlowDemo & = delete;

    auto initialize_session() -> bool {
        using namespace bidi::logging;
        try {
            bidi::SessionGuard guard("http://localhost:9515");
            WebDriver::json args =
                WebDriver::json::array({"--headless", "--no-sandbox"});
            auto ws_url = guard.connect(args, "chrome", true);
            if (!ws_url) {
                log_error(ws_url.error());
                return false;
            }
            websocket_url_ = *ws_url;
            log_info(std::string("WebSocket URL: ") + websocket_url_);
            // SessionGuard will cleanup automatically when guard leaves scope;
            // we only needed URL here.
            return true;
        } catch (const std::exception &e) {
            log_error(std::string("Session init failed: ") + e.what());
            return false;
        }
    }

    static auto on_connect(const std::shared_ptr<bidi::Client> &client)
        -> asyncx::Async<std::shared_ptr<bidi::Client>> {
        auto out = asyncx::Async<std::shared_ptr<bidi::Client>>::make(
            boost::asio::system_executor{});
        if (!client) {
            bidi::logging::log_error("BiDi connect returned null");
            out.fulfill(nullptr);
        } else {
            bidi::logging::log_info("BiDi client connected on on_connect");
            out.fulfill(client);
        }
        return out;
    }

    static auto on_connect_no_async(const std::shared_ptr<bidi::Client> &client)
        -> std::shared_ptr<bidi::Client> {
        if (!client) {
            bidi::logging::log_error("BiDi connect returned null");
            return {};
        }
        bidi::logging::log_info("BiDi client connected on on_connect_no_async");
        return client;
    }

    static auto subscribe(const std::shared_ptr<bidi::Client> &client)
        -> asyncx::Async<std::shared_ptr<bidi::Client>> {
        using namespace bidi;
        using namespace logging;
        using bidi::core::ParsedEvent;
        using ClientPtr = std::shared_ptr<bidi::Client>;
        using ClientAsync = asyncx::Async<ClientPtr>;

        auto session = client->session();
        if (!session) {
            log_error("Cannot subscribe, session is null");
            auto result = ClientAsync::make(client->get_executor());
            result.fail(boost::system::error_code{});
            return result;
        }

        auto sub_async = session->subscribe_event(
            ids::events::bc_contextCreated, [](const ParsedEvent &event) {
                bidi::logging::log_info(std::string("Event received: ") +
                                        event.method);
            });
        auto sub2_async = session->subscribe_event(
            ids::events::log_entryAdded, [](const ParsedEvent &event) {
                bidi::logging::log_info(std::string("Log event: ") +
                                        event.method);
            });

        auto result = ClientAsync::make(client->get_executor());
        auto count = std::make_shared<std::atomic<int>>(0);
        auto on_complete = [result, client, count]() {
            if (++(*count) == 2) {
                bidi::logging::log_info("Subscribed to events");
                result.fulfill(client);
            }
        };
        auto finally_handler = [on_complete, result](const auto &, auto ec_opt,
                                                     const auto &) {
            if (!ec_opt) {
                on_complete();
            } else {
                result.fail(*ec_opt);
            }
        };
        sub_async.finally(finally_handler);
        sub2_async.finally(finally_handler);
        return result;
    }

    auto run_flow() -> asio::awaitable<int> {
        using namespace bidi::logging;
        using namespace bidi::commands::browsing_context;
        log_info("Connecting BiDi Client (await)");
        auto client_opt =
            co_await bidi::Client::connect(io_context_, websocket_url_);
        if (!client_opt) {
            log_info("BiDi connect returned null");
            co_return 1;
        }
        bidi::ClientGuard client_guard(client_opt);
        auto context_id =
            co_await client_guard.client()->create_context(CreateType::window);
        log_info(std::string("Context: ") + context_id);
        auto nav_url = co_await client_guard.client()->navigate(
            context_id, "https://example.com");
        log_info(std::string("Navigation OK: ") + nav_url);
        auto title_obj = co_await client_guard.client()->evaluate(
            "document.title", context_id);
        log_info(std::string("Title: ") + boost::json::serialize(title_obj));
        co_await client_guard.client()->evaluate(
            "console.log('Hello from EndToEndFlowDemo')", context_id);
        auto loc_obj = co_await client_guard.client()->evaluate(
            "window.location.href", context_id);
        log_info(std::string("Location: ") + boost::json::serialize(loc_obj));
        // ClientGuard handles disconnect automatically
        asio::post(io_context_.get_executor(), [this]() {
            finished.store(true);
            if (watchdog_timer_) {
                boost::system::error_code ec;
                watchdog_timer_->cancel(ec);
            }
        });
        co_return 0;
    }

    auto run_bidi_flow() -> asio::awaitable<int> {
        try {
            co_return co_await run_flow();
        } catch (...) {
            co_return 1;
        }
    }

    auto run() -> int {
        if (!initialize_session()) {
            return 1;
        }
        constexpr std::chrono::seconds kWatchdogSeconds{30};
        finished.store(false);
        auto fut =
            asio::co_spawn(io_context_, run_bidi_flow(), asio::use_future);
        watchdog_timer_ =
            std::make_shared<asio::steady_timer>(io_context_.get_executor());
        watchdog_timer_->expires_after(std::chrono::seconds(kWatchdogSeconds));
        watchdog_timer_->async_wait(
            [this](const boost::system::error_code &error_code) {
                if (error_code == asio::error::operation_aborted) {
                    return;
                }
                if (!finished.load()) {
                    asio::post(io_context_.get_executor(),
                               [this]() { io_context_.stop(); });
                }
            });
        io_context_.run();
        finished.store(true);
        if (watchdog_timer_) {
            boost::system::error_code ec;
            watchdog_timer_->cancel(ec);
        }
        try {
            return fut.get();
        } catch (...) {
            return 1;
        }
    }

    void cleanup() {}
};

auto main() -> int {
    bidi::logging::log_info("End-to-End BiDi Flow Example (Coroutines)");
    try {
        EndToEndFlowDemo demo;
        return demo.run();
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Example failed: ") + e.what());
        return 1;
    }
}

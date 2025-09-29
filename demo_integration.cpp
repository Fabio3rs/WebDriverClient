/**
 * @brief Real demo combining WebDriver legacy session with new BiDi WebSocket
 * architecture
 *
 * This demo demonstrates:
 * 1. Creating a WebDriver HTTP session with webSocketUrl capability
 * 2. Extracting the WebSocket URL from session capabilities
 * 3. Connecting the new BiDi Client to the WebSocket endpoint
 * 4. Performing browser automation via BiDi WebSocket protocol
 * 5. Proper session lifecycle management
 */

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/commands.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_future.hpp>
// #include <iostream> removed (unused)
#include <string>
#include <thread>

// Adaptador: converte asyncx::Async<T> em boost::asio::awaitable<T>
template <class T>
auto await_async(asyncx::Async<T> a) -> boost::asio::awaitable<T> {
    co_return co_await boost::asio::async_initiate<
        decltype(boost::asio::use_awaitable),
        void(boost::system::error_code, T)>(
        [a = std::move(a)](auto &&handler) mutable {
            // handler may be move-only; wrap in shared_ptr so the lambda passed
            // to finally is copyable
            using handler_t = std::decay_t<decltype(handler)>;
            auto sp = std::make_shared<handler_t>(
                std::forward<decltype(handler)>(handler));
            a.finally([sp](std::optional<T> v, std::optional<asyncx::EC> ec,
                           const std::exception_ptr & /*ep*/) mutable {
                if (v) {
                    bidi::logging::log_info("[await_async] handler -> success");
                    (*sp)(boost::system::error_code{}, std::move(*v));
                } else if (ec) {
                    bidi::logging::log_error(
                        std::string("[await_async] handler -> error ec=") +
                        std::to_string((*ec).value()));
                    (*sp)(*ec, T{});
                } else {
                    bidi::logging::log_info("[await_async] handler -> aborted");
                    (*sp)(boost::asio::error::operation_aborted, T{});
                }
            });
        },
        boost::asio::use_awaitable);
}

inline auto await_async(asyncx::Async<void> a) -> boost::asio::awaitable<void> {
    co_await boost::asio::async_initiate<decltype(boost::asio::use_awaitable),
                                         void(boost::system::error_code)>(
        [a = std::move(a)](auto &&handler) mutable {
            using handler_t = std::decay_t<decltype(handler)>;
            auto sp = std::make_shared<handler_t>(
                std::forward<decltype(handler)>(handler));
            a.finally([sp](std::optional<asyncx::EC> ec,
                           const std::exception_ptr & /*ep*/) mutable {
                if (!ec) {
                    (*sp)(boost::system::error_code{});
                } else {
                    (*sp)(*ec);
                }
            });
        },
        boost::asio::use_awaitable);
    co_return;
}

namespace asio = boost::asio;

class IntegratedWebDriverDemo {
  private:
    asio::io_context io_context_;
    WebDriver legacy_driver_;
    std::shared_ptr<bidi::Client> bidi_client_;
    std::string session_id_;
    std::string websocket_url_;

  public:
    IntegratedWebDriverDemo() {
        using namespace bidi;
        legacy_driver_.webDriverUrl = "http://localhost:9515";
    }

    ~IntegratedWebDriverDemo() {
        using namespace bidi;
        cleanup();
    }

    static void initialize() {
        using namespace bidi;
        logging::log_info("=== Initializing WebDriver Legacy Session ===");
    }

    auto initialize_session() -> bool {
        using namespace bidi;
        using namespace logging;
        try {
            log_info("=== Initializing WebDriver Legacy Session ===");
            WebDriver::json args =
                WebDriver::json::array({"--headless", "--no-sandbox"});
            auto session_response =
                legacy_driver_.connect(args, "chrome", true);
            session_id_ = legacy_driver_.sessionId;
            log_info(std::string("✓ Session created: ") + session_id_);
            if (session_response.contains("capabilities") &&
                session_response["capabilities"].contains("webSocketUrl")) {
                websocket_url_ =
                    session_response["capabilities"]["webSocketUrl"]
                        .get<std::string>();
                log_info(std::string("✓ WebSocket URL extracted: ") +
                         websocket_url_);
                return true;
            }
            log_error("✗ webSocketUrl not found in session capabilities");
            return false;
        } catch (const std::exception &e) {
            log_error(std::string("✗ Session initialization failed: ") +
                      e.what());
            return false;
        }
    }

    static auto on_connect(const std::shared_ptr<bidi::Client> &client)
        -> asyncx::Async<std::shared_ptr<bidi::Client>> {
        auto out = asyncx::Async<std::shared_ptr<bidi::Client>>::make(
            boost::asio::system_executor{});
        if (!client) {
            bidi::logging::log_error("✗ BiDi connect returned null");
            out.fulfill(nullptr);
        } else {
            bidi::logging::log_info("✓ BiDi client connected on on_connect");
            out.fulfill(client);
        }
        return out;
    }

    static auto on_connect_no_async(const std::shared_ptr<bidi::Client> &client)
        -> std::shared_ptr<bidi::Client> {
        using namespace bidi;
        using namespace logging;
        if (!client) {
            log_error("✗ BiDi connect returned null");
            return {};
        }

        log_info("✓ BiDi client connected on on_connect_no_async");
        return client;
    }

    static auto subscribe(const std::shared_ptr<bidi::Client> &client)
        -> std::shared_ptr<bidi::Client> {
        using namespace bidi;
        using namespace logging;
        using bidi::core::ParsedEvent;

        auto session = client->session();

        if (!session) {
            log_error("✗ Cannot subscribe, session is null");
            return client;
        }

        session->subscribe_event(
            std::string(ids::events::bc_contextCreated),
            [](const ParsedEvent &event) {
                bidi::logging::log_info(std::string("🎯 Event received: ") +
                                        event.method);
                bidi::logging::log_info(std::string("   Params: ") +
                                        boost::json::serialize(event.params));
            });

        session->subscribe_event(
            std::string(ids::events::log_entryAdded),
            [](const ParsedEvent &event) {
                bidi::logging::log_info(
                    "🎯 Attempting to process log_entryAdded event");
                if (event.method.empty()) {
                    bidi::logging::log_error("✗ Event method is empty");
                } else {
                    bidi::logging::log_info(
                        std::string("🎯 Log Event received: ") + event.method);
                }
                if (event.params.empty()) {
                    bidi::logging::log_error("✗ Event params are empty");
                } else {
                    bidi::logging::log_info(
                        std::string("   Log Params: ") +
                        boost::json::serialize(event.params));
                }
            });
        log_info("✓ Subscribed to bc.contextCreated event");

        return client;
    }

    auto run_flow() -> boost::asio::awaitable<int> {
        using namespace bidi;
        using namespace logging;
        using namespace commands::browsing_context;
        log_info("\n=== Connecting BiDi Client (await) ===");
        auto client_ptr =
            co_await await_async(Client::connect(io_context_, websocket_url_)
                                     .map(subscribe)
                                     .map(on_connect_no_async)
                                     .and_then(on_connect));
        if (!client_ptr) {
            log_error("✗ BiDi connect returned null");
            co_return 1;
        }
        bidi_client_ = client_ptr;
        log_info("✓ BiDi client connected");

        log_info("1. Creating browsing context...");
        auto context_id = co_await await_async(
            bidi_client_->create_context(CreateType::window));
        log_info(std::string("✓ Context: ") + context_id);

        log_info("2. Navigating...");
        auto nav_url = co_await await_async(
            bidi_client_->navigate(context_id, "https://example.com"));
        log_info(std::string("✓ Navigation OK: ") + nav_url);

        log_info("3. Evaluating document.title...");
        auto title_obj = co_await await_async(
            bidi_client_->evaluate("document.title", context_id));
        log_info(std::string("✓ Title: ") + boost::json::serialize(title_obj));

        co_await await_async(bidi_client_->evaluate(
            "console.log('Olá mundo! Este é um log')", context_id));

        log_info("4. Evaluating window.location.href...");
        auto loc_obj = co_await await_async(
            bidi_client_->evaluate("window.location.href", context_id));
        log_info(std::string("✓ Location: ") + boost::json::serialize(loc_obj));

        log_info("\n🎉 Flow completed successfully (await-based)");
        // attempt graceful disconnect so io_context can exit
        try {
            if (bidi_client_ && bidi_client_->session()) {
                bidi_client_->session()->disconnect();
            }
        } catch (const std::exception &e) {
            log_error(std::string("Disconnect error: ") + e.what());
        }
        co_return 0;
    }

    auto run_bidi_flow() -> boost::asio::awaitable<int> {
        using namespace bidi;
        using namespace logging;
        using namespace commands::browsing_context;
        try {
            co_return co_await run_flow();
        } catch (const std::exception &e) {
            log_error(std::string("✗ Exception in flow: ") + e.what());
        }
        co_return 1;
    }

    auto run() -> int {
        if (!initialize_session()) {
            return 1;
        }
        // timeout watchdog (seconds) - ajuste conforme necessário
        constexpr int WATCHDOG_SECONDS = 30;

        std::atomic<bool> finished{false};
        auto fut = boost::asio::co_spawn(io_context_, run_bidi_flow(),
                                         boost::asio::use_future);

        std::thread watchdog([&]() {
            for (int i = 0; i < WATCHDOG_SECONDS; ++i) {
                if (finished.load(std::memory_order_acquire)) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!finished.load(std::memory_order_acquire)) {
                bidi::logging::log_error(
                    "✗ Watchdog: timeout reached, stopping io_context_");
                try {
                    io_context_.stop();
                    bidi::logging::log_info(
                        "✗ Watchdog: io_context_.stop() called");
                } catch (const std::exception &e) {
                    bidi::logging::log_error(
                        std::string("✗ Watchdog: exception when stopping "
                                    "io_context_: ") +
                        e.what());
                } catch (...) {
                    bidi::logging::log_error("✗ Watchdog: unknown exception "
                                             "when stopping io_context_");
                }
            }
        });

        io_context_.run();
        finished.store(true, std::memory_order_release);
        if (watchdog.joinable()) {
            watchdog.join();
        }

        try {
            return fut.get();
        } catch (const std::exception &e) {
            bidi::logging::log_error(std::string("✗ Future exception: ") +
                                     e.what());
            return 1;
        }
    }

    void cleanup() {
        try {
            if (bidi_client_) {
                bidi_client_.reset();
            }
        } catch (const std::exception &e) {
            bidi::logging::log_error(std::string("Cleanup error: ") + e.what());
        }
    }
};

auto main() -> int {
    bidi::logging::log_info(
        "🚀 WebDriver Legacy + BiDi Integration Demo (Coroutines)");
    bidi::logging::log_info(
        "=======================================================");
    try {
        IntegratedWebDriverDemo demo;
        return demo.run();
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("❌ Demo failed: ") + e.what());
        return 1;
    }
}

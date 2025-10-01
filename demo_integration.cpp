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
#include "bidi/core.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_future.hpp>
// #include <iostream> removed (unused)
#include <string>

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

// #include <iostream> removed (unused)

namespace asio = boost::asio;

class IntegratedWebDriverDemo {
  private:
    asio::io_context io_context_;
    WebDriver legacy_driver_;
    std::shared_ptr<bidi::Client> bidi_client_;
    std::string session_id_;
    std::string websocket_url_;
    std::atomic<bool> finished{false};
    std::shared_ptr<boost::asio::steady_timer> watchdog_timer_;
    std::optional<bidi::Client::Subscription> subscription_;

  public:
    IntegratedWebDriverDemo() {
        using namespace bidi;
        legacy_driver_.webDriverUrl = "http://localhost:9515";
    }

    ~IntegratedWebDriverDemo() {
        using namespace bidi;
        cleanup();
    }

    // Rule of Five: Explicitly delete copy/move operations
    // This class manages non-copyable resources (WebDriver session, WebSocket)
    IntegratedWebDriverDemo(const IntegratedWebDriverDemo &) = delete;
    IntegratedWebDriverDemo &
    operator=(const IntegratedWebDriverDemo &) = delete;
    IntegratedWebDriverDemo(IntegratedWebDriverDemo &&) = delete;
    IntegratedWebDriverDemo &operator=(IntegratedWebDriverDemo &&) = delete;

    static auto initialize() -> void {
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
        } catch (const std::exception &exception) {
            log_error(std::string("✗ Session initialization failed: ") +
                      exception.what());
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
        -> asyncx::Async<std::shared_ptr<bidi::Client>> {
        using namespace bidi;
        using namespace logging;
        using bidi::core::ParsedEvent;

        // Type aliases para evitar repetição de tipos complexos
        using ClientPtr = std::shared_ptr<bidi::Client>;
        using ClientAsync = asyncx::Async<ClientPtr>;
        using SubscriptionPtr =
            std::shared_ptr<bidi::core::BiDiSession::Subscription>;
        using OptionalSubscription = std::optional<SubscriptionPtr>;
        using OptionalError = std::optional<asyncx::EC>;
        using ExceptionPtr = std::exception_ptr;

        auto session = client->session();

        if (!session) {
            log_error("✗ Cannot subscribe, session is null");
            auto result = ClientAsync::make(client->get_executor());
            result.fail(boost::system::error_code{});
            return result;
        }

        auto sub_async = session->subscribe_event(
            std::string(ids::events::bc_contextCreated),
            [](const ParsedEvent &event) {
                bidi::logging::log_info(std::string("🎯 Event received: ") +
                                        event.method);
                bidi::logging::log_info(std::string("   Params: ") +
                                        boost::json::serialize(event.params));
            });

        auto sub2_async = session->subscribe_event(
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

        // Wait for both subscriptions
        auto result = ClientAsync::make(client->get_executor());
        auto count = std::make_shared<std::atomic<int>>(0);
        auto on_complete = [result, client, count]() {
            if (++(*count) == 2) {
                log_info("✓ Subscribed to events");
                result.fulfill(client);
            }
        };

        auto finally_handler = [on_complete,
                                result](const OptionalSubscription &,
                                        const OptionalError &error_code,
                                        const ExceptionPtr &) {
            if (!error_code) {
                on_complete();
            } else {
                result.fail(*error_code);
            }
        };

        sub_async.finally(finally_handler);
        sub2_async.finally(finally_handler);

        return result;
    }

    auto run_flow() -> boost::asio::awaitable<int> {
        using namespace bidi;
        using namespace logging;
        using namespace commands::browsing_context;
        log_info("\n=== Connecting BiDi Client (await) ===");
        auto client_ptr = co_await Client::connect(io_context_, websocket_url_)
                              .and_then(subscribe)
                              .map(on_connect_no_async)
                              .and_then(on_connect)();
        if (!client_ptr) {
            log_error("✗ BiDi connect returned null");
            co_return 1;
        }
        bidi_client_ = client_ptr;
        log_info("✓ BiDi client connected");

        log_info("1. Creating browsing context...");
        auto context_id =
            co_await bidi_client_->create_context(CreateType::window)();
        log_info(std::string("✓ Context: ") + context_id);

        log_info("2. Navigating...");
        auto nav_url = co_await bidi_client_->navigate(context_id,
                                                       "https://example.com")();
        log_info(std::string("✓ Navigation OK: ") + nav_url);

        log_info("3. Evaluating document.title...");
        auto title_obj =
            co_await bidi_client_->evaluate("document.title", context_id)();
        log_info(std::string("✓ Title: ") + boost::json::serialize(title_obj));

        co_await bidi_client_->evaluate(
            "console.log('Olá mundo! Este é um log')", context_id)();

        log_info("4. Evaluating window.location.href...");
        auto loc_obj = co_await bidi_client_->evaluate("window.location.href",
                                                       context_id)();
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
        // signal completion and cancel watchdog to allow io_context to exit
        boost::asio::post(io_context_.get_executor(), [this]() {
            finished.store(true, std::memory_order_release);
            boost::system::error_code ec;
            if (watchdog_timer_) {
                watchdog_timer_->cancel(ec);
            }
        });
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

        finished.store(false, std::memory_order_release);
        auto fut = boost::asio::co_spawn(io_context_, run_bidi_flow(),
                                         boost::asio::use_future);

        // Substitui o watchdog que usava std::thread por um steady_timer
        watchdog_timer_ = std::make_shared<boost::asio::steady_timer>(
            io_context_.get_executor());
        watchdog_timer_->expires_after(std::chrono::seconds(WATCHDOG_SECONDS));
        watchdog_timer_->async_wait([&](const boost::system::error_code
                                            &error_code) {
            if (error_code == boost::asio::error::operation_aborted) {
                return; // foi cancelado porque finished ficou true
            }
            if (!finished.load(std::memory_order_acquire)) {
                bidi::logging::log_error(
                    "✗ Watchdog: timeout reached, stopping io_context_");
                try {
                    // post stop to executor to be thread-safe
                    boost::asio::post(io_context_.get_executor(), [this]() {
                        try {
                            io_context_.stop();
                            bidi::logging::log_info(
                                "✗ Watchdog: io_context_.stop() called");
                        } catch (const std::exception &e) {
                            bidi::logging::log_error(
                                std::string(
                                    "✗ Watchdog: exception when stopping "
                                    "io_context_: ") +
                                e.what());
                        } catch (...) {
                            bidi::logging::log_error(
                                "✗ Watchdog: unknown exception when stopping "
                                "io_context_");
                        }
                    });
                } catch (const std::exception &e) {
                    bidi::logging::log_error(
                        std::string(
                            "✗ Watchdog: exception when posting stop: ") +
                        e.what());
                }
            }
        });

        io_context_.run();
        finished.store(true, std::memory_order_release);
        // cancel watchdog timer if still pending
        boost::system::error_code ec;
        watchdog_timer_->cancel(ec);

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

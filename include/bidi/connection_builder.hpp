#pragma once
#include "bidi/client.hpp"
#include "bidi/guards.hpp"
#include <string>
#include <vector>

namespace bidi {

class ConnectionBuilder {
  public:
    static auto to(std::string_view webdriver_url) -> ConnectionBuilder {
        ConnectionBuilder builder;
        builder.webdriver_url_ = std::string(webdriver_url);
        return builder;
    }

    [[nodiscard]] auto with_capabilities(WebDriver::json caps) const
        -> ConnectionBuilder {
        ConnectionBuilder out = *this;
        out.capabilities_ = std::move(caps);
        return out;
    }

    [[nodiscard]] auto use_existing_websocket(std::string ws_url,
                                              std::string session_id = "") const
        -> ConnectionBuilder {
        ConnectionBuilder out = *this;
        out.existing_websocket_ = std::move(ws_url);
        out.existing_session_id_ = std::move(session_id);
        return out;
    }

    [[nodiscard]] auto with_chrome() const -> ConnectionBuilder {
        ConnectionBuilder out = *this;
        out.browser_type_ = "chrome";
        return out;
    }

    [[nodiscard]] auto headless() const -> ConnectionBuilder {
        ConnectionBuilder out = *this;
        out.browser_args_.emplace_back("--headless");
        return out;
    }

    [[nodiscard]] auto no_sandbox() const -> ConnectionBuilder {
        ConnectionBuilder out = *this;
        out.browser_args_.emplace_back("--no-sandbox");
        return out;
    }

    [[nodiscard]] auto with_args(std::vector<std::string> args) const
        -> ConnectionBuilder {
        ConnectionBuilder out = *this;
        out.browser_args_ = std::move(args);
        return out;
    }

    // Phase 1: HTTP handshake only (blocks, returns webSocketUrl and
    // SessionGuard for lifecycle management)
    [[nodiscard]] auto get_websocket_url()
        && -> std::pair<std::string, std::shared_ptr<SessionGuard>> {
        WebDriver::json args = WebDriver::json::array();
        for (const auto &arg : browser_args_) {
            args.push_back(arg);
        }

        if (existing_websocket_.has_value()) {
            // If existing websocket URL provided, use dummy SessionGuard
            auto dummy_guard = std::make_shared<SessionGuard>(webdriver_url_);
            return {*existing_websocket_, dummy_guard};
        }

        auto session_guard = std::make_shared<SessionGuard>(webdriver_url_);
        std::expected<std::string, std::string> ws;
        if (capabilities_.has_value()) {
            ws = session_guard->connect_with_payload(*capabilities_);
        } else {
            ws = session_guard->connect(args, browser_type_, true);
        }
        if (!ws) {
            throw std::runtime_error("Session connect failed: " + ws.error());
        }

        return {*ws, session_guard};
    }

    [[nodiscard]] auto
    connect(boost::asio::io_context &ioc) && -> Task<Client::Ptr> {
        // Perform the two-phase connect: SessionGuard HTTP handshake then
        // BiDi websocket Client::connect. Return Task<Client::Ptr> so the
        // caller can co_await it (note: still lazy; caller must call ()()).
        // If user provided full capabilities, use it directly
        WebDriver::json args = WebDriver::json::array();
        for (const auto &arg : browser_args_) {
            args.push_back(arg);
        }

        // Create shared SessionGuard so we can extend its lifetime by
        // capturing it inside the returned Async chain. This guarantees the
        // underlying WebDriver session is not quit while the BiDi client
        // is being established.
        // If an existing websocket url was provided, skip HTTP session creation
        if (existing_websocket_.has_value()) {
            return bidi::Client::connect(ioc, *existing_websocket_);
        }

        auto session_guard = std::make_shared<SessionGuard>(webdriver_url_);
        std::expected<std::string, std::string> ws;
        if (capabilities_.has_value()) {
            ws = session_guard->connect_with_payload(*capabilities_);
        } else {
            ws = session_guard->connect(args, browser_type_, true);
        }
        if (!ws) {
            // Create a failed Task
            auto task = Task<Client::Ptr>::make(ioc.get_executor());
            task.fail(std::make_exception_ptr(
                std::runtime_error("Session connect failed: " + ws.error())));
            return task;
        }

        // Capture session in the map continuation to keep it alive until the
        // Client::connect completes and the returned Client is materialized.
        return bidi::Client::connect(ioc, *ws).map(
            [session_keep = std::move(session_guard)](Client::Ptr client_ptr) {
                (void)session_keep; // keep session alive
                return client_ptr;
            });
    }

  private:
    std::string webdriver_url_{"http://localhost:9515"};
    std::vector<std::string> browser_args_;
    std::string browser_type_{"chrome"};
    std::optional<WebDriver::json> capabilities_;
    std::optional<std::string> existing_websocket_;
    std::optional<std::string> existing_session_id_;
};

inline auto connect_to(std::string_view url) -> ConnectionBuilder {
    return ConnectionBuilder::to(url);
}

} // namespace bidi

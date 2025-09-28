// Modern BiDi Client Demo - Using New Architecture
// (Atualizado: uso de ids.hpp + async_send genérico)

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <memory>

#include "bidi/client.hpp"
#include "bidi/logging.hpp"
#include "bidi_methods.hpp"

using namespace bidi;
using namespace std::chrono_literals;
namespace net = boost::asio;

net::awaitable<void> coroutine_flow(std::shared_ptr<Client> client,
                                    std::string ctx) {
    // Exemplo: usar generic async_send para script.evaluate (awaitPromise true)
    boost::json::object params{
        {"expression", "document.title"},
        {"target", boost::json::object{{"context", ctx}}},
        {"awaitPromise", true}};
    auto result = co_await client->async_send(
        std::string(bidi::ids::methods::script_evaluate), params,
        net::use_awaitable);
    bidi::logging::log_info(std::string("[co_await] title result: ") +
                            boost::json::serialize(result));
    co_return;
}

void example_new_bidi(net::io_context &ioc, const std::string &websocket_url) {
    bidi::logging::log_info(
        "=== New BiDi Core Architecture Demo (ids.hpp + async_send) ===");
    auto client_task = Client::connect(ioc, websocket_url);

    client_task.finally([&ioc](auto client_opt, auto ec_opt, auto ep) {
        if (ep || (ec_opt && *ec_opt) || !client_opt) {
            bidi::logging::log_error("Connection failed");
            ioc.stop();
            return;
        }
        auto client = *client_opt;
        bidi::logging::log_info("✓ Connected via Client::connect()");

        // Create context
        client->create_context().finally([client, &ioc](auto ctx_opt, auto ec2,
                                                        auto ep2) {
            if (ep2 || (ec2 && *ec2) || !ctx_opt) {
                bidi::logging::log_error("create_context failed");
                ioc.stop();
                return;
            }
            auto ctx = *ctx_opt;
            bidi::logging::log_info(std::string("✓ Context: ") + ctx);

            // Navigate using high-level API
            client->navigate(ctx, "https://example.com")
                .finally([client, ctx, &ioc](auto nav_opt, auto ec3, auto ep3) {
                    if (ep3 || (ec3 && *ec3) || !nav_opt) {
                        bidi::logging::log_error("navigate failed");
                        ioc.stop();
                        return;
                    }
                    bidi::logging::log_info(std::string("✓ Navigated to: ") +
                                            *nav_opt);

                    // Script evaluate with awaitable generic async_send
                    net::co_spawn(ioc, coroutine_flow(client, ctx),
                                  net::detached);

                    // Also show high-level wrapper evaluate
                    client->evaluate("document.readyState", ctx)
                        .finally([&ioc](auto res_opt, auto ec4, auto ep4) {
                            if (!ep4 && !(ec4 && *ec4) && res_opt) {
                                bidi::logging::log_info(
                                    std::string("✓ readyState: ") +
                                    boost::json::serialize(*res_opt));
                            }
                            ioc.stop();
                        });
                });
        });
    });
}

int main() {
    try {
        bidi::logging::log_info(
            "BiDi Client Demo with New Architecture (Enhanced)");
        net::io_context ioc;
        const std::string websocket_url =
            "ws://localhost:9222/devtools/browser"; // substituir por real
        example_new_bidi(ioc, websocket_url);
        ioc.run();
        bidi::logging::log_info("Demo completed.");
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Exception: ") + e.what());
        return 1;
    }
    return 0;
}

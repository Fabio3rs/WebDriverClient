// test_new_bidi.cpp — Test program for new BiDi Core architecture
#include "bidi/client.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
// #include <iostream> removed (unused)

namespace net = boost::asio;

int main() {
    try {
        net::io_context ioc;

        bidi::logging::log_info("Testing new BiDi Core architecture...");

        // Test 1: Create Client connection
        auto client_task =
            bidi::Client::connect(ioc, "ws://localhost:9222/devtools/browser");

        client_task.finally([&ioc](auto client_opt, auto ec_opt, auto ep) {
            if (ep) {
                bidi::logging::log_error("Connection failed with exception",
                                         std::error_code{}, ep);
                ioc.stop();
                return;
            }

            if (ec_opt && *ec_opt) {
                bidi::logging::log_error(std::string("Connection failed: ") +
                                             ec_opt->message(),
                                         *ec_opt);
                ioc.stop();
                return;
            }

            if (!client_opt) {
                bidi::logging::log_error("No client returned");
                ioc.stop();
                return;
            }

            auto client = *client_opt;
            bidi::logging::log_info("Client connected successfully");

            // Test 2: Create browsing context
            auto create_task = client->create_context(
                bidi::commands::browsing_context::CreateType::tab);

            create_task.finally([client, &ioc](auto context_opt, auto ec_opt,
                                               auto ep) {
                if (ep || (ec_opt && *ec_opt) || !context_opt) {
                    bidi::logging::log_error("Create context failed");
                    ioc.stop();
                    return;
                }

                auto context_id = *context_opt;
                bidi::logging::log_info(std::string("Created context: ") +
                                        context_id);

                // Test 3: Navigate
                auto nav_task =
                    client->navigate(context_id, "https://example.com");

                nav_task.finally([&ioc](auto url_opt, auto ec_opt, auto ep) {
                    if (ep || (ec_opt && *ec_opt) || !url_opt) {
                        bidi::logging::log_error("Navigation failed");
                    } else {
                        bidi::logging::log_info(std::string("Navigated to: ") +
                                                *url_opt);
                    }
                    ioc.stop();
                });
            });
        });

        // Run event loop
        ioc.run();

        bidi::logging::log_info("Test completed");

    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Exception: ") + e.what());
        return 1;
    }

    return 0;
}

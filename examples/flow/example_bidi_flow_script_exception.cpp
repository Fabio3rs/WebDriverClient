// Example: Demonstrate handling of a JavaScript exception thrown inside
// script.evaluate Focus: show how current high-level Client surfaces script
// errors (runtime) as std::runtime_error NOTE: This example intentionally
// triggers a ReferenceError in page context.

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/connection_builder.hpp"
#include "bidi/guards.hpp"
#include "bidi/logging.hpp"
#include "bidi/script_eval.hpp"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <memory>
#include <string>

namespace asio = boost::asio;

namespace {

auto run_script_test(bidi::ClientGuard &guard,
                     const std::string &ctx) -> asio::awaitable<int> {
    using namespace std::chrono_literals;
    using bidi::script::script_eval_policy::throw_on_script_exception;

    const std::string failing_expression =
        "(() => { throw new Error('IntentionalScriptError: variable not "
        "defined'); })()";

    std::cout << "\n\n\n";
    auto resa = guard.client()->evaluate(failing_expression, ctx,
                                         throw_on_script_exception, true);
    bidi::logging::log_info(
        "Waiting 100ms before awaiting the failing evaluation...");

    // Use the client's executor for the timer (concise constructor)
    auto ex = guard.client()->get_executor();
    asio::steady_timer timer(ex, 100ms);
    co_await timer.async_wait(asio::use_awaitable);

    bidi::logging::log_info("Awaiting the evaluation that should fail "
                            "with script error...");

    auto res = co_await resa();

    std::cout << "\n\n\n";
    bidi::logging::log_error(
        "Expected script exception but evaluation succeeded: " +
        boost::json::serialize(res.raw));

    co_return 1;
}

} // namespace

// Coroutine that operates on an already-connected Client
auto run_script_exception_flow_with_client(
    std::shared_ptr<bidi::Client> client_ptr) -> asio::awaitable<int> {
    try {
        bidi::ClientGuard guard(client_ptr);
        auto ctx = co_await guard.client()->create_context()();
        (void)co_await guard.client()->navigate(ctx, "https://example.com")();

        try {
            co_await run_script_test(guard, ctx);
            co_return 1;
        } catch (const bidi::script::ScriptEvaluateException &e) {
            bidi::logging::log_info(
                std::string("Successfully captured script exception: ") +
                e.what());
            const auto &details = e.details();
            bidi::logging::log_info("  exception_type: " +
                                    details.exception_type);
            bidi::logging::log_info("  text: " + details.text);
            bidi::logging::log_info("  value: " + details.value);
            bidi::logging::log_info("  name: " + details.name);
            bidi::logging::log_info("  error_type: " + details.error_type);
            if (details.line_number.has_value()) {
                bidi::logging::log_info("  line_number: " +
                                        std::to_string(*details.line_number));
            }
            if (details.column_number.has_value()) {
                bidi::logging::log_info("  column_number: " +
                                        std::to_string(*details.column_number));
            }
            bidi::logging::log_info("  raw: " +
                                    boost::json::serialize(details.raw));
            if (!details.stack_frames.empty()) {
                bidi::logging::log_info("  stack_frames:");
                for (const auto &frame : details.stack_frames) {
                    bidi::logging::log_info("    function_name: " +
                                            frame.function_name);
                    bidi::logging::log_info("    url: " + frame.url);
                    bidi::logging::log_info("    line_number: " +
                                            std::to_string(frame.line_number));
                    bidi::logging::log_info(
                        "    column_number: " +
                        std::to_string(frame.column_number));
                }
            }
            co_return 0;
        } catch (const std::exception &e) {
            bidi::logging::log_info(std::string("Captured script exception: ") +
                                    e.what());
            co_return 0;
        } catch (...) {
            bidi::logging::log_error(
                "Unknown non-std exception while evaluating failing script");
            co_return 1;
        }
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Fatal error in coroutine: ") +
                                 e.what());
        co_return 1;
    }
}

auto main() -> int {
    bidi::logging::log_info("BiDi Script Exception Example");
    try {
        asio::io_context ioc;
        int exit_code = 1;
        // Starter coroutine that connects and runs the flow. No lambda capture
        auto starter = [](asio::io_context *ioc_ptr) -> asio::awaitable<int> {
            try {
                // Optional: demonstrate how to reuse an existing websocket URL
                // auto client_ptr = co_await
                // bidi::connect_to("http://localhost:9515")
                //                           .use_existing_websocket("ws://host:port/session/...")
                //                           .connect(*ioc_ptr)();

                auto client_ptr =
                    co_await bidi::connect_to("http://localhost:9515")
                        .headless()
                        .no_sandbox()
                        .connect(*ioc_ptr)();
                co_return co_await run_script_exception_flow_with_client(
                    client_ptr);
            } catch (const std::exception &ex) {
                bidi::logging::log_error(
                    std::string("Failed to connect BiDi client: ") + ex.what());
                co_return 1;
            }
        };

        // Spawn the starter coroutine
        asio::co_spawn(
            ioc, starter(&ioc), [&](const std::exception_ptr &exc, int result) {
                if (exc) {
                    try {
                        std::rethrow_exception(exc);
                    } catch (const std::exception &e) {
                        bidi::logging::log_error(
                            std::string("Unhandled exception: ") + e.what());
                    }
                    exit_code = 1;
                } else {
                    exit_code = result;
                }
            });

        ioc.run();
        return exit_code;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Fatal top-level exception: ") +
                                 e.what());
        return 1;
    }
}

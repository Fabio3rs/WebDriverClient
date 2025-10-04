// Example: Demonstrate handling of a JavaScript exception thrown inside
// script.evaluate Focus: show how current high-level Client surfaces script
// errors (runtime) as std::runtime_error NOTE: This example intentionally
// triggers a ReferenceError in page context.

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
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

// Coroutine that creates context, navigates and triggers a JS exception.
asio::awaitable<int> run_script_exception_flow(std::string websocket_url,
                                               asio::io_context &io_context_) {
    try {
        auto client_ptr =
            co_await bidi::Client::connect(io_context_, websocket_url)();
        if (!client_ptr) {
            bidi::logging::log_error("Failed to connect BiDi client");
            co_return 1;
        }
        bidi::ClientGuard guard(client_ptr);
        auto ctx = co_await guard.client()->create_context()();
        (void)co_await guard.client()->navigate(ctx, "https://example.com")();

        using namespace std::chrono_literals;
        using bidi::script::script_eval_policy::throw_on_script_exception;

        // Intentionally evaluate an expression that throws: accessing undefined
        // variable triggers ReferenceError.
        const std::string failing_expression =
            "(() => { throw new Error('IntentionalScriptError: variable not "
            "defined'); })()";
        try {
            std::cout << "\n\n\n";
            auto resa = guard.client()->evaluate(
                failing_expression, ctx, throw_on_script_exception, true);
            bidi::logging::log_info(
                "Waiting 100ms before awaiting the failing evaluation...");

            // Don't block the io_context thread: use an awaitable timer so
            // other async operations (including the websocket write) can
            // progress while we wait.
            asio::steady_timer timer(io_context_, 100ms);
            co_await timer.async_wait(asio::use_awaitable);

            bidi::logging::log_info("Awaiting the evaluation that should fail "
                                    "with script error...");

            auto res = co_await resa();

            std::cout << "\n\n\n";
            // If we reach here the driver did not surface the error as
            // expected.
            bidi::logging::log_error(
                "Expected script exception but evaluation succeeded: " +
                boost::json::serialize(res.raw));
            co_return 1;
        } catch (const bidi::script::ScriptEvaluateException &e) {
            // This is the expected path: we caught the script exception
            // wrapped in ScriptEvaluateException
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
            co_return 0; // success path of the example: we demonstrated capture
        } catch (const std::exception &e) {
            // Current implementation wraps everything into std::runtime_error.
            bidi::logging::log_info(std::string("Captured script exception: ") +
                                    e.what());
            co_return 0; // success path of the example: we demonstrated capture
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

int main() {
    bidi::logging::log_info("BiDi Script Exception Example");
    try {
        // Acquire websocket URL via SessionGuard (legacy HTTP handshake)
        // enabling BiDi
        bidi::SessionGuard session("http://localhost:9515");
        WebDriver::json args =
            WebDriver::json::array({"--headless", "--no-sandbox"});
        auto ws_url_res = session.connect(args, "chrome", true);
        if (!ws_url_res) {
            bidi::logging::log_error(ws_url_res.error());
            return 1;
        }
        asio::io_context ioc;
        int exit_code = 1;
        asio::co_spawn(ioc, run_script_exception_flow(*ws_url_res, ioc),
                       [&](std::exception_ptr ep, int result) {
                           if (ep) {
                               try {
                                   std::rethrow_exception(ep);
                               } catch (const std::exception &e) {
                                   bidi::logging::log_error(
                                       std::string("Unhandled exception: ") +
                                       e.what());
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

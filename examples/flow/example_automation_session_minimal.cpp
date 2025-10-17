// Example: Minimal automation session usage with AutomationSession facade
// Demonstrates type-safe API usage with zero manual JSON parsing

#include "bidi/automation_session.hpp"
#include "bidi/logging.hpp"
#include "bidi/script_eval.hpp"
#include <boost/asio/awaitable.hpp>
#include <iostream>

namespace asio = boost::asio;

// AutomationSession provides 85% boilerplate reduction
// This example demonstrates TYPE-SAFE API patterns (no manual JSON parsing)

auto main() -> int {
    try {
        // Phase 1: Blocking setup (acceptable for one-time initialization)
        auto session = bidi::AutomationSession::start();

        // Phase 2: Async workflow demonstrating TYPE-SAFE API
        // SAFETY: session.run() is a blocking call that executes the coroutine
        // to completion before returning. The session object's lifetime is
        // guaranteed to outlive the lambda execution. Reference capture is
        // safe.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        return session.run([&session]() -> asio::awaitable<int> {
            // Navigate to example.com (direct co_await on temporary)
            co_await session.navigate("https://example.com");

            // ========== CONVENIENCE WRAPPERS ==========
            // Recommended for common operations

            auto title = co_await session.get_title();
            std::cout << "Page title: " << title << "\n";

            auto url = co_await session.get_url();
            std::cout << "Current URL: " << url << "\n";

            // ========== TYPE-SAFE EVALUATION ==========
            // Eliminates manual JSON parsing, provides compile-time type safety

            // String extraction (recommended approach)
            auto state = co_await session.evaluate_as<std::string>(
                "document.readyState");
            std::cout << "Ready state: " << state << "\n";

            // Integer extraction
            auto link_count =
                co_await session.evaluate_as<int>("document.links.length");
            std::cout << "Link count: " << link_count << "\n";

            // Boolean extraction
            auto has_focus =
                co_await session.evaluate_as<bool>("document.hasFocus()");
            std::cout << "Has focus: " << (has_focus ? "yes" : "no") << "\n";

            // ========== EXTRACTION WITH FALLBACK ==========
            // No exceptions thrown if evaluation fails or value is missing

            auto meta_count = co_await session.evaluate_as_or(
                "document.querySelectorAll('meta').length", 0);
            std::cout << "Meta tags: " << meta_count << "\n";

            auto description = co_await session.evaluate_as_or(
                "document.querySelector('meta[name=\"description\"]')?.content",
                std::string("No description"));
            std::cout << "Description: " << description << "\n";

            // ========== ADVANCED: POLICY-AWARE EVALUATION ==========
            // Provides rich exception details when scripts fail

            using namespace bidi::script;

            // Policy-aware evaluation for better error handling
            auto outcome = co_await session.evaluate_outcome(
                "document.title", script_eval_policy::return_outcome);

            if (outcome.has_exception()) {
                std::cerr << "Script error: " << outcome.exception->text
                          << "\n";
                if (outcome.exception->line_number) {
                    std::cerr << "  at line " << *outcome.exception->line_number
                              << "\n";
                }
            } else {
                // Extract from outcome with type safety
                auto title_str =
                    extract_value_from_outcome<std::string>(outcome);
                std::cout << "Title (from outcome): " << title_str << "\n";
            }

            // ========== DEMONSTRATING SCRIPT EXCEPTION HANDLING ==========
            // Type-safe evaluation with exception on script error

            try {
                // This will throw ScriptEvaluateException
                auto invalid = co_await session.evaluate_as<std::string>(
                    "nonexistent.property.access");
                std::cout << "Should not reach here: " << invalid << "\n";
            } catch (const bidi::script::ScriptEvaluateException &e) {
                // Rich exception details available
                std::cout << "Expected script exception caught: " << e.what()
                          << "\n";
            }

            co_return 0; // Success
        });

    } catch (const bidi::script::ScriptEvaluateException &e) {
        // Handle script-specific errors
        bidi::logging::log_error(std::string("Script error: ") + e.what());
        return 1;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Fatal error: ") + e.what());
        return 1;
    }
}

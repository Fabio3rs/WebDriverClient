#pragma once
/**
 * @file wait_helpers.hpp
 * @brief Zero busy-wait element waiting using browser MutationObserver.
 *
 * Architectural rationale:
 * - Zero busy-wait on browser side: Uses MutationObserver instead of polling
 *   loops to detect when elements appear in the DOM. This is consistent with
 *   the project's zero busy-wait principle.
 * - Event-driven on client side: Returns a Promise that resolves when the
 *   element appears or timeout expires, integrating with BiDi's async model.
 * - Efficient DOM watching: MutationObserver is a browser-native primitive
 *   that suspends until DOM changes occur (kernel-level event notification).
 * - Timeout handling: Explicit timeout with automatic cleanup (observer
 *   disconnection) prevents resource leaks in the browser context.
 *
 * Integration with architecture:
 * - Used via script.evaluate BiDi command (promise-based execution)
 * - await_promise=true enables co_await on client side
 * - No CPU waste on either browser or client side
 *
 * @note This script is meant to be evaluated in the browser context via
 *       BiDi script.evaluate command with await_promise=true.
 */

#include <string>

namespace webdriver::wait {

inline const char *wait_for_element_script = R"js(
(function waitForElement(selector, selectorType, timeout = 5000) {
    return new Promise((resolve, reject) => {
        function getElement(selector, type) {
            if (type === "css" || type === "css selector") {
                return document.querySelector(selector);
            } else if (type === "xpath") {
                return document.evaluate(
                    selector,
                    document,
                    null,
                    XPathResult.FIRST_ORDERED_NODE_TYPE,
                    null
                ).singleNodeValue;
            }
            throw new Error("Unsupported selector type: " + type);
        }

        const existingElement = getElement(selector, selectorType);
        if (existingElement) { resolve(existingElement); return; }

        const observer = new MutationObserver(() => {
            const element = getElement(selector, selectorType);
            if (element) {
                if (timeoutId) { clearTimeout(timeoutId); }
                observer.disconnect();
                resolve(element);
            }
        });

        observer.observe(document.body, { childList: true, subtree: true });

        const timeoutId = setTimeout(() => {
            observer.disconnect();
            const element = getElement(selector, selectorType);
            if (element) { resolve(element); return; }
            reject(new Error(`Timeout reached: Element "${selector}" not found.`));
        }, timeout);
    });
})(arguments[0], arguments[1], arguments[2]);
)js";

} // namespace webdriver::wait

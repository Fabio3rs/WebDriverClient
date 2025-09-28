#pragma once

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

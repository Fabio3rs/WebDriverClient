/**
 * @file tests/network_intercept_validation_helpers.cpp
 * @brief Implementação das functions de validação para network interception
 *
 * Implementações das funções definidas em
 * network_intercept_validation_helpers.hpp
 */

#include "network_intercept_validation_helpers.hpp"
#include "bidi/client.hpp"
#include <bidi/commands/util.hpp>
#include <cassert>
#include <memory>
#include <vector>

// Provide the small detail helpers (JS snippets and JSON parsing) expected by
// the implementation. These were previously declared in a 'detail' namespace.
namespace bidi::testing::detail {

// JavaScript helper that performs a fetch and returns a structured result
// with diagnostic fields (exception message, userAgent) to aid debugging.
// Note: Arguments are passed via call_function, so we use a simpler function
// body
constexpr std::string_view fetch_js_impl = R"js(
async function(url) {
    const diagnostics = { userAgent: navigator.userAgent };
    try {
        console.log('[diagnostic] fetch start', url);
        const resp = await fetch(url);
        const text = await resp.text();
        const headers = {};
        for (const [k, v] of resp.headers.entries()) headers[k] = v;
        diagnostics.status = resp.status;
        diagnostics.statusText = resp.statusText;
        console.log('[diagnostic] fetch success', url, resp.status);
        return { status: resp.status, statusText: resp.statusText, body: text, headers, diagnostics };
    } catch (e) {
        diagnostics.exception = String(e && e.message ? e.message : e);
        console.log('[diagnostic] fetch exception', diagnostics.exception);
        return { status: null, statusText: null, body: null, headers: {}, diagnostics };
    }
}
)js";

// JS helper that returns whether a fetch fails (blocked).
// Returns { blocked: true/false }
// Verify blocked now returns diagnostic information to help identify why
// a fetch may have succeeded or failed when an interceptor is active.
constexpr std::string_view verify_blocked_js_impl = R"js(
async function(url) {
    const diagnostics = { userAgent: navigator.userAgent };
    try {
        console.log('[diagnostic] verify_blocked fetch', url);
        await fetch(url);
        console.log('[diagnostic] verify_blocked fetch succeeded', url);
        return { blocked: false, diagnostics };
    } catch (e) {
        diagnostics.exception = String(e && e.message ? e.message : e);
        console.log('[diagnostic] verify_blocked fetch exception', diagnostics.exception);
        return { blocked: true, diagnostics };
    }
}
)js";

// Helper to extract string value from BiDi wrapped value or direct string
inline auto extract_string_value(const boost::json::value &val) -> std::string {
    if (val.is_string()) {
        // Direct string
        return std::string(val.as_string().c_str());
    }
    if (val.is_object()) {
        // BiDi wrapped: {"type":"string","value":"..."}
        if (const auto *value_ptr = val.as_object().if_contains("value")) {
            if (value_ptr->is_string()) {
                return std::string(value_ptr->as_string().c_str());
            }
        }
    }
    return {};
}

// Helper to extract int64 value from BiDi wrapped value or direct number
inline auto extract_int_value(const boost::json::value &val) -> int {
    if (val.is_int64()) {
        // Direct number
        return static_cast<int>(val.as_int64());
    }
    if (val.is_object()) {
        // BiDi wrapped: {"type":"number","value":201}
        if (const auto *value_ptr = val.as_object().if_contains("value")) {
            if (value_ptr->is_int64()) {
                return static_cast<int>(value_ptr->as_int64());
            }
        }
    }
    return 0;
}

// Helper to extract headers from BiDi-wrapped object format
inline void
extract_headers_from_bidi_object(const boost::json::object &headers_obj,
                                 FetchResult &result) {
    // Check if it's BiDi wrapped: {"type":"object","value":[[key,val],...]}
    if (const auto *value_ptr = headers_obj.if_contains("value")) {
        if (!value_ptr->is_array()) {
            return;
        }
        for (const auto &item : value_ptr->as_array()) {
            if (!item.is_array() || item.as_array().size() != 2) {
                continue;
            }
            const auto &key_val = item.as_array()[0];
            const auto &val_val = item.as_array()[1];

            std::string key_str;
            if (key_val.is_string()) {
                key_str = std::string(key_val.as_string().c_str());
            }

            std::string value_str = extract_string_value(val_val);
            if (!key_str.empty() || !value_str.empty()) {
                result.headers.emplace_back(key_str, value_str);
            }
        }
    }
}

// Helper to extract headers from direct object format
inline void
extract_headers_from_direct_object(const boost::json::object &headers_obj,
                                   FetchResult &result) {
    for (const auto &pair : headers_obj) {
        result.headers.emplace_back(std::string(pair.key()),
                                    extract_string_value(pair.value()));
    }
}

// Convert the JS outcome (boost::json::value) to FetchResult structure.
// Handles both direct values and BiDi-wrapped values:
// - Direct: {"status": 201, "statusText": "OK", "body": "...", "headers":
// {...}}
// - BiDi wrapped: {"status": {"type":"number","value":201}, ...}
// Helper to parse BiDi result value array format into a property map
inline auto parse_bidi_value_array(const boost::json::array &value_array)
    -> std::unordered_map<std::string, boost::json::value> {
    std::unordered_map<std::string, boost::json::value> props;

    // BiDi format: [["key1", value1], ["key2", value2], ...]
    for (const auto &item : value_array) {
        if (!item.is_array() || item.as_array().size() != 2) {
            continue;
        }
        const auto &key_val = item.as_array()[0];
        const auto &val_val = item.as_array()[1];

        if (key_val.is_string()) {
            std::string key_str(key_val.as_string().c_str());
            props[key_str] = val_val;
        }
    }

    return props;
}

inline FetchResult json_to_fetch_result(const boost::json::object &obj) {
    FetchResult res;

    try {
        // Extract result.value which is an array of [key, value] pairs
        const auto &result = obj.at("result");
        if (!result.is_object()) {
            return res;
        }

        const auto &result_obj = result.as_object();
        const auto *const value_it = result_obj.if_contains("value");
        if (value_it == nullptr || !value_it->is_array()) {
            return res;
        }

        // Parse BiDi array format to property map
        auto props = parse_bidi_value_array(value_it->as_array());

        if (props.empty()) {
            return res;
        }

        // Extract status code
        if (const auto it = props.find("status"); it != props.end()) {
            res.status_code = extract_int_value(it->second);
            constexpr int http_success_min = 200;
            constexpr int http_success_max = 300;
            res.is_success = (res.status_code >= http_success_min &&
                              res.status_code < http_success_max);
        }

        // Extract status text
        if (const auto it = props.find("statusText"); it != props.end()) {
            res.status_text = extract_string_value(it->second);
        }

        // Extract body
        if (const auto it = props.find("body"); it != props.end()) {
            res.body = extract_string_value(it->second);
        }

        // Extract headers - handles BiDi object format with nested key-value
        // pairs
        if (const auto it = props.find("headers"); it != props.end()) {
            const auto &headers_val = it->second;
            if (headers_val.is_object()) {
                const auto &headers_obj = headers_val.as_object();

                // Check if it's BiDi wrapped format first
                if (headers_obj.if_contains("value") != nullptr) {
                    extract_headers_from_bidi_object(headers_obj, res);
                } else {
                    // Direct object format
                    extract_headers_from_direct_object(headers_obj, res);
                }
            }
        }
    } catch (const std::exception &) {
        // If parsing fails, return empty result
        return res;
    }

    return res;
}

} // namespace bidi::testing::detail

namespace bidi::testing {

auto make_fetch_request(const std::shared_ptr<bidi::Client> &client,
                        std::string_view context_id, std::string_view url)
    -> bidi::Task<FetchResult> {

    // Create LocalValue argument: { "type": "string", "value": url }
    boost::json::array arguments;
    arguments.push_back(bidi::commands::local_value_string(url));

    auto session = client->session();
    auto logs = std::make_shared<std::vector<std::string>>();

    return session
        ->subscribe_event_async(
            std::string(bidi::ids::events::log_entryAdded),
            [logs](const bidi::core::ParsedEvent &evt) {
                try {
                    boost::json::value params = evt.params;
                    logs->push_back(boost::json::serialize(params));
                } catch (const std::exception &e) {
                    logs->push_back(std::string("[error serializing log] ") +
                                    e.what());
                    assert(false && "Exception serializing log event");
                }
            })
        .and_then([client, context_id = std::string(context_id), arguments,
                   logs](std::shared_ptr<bidi::core::BiDiSession::Subscription>
                             subscription)
                      -> decltype(client->call_function(
                          detail::fetch_js_impl, context_id, arguments,
                          bidi::script::script_eval_policy::
                              throw_on_script_exception)) {
            // keep 'subscription' alive by releasing it
            subscription->release();
            return client->call_function(
                detail::fetch_js_impl, context_id, arguments,
                bidi::script::script_eval_policy::throw_on_script_exception);
        })
        .map([logs](script::ScriptEvalOutcome outcome) {
            FetchResult res;
            if (!outcome.result.is_null() && outcome.result.is_object()) {
                res = detail::json_to_fetch_result(outcome.result.as_object());
            }
            if (!logs->empty()) {
                res.body += "\n[console_logs]\n";
                for (const auto &entry_str : *logs) {
                    res.body += entry_str + "\n";
                }
            }
            return res;
        });
}

// Helper to extract boolean from nested BiDi object representation
inline auto extract_blocked_value(const boost::json::object &obj) -> bool {
    // Try direct property first
    if (const auto *ptr = obj.if_contains("blocked")) {
        if (ptr->is_bool()) {
            return ptr->as_bool();
        }
    }

    // Check for complex object: {"type":"object","value":[[key,val],...]}
    if (const auto *value_ptr = obj.if_contains("value")) {
        if (!value_ptr->is_array()) {
            return false;
        }
        for (const auto &item : value_ptr->as_array()) {
            if (!item.is_array() || item.as_array().size() != 2) {
                continue;
            }
            const auto &key = item.as_array()[0];
            const auto &val = item.as_array()[1];
            std::string key_str;
            if (key.is_string()) {
                key_str = std::string(key.as_string().c_str());
            }
            if (key_str != "blocked") {
                continue;
            }
            // Extract bool from val
            if (val.is_bool()) {
                return val.as_bool();
            }
            if (val.is_object()) {
                if (const auto *inner = val.as_object().if_contains("value")) {
                    if (inner->is_bool()) {
                        return inner->as_bool();
                    }
                }
            }
        }
    }

    return false;
}

auto make_verify_request_blocked(const std::shared_ptr<bidi::Client> &client,
                                 std::string_view context_id,
                                 std::string_view url) -> bidi::Task<bool> {
    boost::json::array arguments;
    arguments.push_back(bidi::commands::local_value_string(url));

    auto session = client->session();
    auto logs = std::make_shared<std::vector<std::string>>();

    return session
        ->subscribe_event_async(
            std::string(bidi::ids::events::log_entryAdded),
            [logs](const bidi::core::ParsedEvent &evt) {
                try {
                    std::cout
                        << "Log entry: " << boost::json::serialize(evt.params)
                        << std::endl;
                    boost::json::value params = evt.params;
                    logs->push_back(boost::json::serialize(params));
                } catch (...) {
                }
            })
        .and_then(
            [client, context_id = std::string(context_id), arguments,
             logs](const std::shared_ptr<bidi::core::BiDiSession::Subscription>
                       &subscription)
                -> decltype(client->call_function(
                    detail::verify_blocked_js_impl, context_id, arguments,
                    bidi::script::script_eval_policy::return_outcome)) {
                subscription->release();
                return client->call_function(
                    detail::verify_blocked_js_impl, context_id, arguments,
                    bidi::script::script_eval_policy::return_outcome);
            })
        .map([](script::ScriptEvalOutcome outcome) -> bool {
            std::cout << "Log entry: " << boost::json::serialize(outcome.raw)
                      << std::endl;
            if (outcome.has_exception()) {
                return true; // Exception = blocked
            }
            if (!outcome.result.is_object()) {
                return false;
            }

            const auto &result_obj = outcome.result.as_object();

            // The result has structure: {realm, result: {type, value, handle},
            // type} We need to extract the nested 'result' object
            if (const auto *result_ptr = result_obj.if_contains("result")) {
                if (result_ptr->is_object()) {
                    return extract_blocked_value(result_ptr->as_object());
                }
            }

            // Fallback: treat as direct object (for testing purposes)
            return extract_blocked_value(result_obj);
        });
}

auto make_verify_response_body(const std::shared_ptr<bidi::Client> &client,
                               std::string_view context_id,
                               std::string_view url,
                               std::string_view expected_body)
    -> bidi::Task<bool> {

    return make_fetch_request(client, context_id, url)
        .map([expected_body](const FetchResult &result) {
            return result.body == expected_body;
        });
}

auto make_verify_response_headers(
    const std::shared_ptr<bidi::Client> &client, std::string_view context_id,
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>> &expected_headers)
    -> bidi::Task<bool> {

    return make_fetch_request(client, context_id, url)
        .map([expected_headers](const FetchResult &result) {
            return std::ranges::all_of(expected_headers, [&](const auto &hdr) {
                return result.has_header(hdr.first, hdr.second);
            });
        });
}

auto make_verify_response_status(const std::shared_ptr<bidi::Client> &client,
                                 std::string_view context_id,
                                 std::string_view url, int expected_status)
    -> bidi::Task<bool> {

    return make_fetch_request(client, context_id, url)
        .map([expected_status](const FetchResult &result) {
            return result.status_code == expected_status;
        });
}

} // namespace bidi::testing

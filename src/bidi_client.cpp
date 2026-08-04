// src/bidi_client.cpp — High-level BiDi Client implementation
#include <format>
#include <memory>
#include <utility>

#include "bidi/client.hpp"
#include "bidi/core.hpp"
#include "bidi/ids.hpp"
#include "bidi/logging.hpp"
#include "bidi/script_eval.hpp"
#include "bidi_methods.hpp"

// script_eval.hpp já fornece declarações necessárias

namespace bidi {

// ======================== Client Implementation ========================

Client::Client(std::shared_ptr<core::BiDiSession> session)
    : session_(std::move(session)) {}

auto Client::connect(boost::asio::io_context &ioc,
                     std::string_view websocket_url,
                     std::source_location loc) -> Task<Client::Ptr> {
    auto ex = ioc.get_executor();
    auto result = Task<Ptr>::make(ex, loc);
    auto ws_client = std::make_shared<core::WebSocketClient>(ioc);
    auto session = std::make_shared<core::BiDiSession>(ws_client);
    session->async_start(
        websocket_url,
        [result, session](boost::system::error_code error_code) mutable {
            if (error_code) {
                result.fail(std::make_exception_ptr(
                    std::runtime_error("Failed to connect to BiDi WebSocket: " +
                                       error_code.message())));
                return;
            }
            auto client = std::make_shared<Client>(session);
            result.fulfill(client);
        });
    return result;
}

auto Client::get_executor() const -> boost::asio::any_io_executor {
    return session_->get_executor();
}

// ======================== BrowsingContext API ========================

auto Client::create_user_context(const boost::json::object &params,
                                 const std::source_location &loc)
    -> Task<std::string> {
    auto ex = get_executor();
    auto result = Task<std::string>::make(ex);
    session_->send_command(
        bidi::ids::methods::browser_createUserContext, params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browser.createUserContext failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            const auto *context_it = response.result.find("userContext");
            if (context_it != response.result.end() &&
                context_it->value().is_string()) {
                result.fulfill(context_it->value().as_string().c_str());
            } else {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "Invalid browser.createUserContext response")));
            }
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::create_context(commands::browsing_context::CreateType type,
                            const std::source_location &loc)
    -> Task<std::string> {
    auto ex = get_executor();
    auto result = Task<std::string>::make(ex);
    auto params = commands::browsing_context::create(type);
    session_->send_command(
        bidi::ids::methods::bc_create, params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.create failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            const auto *context_it = response.result.find("context");
            if (context_it != response.result.end() &&
                context_it->value().is_string()) {
                result.fulfill(context_it->value().as_string().c_str());
            } else {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "Invalid browsingContext.create response")));
            }
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::navigate(std::string_view context, std::string_view url,
                      commands::browsing_context::ReadinessState wait,
                      const std::source_location &loc) -> Task<std::string> {
    auto ex = get_executor();
    auto result = Task<std::string>::make(ex);
    auto params = commands::browsing_context::navigate(context, url, wait);
    session_->send_command(
        bidi::ids::methods::bc_navigate, params,
        [result, url](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.navigate failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            const auto *url_it = response.result.find("url");
            if (url_it != response.result.end() &&
                url_it->value().is_string()) {
                result.fulfill(url_it->value().as_string().c_str());
            } else {
                // convert string_view to std::string for Task fulfillment
                result.fulfill(std::string(url));
            }
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::reload(std::string_view context, bool ignore_cache,
                    commands::browsing_context::ReadinessState wait,
                    const std::source_location &loc) -> Task<std::string> {
    auto ex = get_executor();
    auto result = Task<std::string>::make(ex);
    auto params =
        commands::browsing_context::reload(context, ignore_cache, wait);
    session_->send_command(
        "browsingContext.reload", params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.reload failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            const auto *url_it = response.result.find("url");
            if (url_it != response.result.end() &&
                url_it->value().is_string()) {
                result.fulfill(std::string(url_it->value().as_string()));
            } else {
                result.fulfill(std::string{});
            }
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::activate(std::string_view context,
                      const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex);
    auto params = commands::browsing_context::activate(context);
    session_->send_command(
        "browsingContext.activate", params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.activate failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill();
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::close_context(std::string_view context,
                           const std::source_location &loc) -> Task<bool> {
    auto ex = get_executor();
    auto result = Task<bool>::make(ex);
    auto params = commands::browsing_context::close(context);
    session_->send_command(
        bidi::ids::methods::bc_close, params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.close failed: " + response.error_code_raw +
                    " - " + response.error_message)));
                return;
            }
            result.fulfill(true);
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::get_context_tree(std::string_view root,
                              const std::source_location &loc)
    -> Task<boost::json::object> {
    auto ex = get_executor();
    auto result = Task<boost::json::object>::make(ex);
    auto params = commands::browsing_context::get_tree(root);
    session_->send_command(
        bidi::ids::methods::bc_getTree, params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.getTree failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill(response.result);
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::handle_user_prompt(std::string_view context,
                                std::optional<bool> accept,
                                std::optional<std::string_view> user_text,
                                const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex);
    auto params = commands::browsing_context::handle_user_prompt(
        context, accept, user_text);
    session_->send_command(
        bidi::ids::methods::bc_handleUserPrompt, params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.handleUserPrompt failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill();
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::locate_nodes(std::string_view context,
                          const types::browsing_context::Locator &locator,
                          std::optional<std::uint64_t> max_node_count,
                          std::optional<std::string_view> sandbox,
                          std::optional<std::vector<std::string>> start_nodes,
                          const std::source_location &loc)
    -> Task<std::vector<types::script::NodeRemoteValue>> {
    auto ex = get_executor();
    auto result = Task<std::vector<types::script::NodeRemoteValue>>::make(ex);
    auto params = commands::browsing_context::locate_nodes(
        context, locator, max_node_count, sandbox, std::move(start_nodes));
    session_->send_command(
        bidi::ids::methods::bc_locateNodes, params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.locateNodes failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }

            // Parse nodes array from response
            std::vector<types::script::NodeRemoteValue> nodes;
            const auto *nodes_it = response.result.find("nodes");
            if (nodes_it == response.result.end() ||
                !nodes_it->value().is_array()) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.locateNodes: missing or invalid 'nodes' "
                    "array in response")));
                return;
            }

            const auto &nodes_array = nodes_it->value().as_array();
            nodes.reserve(nodes_array.size());

            for (const auto &node_value : nodes_array) {
                try {
                    if (!node_value.is_object()) {
                        logging::log_warning(
                            "browsingContext.locateNodes: skipping non-object "
                            "node entry");
                        continue;
                    }

                    const auto &node_obj = node_value.as_object();
                    types::script::NodeRemoteValue node;

                    // Extract handle (optional)
                    const auto *handle_it = node_obj.find("handle");
                    if (handle_it != node_obj.end() &&
                        handle_it->value().is_string()) {
                        node.handle =
                            std::string(handle_it->value().as_string());
                    }

                    // Extract internalId (optional)
                    const auto *internal_id_it = node_obj.find("internalId");
                    if (internal_id_it != node_obj.end() &&
                        internal_id_it->value().is_string()) {
                        node.internal_id =
                            std::string(internal_id_it->value().as_string());
                    }

                    // Extract sharedId (optional)
                    const auto *shared_id_it = node_obj.find("sharedId");
                    if (shared_id_it != node_obj.end() &&
                        shared_id_it->value().is_string()) {
                        node.shared_id =
                            std::string(shared_id_it->value().as_string());
                    }

                    // Extract nodeType (optional)
                    const auto *node_type_it = node_obj.find("nodeType");
                    if (node_type_it != node_obj.end() &&
                        node_type_it->value().is_string()) {
                        node.node_type =
                            std::string(node_type_it->value().as_string());
                    }

                    // Extract localName (optional)
                    const auto *local_name_it = node_obj.find("localName");
                    if (local_name_it != node_obj.end() &&
                        local_name_it->value().is_string()) {
                        node.local_name =
                            std::string(local_name_it->value().as_string());
                    }

                    nodes.push_back(std::move(node));
                } catch (const std::exception &e) {
                    // Resilient parsing: log warning but continue processing
                    logging::log_warning(
                        std::format("browsingContext.locateNodes: failed to "
                                    "parse node entry: {}",
                                    e.what()));
                    continue;
                }
            }

            result.fulfill(std::move(nodes));
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::capture_screenshot(
    std::string_view context, std::optional<std::string_view> origin,
    std::optional<types::browsing_context::ImageFormat> format,
    std::optional<types::browsing_context::ClipRectangle> clip,
    const std::source_location &loc) -> Task<std::string> {
    auto ex = get_executor();
    auto result = Task<std::string>::make(ex);
    auto params = commands::browsing_context::capture_screenshot(
        context, origin, std::move(format), std::move(clip));
    session_->send_command(
        bidi::ids::methods::bc_captureScreenshot, params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.captureScreenshot failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }

            const auto *data_it = response.result.find("data");
            if (data_it == response.result.end() ||
                !data_it->value().is_string()) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "browsingContext.captureScreenshot: missing 'data' string "
                    "in response")));
                return;
            }

            result.fulfill(std::string(data_it->value().as_string().c_str()));
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

// ======================== Script API ========================

auto Client::evaluate(std::string_view expression, std::string_view context,
                      bool await_promise, const std::source_location &loc)
    -> Task<boost::json::object> {
    auto ex = get_executor();
    auto result = Task<boost::json::object>::make(ex);
    commands::script::Target target{.context = context, .sandbox = {}};
    auto params = commands::script::evaluate(expression, target, await_promise);
    session_->send_command(
        bidi::ids::methods::script_evaluate, params,
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "script.evaluate failed: " + response.error_code_raw +
                    " - " + response.error_message)));
                return;
            }
            result.fulfill(response.result);
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::evaluate(std::string_view expression, std::string_view context,
                      script::script_eval_policy policy, bool await_promise,
                      const std::source_location &loc)
    -> Task<script::ScriptEvalOutcome> {
    auto ex = get_executor();
    auto task = Task<script::ScriptEvalOutcome>::make(ex);
    commands::script::Target target{.context = context, .sandbox = {}};
    auto params = commands::script::evaluate(expression, target, await_promise);
    auto responseHandler = [task, policy](
                               const core::ParsedResponse &response) mutable {
        // Diagnóstico temporário: logar estado bruto antes da aplicação da
        // policy
        try {
            const bool is_success = response.is_success;
            std::string result_type;
            if (response.result.if_contains("type") &&
                response.result.at("type").is_string()) {
                result_type =
                    std::string(response.result.at("type").as_string().c_str());
            } else {
                result_type = "<none>";
            }
            std::string_view policy_str =
                (policy ==
                 script::script_eval_policy::throw_on_script_exception)
                    ? "throw_on_script_exception"
                    : "return_outcome";
            auto diag_message = std::format(
                "[DIAG] script.evaluate raw is_success={}, result.type={}, "
                "policy={}",
                is_success ? "true" : "false", result_type, policy_str);
            bidi::logging::log_info(diag_message);
        } catch (...) { // NOLINT
            // logging best-effort
        }
        if (!response.is_success) {
            bidi::logging::log_info(
                "script.evaluate failed: " + response.error_code_raw + " - " +
                response.error_message);
            task.fail(std::make_exception_ptr(std::runtime_error(
                std::string("script.evaluate failed: ") +
                response.error_code_raw + " - " + response.error_message)));
            return;
        }
        auto decision = script::apply_policy(response, policy);
        if (decision.action ==
            script::PolicyApplicationResult::Action::throw_exception) {
            bidi::logging::log_info(
                "Script evaluation failed, applying policy: " +
                std::to_string(static_cast<int>(policy)));
            task.fail(std::make_exception_ptr(script::ScriptEvaluateException(
                std::move(decision.exception))));
            return;
        }
        task.fulfill(std::move(decision.outcome));
    };
    session_->send_command(bidi::ids::methods::script_evaluate, params,
                           responseHandler, core::BiDiSession::kDefaultTimeout,
                           loc);
    return task;
}

auto Client::call_function(
    std::string_view function_declaration, std::string_view context,
    boost::json::array arguments, bool await_promise,
    const std::source_location &loc) -> Task<boost::json::object> {
    auto ex = get_executor();
    auto result = Task<boost::json::object>::make(ex);
    commands::script::Target target{.context = context, .sandbox = {}};
    auto params = commands::script::call_function(
        function_declaration, target, std::move(arguments), await_promise);
    session_->send_command(
        bidi::ids::methods::script_callFunction, params,
        [result](core::ParsedResponse response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "script.callFunction failed: " + response.error_code_raw +
                    " - " + response.error_message)));
                return;
            }
            result.fulfill(std::move(response.result));
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::call_function(
    std::string_view function_declaration, std::string_view context,
    boost::json::array arguments, script::script_eval_policy policy,
    bool await_promise,
    const std::source_location &loc) -> Task<script::ScriptEvalOutcome> {
    auto ex = get_executor();
    auto task = Task<script::ScriptEvalOutcome>::make(ex);
    commands::script::Target target{.context = context, .sandbox = {}};
    auto params = commands::script::call_function(
        function_declaration, target, std::move(arguments), await_promise);
    auto responseHandler = [task, policy](
                               const core::ParsedResponse &response) mutable {
        // Diagnóstico temporário: logar estado bruto antes da aplicação da
        // policy
        try {
            const bool is_success = response.is_success;
            std::string result_type;
            if (response.result.if_contains("type") &&
                response.result.at("type").is_string()) {
                result_type =
                    std::string(response.result.at("type").as_string().c_str());
            } else {
                result_type = "<none>";
            }
            std::string_view policy_str =
                (policy ==
                 script::script_eval_policy::throw_on_script_exception)
                    ? "throw_on_script_exception"
                    : "return_outcome";
            auto diag_message = std::format(
                "[DIAG] script.evaluate raw is_success={}, result.type={}, "
                "policy={}",
                is_success ? "true" : "false", result_type, policy_str);
            bidi::logging::log_info(diag_message);
        } catch (...) { // NOLINT
            // logging best-effort
        }
        if (!response.is_success) {
            bidi::logging::log_info(
                "script.evaluate failed: " + response.error_code_raw + " - " +
                response.error_message);
            task.fail(std::make_exception_ptr(std::runtime_error(
                std::string("script.evaluate failed: ") +
                response.error_code_raw + " - " + response.error_message)));
            return;
        }
        auto decision = script::apply_policy(response, policy);
        if (decision.action ==
            script::PolicyApplicationResult::Action::throw_exception) {
            bidi::logging::log_info(
                "Script evaluation failed, applying policy: " +
                std::to_string(static_cast<int>(policy)));
            task.fail(std::make_exception_ptr(script::ScriptEvaluateException(
                std::move(decision.exception))));
            return;
        }
        task.fulfill(std::move(decision.outcome));
    };
    session_->send_command(bidi::ids::methods::script_callFunction, params,
                           responseHandler, core::BiDiSession::kDefaultTimeout,
                           loc);

    return task;
}

auto Client::add_preload_script(
    std::string_view function_declaration, boost::json::array arguments,
    std::string_view sandbox,
    const std::source_location &loc) -> Task<std::string> {
    auto ex = get_executor();
    auto result = Task<std::string>::make(ex, loc);
    auto params = commands::script::add_preload_script(
        function_declaration, std::move(arguments), sandbox);
    auto responseHandler = [result](
                               const core::ParsedResponse &response) mutable {
        if (!response.is_success) {
            result.fail(std::make_exception_ptr(std::runtime_error(
                std::string("script.addPreloadScript failed: ") +
                response.error_code_raw + " - " + response.error_message)));
            return;
        }
        try {
            auto script_id =
                std::string(response.result.at("script").as_string().c_str());
            result.fulfill(std::move(script_id));
        } catch (const std::exception &e) {
            result.fail(std::make_exception_ptr(std::runtime_error(
                std::string("Failed to parse addPreloadScript response: ") +
                e.what())));
        }
    };
    session_->send_command(ids::methods::script_addPreloadScript, params,
                           std::move(responseHandler),
                           core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::remove_preload_script(
    std::string_view script_id, const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex, loc);
    auto params = commands::script::remove_preload_script(script_id);
    auto responseHandler =
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    std::string("script.removePreloadScript failed: ") +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill();
        };
    session_->send_command(ids::methods::script_removePreloadScript, params,
                           std::move(responseHandler),
                           core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

// ======================== Network Interception API ========================

auto Client::add_intercept(
    std::vector<types::network::InterceptPhase> phases,
    std::optional<std::vector<std::string>> contexts,
    std::optional<std::vector<types::network::UrlPattern>> url_patterns,
    const std::source_location &loc) -> Task<types::network::InterceptId> {
    auto ex = get_executor();
    auto result = Task<types::network::InterceptId>::make(ex, loc);

    types::network::AddInterceptParameters params{
        .phases = std::move(phases),
        .contexts = std::move(contexts),
        .url_patterns = std::move(url_patterns)};

    session_->send_command(
        bidi::ids::methods::net_addIntercept,
        boost::json::value_from(params).as_object(),
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "network.addIntercept failed: " + response.error_code_raw +
                    " - " + response.error_message)));
                return;
            }

            try {
                const auto *intercept_it = response.result.find("intercept");
                if (intercept_it != response.result.end() &&
                    intercept_it->value().is_string()) {
                    result.fulfill(
                        std::string(intercept_it->value().as_string().c_str()));
                } else {
                    result.fail(std::make_exception_ptr(
                        std::runtime_error("network.addIntercept: missing "
                                           "'intercept' field in response")));
                }
            } catch (const std::exception &e) {
                result.fail(
                    std::make_exception_ptr(std::runtime_error(std::format(
                        "Failed to parse network.addIntercept response: {}",
                        e.what()))));
            }
        },
        core::BiDiSession::kDefaultTimeout, loc);

    return result;
}

auto Client::remove_intercept(types::network::InterceptId intercept_id,
                              const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex, loc);

    types::network::RemoveInterceptParameters params{
        .intercept = std::move(intercept_id)};

    session_->send_command(
        bidi::ids::methods::net_removeIntercept,
        boost::json::value_from(params).as_object(),
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "network.removeIntercept failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill();
        },
        core::BiDiSession::kDefaultTimeout, loc);

    return result;
}

auto Client::continue_request(
    types::network::RequestId request_id,
    std::optional<types::network::BytesValue> body,
    std::optional<std::vector<types::network::CookieHeader>> cookies,
    std::optional<std::vector<types::network::Header>> headers,
    std::optional<std::string> method, std::optional<std::string> url,
    const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex, loc);

    types::network::ContinueRequestParameters params{
        .request = std::move(request_id),
        .body = std::move(body),
        .cookies = std::move(cookies),
        .headers = std::move(headers),
        .method = std::move(method),
        .url = std::move(url)};

    session_->send_command(
        bidi::ids::methods::net_continueRequest,
        boost::json::value_from(params).as_object(),
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "network.continueRequest failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill();
        },
        core::BiDiSession::kDefaultTimeout, loc);

    return result;
}

auto Client::fail_request(types::network::RequestId request_id,
                          const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex, loc);

    types::network::FailRequestParameters params{.request =
                                                     std::move(request_id)};

    session_->send_command(
        bidi::ids::methods::net_failRequest,
        boost::json::value_from(params).as_object(),
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "network.failRequest failed: " + response.error_code_raw +
                    " - " + response.error_message)));
                return;
            }
            result.fulfill();
        },
        core::BiDiSession::kDefaultTimeout, loc);

    return result;
}

auto Client::continue_response(
    types::network::RequestId request_id,
    std::optional<std::vector<types::network::SetCookieHeader>> cookies,
    std::optional<types::network::AuthCredentials> credentials,
    std::optional<std::vector<types::network::Header>> headers,
    std::optional<std::string> reason_phrase,
    std::optional<std::uint64_t> status_code,
    const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex, loc);

    types::network::ContinueResponseParameters params{
        .request = std::move(request_id),
        .cookies = std::move(cookies),
        .credentials = std::move(credentials),
        .headers = std::move(headers),
        .reason_phrase = std::move(reason_phrase),
        .status_code = status_code};

    session_->send_command(
        bidi::ids::methods::net_continueResponse,
        boost::json::value_from(params).as_object(),
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "network.continueResponse failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill();
        },
        core::BiDiSession::kDefaultTimeout, loc);

    return result;
}

auto Client::provide_response(
    types::network::RequestId request_id,
    std::optional<types::network::BytesValue> body,
    std::optional<std::vector<types::network::SetCookieHeader>> cookies,
    std::optional<std::vector<types::network::Header>> headers,
    std::optional<std::string> reason_phrase,
    std::optional<std::uint64_t> status_code,
    const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex, loc);

    types::network::ProvideResponseParameters params{
        .request = std::move(request_id),
        .body = std::move(body),
        .cookies = std::move(cookies),
        .headers = std::move(headers),
        .reason_phrase = std::move(reason_phrase),
        .status_code = status_code};

    session_->send_command(
        bidi::ids::methods::net_provideResponse,
        boost::json::value_from(params).as_object(),
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "network.provideResponse failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill();
        },
        core::BiDiSession::kDefaultTimeout, loc);

    return result;
}

auto Client::continue_with_auth(
    types::network::RequestId request_id, types::network::AuthAction action,
    std::optional<types::network::AuthCredentials> credentials,
    const std::source_location &loc) -> Task<void> {
    auto ex = get_executor();
    auto result = Task<void>::make(ex, loc);

    types::network::ContinueWithAuthParameters params{
        .request = std::move(request_id),
        .action =
            credentials.has_value()
                ? std::variant<types::network::ContinueWithAuthCredentials,
                               types::network::ContinueWithAuthNoCredentials>(
                      types::network::ContinueWithAuthCredentials{
                          .action = action,
                          .credentials = std::move(*credentials)})
                : std::variant<types::network::ContinueWithAuthCredentials,
                               types::network::ContinueWithAuthNoCredentials>(
                      types::network::ContinueWithAuthNoCredentials{
                          .action = action})};

    session_->send_command(
        bidi::ids::methods::net_continueWithAuth,
        boost::json::value_from(params).as_object(),
        [result](const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "network.continueWithAuth failed: " +
                    response.error_code_raw + " - " + response.error_message)));
                return;
            }
            result.fulfill();
        },
        core::BiDiSession::kDefaultTimeout, loc);

    return result;
}

// ======================== Session API ========================

auto Client::subscribe(const std::vector<std::string> &events,
                       const std::vector<std::string> &contexts,
                       const std::source_location &loc)
    -> Task<Client::Subscription> {
    auto ex = get_executor();
    auto result = Task<Subscription>::make(ex);
    auto params = commands::session::subscribe(events, contexts);
    session_->send_command(
        bidi::ids::methods::session_subscribe, params,
        [result, events, self = weak_from_this()](
            const core::ParsedResponse &response) mutable {
            if (!response.is_success) {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "session.subscribe failed: " + response.error_code_raw +
                    " - " + response.error_message)));
                return;
            }
            if (auto client = self.lock()) {
                result.fulfill(Subscription(client, events));
            } else {
                result.fail(std::make_exception_ptr(std::runtime_error(
                    "Client was destroyed during subscription")));
            }
        },
        core::BiDiSession::kDefaultTimeout, loc);
    return result;
}

auto Client::set_event_handler(
    std::string_view method, std::function<void(boost::json::object)> handler,
    const std::source_location &loc) -> boost::asio::awaitable<void> {
    auto sub_async = session_->subscribe_event(
        method,
        [handler = std::move(handler)](const core::ParsedEvent &event) {
            handler(event.params);
        },
        loc);
    // Convert asyncx::Async to awaitable and co_await
    co_await asyncx::as_awaitable(std::move(sub_async));
    co_return;
}

auto Client::set_event_handler_subscription(
    std::string_view method, std::function<void(boost::json::object)> handler,
    const std::source_location &loc)
    -> asyncx::Async<std::shared_ptr<bidi::core::BiDiSession::Subscription>> {
    return session_->subscribe_event(
        method,
        [handler = std::move(handler)](const core::ParsedEvent &event) {
            handler(event.params);
        },
        loc);
}

void Client::unsubscribe_events(const std::vector<std::string> &events,
                                const std::source_location &loc) {
    logging::log_debug(
        std::format("Client: Unsubscribing from events {}", events.size()));
    auto params = commands::session::unsubscribe(events);
    session_->send_command(
        bidi::ids::methods::session_unsubscribe, params,
        [](const core::ParsedResponse &response) {
            if (!response.is_success) {
                bidi::logging::log_error(
                    std::string("session.unsubscribe failed: ") +
                    response.error_code_raw + " - " + response.error_message);
            }
        },
        core::BiDiSession::kDefaultTimeout, loc);
}

// ======================== Subscription Implementation ========================

Client::Subscription::Subscription(std::weak_ptr<Client> client,
                                   std::vector<std::string> events)
    : client_(std::move(client)), events_(std::move(events)) {}

Client::Subscription::~Subscription() noexcept {
    try {
        if (!events_.empty()) {
            if (auto client = client_.lock()) {
                client->unsubscribe_events(events_);
            }
        }
    } catch (const std::exception &e) {
        bidi::logging::log_error(
            std::string("~Client::Subscription exception: ") + e.what());
    } catch (...) {
        bidi::logging::log_error("~Client::Subscription unknown exception");
    }
}

Client::Subscription::Subscription(Subscription &&other) noexcept
    : client_(std::move(other.client_)), events_(std::move(other.events_)) {
    other.events_.clear();
}

auto Client::Subscription::operator=(Subscription &&other) noexcept
    -> Client::Subscription & {
    if (this != &other) {
        if (!events_.empty()) {
            if (auto client = client_.lock()) {
                client->unsubscribe_events(events_);
            }
        }
        client_ = std::move(other.client_);
        events_ = std::move(other.events_);
        other.events_.clear();
    }
    return *this;
}

// ======================== Cleanup Methods ========================

auto Client::pending_request_count() const -> std::size_t {
    if (!session_) {
        return 0;
    }
    return session_->test_pending_size();
}

void Client::clear_pending_requests() noexcept {
    try {
        if (!session_) {
            return;
        }

        const auto count = session_->test_pending_size();
        if (count == 0) {
            return;
        }

        // Log warning about pending requests being discarded
        logging::log_error(std::format("Client::clear_pending_requests: "
                                       "discarding {} pending request(s)",
                                       count));

        // Access session's pending_responses through a posted task to the
        // strand This ensures thread-safe access to the internal map
        boost::asio::post(session_->get_executor(), [session = session_]() {
            try {
                // Clear pending responses (unsafe outside strand, but we're
                // in the strand now)
                session->test_pending_clear();
            } catch (const std::exception &e) {
                logging::log_error(std::format(
                    "Client: exception clearing pending requests: {}",
                    e.what()));
            } catch (...) {
                logging::log_error(
                    "Client: unknown exception clearing pending requests");
            }
        });
    } catch (const std::exception &e) {
        logging::log_error(
            std::format("Client::clear_pending_requests failed: {}", e.what()));
    } catch (...) {
        logging::log_error("Client::clear_pending_requests: unknown exception");
    }
}

void Client::clear_event_handlers() noexcept {
    try {
        if (!session_) {
            return;
        }

        // Access session's event handlers through a posted task to the strand
        boost::asio::post(session_->get_executor(), [session = session_]() {
            try {
                session->test_clear_event_handlers();
                logging::log_error("Client: cleared all event handlers");
            } catch (const std::exception &e) {
                logging::log_error(std::format(
                    "Client: exception clearing event handlers: {}", e.what()));
            } catch (...) {
                logging::log_error(
                    "Client: unknown exception clearing event handlers");
            }
        });
    } catch (const std::exception &e) {
        logging::log_error(
            std::format("Client::clear_event_handlers failed: {}", e.what()));
    } catch (...) {
        logging::log_error("Client::clear_event_handlers: unknown exception");
    }
}

void Client::drain_and_cleanup() noexcept {
    try {
        if (!session_) {
            return;
        }

        logging::log_error(
            "Client: starting drain_and_cleanup (comprehensive shutdown)");

        // Step 1: Clear pending requests
        try {
            const auto pending_count = session_->test_pending_size();
            if (pending_count > 0) {
                logging::log_error(std::format(
                    "Client: drain_and_cleanup - failing {} pending request(s)",
                    pending_count));
                session_->test_pending_clear();
            }
        } catch (const std::exception &e) {
            logging::log_error(std::format(
                "Client: drain_and_cleanup - failed to clear pending: {}",
                e.what()));
        } catch (...) {
            logging::log_error("Client: drain_and_cleanup - unknown exception "
                               "clearing pending");
        }

        // Step 2: Clear event handlers
        try {
            session_->test_clear_event_handlers();
            logging::log_error(
                "Client: drain_and_cleanup - cleared event handlers");
        } catch (const std::exception &e) {
            logging::log_error(std::format(
                "Client: drain_and_cleanup - failed to clear handlers: {}",
                e.what()));
        } catch (...) {
            logging::log_error("Client: drain_and_cleanup - unknown exception "
                               "clearing handlers");
        }

        // Step 3: Disconnect session (closes WebSocket)
        try {
            session_->disconnect();
            logging::log_error(
                "Client: drain_and_cleanup - session disconnected");
        } catch (const std::exception &e) {
            logging::log_error(std::format(
                "Client: drain_and_cleanup - failed to disconnect: {}",
                e.what()));
        } catch (...) {
            logging::log_error(
                "Client: drain_and_cleanup - unknown exception disconnecting");
        }

        // Step 4: Reset session reference
        try {
            session_.reset();
            logging::log_error("Client: drain_and_cleanup - session reset");
        } catch (const std::exception &e) {
            logging::log_error(std::format(
                "Client: drain_and_cleanup - failed to reset session: {}",
                e.what()));
        } catch (...) {
            logging::log_error("Client: drain_and_cleanup - unknown exception "
                               "resetting session");
        }

        logging::log_error("Client: drain_and_cleanup completed");
    } catch (const std::exception &e) {
        logging::log_error(
            std::format("Client::drain_and_cleanup failed: {}", e.what()));
    } catch (...) {
        logging::log_error("Client::drain_and_cleanup: unknown exception");
    }
}

void Client::disconnect() noexcept {
    try {
        if (!session_) {
            return;
        }

        session_->disconnect();
        logging::log_error("Client: disconnected");
    } catch (const std::exception &e) {
        logging::log_error(
            std::format("Client::disconnect failed: {}", e.what()));
    } catch (...) {
        logging::log_error("Client::disconnect: unknown exception");
    }
}

} // namespace bidi

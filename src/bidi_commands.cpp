// src/bidi_commands.cpp — BiDi Command Builders implementation
#include "bidi/commands.hpp"

namespace bidi::commands {

// ======================== Session Commands ========================

namespace session {

auto subscribe(const std::vector<std::string> &events,
               const std::vector<std::string> &contexts)
    -> boost::json::object {
    boost::json::object params;

    // events is required
    boost::json::array events_array;
    for (const auto &event : events) {
        events_array.emplace_back(event);
    }
    params["events"] = std::move(events_array);

    // contexts is optional
    if (!contexts.empty()) {
        boost::json::array contexts_array;
        for (const auto &context : contexts) {
            contexts_array.emplace_back(context);
        }
        params["contexts"] = std::move(contexts_array);
    }

    return params;
}

auto unsubscribe(const std::vector<std::string> &events,
                 const std::vector<std::string> &contexts)
    -> boost::json::object {
    // Same structure as subscribe
    return subscribe(events, contexts);
}

auto status() -> boost::json::object {
    return boost::json::object{}; // session.status has no parameters
}

} // namespace session

// ======================== BrowsingContext Commands ========================

namespace browsing_context {

auto create(CreateType type, std::string_view reference_context)
    -> boost::json::object {
    boost::json::object params;
    params["type"] = to_string(type);

    if (!reference_context.empty()) {
        params["referenceContext"] = reference_context;
    }

    return params;
}

auto navigate(std::string_view context, std::string_view url,
              ReadinessState wait) -> boost::json::object {
    boost::json::object params;
    params["context"] = context;
    params["url"] = url;
    params["wait"] = to_string(wait);
    return params;
}

auto close(std::string_view context) -> boost::json::object {
    boost::json::object params;
    params["context"] = context;
    return params;
}

auto get_tree(std::string_view root, int max_depth) -> boost::json::object {
    boost::json::object params;

    if (!root.empty()) {
        params["root"] = root;
    }

    if (max_depth >= 0) {
        params["maxDepth"] = max_depth;
    }

    return params;
}

auto reload(std::string_view context, bool ignore_cache, ReadinessState wait)
    -> boost::json::object {
    boost::json::object params;
    params["context"] = context;
    params["ignoreCache"] = ignore_cache;
    params["wait"] = to_string(wait);
    return params;
}

auto handle_user_prompt(std::string_view context, std::optional<bool> accept,
                        std::optional<std::string_view> user_text)
    -> boost::json::object {
    boost::json::object params;
    params["context"] = context;

    if (accept.has_value()) {
        params["accept"] = *accept;
    }

    if (user_text.has_value()) {
        params["userText"] = *user_text;
    }

    return params;
}

auto locate_nodes(std::string_view context,
                  const types::browsing_context::Locator &locator,
                  std::optional<std::uint64_t> max_node_count,
                  std::optional<std::string_view> sandbox,
                  std::optional<std::vector<std::string>> start_nodes)
    -> boost::json::object {
    boost::json::object params;
    params["context"] = context;

    // Serialize locator using Boost.JSON tag_invoke
    params["locator"] = boost::json::value_from(locator);

    if (max_node_count.has_value()) {
        params["maxNodeCount"] = *max_node_count;
    }

    if (sandbox.has_value() && !sandbox->empty()) {
        params["sandbox"] = *sandbox;
    }

    if (start_nodes.has_value() && !start_nodes->empty()) {
        boost::json::array start_nodes_array;
        for (const auto &node : *start_nodes) {
            boost::json::object shared_ref;
            shared_ref["sharedId"] = node;
            start_nodes_array.emplace_back(std::move(shared_ref));
        }
        params["startNodes"] = std::move(start_nodes_array);
    }

    return params;
}

auto capture_screenshot(
    std::string_view context, std::optional<std::string_view> origin,
    std::optional<types::browsing_context::ImageFormat> format,
    std::optional<types::browsing_context::ClipRectangle> clip)
    -> boost::json::object {
    boost::json::object params;
    params["context"] = context;

    if (origin.has_value() && !origin->empty()) {
        params["origin"] = *origin;
    }

    if (format.has_value()) {
        params["format"] = boost::json::value_from(*format);
    }

    if (clip.has_value()) {
        params["clip"] = boost::json::value_from(*clip);
    }

    return params;
}

} // namespace browsing_context

// ======================== Script Commands ========================

namespace script {

auto evaluate(std::string_view expression, const Target &target,
              bool await_promise, ResultOwnership ownership)
    -> boost::json::object {
    boost::json::object params;
    params["expression"] = expression;
    params["awaitPromise"] = await_promise;
    params["resultOwnership"] = to_string(ownership);

    // Build target object
    boost::json::object target_obj;
    target_obj["context"] = target.context;
    if (!target.sandbox.empty()) {
        target_obj["sandbox"] = target.sandbox;
    }
    params["target"] = std::move(target_obj);

    return params;
}

auto call_function(std::string_view function_declaration, const Target &target,
                   boost::json::array arguments, bool await_promise,
                   ResultOwnership ownership) -> boost::json::object {
    boost::json::object params;
    params["functionDeclaration"] = function_declaration;
    params["awaitPromise"] = await_promise;
    params["resultOwnership"] = to_string(ownership);

    if (!arguments.empty()) {
        params["arguments"] = std::move(arguments);
    }

    // Build target object
    boost::json::object target_obj;
    target_obj["context"] = target.context;
    if (!target.sandbox.empty()) {
        target_obj["sandbox"] = target.sandbox;
    }
    params["target"] = std::move(target_obj);

    return params;
}

auto disown(const boost::json::array &handles, const Target &target)
    -> boost::json::object {
    boost::json::object params;
    params["handles"] = handles;

    // Build target object
    boost::json::object target_obj;
    target_obj["context"] = target.context;
    if (!target.sandbox.empty()) {
        target_obj["sandbox"] = target.sandbox;
    }
    params["target"] = std::move(target_obj);

    return params;
}

} // namespace script

// ======================== Network Commands ========================

namespace network {

auto continue_request(std::string_view request_id, std::string_view url,
                      std::string_view method,
                      const boost::json::object &headers, std::string_view body)
    -> boost::json::object {
    boost::json::object params;
    params["request"] = request_id;

    if (!url.empty()) {
        params["url"] = url;
    }

    if (!method.empty()) {
        params["method"] = method;
    }

    if (!headers.empty()) {
        params["headers"] = headers;
    }

    if (!body.empty()) {
        params["body"] =
            boost::json::object{{"type", "string"}, {"value", body}};
    }

    return params;
}

auto fail_request(std::string_view request_id) -> boost::json::object {
    boost::json::object params;
    params["request"] = request_id;
    return params;
}

} // namespace network

// ======================== Enum Converters (Internal Utilities)
// ========================
//
// These functions convert command parameter enums to their W3C BiDi string
// representations. They are internal helpers used by command builders above.

auto to_string(browsing_context::CreateType type) noexcept -> std::string_view {
    switch (type) {
    case browsing_context::CreateType::tab:
        return "tab";
    case browsing_context::CreateType::window:
        return "window";
    }
    return "window"; // default
}

auto to_string(browsing_context::ReadinessState state) noexcept
    -> std::string_view {
    switch (state) {
    case browsing_context::ReadinessState::none:
        return "none";
    case browsing_context::ReadinessState::interactive:
        return "interactive";
    case browsing_context::ReadinessState::complete:
        return "complete";
    }
    return "complete"; // default
}

auto to_string(script::ResultOwnership ownership) noexcept -> std::string_view {
    switch (ownership) {
    case script::ResultOwnership::root:
        return "root";
    case script::ResultOwnership::none:
        return "none";
    }
    return "root"; // default
}

auto to_string(log::Level level) noexcept -> std::string_view {
    switch (level) {
    case log::Level::debug:
        return "debug";
    case log::Level::info:
        return "info";
    case log::Level::warn:
        return "warn";
    case log::Level::error:
        return "error";
    }
    return "info"; // default
}

// ======================== LocalValue Builders (BiDi Parameter Primitives)
// ========================
//
// These functions construct BiDi LocalValue objects from C++ primitives.
// LocalValue is the W3C BiDi protocol type for JavaScript values passed as
// command arguments (e.g., script.callFunction arguments array).
//
// For automatic C++ → LocalValue conversion with type deduction, see
// bidi::script::make_arg_json() in include/bidi/script/marshalling.hpp

auto local_value_string(std::string_view value) -> boost::json::object {
    return boost::json::object{{"type", "string"}, {"value", value}};
}

auto local_value_number(double value) -> boost::json::object {
    return boost::json::object{{"type", "number"}, {"value", value}};
}

auto local_value_boolean(bool value) -> boost::json::object {
    return boost::json::object{{"type", "boolean"}, {"value", value}};
}

auto local_value_null() -> boost::json::object {
    return boost::json::object{{"type", "null"}};
}

} // namespace bidi::commands

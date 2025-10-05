// src/bidi_script_eval.cpp - Implementation of parsing/diagnosis of script exceptions
#include "bidi/core.hpp"
#include "bidi/script_eval.hpp"

namespace bidi::script {

namespace { // helpers locais

inline const boost::json::value *
try_find_value(const boost::json::object &object,
               std::string_view key) noexcept {
    const auto *const iter = object.find(key);
    if (iter == object.end()) {
        return nullptr;
    }
    return &iter->value();
}

inline std::string get_string_or_empty(const boost::json::object &object,
                                       std::string_view key) noexcept {
    if (const auto *value_ptr = try_find_value(object, key);
        value_ptr != nullptr && value_ptr->is_string()) {
        return std::string(value_ptr->as_string().c_str());
    }
    return {};
}

inline const boost::json::object *
get_object_ptr(const boost::json::object &object,
               std::string_view key) noexcept {
    if (const auto *value_ptr = try_find_value(object, key);
        value_ptr != nullptr && value_ptr->is_object()) {
        return &value_ptr->as_object();
    }
    return nullptr;
}

inline const boost::json::array *
get_array_ptr(const boost::json::object &object,
              std::string_view key) noexcept {
    if (const auto *value_ptr = try_find_value(object, key);
        value_ptr != nullptr && value_ptr->is_array()) {
        return &value_ptr->as_array();
    }
    return nullptr;
}

inline std::optional<int64_t> get_int_opt(const boost::json::object &object,
                                          std::string_view key) noexcept {
    if (const auto *value_ptr = try_find_value(object, key);
        value_ptr != nullptr && value_ptr->is_int64()) {
        return value_ptr->as_int64();
    }
    return std::nullopt;
}

} // namespace

bool is_script_exception_result(const boost::json::object &result) noexcept {
    const auto *const iter = result.find("type");
    if (iter == result.end()) {
        return false;
    }
    const auto &type_value = iter->value();
    return type_value.is_string() && type_value.as_string() == "exception";
}

ScriptExceptionDetails
parse_script_exception(const boost::json::object &result) noexcept {
    ScriptExceptionDetails det;
    det.exception_type = get_string_or_empty(result, "type");
    if (const auto *details = get_object_ptr(result, "exceptionDetails")) {
        det.raw = *details; // snapshot
        det.text = get_string_or_empty(*details, "text");
        if (const auto *inner = get_object_ptr(*details, "exception")) {
            det.value = get_string_or_empty(*inner, "value");
            det.name = get_string_or_empty(*inner, "className");
            det.error_type = get_string_or_empty(*inner, "type");
        }
        det.line_number = get_int_opt(*details, "lineNumber");
        det.column_number = get_int_opt(*details, "columnNumber");
        if (const auto *stack = get_array_ptr(*details, "stackTrace")) {
            det.stack_frames.reserve(stack->size());
            for (const auto &frame_value : *stack) {
                if (!frame_value.is_object()) {
                    continue;
                }
                const auto &frame_obj = frame_value.as_object();
                ScriptStackFrame frame_record;
                frame_record.url = get_string_or_empty(frame_obj, "url");
                frame_record.function_name =
                    get_string_or_empty(frame_obj, "functionName");
                if (auto ln = get_int_opt(frame_obj, "lineNumber")) {
                    frame_record.line_number = *ln;
                }
                if (auto cn = get_int_opt(frame_obj, "columnNumber")) {
                    frame_record.column_number = *cn;
                }
                det.stack_frames.push_back(std::move(frame_record));
            }
        }
    } else {
        // Sem subobjeto: manter raw vazio mas ainda classificar
    }
    return det;
}

PolicyApplicationResult apply_policy(const core::ParsedResponse &response,
                                     script_eval_policy policy) noexcept {
    PolicyApplicationResult policy_result;
    if (!response.is_success) {
        return policy_result; // caller decidirá lançar erro de protocolo
    }
    const bool is_exc = is_script_exception_result(response.result);
    if (!is_exc) {
        policy_result.outcome.result = response.result;
        policy_result.outcome.raw = response.result;
        return policy_result;
    }
    auto details = parse_script_exception(response.result);
    if (policy == script_eval_policy::throw_on_script_exception) {
        policy_result.action = PolicyApplicationResult::Action::throw_exception;
        policy_result.exception = std::move(details);
    } else {
        policy_result.outcome.exception = std::move(details);
        policy_result.outcome.raw = response.result;
    }
    return policy_result;
}

} // namespace bidi::script

#pragma once

#include "bidi/core.hpp"
#include <boost/json.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace bidi::ws {

using id_type = bidi::core::id_type;

inline auto is_id_safe(std::uint64_t id) noexcept -> bool {
    return bidi::core::is_id_safe(id);
}

inline auto build_command(id_type id, std::string_view method,
                          const boost::json::object &params = {})
    -> std::string {
    return bidi::core::build_command(id, method, params);
}

enum class MessageKind { Response, Event, Unknown };

inline auto detect_message_kind(std::string_view payload) noexcept
    -> MessageKind {
    try {
        // Fast attempt: parse as object and check for "type" == "event" or
        // presence of "id"
        boost::json::stream_parser p;
        p.write(payload.data(), payload.size());
        p.finish();
        boost::json::value v = p.release();
        if (!v.is_object()) {
            return MessageKind::Unknown;
        }
        const auto &obj = v.as_object();
        const auto *it_type = obj.find("type");
        if (it_type != obj.end() && it_type->value().is_string()) {
            if (it_type->value().as_string() == "event") {
                return MessageKind::Event;
            }
        }
        if (obj.if_contains("id") != nullptr) {
            return MessageKind::Response;
        }
        return MessageKind::Unknown;
    } catch (...) {
        return MessageKind::Unknown;
    }
}

} // namespace bidi::ws

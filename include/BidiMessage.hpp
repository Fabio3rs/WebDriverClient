#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <boost/json.hpp>

namespace bidi::ws {

using id_type = std::uint32_t;
static constexpr std::uint64_t MAX_SAFE_ID = 9007199254740991ULL; // 2^53-1

inline bool is_id_safe(std::uint64_t id) noexcept {
    return id <= MAX_SAFE_ID;
}

inline std::string build_command(id_type id,
                                 std::string_view method,
                                 const boost::json::object &params = {}) {
    boost::json::object obj;
    obj["id"] = id;
    obj["method"] = std::string(method);
    obj["params"] = params;
    return boost::json::serialize(obj);
}

enum class MessageKind { Response, Event, Unknown };

inline MessageKind detect_message_kind(std::string_view payload) noexcept {
    try {
        // Fast attempt: parse as object and check for "type" == "event" or presence of "id"
        boost::json::stream_parser p;
        p.write(payload.data(), payload.size());
        p.finish();
        boost::json::value v = p.release();
        if (!v.is_object()) return MessageKind::Unknown;
        const auto &obj = v.as_object();
        auto it_type = obj.find("type");
        if (it_type != obj.end() && it_type->value().is_string()) {
            if (it_type->value().as_string() == "event") return MessageKind::Event;
        }
        if (obj.if_contains("id")) return MessageKind::Response;
        return MessageKind::Unknown;
    } catch (...) {
        return MessageKind::Unknown;
    }
}

} // namespace bidi::ws

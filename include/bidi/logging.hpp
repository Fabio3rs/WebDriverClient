#pragma once
/**
 * @file logging.hpp
 * @brief Structured logging helpers used by the project.
 *
 * Design notes:
 * - Uses `std::source_location::current()` to capture call-site information
 *   (file, line, function) without macros. This improves observability and
 *   makes debugging easier in tests and examples.
 * - For production builds that are sensitive to metadata size, define
 *   `WEBDRIVER_STRIP_LOG_LOCATION` to omit `source` from the JSON payload.
 * - The log format is JSON to simplify ingestion by tests and external
 *   telemetry; `build_log()` returns a serialized JSON string.
 */

#include <boost/json.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <source_location>
#include <sstream>
#include <string>
#include <system_error>

namespace bidi::logging {

inline auto now_iso8601() -> std::string {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto now_t = system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&now_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Create a lightweight trace id (hex of timestamp + address)
inline auto make_trace_id() -> std::string {
    auto epoch_count =
        std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream ss;
    ss << std::hex << epoch_count;
    return ss.str();
}

inline auto error_to_json(const std::error_code &err_code)
    -> boost::json::object {
    boost::json::object obj;
    obj["domain"] = err_code.category().name();
    obj["value"] = err_code.value();
    obj["message"] = err_code.message();
    return obj;
}

inline auto exception_to_json(const std::exception_ptr &ex_ptr)
    -> boost::json::object {
    boost::json::object obj;
    if (!ex_ptr) {
        return obj;
    }
    try {
        std::rethrow_exception(ex_ptr);
    } catch (const std::system_error &se) {
        obj = error_to_json(se.code());
    } catch (const std::exception &e) {
        obj["message"] = e.what();
    } catch (...) {
        obj["message"] = "unknown exception";
    }
    return obj;
}

// Convert source_location to JSON
inline auto location_to_json(const std::source_location &loc)
    -> boost::json::object {
    boost::json::object obj;
    if (loc.line() != 0) { // default-constructed indicates "unspecified"
        obj["file"] = loc.file_name();
        obj["line"] = loc.line();
        obj["column"] = loc.column();
        obj["function"] = loc.function_name();
    }
    return obj;
}

// Structured log entry builder
inline auto
build_log(const std::string &level, const std::string &message,
          const std::error_code &err_code = {},
          const std::exception_ptr &ex_ptr = nullptr,
          const std::string &trace_id = {},
          const std::source_location &loc = std::source_location::current())
    -> std::string {
    boost::json::object root;
    root["ts"] = now_iso8601();
    root["level"] = level;
    root["message"] = message;
    root["trace_id"] = trace_id.empty() ? make_trace_id() : trace_id;
    // Optionally include source location
#ifndef WEBDRIVER_STRIP_LOG_LOCATION
    {
        auto src = location_to_json(loc);
        if (!src.empty()) {
            root["source"] = std::move(src);
        }
    }
#endif
    if (err_code) {
        root["error"] = error_to_json(err_code);
    }
    if (ex_ptr) {
        root["exception"] = exception_to_json(ex_ptr);
    }
    return boost::json::serialize(root);
}

// Log sink type and global setter
using LogSink = std::function<void(const std::string &)>;

inline auto get_default_sink_ref() -> LogSink & {
    static LogSink sink = [](const std::string &msg_str) {
        std::cout << msg_str << '\n';
    };
    return sink;
}

inline void set_log_sink(LogSink sink) {
    get_default_sink_ref() = std::move(sink);
}

inline void reset_log_sink() {
    get_default_sink_ref() = [](const std::string &msg_str) {
        std::cout << msg_str << '\n';
    };
}

// Use configured sink to emit logs
// New overload capturing source location automatically
inline void
log_error(const std::string &msg, const std::error_code &err_code = {},
          const std::exception_ptr &ex_ptr = nullptr,
          const std::string &trace_id = {},
          const std::source_location &loc = std::source_location::current()) {
    auto payload = build_log("error", msg, err_code, ex_ptr, trace_id, loc);
    auto &sink = get_default_sink_ref();
    if (sink) {
        sink(payload);
    } else {
        std::cerr << payload << '\n';
    }
}

inline void
log_info(const std::string &msg, const std::string &trace_id = {},
         const std::source_location &loc = std::source_location::current()) {
    auto payload = build_log("info", msg, {}, nullptr, trace_id, loc);
    auto &sink = get_default_sink_ref();
    if (sink) {
        sink(payload);
    } else {
        std::cout << payload << '\n';
    }
}

inline void
log_warning(const std::string &msg, const std::string &trace_id = {},
            const std::source_location &loc = std::source_location::current()) {
    auto payload = build_log("warning", msg, {}, nullptr, trace_id, loc);
    auto &sink = get_default_sink_ref();
    if (sink) {
        sink(payload);
    } else {
        std::cout << payload << '\n';
    }
}

inline void
log_debug(const std::string &msg, const std::string &trace_id = {},
          const std::source_location &loc = std::source_location::current()) {
    auto payload = build_log("debug", msg, {}, nullptr, trace_id, loc);
    auto &sink = get_default_sink_ref();
    if (sink) {
        sink(payload);
    } else {
        std::cout << payload << '\n';
    }
}

} // namespace bidi::logging

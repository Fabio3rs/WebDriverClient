#pragma once

#include <boost/json.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

namespace bidi::logging {

inline std::string now_iso8601() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto now_t = system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&now_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Create a lightweight trace id (hex of timestamp + address)
inline std::string make_trace_id() {
    auto epoch_count =
        std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream ss;
    ss << std::hex << epoch_count;
    return ss.str();
}

inline boost::json::object error_to_json(const std::error_code &err_code) {
    boost::json::object obj;
    obj["domain"] = err_code.category().name();
    obj["value"] = err_code.value();
    obj["message"] = err_code.message();
    return obj;
}

inline boost::json::object exception_to_json(const std::exception_ptr &ex_ptr) {
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

// Structured log entry builder
inline std::string build_log(const std::string &level,
                             const std::string &message,
                             const std::error_code &err_code = {},
                             const std::exception_ptr &ex_ptr = nullptr,
                             const std::string &trace_id = {}) {
    boost::json::object root;
    root["ts"] = now_iso8601();
    root["level"] = level;
    root["message"] = message;
    root["trace_id"] = trace_id.empty() ? make_trace_id() : trace_id;
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

inline LogSink &get_default_sink_ref() {
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
inline void log_error(const std::string &msg,
                      const std::error_code &err_code = {},
                      const std::exception_ptr &ex_ptr = nullptr,
                      const std::string &trace_id = {}) {
    auto payload = build_log("error", msg, err_code, ex_ptr, trace_id);
    auto &sink = get_default_sink_ref();
    if (sink) {
        sink(payload);
    } else {
        std::cerr << payload << '\n';
    }
}

inline void log_info(const std::string &msg, const std::string &trace_id = {}) {
    auto payload = build_log("info", msg, {}, nullptr, trace_id);
    auto &sink = get_default_sink_ref();
    if (sink) {
        sink(payload);
    } else {
        std::cout << payload << '\n';
    }
}

inline void log_warning(const std::string &msg,
                        const std::string &trace_id = {}) {
    auto payload = build_log("warning", msg, {}, nullptr, trace_id);
    auto &sink = get_default_sink_ref();
    if (sink) {
        sink(payload);
    } else {
        std::cout << payload << '\n';
    }
}

inline void log_debug(const std::string &msg,
                      const std::string &trace_id = {}) {
    auto payload = build_log("debug", msg, {}, nullptr, trace_id);
    auto &sink = get_default_sink_ref();
    if (sink) {
        sink(payload);
    } else {
        std::cout << payload << '\n';
    }
}

} // namespace bidi::logging

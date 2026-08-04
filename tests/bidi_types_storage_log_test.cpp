// tests/bidi_types_storage_log_test.cpp - Unit tests for storage and log types
#include "bidi/types/log.hpp"
#include "bidi/types/storage.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>

// ==================== Storage Module Tests ====================

using namespace bidi::types::storage;

TEST(BidiTypesStorage, CookieAlias) {
    // Verify Cookie is correctly aliased from network::Cookie
    Cookie cookie;
    cookie.name = "session";
    cookie.value = bidi::types::network::StringBytes{"token123"};
    cookie.domain = "example.com";
    cookie.path = "/";

    EXPECT_EQ(cookie.name, "session");
    EXPECT_EQ(cookie.domain, "example.com");

    // Verify it's the same type as network::Cookie
    bidi::types::network::Cookie net_cookie = cookie;
    EXPECT_EQ(net_cookie.name, cookie.name);
}

TEST(BidiTypesStorage, PartitionDescriptorEquality) {
    PartitionDescriptor part1;
    part1.user_context = "user-123";
    part1.source_origin = "https://example.com";

    PartitionDescriptor part2;
    part2.user_context = "user-123";
    part2.source_origin = "https://example.com";

    PartitionDescriptor part3;
    part3.user_context = "user-456";

    EXPECT_EQ(part1, part2);
    EXPECT_NE(part1, part3);
}

TEST(BidiTypesStorage, PartitionDescriptorOptionalFields) {
    PartitionDescriptor part;

    // Both fields are optional by default
    EXPECT_FALSE(part.user_context.has_value());
    EXPECT_FALSE(part.source_origin.has_value());

    part.user_context = "user-123";
    EXPECT_TRUE(part.user_context.has_value());
    EXPECT_EQ(*part.user_context, "user-123");

    part.source_origin = "https://app.example.com";
    EXPECT_TRUE(part.source_origin.has_value());
    EXPECT_EQ(*part.source_origin, "https://app.example.com");
}

TEST(BidiTypesStorage, PartitionDescriptorDefaultConstruction) {
    PartitionDescriptor part;

    EXPECT_FALSE(part.user_context.has_value());
    EXPECT_FALSE(part.source_origin.has_value());
}

TEST(BidiTypesStorage, PartitionDescriptorPartialFields) {
    // Test with only one field set
    PartitionDescriptor part1;
    part1.user_context = "user-123";

    EXPECT_TRUE(part1.user_context.has_value());
    EXPECT_FALSE(part1.source_origin.has_value());

    PartitionDescriptor part2;
    part2.source_origin = "https://example.com";

    EXPECT_FALSE(part2.user_context.has_value());
    EXPECT_TRUE(part2.source_origin.has_value());
}

// ==================== Log Module Tests ====================

using namespace bidi::types::log;

TEST(BidiTypesLog, LevelToString) {
    using enum Level;

    EXPECT_EQ(to_string(Debug), "debug");
    EXPECT_EQ(to_string(Info), "info");
    EXPECT_EQ(to_string(Warn), "warn");
    EXPECT_EQ(to_string(Error), "error");
}

TEST(BidiTypesLog, LevelConstexpr) {
    constexpr auto str = to_string(Level::Info);
    static_assert(str == "info");
}

TEST(BidiTypesLog, LevelParse) {
    auto level1 = parse_level("debug");
    ASSERT_TRUE(level1.has_value());
    EXPECT_EQ(*level1, Level::Debug);

    auto level2 = parse_level("info");
    ASSERT_TRUE(level2.has_value());
    EXPECT_EQ(*level2, Level::Info);

    auto level3 = parse_level("warn");
    ASSERT_TRUE(level3.has_value());
    EXPECT_EQ(*level3, Level::Warn);

    auto level4 = parse_level("error");
    ASSERT_TRUE(level4.has_value());
    EXPECT_EQ(*level4, Level::Error);

    auto unknown = parse_level("invalid");
    EXPECT_FALSE(unknown.has_value());
}

TEST(BidiTypesLog, LevelRoundTrip) {
    using enum Level;

    auto test_round_trip = [](Level level) {
        auto str = to_string(level);
        auto parsed = parse_level(str);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, level);
    };

    test_round_trip(Debug);
    test_round_trip(Info);
    test_round_trip(Warn);
    test_round_trip(Error);
}

TEST(BidiTypesLog, LevelBoostJson) {
    using enum Level;

    // Serialization
    auto jv1 = boost::json::value_from(Debug);
    EXPECT_EQ(jv1.as_string(), "debug");

    auto jv2 = boost::json::value_from(Info);
    EXPECT_EQ(jv2.as_string(), "info");

    auto jv3 = boost::json::value_from(Warn);
    EXPECT_EQ(jv3.as_string(), "warn");

    auto jv4 = boost::json::value_from(Error);
    EXPECT_EQ(jv4.as_string(), "error");

    // Deserialization
    boost::json::value jv5 = "warn";
    auto level = boost::json::value_to<Level>(jv5);
    EXPECT_EQ(level, Warn);

    // Round-trip
    auto jv6 = boost::json::value_from(Error);
    auto parsed = boost::json::value_to<Level>(jv6);
    EXPECT_EQ(parsed, Error);
}

TEST(BidiTypesLog, LevelInvalidJson) {
    boost::json::value jv = "invalid_level";
    EXPECT_THROW(boost::json::value_to<Level>(jv), std::runtime_error);
}

TEST(BidiTypesLog, ConsoleLogEntryEquality) {
    ConsoleLogEntry entry1;
    entry1.method = "log";
    entry1.level = Level::Info;
    entry1.text = "Hello, world!";
    entry1.timestamp_ms = 1234567890;

    ConsoleLogEntry entry2;
    entry2.method = "log";
    entry2.level = Level::Info;
    entry2.text = "Hello, world!";
    entry2.timestamp_ms = 1234567890;

    ConsoleLogEntry entry3;
    entry3.method = "error";
    entry3.level = Level::Error;
    entry3.text = "Error occurred";
    entry3.timestamp_ms = 9876543210;

    EXPECT_EQ(entry1, entry2);
    EXPECT_NE(entry1, entry3);
}

TEST(BidiTypesLog, ConsoleLogEntryDefaultValues) {
    ConsoleLogEntry entry;

    EXPECT_TRUE(entry.method.empty());
    EXPECT_TRUE(entry.args.empty());
    EXPECT_TRUE(entry.text.empty());
    EXPECT_EQ(entry.timestamp_ms, 0U);
    EXPECT_FALSE(entry.realm.has_value());
}

TEST(BidiTypesLog, ConsoleLogEntryOptionalRealm) {
    ConsoleLogEntry entry;
    entry.method = "log";
    entry.level = Level::Info;
    entry.text = "Test message";

    // realm is optional
    EXPECT_FALSE(entry.realm.has_value());

    bidi::types::script::RealmInfo realm;
    realm.realm = "realm-123";
    realm.type = bidi::types::script::RealmType::Window;
    realm.origin = "https://example.com";

    entry.realm = realm;

    EXPECT_TRUE(entry.realm.has_value());
    EXPECT_EQ(entry.realm->realm, "realm-123");
    EXPECT_EQ(entry.realm->type, bidi::types::script::RealmType::Window);
}

TEST(BidiTypesLog, ConsoleLogEntryWithArgs) {
    ConsoleLogEntry entry;
    entry.method = "log";
    entry.level = Level::Info;
    entry.text = "Values: 123, hello";

    entry.args.emplace_back(123);
    entry.args.emplace_back("hello");
    entry.args.emplace_back(true);

    EXPECT_EQ(entry.args.size(), 3U);
    EXPECT_EQ(entry.args[0].as_int64(), 123);
    EXPECT_EQ(entry.args[1].as_string(), "hello");
    EXPECT_EQ(entry.args[2].as_bool(), true);
}

TEST(BidiTypesLog, ConsoleLogEntryAllLevels) {
    // Test ConsoleLogEntry with all log levels
    ConsoleLogEntry debug_entry;
    debug_entry.method = "debug";
    debug_entry.level = Level::Debug;
    EXPECT_EQ(debug_entry.level, Level::Debug);

    ConsoleLogEntry info_entry;
    info_entry.method = "info";
    info_entry.level = Level::Info;
    EXPECT_EQ(info_entry.level, Level::Info);

    ConsoleLogEntry warn_entry;
    warn_entry.method = "warn";
    warn_entry.level = Level::Warn;
    EXPECT_EQ(warn_entry.level, Level::Warn);

    ConsoleLogEntry error_entry;
    error_entry.method = "error";
    error_entry.level = Level::Error;
    EXPECT_EQ(error_entry.level, Level::Error);
}

TEST(BidiTypesLog, JavaScriptLogEntryEquality) {
    JavaScriptLogEntry entry1;
    entry1.level = Level::Error;
    entry1.text = "TypeError: x is not a function";
    entry1.timestamp_ms = 1234567890;
    entry1.stack_trace = "at line 42";

    JavaScriptLogEntry entry2;
    entry2.level = Level::Error;
    entry2.text = "TypeError: x is not a function";
    entry2.timestamp_ms = 1234567890;
    entry2.stack_trace = "at line 42";

    JavaScriptLogEntry entry3;
    entry3.level = Level::Warn;
    entry3.text = "Different error";

    EXPECT_EQ(entry1, entry2);
    EXPECT_NE(entry1, entry3);
}

TEST(BidiTypesLog, JavaScriptLogEntryDefaultValues) {
    JavaScriptLogEntry entry;

    EXPECT_TRUE(entry.text.empty());
    EXPECT_EQ(entry.timestamp_ms, 0U);
    EXPECT_FALSE(entry.realm.has_value());
    EXPECT_FALSE(entry.stack_trace.has_value());
}

TEST(BidiTypesLog, JavaScriptLogEntryOptionalFields) {
    JavaScriptLogEntry entry;
    entry.level = Level::Error;
    entry.text = "ReferenceError: foo is not defined";

    // Optional fields
    EXPECT_FALSE(entry.realm.has_value());
    EXPECT_FALSE(entry.stack_trace.has_value());

    bidi::types::script::RealmInfo realm;
    realm.realm = "realm-456";
    realm.type = bidi::types::script::RealmType::Worker;
    realm.origin = "https://worker.example.com";

    entry.realm = realm;
    entry.stack_trace =
        "ReferenceError: foo is not defined\n    at <anonymous>:1:1";

    EXPECT_TRUE(entry.realm.has_value());
    EXPECT_EQ(entry.realm->realm, "realm-456");
    EXPECT_TRUE(entry.stack_trace.has_value());
    EXPECT_NE(entry.stack_trace->find("ReferenceError"), std::string::npos);
}

TEST(BidiTypesLog, JavaScriptLogEntryWithRealm) {
    JavaScriptLogEntry entry;
    entry.level = Level::Error;
    entry.text = "Script error";
    entry.timestamp_ms = 9999999999;

    bidi::types::script::RealmInfo realm;
    realm.realm = "realm-789";
    realm.type = bidi::types::script::RealmType::Window;
    realm.origin = "https://app.example.com";

    entry.realm = realm;

    EXPECT_TRUE(entry.realm.has_value());
    EXPECT_EQ(entry.realm->type, bidi::types::script::RealmType::Window);
    EXPECT_EQ(entry.realm->origin, "https://app.example.com");
}

TEST(BidiTypesLog, JavaScriptLogEntryAllLevels) {
    // JavaScript exceptions can have different log levels
    JavaScriptLogEntry debug_entry;
    debug_entry.level = Level::Debug;
    debug_entry.text = "Debug exception";
    EXPECT_EQ(debug_entry.level, Level::Debug);

    JavaScriptLogEntry info_entry;
    info_entry.level = Level::Info;
    info_entry.text = "Info exception";
    EXPECT_EQ(info_entry.level, Level::Info);

    JavaScriptLogEntry warn_entry;
    warn_entry.level = Level::Warn;
    warn_entry.text = "Warning exception";
    EXPECT_EQ(warn_entry.level, Level::Warn);

    JavaScriptLogEntry error_entry;
    error_entry.level = Level::Error;
    error_entry.text = "Error exception";
    EXPECT_EQ(error_entry.level, Level::Error);
}

// ==================== Type Safety Tests ====================

TEST(BidiTypesStorageLog, StructDefaultConstruction) {
    // Storage types
    PartitionDescriptor partition;
    EXPECT_FALSE(partition.user_context.has_value());
    EXPECT_FALSE(partition.source_origin.has_value());

    // Log types
    ConsoleLogEntry console_log;
    EXPECT_TRUE(console_log.method.empty());
    EXPECT_EQ(console_log.timestamp_ms, 0U);

    JavaScriptLogEntry js_log;
    EXPECT_TRUE(js_log.text.empty());
    EXPECT_EQ(js_log.timestamp_ms, 0U);
}

TEST(BidiTypesStorageLog, CompleteConsoleLogEntry) {
    // Test fully populated ConsoleLogEntry
    ConsoleLogEntry entry;
    entry.method = "log";
    entry.level = Level::Info;
    entry.text = "Application started successfully";
    entry.timestamp_ms = 1704067200000; // 2024-01-01T00:00:00.000Z

    entry.args.emplace_back("arg1");
    entry.args.emplace_back(42);

    bidi::types::script::RealmInfo realm;
    realm.realm = "main-realm";
    realm.type = bidi::types::script::RealmType::Window;
    realm.origin = "https://example.com";
    entry.realm = realm;

    // Verify all fields
    EXPECT_EQ(entry.method, "log");
    EXPECT_EQ(entry.level, Level::Info);
    EXPECT_EQ(entry.text, "Application started successfully");
    EXPECT_EQ(entry.timestamp_ms, 1704067200000U);
    EXPECT_EQ(entry.args.size(), 2U);
    EXPECT_TRUE(entry.realm.has_value());
}

TEST(BidiTypesStorageLog, CompleteJavaScriptLogEntry) {
    // Test fully populated JavaScriptLogEntry
    JavaScriptLogEntry entry;
    entry.level = Level::Error;
    entry.text = "Uncaught TypeError: Cannot read property 'x' of undefined";
    entry.timestamp_ms = 1704067200000;
    entry.stack_trace =
        "TypeError: Cannot read property 'x' of undefined\n"
        "    at Object.<anonymous> (script.js:10:5)\n"
        "    at Module._compile (node:internal/modules/cjs/loader:1159:14)";

    bidi::types::script::RealmInfo realm;
    realm.realm = "error-realm";
    realm.type = bidi::types::script::RealmType::Window;
    realm.origin = "https://app.example.com";
    entry.realm = realm;

    // Verify all fields
    EXPECT_EQ(entry.level, Level::Error);
    EXPECT_NE(entry.text.find("TypeError"), std::string::npos);
    EXPECT_EQ(entry.timestamp_ms, 1704067200000U);
    EXPECT_TRUE(entry.stack_trace.has_value());
    EXPECT_NE(entry.stack_trace->find("script.js:10:5"), std::string::npos);
    EXPECT_TRUE(entry.realm.has_value());
}

// ==================== LogEntry Variant Tests ====================

TEST(BidiTypesLog, LogEntryVariantConsoleLog) {
    ConsoleLogEntry console_entry;
    console_entry.method = "log";
    console_entry.level = Level::Info;
    console_entry.text = "Test message";
    console_entry.timestamp_ms = 1234567890;

    LogEntry log_entry = console_entry;

    ASSERT_TRUE(std::holds_alternative<ConsoleLogEntry>(log_entry.value));
    const auto &entry = std::get<ConsoleLogEntry>(log_entry.value);
    EXPECT_EQ(entry.method, "log");
    EXPECT_EQ(entry.level, Level::Info);
    EXPECT_EQ(entry.text, "Test message");
}

TEST(BidiTypesLog, LogEntryVariantJavaScriptLog) {
    JavaScriptLogEntry js_entry;
    js_entry.level = Level::Error;
    js_entry.text = "TypeError occurred";
    js_entry.timestamp_ms = 9876543210;
    js_entry.stack_trace = "at line 42";

    LogEntry log_entry = js_entry;

    ASSERT_TRUE(std::holds_alternative<JavaScriptLogEntry>(log_entry.value));
    const auto &entry = std::get<JavaScriptLogEntry>(log_entry.value);
    EXPECT_EQ(entry.level, Level::Error);
    EXPECT_EQ(entry.text, "TypeError occurred");
    EXPECT_TRUE(entry.stack_trace.has_value());
}

TEST(BidiTypesLog, LogEntryVariantVisit) {
    ConsoleLogEntry console_entry;
    console_entry.method = "warn";
    console_entry.level = Level::Warn;
    console_entry.text = "Warning message";

    LogEntry log_entry = console_entry;

    bool visited_console = false;
    bool visited_js = false;

    std::visit(
        [&](const auto &entry) {
            using T = std::decay_t<decltype(entry)>;
            if constexpr (std::is_same_v<T, ConsoleLogEntry>) {
                visited_console = true;
                EXPECT_EQ(entry.level, Level::Warn);
            } else if constexpr (std::is_same_v<T, JavaScriptLogEntry>) {
                visited_js = true;
            }
        },
        log_entry.value);

    EXPECT_TRUE(visited_console);
    EXPECT_FALSE(visited_js);
}

// ==================== Boost.JSON Serialization Tests ====================

TEST(BidiTypesLog, ConsoleLogEntryJsonSerialization) {
    ConsoleLogEntry entry;
    entry.method = "log";
    entry.level = Level::Info;
    entry.text = "Hello from console";
    entry.timestamp_ms = 1704067200000;
    entry.args.emplace_back(123);
    entry.args.emplace_back("test");

    auto jv = boost::json::value_from(entry);
    const auto &obj = jv.as_object();

    EXPECT_EQ(obj.at("type").as_string(), "console");
    EXPECT_EQ(obj.at("method").as_string(), "log");
    EXPECT_EQ(obj.at("level").as_string(), "info");
    EXPECT_EQ(obj.at("text").as_string(), "Hello from console");
    EXPECT_EQ(obj.at("timestamp").as_uint64(), 1704067200000U);
    EXPECT_TRUE(obj.contains("args"));
    EXPECT_EQ(obj.at("args").as_array().size(), 2U);
}

TEST(BidiTypesLog, ConsoleLogEntryJsonDeserialization) {
    boost::json::object obj = {
        {"type", "console"},       {"method", "warn"},
        {"level", "warn"},         {"text", "Warning message"},
        {"timestamp", 1234567890}, {"args", boost::json::array{42, "hello"}}};

    boost::json::value jv = obj;
    auto entry = boost::json::value_to<ConsoleLogEntry>(jv);

    EXPECT_EQ(entry.method, "warn");
    EXPECT_EQ(entry.level, Level::Warn);
    EXPECT_EQ(entry.text, "Warning message");
    EXPECT_EQ(entry.timestamp_ms, 1234567890U);
    EXPECT_EQ(entry.args.size(), 2U);
    EXPECT_EQ(entry.args[0].as_int64(), 42);
    EXPECT_EQ(entry.args[1].as_string(), "hello");
}

TEST(BidiTypesLog, ConsoleLogEntryJsonRoundTrip) {
    ConsoleLogEntry original;
    original.method = "error";
    original.level = Level::Error;
    original.text = "Error occurred";
    original.timestamp_ms = 9999999999;
    original.args.emplace_back(true);
    original.args.emplace_back(3.14);

    auto jv = boost::json::value_from(original);
    auto parsed = boost::json::value_to<ConsoleLogEntry>(jv);

    EXPECT_EQ(parsed.method, original.method);
    EXPECT_EQ(parsed.level, original.level);
    EXPECT_EQ(parsed.text, original.text);
    EXPECT_EQ(parsed.timestamp_ms, original.timestamp_ms);
    EXPECT_EQ(parsed.args.size(), original.args.size());
}

TEST(BidiTypesLog, JavaScriptLogEntryJsonSerialization) {
    JavaScriptLogEntry entry;
    entry.level = Level::Error;
    entry.text = "TypeError: x is undefined";
    entry.timestamp_ms = 1704067200000;
    entry.stack_trace = "at Object.<anonymous> (test.js:10:5)";

    auto jv = boost::json::value_from(entry);
    const auto &obj = jv.as_object();

    EXPECT_EQ(obj.at("type").as_string(), "javascript");
    EXPECT_EQ(obj.at("level").as_string(), "error");
    EXPECT_EQ(obj.at("text").as_string(), "TypeError: x is undefined");
    EXPECT_EQ(obj.at("timestamp").as_uint64(), 1704067200000U);
    EXPECT_TRUE(obj.contains("stackTrace"));
    EXPECT_NE(obj.at("stackTrace").as_string().find("test.js"),
              std::string_view::npos);
}

TEST(BidiTypesLog, JavaScriptLogEntryJsonDeserialization) {
    boost::json::object obj = {{"type", "javascript"},
                               {"level", "error"},
                               {"text", "ReferenceError: foo is not defined"},
                               {"timestamp", 1234567890},
                               {"stackTrace", "at <anonymous>:1:1"}};

    boost::json::value jv = obj;
    auto entry = boost::json::value_to<JavaScriptLogEntry>(jv);

    EXPECT_EQ(entry.level, Level::Error);
    EXPECT_EQ(entry.text, "ReferenceError: foo is not defined");
    EXPECT_EQ(entry.timestamp_ms, 1234567890U);
    EXPECT_TRUE(entry.stack_trace.has_value());
    EXPECT_EQ(*entry.stack_trace, "at <anonymous>:1:1");
}

TEST(BidiTypesLog, JavaScriptLogEntryJsonRoundTrip) {
    JavaScriptLogEntry original;
    original.level = Level::Warn;
    original.text = "Warning from JavaScript";
    original.timestamp_ms = 5555555555;
    original.stack_trace = "at function (app.js:42:10)";

    auto jv = boost::json::value_from(original);
    auto parsed = boost::json::value_to<JavaScriptLogEntry>(jv);

    EXPECT_EQ(parsed.level, original.level);
    EXPECT_EQ(parsed.text, original.text);
    EXPECT_EQ(parsed.timestamp_ms, original.timestamp_ms);
    EXPECT_EQ(parsed.stack_trace, original.stack_trace);
}

TEST(BidiTypesLog, LogEntryVariantJsonSerializationConsole) {
    ConsoleLogEntry console_entry;
    console_entry.method = "log";
    console_entry.level = Level::Info;
    console_entry.text = "Console message";
    console_entry.timestamp_ms = 1111111111;
    console_entry.args.emplace_back("test");

    LogEntry log_entry = console_entry;
    auto jv = boost::json::value_from(log_entry);
    const auto &obj = jv.as_object();

    EXPECT_EQ(obj.at("type").as_string(), "console");
    EXPECT_EQ(obj.at("method").as_string(), "log");
    EXPECT_EQ(obj.at("level").as_string(), "info");
}

TEST(BidiTypesLog, LogEntryVariantJsonSerializationJavaScript) {
    JavaScriptLogEntry js_entry;
    js_entry.level = Level::Error;
    js_entry.text = "Script error";
    js_entry.timestamp_ms = 2222222222;

    LogEntry log_entry = js_entry;
    auto jv = boost::json::value_from(log_entry);
    const auto &obj = jv.as_object();

    EXPECT_EQ(obj.at("type").as_string(), "javascript");
    EXPECT_EQ(obj.at("level").as_string(), "error");
    EXPECT_EQ(obj.at("text").as_string(), "Script error");
}

TEST(BidiTypesLog, LogEntryVariantJsonDeserializationConsole) {
    boost::json::object obj = {
        {"type", "console"},       {"method", "warn"},
        {"level", "warn"},         {"text", "Warning"},
        {"timestamp", 3333333333}, {"args", boost::json::array{}}};

    boost::json::value jv = obj;
    auto log_entry = boost::json::value_to<LogEntry>(jv);

    ASSERT_TRUE(std::holds_alternative<ConsoleLogEntry>(log_entry.value));
    const auto &entry = std::get<ConsoleLogEntry>(log_entry.value);
    EXPECT_EQ(entry.method, "warn");
    EXPECT_EQ(entry.level, Level::Warn);
    EXPECT_EQ(entry.text, "Warning");
}

TEST(BidiTypesLog, LogEntryVariantJsonDeserializationJavaScript) {
    boost::json::object obj = {{"type", "javascript"},
                               {"level", "error"},
                               {"text", "Error message"},
                               {"timestamp", 4444444444}};

    boost::json::value jv = obj;
    auto log_entry = boost::json::value_to<LogEntry>(jv);

    ASSERT_TRUE(std::holds_alternative<JavaScriptLogEntry>(log_entry.value));
    const auto &entry = std::get<JavaScriptLogEntry>(log_entry.value);
    EXPECT_EQ(entry.level, Level::Error);
    EXPECT_EQ(entry.text, "Error message");
}

TEST(BidiTypesLog, LogEntryVariantJsonRoundTripConsole) {
    ConsoleLogEntry original;
    original.method = "debug";
    original.level = Level::Debug;
    original.text = "Debug message";
    original.timestamp_ms = 7777777777;

    LogEntry log_entry = original;
    auto jv = boost::json::value_from(log_entry);
    auto parsed = boost::json::value_to<LogEntry>(jv);

    ASSERT_TRUE(std::holds_alternative<ConsoleLogEntry>(parsed.value));
    const auto &entry = std::get<ConsoleLogEntry>(parsed.value);
    EXPECT_EQ(entry.method, original.method);
    EXPECT_EQ(entry.level, original.level);
    EXPECT_EQ(entry.text, original.text);
}

TEST(BidiTypesLog, LogEntryVariantJsonRoundTripJavaScript) {
    JavaScriptLogEntry original;
    original.level = Level::Warn;
    original.text = "JavaScript warning";
    original.timestamp_ms = 8888888888;
    original.stack_trace = "stack trace here";

    LogEntry log_entry = original;
    auto jv = boost::json::value_from(log_entry);
    auto parsed = boost::json::value_to<LogEntry>(jv);

    ASSERT_TRUE(std::holds_alternative<JavaScriptLogEntry>(parsed.value));
    const auto &entry = std::get<JavaScriptLogEntry>(parsed.value);
    EXPECT_EQ(entry.level, original.level);
    EXPECT_EQ(entry.text, original.text);
    EXPECT_EQ(entry.stack_trace, original.stack_trace);
}

TEST(BidiTypesLog, LogEntryVariantJsonMissingType) {
    boost::json::object obj = {{"level", "info"},
                               {"text", "Missing type field"},
                               {"timestamp", 5555555555}};

    boost::json::value jv = obj;
    EXPECT_THROW(boost::json::value_to<LogEntry>(jv), std::runtime_error);
}

TEST(BidiTypesLog, LogEntryVariantJsonInvalidType) {
    boost::json::object obj = {{"type", "invalid_type"},
                               {"level", "info"},
                               {"text", "Invalid type"},
                               {"timestamp", 6666666666}};

    boost::json::value jv = obj;
    EXPECT_THROW(boost::json::value_to<LogEntry>(jv), std::runtime_error);
}

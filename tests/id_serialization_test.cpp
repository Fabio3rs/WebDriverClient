// tests/id_serialization_test.cpp
#include "bidi/core.hpp"
#include <gtest/gtest.h>

using namespace bidi::core;

TEST(IdSafety, MaxSafeId) {
    constexpr auto max = MAX_SAFE_ID;
    EXPECT_TRUE(is_id_safe(max));
    EXPECT_FALSE(is_id_safe(max + 1ULL));
}

TEST(ParseResponse, SafeId) {
    const auto id = MAX_SAFE_ID;
    // Use core build_command to serialize
    const auto payload = build_command(id, "dummy.method", {});

    // Inspect raw JSON
    auto parsed_val = boost::json::parse(payload);
    ASSERT_TRUE(parsed_val.is_object());
    auto &obj = parsed_val.as_object();
    // id should be numeric when safe
    ASSERT_TRUE(obj["id"].is_int64());

    // Now craft a response payload to parse_response
    boost::json::object resp;
    resp["id"] = static_cast<std::int64_t>(id);
    resp["type"] = "success";
    resp["result"] = boost::json::object{};
    const auto resp_payload = boost::json::serialize(resp);
    auto parsed = parse_response(resp_payload);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, id);
    EXPECT_TRUE(is_id_safe(parsed->id));
}

TEST(ParseResponse, UnsafeId) {
    const auto id = MAX_SAFE_ID + 1ULL;
    // Serialize via core::build_command which should emit id as string
    const auto payload = build_command(id, "dummy.method", {});

    auto parsed_val = boost::json::parse(payload);
    ASSERT_TRUE(parsed_val.is_object());
    auto &obj = parsed_val.as_object();
    // id should be string when unsafe
    ASSERT_TRUE(obj["id"].is_string());

    // Now craft response payload using string id to simulate receiver behavior
    boost::json::object resp;
    resp["id"] = std::to_string(id);
    resp["type"] = "success";
    resp["result"] = boost::json::object{};
    const auto resp_payload = boost::json::serialize(resp);
    auto parsed = parse_response(resp_payload);
    // parse_response should accept string-encoded id and parse it into id_type
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, id);
    EXPECT_FALSE(is_id_safe(parsed->id));
}

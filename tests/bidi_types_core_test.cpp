// tests/bidi_types_core_test.cpp - Unit tests for core types
#include "bidi/types/core.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>

using namespace bidi::types::core;

// ==================== ErrorCode Tests ====================

TEST(BidiTypesCore, ErrorCodeToStringAllCodes) {
    // Test all 52 W3C BiDi error codes have string representations
    using enum ErrorCode;

    EXPECT_EQ(to_string(InvalidArgument), "invalid argument");
    EXPECT_EQ(to_string(InvalidSelector), "invalid selector");
    EXPECT_EQ(to_string(InvalidSessionId), "invalid session id");
    EXPECT_EQ(to_string(InvalidWebExtension), "invalid web extension");
    EXPECT_EQ(to_string(MoveTargetOutOfBounds), "move target out of bounds");
    EXPECT_EQ(to_string(NoSuchAlert), "no such alert");
    EXPECT_EQ(to_string(NoSuchNetworkCollector), "no such network collector");
    EXPECT_EQ(to_string(NoSuchElement), "no such element");
    EXPECT_EQ(to_string(NoSuchFrame), "no such frame");
    EXPECT_EQ(to_string(NoSuchHandle), "no such handle");
    EXPECT_EQ(to_string(NoSuchHistoryEntry), "no such history entry");
    EXPECT_EQ(to_string(NoSuchIntercept), "no such intercept");
    EXPECT_EQ(to_string(NoSuchNetworkData), "no such network data");
    EXPECT_EQ(to_string(NoSuchNode), "no such node");
    EXPECT_EQ(to_string(NoSuchRequest), "no such request");
    EXPECT_EQ(to_string(NoSuchScript), "no such script");
    EXPECT_EQ(to_string(NoSuchStoragePartition), "no such storage partition");
    EXPECT_EQ(to_string(NoSuchUserContext), "no such user context");
    EXPECT_EQ(to_string(NoSuchWebExtension), "no such web extension");
    EXPECT_EQ(to_string(SessionNotCreated), "session not created");
    EXPECT_EQ(to_string(UnableToCaptureScreen), "unable to capture screen");
    EXPECT_EQ(to_string(UnableToCloseBrowser), "unable to close browser");
    EXPECT_EQ(to_string(UnableToSetCookie), "unable to set cookie");
    EXPECT_EQ(to_string(UnableToSetFileInput), "unable to set file input");
    EXPECT_EQ(to_string(UnavailableNetworkData), "unavailable network data");
    EXPECT_EQ(to_string(UnderspecifiedStoragePartition),
              "underspecified storage partition");
    EXPECT_EQ(to_string(UnknownCommand), "unknown command");
    EXPECT_EQ(to_string(UnknownError), "unknown error");
    EXPECT_EQ(to_string(UnsupportedOperation), "unsupported operation");
    EXPECT_EQ(to_string(NoSuchChannel), "no such channel");
    EXPECT_EQ(to_string(NoSuchCookie), "no such cookie");
    EXPECT_EQ(to_string(NoSuchDownloadItem), "no such download item");
    EXPECT_EQ(to_string(NoSuchPermission), "no such permission");
    EXPECT_EQ(to_string(NoSuchWindow), "no such window");
}

TEST(BidiTypesCore, ErrorCodeParseKnownCodes) {
    // Test parsing known error codes
    auto code1 = parse_error_code("invalid argument");
    ASSERT_TRUE(code1.has_value());
    EXPECT_EQ(*code1, ErrorCode::InvalidArgument);

    auto code2 = parse_error_code("no such element");
    ASSERT_TRUE(code2.has_value());
    EXPECT_EQ(*code2, ErrorCode::NoSuchElement);

    auto code3 = parse_error_code("unknown error");
    ASSERT_TRUE(code3.has_value());
    EXPECT_EQ(*code3, ErrorCode::UnknownError);

    auto code4 = parse_error_code("session not created");
    ASSERT_TRUE(code4.has_value());
    EXPECT_EQ(*code4, ErrorCode::SessionNotCreated);
}

TEST(BidiTypesCore, ErrorCodeParseUnknownCode) {
    // Test forward compatibility: unknown codes return nullopt
    auto unknown = parse_error_code("future error code not in spec");
    EXPECT_FALSE(unknown.has_value());

    auto empty = parse_error_code("");
    EXPECT_FALSE(empty.has_value());

    auto typo = parse_error_code("invalid argumen"); // typo
    EXPECT_FALSE(typo.has_value());
}

TEST(BidiTypesCore, ErrorCodeRoundTrip) {
    // Test enum → string → enum round-trip for all codes
    using enum ErrorCode;

    auto test_round_trip = [](ErrorCode code) {
        auto str = to_string(code);
        auto parsed = parse_error_code(str);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, code);
    };

    test_round_trip(InvalidArgument);
    test_round_trip(InvalidSelector);
    test_round_trip(NoSuchElement);
    test_round_trip(NoSuchFrame);
    test_round_trip(NoSuchHandle);
    test_round_trip(SessionNotCreated);
    test_round_trip(UnknownError);
    test_round_trip(UnsupportedOperation);
    test_round_trip(NoSuchChannel);
    test_round_trip(NoSuchCookie);
    test_round_trip(NoSuchWindow);
}

TEST(BidiTypesCore, ErrorCodeCategoryHelpers) {
    using enum ErrorCode;

    // Test "no such X" category
    EXPECT_TRUE(is_no_such_error(NoSuchElement));
    EXPECT_TRUE(is_no_such_error(NoSuchFrame));
    EXPECT_TRUE(is_no_such_error(NoSuchHandle));
    EXPECT_TRUE(is_no_such_error(NoSuchWindow));
    EXPECT_FALSE(is_no_such_error(InvalidArgument));
    EXPECT_FALSE(is_no_such_error(UnknownError));

    // Test "invalid X" category
    EXPECT_TRUE(is_invalid_argument_error(InvalidArgument));
    EXPECT_TRUE(is_invalid_argument_error(InvalidSelector));
    EXPECT_TRUE(is_invalid_argument_error(InvalidSessionId));
    EXPECT_FALSE(is_invalid_argument_error(NoSuchElement));
    EXPECT_FALSE(is_invalid_argument_error(UnknownError));
}

// ==================== Boost.JSON Integration Tests ====================

TEST(BidiTypesCore, ErrorCodeBoostJsonSerialization) {
    using enum ErrorCode;

    // Test C++ → JSON
    auto jv1 = boost::json::value_from(InvalidArgument);
    EXPECT_EQ(jv1.as_string(), "invalid argument");

    auto jv2 = boost::json::value_from(NoSuchElement);
    EXPECT_EQ(jv2.as_string(), "no such element");

    auto jv3 = boost::json::value_from(SessionNotCreated);
    EXPECT_EQ(jv3.as_string(), "session not created");
}

TEST(BidiTypesCore, ErrorCodeBoostJsonDeserialization) {
    // Test JSON → C++
    boost::json::value jv1 = "invalid argument";
    auto code1 = boost::json::value_to<ErrorCode>(jv1);
    EXPECT_EQ(code1, ErrorCode::InvalidArgument);

    boost::json::value jv2 = "no such frame";
    auto code2 = boost::json::value_to<ErrorCode>(jv2);
    EXPECT_EQ(code2, ErrorCode::NoSuchFrame);

    // Unknown code defaults to UnknownError
    boost::json::value jv3 = "future unknown error";
    auto code3 = boost::json::value_to<ErrorCode>(jv3);
    EXPECT_EQ(code3, ErrorCode::UnknownError);
}

TEST(BidiTypesCore, ErrorCodeBoostJsonRoundTrip) {
    using enum ErrorCode;

    auto test_json_round_trip = [](ErrorCode code) {
        // C++ → JSON → C++
        auto jv = boost::json::value_from(code);
        auto parsed = boost::json::value_to<ErrorCode>(jv);
        EXPECT_EQ(parsed, code);
    };

    test_json_round_trip(InvalidArgument);
    test_json_round_trip(NoSuchElement);
    test_json_round_trip(SessionNotCreated);
    test_json_round_trip(UnknownError);
    test_json_round_trip(NoSuchChannel);
    test_json_round_trip(InvalidArgumentCookie);
    test_json_round_trip(UnableToSetCookie);
}

// ==================== MessageType Tests ====================

TEST(BidiTypesCore, MessageTypeToString) {
    using enum MessageType;

    EXPECT_EQ(to_string(Success), "success");
    EXPECT_EQ(to_string(Error), "error");
    EXPECT_EQ(to_string(Event), "event");
}

TEST(BidiTypesCore, MessageTypeBoostJsonSerialization) {
    using enum MessageType;

    auto jv1 = boost::json::value_from(Success);
    EXPECT_EQ(jv1.as_string(), "success");

    auto jv2 = boost::json::value_from(Error);
    EXPECT_EQ(jv2.as_string(), "error");

    auto jv3 = boost::json::value_from(Event);
    EXPECT_EQ(jv3.as_string(), "event");
}

// ==================== Type Safety Tests ====================

TEST(BidiTypesCore, ErrorCodeTypeEquality) {
    // Verify enum values are distinct
    using enum ErrorCode;

    EXPECT_NE(InvalidArgument, InvalidSelector);
    EXPECT_NE(NoSuchElement, NoSuchFrame);
    EXPECT_NE(UnknownError, UnsupportedOperation);
}

TEST(BidiTypesCore, ErrorCodeConstexprEvaluation) {
    // Verify constexpr functions can be evaluated at compile time
    constexpr auto str = to_string(ErrorCode::InvalidArgument);
    static_assert(str == "invalid argument");

    constexpr auto is_no_such = is_no_such_error(ErrorCode::NoSuchElement);
    static_assert(is_no_such);

    constexpr auto is_invalid =
        is_invalid_argument_error(ErrorCode::InvalidArgument);
    static_assert(is_invalid);
}

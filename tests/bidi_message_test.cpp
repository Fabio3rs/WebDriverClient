#include <gtest/gtest.h>
#include "BidiMessage.hpp"

using namespace bidi::ws;

TEST(BidiMessage, IsIdSafe) {
    EXPECT_TRUE(is_id_safe(1));
    EXPECT_TRUE(is_id_safe(9007199254740991ULL));
    EXPECT_FALSE(is_id_safe(9007199254740992ULL));
}

TEST(BidiMessage, BuildAndDetect) {
    auto s = build_command(42, "session.new", { {"capabilities", {}} });
    auto kind = detect_message_kind(s);
    // should be recognized as Response because it has id
    EXPECT_EQ(kind, MessageKind::Response);
}

#include "wait_helpers.hpp"
#include <gtest/gtest.h>

using namespace webdriver::wait;

TEST(WaitHelpers, ScriptContainsFunctionName) {
    std::string s = wait_for_element_script;
    // Quick sanity checks
    EXPECT_NE(s.find("waitForElement"), std::string::npos);
    EXPECT_NE(s.find("MutationObserver"), std::string::npos);
    EXPECT_NE(s.find("setTimeout"), std::string::npos);
}

TEST(WaitHelpers, ScriptHasArgumentsPlaceholders) {
    std::string s = wait_for_element_script;
    EXPECT_NE(s.find("arguments[0]"), std::string::npos);
    EXPECT_NE(s.find("arguments[1]"), std::string::npos);
    EXPECT_NE(s.find("arguments[2]"), std::string::npos);
}

// Note: This unit test validates the helper string only. Integration tests that
// exercise executeAsyncScript should be implemented using a mock or a real
// browser endpoint; that is out of scope for this unit test.

#include "wait_helpers.hpp"
#include <gtest/gtest.h>

using namespace webdriver::wait;

TEST(WaitHelpers, ScriptContainsFunctionName) {
    const std::string &script = wait_for_element_script;
    // Quick sanity checks
    EXPECT_NE(script.find("waitForElement"), std::string::npos);
    EXPECT_NE(script.find("MutationObserver"), std::string::npos);
    EXPECT_NE(script.find("setTimeout"), std::string::npos);
}

TEST(WaitHelpers, ScriptHasArgumentsPlaceholders) {
    const std::string &script = wait_for_element_script;
    EXPECT_NE(script.find("arguments[0]"), std::string::npos);
    EXPECT_NE(script.find("arguments[1]"), std::string::npos);
    EXPECT_NE(script.find("arguments[2]"), std::string::npos);
}

// Note: This unit test validates the helper string only. Integration tests that
// exercise executeAsyncScript should be implemented using a mock or a real
// browser endpoint; that is out of scope for this unit test.

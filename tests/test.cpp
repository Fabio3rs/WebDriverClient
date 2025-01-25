#include "WebDriverClient.hpp"
#include <gtest/gtest.h>
#include <stdexcept>

static const char *serverUrl = "http://localhost:8080";

/*static auto startWebDriver() {
    std::string cmd = "chromedriver --port=9515 --url-base=/wd/hub";
    std::system(cmd.c_str());
}*/

class SimpleTests : public ::testing::Test {
  protected:
    void SetUp() override {
        WebDriver::json args = WebDriver::json::array({
            "--headless",
            "--disable-gpu",
            "--no-sandbox",
            "--disable-dev-shm-usage",
        });

        browser.connect(args);
    }

    void TearDown() override {
        // std::system("pkill chromedriver");
    }
    WebDriver browser;
};

TEST_F(SimpleTests, Test1) {
    browser.get(serverUrl);

    auto title = browser.getTitle().get<std::string>();
    EXPECT_EQ(title, "Sample Test Page");
    EXPECT_EQ(1, 1);
}

TEST_F(SimpleTests, LocateClickMeButton) {
    browser.get(serverUrl);

    auto button = browser.findElement("css selector", "[id=click-me-button]");
    EXPECT_FALSE(button.empty());

    browser.getElementText(button);
}

TEST_F(SimpleTests, WaitForElement) {
    using namespace std::chrono_literals;
    browser.get(serverUrl);

    auto newElement = browser.waitElement("css", "#new-timed-element", 2000ms);
    EXPECT_FALSE(newElement.empty());
}

TEST_F(SimpleTests, WaitForElementTimeout) {
    using namespace std::chrono_literals;
    browser.get(serverUrl);

    EXPECT_THROW(auto newElement =
                     browser.waitElement("css", "#new-timed-element", 100ms);
                 , std::runtime_error);
}

TEST_F(SimpleTests, LocateMultipleElements) {
    browser.get(serverUrl);

    auto genderOptions = browser.findElement("css selector", "[id=gender]");
    EXPECT_FALSE(genderOptions.empty());

    auto options = browser.findChildElements(
        WebDriver::getIdFromElement(genderOptions), "css selector", "option");

    EXPECT_TRUE(options.is_array());

    /*
            <option value="">Select gender</option>
            <option value="male">Male</option>
            <option value="female">Female</option>
            <option value="other">Other</option>
    */
    EXPECT_EQ(options.size(), 4);
}

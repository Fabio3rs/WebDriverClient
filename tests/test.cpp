#include "WebDriverClient.hpp"
#include <Poco/JSON/Array.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

static const char *serverUrl = "http://localhost:8080";

/*static auto startWebDriver() {
    std::string cmd = "chromedriver --port=9515 --url-base=/wd/hub";
    std::system(cmd.c_str());
}*/

static auto initWebDriverClient() -> WebDriver {
    WebDriver browser;

    WebDriver::json args = WebDriver::json::array({
        "--headless",
        "--disable-gpu",
        "--no-sandbox",
        "--disable-dev-shm-usage",
    });

    browser.connect(args);

    return browser;
}

TEST(SampleTest, Test1) {
    WebDriver browser = initWebDriverClient();

    browser.get(serverUrl);

    auto title = browser.getTitle().get<std::string>();
    EXPECT_EQ(title, "Sample Test Page");
    EXPECT_EQ(1, 1);
}

TEST(SampleTest, LocateClickMeButton) {
    WebDriver browser = initWebDriverClient();

    browser.get(serverUrl);

    auto button = browser.findElement("css selector", "[id=click-me-button]");
    EXPECT_FALSE(button.empty());

    browser.getElementText(button);
}

TEST(SampleTest, LocateMultipleElements) {
    WebDriver browser = initWebDriverClient();

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

#include "Strutils.hpp"
#include "WebDriverClient.hpp"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <thread>
#include <unistd.h>

auto getenvor(const char *name, const char *def = "") -> std::string {
    auto env = std::getenv(name);
    if (env == nullptr) {
        return def;
    }

    return env;
}

template <class T>
static T multiTry(std::function<T()> fn, std::chrono::seconds maxTime) {
    auto start = std::chrono::system_clock::now();
    while (true) {
        try {
            return fn();
        } catch (const std::exception &e) {
            std::cerr << "Error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        auto end = std::chrono::system_clock::now();

        if (end - start > maxTime) {
            throw std::runtime_error("Timeout");
        }
    }
}

static void load_dotenv(std::string path = ".env") {
    std::ifstream dotenv(path);

    if (!dotenv.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(dotenv, line)) {
        auto parts = Strutils::split(line, "=");
        if (parts.size() != 2) {
            continue;
        }

        auto key = Strutils::trimCopy(parts[0]);
        auto value = Strutils::trimCopy(parts[1]);

        setenv(key.c_str(), value.c_str(), 1);
    }
}

void interruptsignal(int signal) {
    std::cerr << "Interrupt signal (" << signal << ") received.\n";
    throw std::runtime_error("Interrupt signal received");
}

void maincode() {
    signal(SIGINT, interruptsignal);
    signal(SIGTERM, interruptsignal);
    load_dotenv();
    std::cout << "Hello, World!" << std::endl;

    WebDriver browser;

    WebDriver::json args = WebDriver::json::array(/*{"--headless"}*/);

    browser.connect(args, "chrome");

    browser.get("https://duckduckgo.com");

    auto searchBox =
        browser.waitElement("css", "input[name=q]", std::chrono::seconds(10));

    browser.sendKeysToElement(searchBox, "Hello, World!");
    // webdriverTest();

    std::this_thread::sleep_for(std::chrono::seconds(5));
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {
    try {
        maincode();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#include "Strutils.hpp"
#include "WebDriverClient.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
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
    auto *env = std::getenv(name);
    if (env == nullptr) {
        return def;
    }

    return env;
}

template <class T, class Handler>
static void async_multi_try(boost::asio::io_context &ioc, std::function<T()> fn,
                            std::chrono::seconds maxTime, Handler handler) {
    using namespace std::chrono;
    struct State {
        steady_clock::time_point start;
        std::shared_ptr<boost::asio::steady_timer> timer;
    };
    auto st = std::make_shared<State>();
    st->start = steady_clock::now();
    st->timer = std::make_shared<boost::asio::steady_timer>(ioc);

    auto attempt = std::make_shared<std::function<void()>>();
    *attempt = [st, attempt, &ioc, fn = std::move(fn), maxTime,
                handler]() mutable {
        std::thread([st, attempt, &ioc, fn = fn, maxTime, handler]() mutable {
            try {
                auto r = fn();
                ioc.post([handler, r = std::move(r)]() mutable {
                    handler(boost::system::error_code(), std::move(r));
                });
            } catch (...) {
                auto now = std::chrono::steady_clock::now();
                if (now - st->start > maxTime) {
                    ioc.post([handler]() mutable {
                        handler(make_error_code(std::errc::timed_out), T{});
                    });
                    return;
                }
                // schedule retry after small delay without busy-wait
                st->timer->expires_after(std::chrono::milliseconds(100));
                st->timer->async_wait(
                    [attempt](const boost::system::error_code &ec) {
                        if (ec) {
                            return;
                        }
                        (*attempt)();
                    });
            }
        }).detach();
    };

    // start first attempt
    (*attempt)();
}

static void load_dotenv(const std::string &path = ".env") {
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
    bidi::logging::log_error(std::string("Interrupt signal (") +
                             std::to_string(signal) + ") received.");
    throw std::runtime_error("Interrupt signal received");
}

void maincode() {
    signal(SIGINT, interruptsignal);
    signal(SIGTERM, interruptsignal);
    load_dotenv();
    bidi::logging::log_info("Hello, World!");

    WebDriver browser;

    WebDriver::json args = WebDriver::json::array(/*{"--headless"}*/);

    browser.connect(args, "chrome");

    browser.get("https://duckduckgo.com");

    auto searchBox =
        browser.waitElement("css", "input[name=q]", std::chrono::seconds(10));

    browser.sendKeysToElement(searchBox, "Hello, World!");
    // webdriverTest();

    // no busy-waiting sleep here; main thread continues or uses io_context
}

auto main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) -> int {
    try {
        maincode();
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Error: ") + e.what());
        return 1;
    }
    return 0;
}

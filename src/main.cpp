#include "Strutils.hpp"
#include "WebDriverClient.hpp"
#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <thread>
#include <unistd.h>

auto getenv_or_default(std::string_view env_name,
                       std::optional<std::string_view> default_value =
                           std::nullopt) -> std::string {
    // getenv expects a null-terminated C string, construct one temporarily
    const std::string env_name_str{env_name};
    auto *env = std::getenv(env_name_str.c_str());
    if (env == nullptr) {
        if (default_value.has_value()) {
            return std::string(*default_value);
        }
        return std::string{};
    }

    return std::string(env);
}

template <class T, class Handler>
static void async_multi_try(boost::asio::io_context &ioc,
                            std::function<T()> func,
                            std::chrono::seconds max_time, Handler handler) {
    using namespace std::chrono;
    struct State {
        steady_clock::time_point start;
        std::shared_ptr<boost::asio::steady_timer> timer;
    };
    auto st = std::make_shared<State>();
    st->start = steady_clock::now();
    st->timer = std::make_shared<boost::asio::steady_timer>(ioc);

    auto attempt = std::make_shared<std::function<void()>>();
    // shared thread_pool used to run blocking attempts without detaching
    // threads
    static boost::asio::thread_pool blocking_pool{
        std::max(1U, std::thread::hardware_concurrency())};

    constexpr auto k_retry_delay_ms = std::chrono::milliseconds(100);

    *attempt = [st, attempt, &ioc, func = std::move(func), max_time,
                handler]() mutable {
        boost::asio::post(blocking_pool, [st, attempt, &ioc, func = func,
                                          max_time, handler]() mutable {
            try {
                auto result_value = func();
                ioc.post([handler,
                          result_value = std::move(result_value)]() mutable {
                    handler(boost::system::error_code(),
                            std::move(result_value));
                });
            } catch (...) {
                auto now = std::chrono::steady_clock::now();
                if (now - st->start > max_time) {
                    ioc.post([handler]() mutable {
                        handler(make_error_code(std::errc::timed_out), T{});
                    });
                    return;
                }
                // schedule retry after small delay without busy-wait
                st->timer->expires_after(k_retry_delay_ms);
                st->timer->async_wait(
                    [attempt](const boost::system::error_code &error_code) {
                        if (error_code) {
                            return;
                        }
                        (*attempt)();
                    });
            }
        });
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

    // connect may return a result; keep it for diagnostics
    auto connect_res = browser.connect(args, "chrome");

    browser.get("https://duckduckgo.com");

    constexpr auto k_element_wait_seconds = std::chrono::seconds(10);
    auto search_box_handle =
        browser.waitElement("css", "input[name=q]", k_element_wait_seconds);

    browser.sendKeysToElement(search_box_handle, "Hello, World!");
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

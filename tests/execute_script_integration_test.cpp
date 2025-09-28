// Minimal integration test: start a tiny HTTP server that responds to
// /session/<id>/execute/sync and /session/<id>/execute_async and verify
// WebDriver::executeSyncScript / executeAsyncScript round-trip.

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "WebDriverClient.hpp"

using json = nlohmann::json;

class SimpleHttpServer {
  public:
    SimpleHttpServer() : running_(false), listen_fd_(-1) {}

    ~SimpleHttpServer() { stop(); }

    // start server on ephemeral port (0) and return the bound port
    uint16_t start() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
            throw std::runtime_error("socket failed");

        int on = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = 0; // ephemeral

        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr),
                 sizeof(addr)) < 0) {
            close(listen_fd_);
            throw std::runtime_error("bind failed");
        }

        if (listen(listen_fd_, 8) < 0) {
            close(listen_fd_);
            throw std::runtime_error("listen failed");
        }

        // get assigned port
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&bound),
                        &len) < 0) {
            close(listen_fd_);
            throw std::runtime_error("getsockname failed");
        }

        port_ = ntohs(bound.sin_port);

        running_.store(true);
        thread_ = std::thread(&SimpleHttpServer::acceptLoop, this);

        return port_;
    }

    void stop() {
        if (!running_.load())
            return;
        running_.store(false);
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable())
            thread_.join();
    }

  private:
    void acceptLoop() {
        while (running_.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int fd =
                accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr),
                       &client_len);
            if (fd < 0) {
                // could be interrupted by shutdown
                if (!running_.load())
                    break;
                continue;
            }

            // handle in short-lived thread so we can accept next
            std::thread(&SimpleHttpServer::handleClient, this, fd).detach();
        }
    }

    static std::string readAll(int fd, size_t bytes) {
        std::string out;
        out.resize(bytes);
        size_t off = 0;
        while (off < bytes) {
            ssize_t r = ::read(fd, &out[off], bytes - off);
            if (r <= 0)
                break;
            off += static_cast<size_t>(r);
        }
        out.resize(off);
        return out;
    }

    void handleClient(int fd) {
        // Read request headers
        std::string req;
        char buf[1024];
        ssize_t n;
        // read until \r\n\r\n
        while (true) {
            n = ::read(fd, buf, sizeof(buf));
            if (n <= 0)
                break;
            req.append(buf, buf + n);
            if (req.find("\r\n\r\n") != std::string::npos)
                break;
            // continue reading
        }

        if (req.empty()) {
            close(fd);
            return;
        }

        // parse request line
        std::istringstream s(req);
        std::string request_line;
        std::getline(s, request_line);
        if (!request_line.empty() && request_line.back() == '\r')
            request_line.pop_back();

        std::string method, path, httpver;
        {
            std::istringstream rl(request_line);
            rl >> method >> path >> httpver;
        }

        // find Content-Length
        size_t content_length = 0;
        std::string line;
        while (std::getline(s, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break;
            std::string key;
            std::istringstream ls(line);
            if (std::getline(ls, key, ':')) {
                if (key == "Content-Length") {
                    std::string value;
                    std::getline(ls, value);
                    try {
                        content_length = std::stoul(value);
                    } catch (...) {
                        content_length = 0;
                    }
                }
            }
        }

        std::string body;
        // there might be leftover bytes in req after header
        auto header_end = req.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            size_t already = req.size() - (header_end + 4);
            if (already > 0)
                body = req.substr(header_end + 4, already);
        }

        if (body.size() < content_length) {
            auto need = content_length - body.size();
            body += readAll(fd, need);
        }

        // Prepare response
        json resp_body;
        try {
            if (!body.empty()) {
                auto parsed = json::parse(body);
                // Expect object with script and args
                json value = json::object();
                if (parsed.contains("script")) {
                    value["script"] = parsed["script"];
                } else {
                    value["script"] = nullptr;
                }

                if (parsed.contains("args") && parsed["args"].is_array()) {
                    value["args"] = parsed["args"];
                } else {
                    value["args"] = json::array();
                }
                // Decide by path whether sync or async
                if (path.find("execute/sync") != std::string::npos ||
                    path.find("execute/sync") != std::string::npos) {
                    value["type"] = "sync";
                }
                if (path.find("execute_async") != std::string::npos ||
                    path.find("execute/async") != std::string::npos) {
                    value["type"] = "async";
                }
                resp_body = json::object({{"value", value}});
            } else {
                resp_body = json::object({{"value", json::object()}});
            }
        } catch (const std::exception &e) {
            resp_body = json::object(
                {{"value",
                  json::object({{"error", std::string("parse_error")}})}});
        }

        auto body_str = resp_body.dump();
        std::ostringstream reply;
        reply << "HTTP/1.1 200 OK\r\n";
        reply << "Content-Type: application/json\r\n";
        reply << "Content-Length: " << body_str.size() << "\r\n";
        reply << "Connection: close\r\n";
        reply << "\r\n";
        reply << body_str;

        auto out = reply.str();
        size_t sent = 0;
        while (sent < out.size()) {
            ssize_t w = ::write(fd, out.data() + sent, out.size() - sent);
            if (w <= 0)
                break;
            sent += static_cast<size_t>(w);
        }

        close(fd);
    }

    std::atomic<bool> running_;
    int listen_fd_;
    uint16_t port_;
    std::thread thread_;
};

TEST(ExecuteScriptIntegration, ExecuteSyncAndAsync) {
    SimpleHttpServer srv;
    uint16_t port = srv.start();

    WebDriver wd;
    wd.sessionId = "testsession";
    wd.webDriverUrl = "http://127.0.0.1:" + std::to_string(port);

    // call sync
    auto res_sync = wd.executeSyncScript("return 1;", "arg1", 123);
    ASSERT_TRUE(res_sync.is_object());
    EXPECT_EQ(res_sync.value("type", ""), "sync");
    EXPECT_TRUE(res_sync.contains("args"));
    EXPECT_EQ(res_sync["args"].size(), 2);

    // call async
    auto res_async = wd.executeAsyncScript("(done)=>done(42);", "a", "b");
    ASSERT_TRUE(res_async.is_object());
    EXPECT_EQ(res_async.value("type", ""), "async");
    EXPECT_TRUE(res_async.contains("args"));
    EXPECT_EQ(res_async["args"].size(), 2);

    srv.stop();
}

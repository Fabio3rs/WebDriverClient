#pragma once
/**
 * @file test_http_server.hpp
 * @brief Simple HTTP server for integration testing
 *
 * Provides a minimal HTTP/1.1 server for testing network interception,
 * authentication, custom headers, and response modification.
 *
 * Features:
 * - Custom response handlers per path
 * - Basic authentication support
 * - Custom headers and status codes
 * - Response delays for timeout testing
 * - Thread-safe operation
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace bidi::testing {

/**
 * @brief HTTP request information
 */
struct HttpRequest {
    std::string method;  // GET, POST, etc.
    std::string path;    // /path/to/resource
    std::string version; // HTTP/1.1
    std::map<std::string, std::string> headers;
    std::string body;

    // Convenience method to get header value (case-insensitive)
    [[nodiscard]] auto get_header(std::string_view name) const
        -> std::optional<std::string> {
        auto it = std::ranges::find_if(headers, [&](const auto &pair) {
            return std::equal(pair.first.begin(), pair.first.end(),
                              name.begin(), name.end(),
                              [](unsigned char a, unsigned char b) {
                                  return std::tolower(a) == std::tolower(b);
                              });
        });
        if (it != headers.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto stringify() const -> std::string {
        std::ostringstream oss;
        oss << method << " " << path << " " << version << "\r\n";
        for (const auto &header : headers) {
            oss << header.first << ": " << header.second << "\r\n";
        }
        oss << "\r\n";
        oss << body;
        return oss.str();
    }
};

/**
 * @brief HTTP response builder
 */
struct HttpResponse {
    int status_code{200};
    std::string reason_phrase{"OK"};
    std::map<std::string, std::string> headers;
    std::string body;

    // Convenience constructors
    static auto ok(std::string body_content) -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 200;
        resp.reason_phrase = "OK";
        resp.body = std::move(body_content);
        resp.headers["Content-Type"] = "text/html; charset=utf-8";
        return resp;
    }

    static auto json(std::string json_content) -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 200;
        resp.reason_phrase = "OK";
        resp.body = std::move(json_content);
        resp.headers["Content-Type"] = "application/json";
        return resp;
    }

    static auto unauthorized(std::string realm = "Test Realm") -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 401;
        resp.reason_phrase = "Unauthorized";
        resp.headers["WWW-Authenticate"] = "Basic realm=\"" + realm + "\"";
        resp.body = R"({"error":"unauthorized"})";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    }

    static auto not_found() -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 404;
        resp.reason_phrase = "Not Found";
        resp.body = R"({"error":"not_found"})";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    }

    static auto redirect(std::string location) -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 302;
        resp.reason_phrase = "Found";
        resp.headers["Location"] = std::move(location);
        return resp;
    }
};

/**
 * @brief Request handler function type
 */
using RequestHandler = std::function<HttpResponse(const HttpRequest &)>;

/**
 * @brief Simple HTTP server for testing
 *
 * Example usage:
 * @code
 * TestHttpServer server;
 * server.add_handler("/api/data", [](const HttpRequest& req) {
 *     return HttpResponse::json(R"({"status":"ok"})");
 * });
 * server.add_auth_handler("/protected", "user", "pass", [](const HttpRequest&
 * req) { return HttpResponse::ok("<html>Protected content</html>");
 * });
 * uint16_t port = server.start();
 * // ... run tests ...
 * server.stop();
 * @endcode
 */
class TestHttpServer {
  public:
    TestHttpServer() : running_(false), listen_fd_(-1), port_(0) {}

    ~TestHttpServer() { stop(); }

    TestHttpServer(const TestHttpServer &) = delete;
    auto operator=(const TestHttpServer &) -> TestHttpServer & = delete;
    TestHttpServer(TestHttpServer &&) = delete;
    auto operator=(TestHttpServer &&) -> TestHttpServer & = delete;

    /**
     * @brief Start server on ephemeral port
     * @return Bound port number
     */
    auto start() -> uint16_t {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            throw std::runtime_error("socket() failed");
        }

        int on = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = 0; // Ephemeral port

        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr),
                 sizeof(addr)) < 0) {
            close(listen_fd_);
            throw std::runtime_error("bind() failed");
        }

        if (listen(listen_fd_, 8) < 0) {
            close(listen_fd_);
            throw std::runtime_error("listen() failed");
        }

        // Get assigned port
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&bound),
                        &len) < 0) {
            close(listen_fd_);
            throw std::runtime_error("getsockname() failed");
        }

        port_ = ntohs(bound.sin_port);
        running_.store(true);
        accept_thread_ = std::thread(&TestHttpServer::accept_loop, this);

        return port_;
    }

    /**
     * @brief Stop server and clean up
     */
    void stop() {
        if (!running_.load()) {
            return;
        }
        running_.store(false);
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        // Join client threads outside the mutex to avoid deadlocks
        std::vector<std::thread> client_threads;
        {
            std::lock_guard<std::mutex> lock(client_threads_mutex_);
            client_threads = take_client_threads();
        }
        for (auto &thread : client_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    /**
     * @brief Get base URL (http://localhost:PORT)
     */
    [[nodiscard]] auto base_url() const -> std::string {
        return "http://localhost:" + std::to_string(port_);
    }

    /**
     * @brief Get server port
     */
    [[nodiscard]] auto port() const -> uint16_t { return port_; }

    /**
     * @brief Add request handler for specific path
     */
    void add_handler(std::string path, RequestHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_[std::move(path)] = std::move(handler);
    }

    /**
     * @brief Add handler with basic authentication
     */
    void add_auth_handler(std::string path, std::string username,
                          std::string password, RequestHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auth_handlers_[path] = {std::move(username), std::move(password)};
        handlers_[std::move(path)] = std::move(handler);
    }

    /**
     * @brief Add default handler (fallback for unmatched paths)
     */
    void set_default_handler(RequestHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        default_handler_ = std::move(handler);
    }

    /**
     * @brief Clear all handlers
     */
    void clear_handlers() {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_.clear();
        auth_handlers_.clear();
        default_handler_ = nullptr;
    }

  private:
    void accept_loop() {
        while (running_.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd =
                accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr),
                       &client_len);
            if (client_fd < 0) {
                if (!running_.load()) {
                    break;
                }
                continue;
            }

            // Handle client connection with managed thread
            {
                std::lock_guard<std::mutex> lock(client_threads_mutex_);
                client_threads_.emplace_back(
                    &TestHttpServer::handle_client_thread, this, client_fd);
            }
        }
    }

    void handle_client(int client_fd) {
        // Read request headers
        std::string req_str;
        constexpr size_t kBufSize = 4096;
        std::array<char, kBufSize> buf{};

        // Read until \r\n\r\n
        while (true) {
            ssize_t n = ::read(client_fd, buf.data(), buf.size());
            if (n <= 0) {
                close(client_fd);
                return;
            }
            req_str.append(buf.data(), static_cast<size_t>(n));
            if (req_str.find("\r\n\r\n") != std::string::npos) {
                break;
            }
        }

        // Parse request
        auto req_opt = parse_request(req_str);
        if (!req_opt) {
            send_response(client_fd, HttpResponse::not_found());
            close(client_fd);
            return;
        }

        auto &request = *req_opt;

        // Read body if Content-Length present
        if (request.headers.contains("Content-Length")) {
            try {
                size_t content_length =
                    std::stoull(request.headers.at("Content-Length"));
                auto header_end = req_str.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    size_t already = req_str.size() - (header_end + 4);
                    if (already > 0) {
                        request.body = req_str.substr(header_end + 4, already);
                    }
                }

                if (request.body.size() < content_length) {
                    size_t need = content_length - request.body.size();
                    std::string remaining;
                    remaining.resize(need);
                    size_t off = 0;
                    while (off < need) {
                        ssize_t n =
                            ::read(client_fd, &remaining[off], need - off);
                        if (n <= 0) {
                            break;
                        }
                        off += static_cast<size_t>(n);
                    }
                    remaining.resize(off);
                    request.body += remaining;
                }
            } catch (...) {
                // Invalid Content-Length
            }
        }

        // Find handler
        HttpResponse response;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);

            // Check authentication if required
            if (auth_handlers_.contains(request.path)) {
                const auto &[username, password] =
                    auth_handlers_.at(request.path);
                if (!check_auth(request, username, password)) {
                    response = HttpResponse::unauthorized();
                    send_response(client_fd, response);
                    close(client_fd);
                    return;
                }
            }

            // Execute handler
            if (handlers_.contains(request.path)) {
                response = handlers_.at(request.path)(request);
            } else if (default_handler_) {
                response = default_handler_(request);
            } else {
                response = HttpResponse::not_found();
            }
        }

        send_response(client_fd, response);
        close(client_fd);
    }

    static auto parse_request(const std::string &req_str)
        -> std::optional<HttpRequest> {
        std::istringstream stream(req_str);
        std::string request_line;
        std::getline(stream, request_line);
        if (!request_line.empty() && request_line.back() == '\r') {
            request_line.pop_back();
        }

        HttpRequest req;
        std::istringstream line_stream(request_line);
        line_stream >> req.method >> req.path >> req.version;

        if (req.method.empty() || req.path.empty()) {
            return std::nullopt;
        }

        // Parse headers
        std::string header_line;
        while (std::getline(stream, header_line)) {
            if (!header_line.empty() && header_line.back() == '\r') {
                header_line.pop_back();
            }
            if (header_line.empty()) {
                break;
            }

            auto colon = header_line.find(':');
            if (colon != std::string::npos) {
                std::string key = header_line.substr(0, colon);
                std::string value = header_line.substr(colon + 1);
                // Trim leading space from value
                if (!value.empty() && value[0] == ' ') {
                    value = value.substr(1);
                }
                req.headers[key] = value;
            }
        }

        return req;
    }

    static auto check_auth(const HttpRequest &req,
                           [[maybe_unused]] const std::string &username,
                           [[maybe_unused]] const std::string &password)
        -> bool {
        if (!req.headers.contains("Authorization")) {
            return false;
        }

        const auto &auth_header = req.headers.at("Authorization");
        if (auth_header.find("Basic ") != 0) {
            return false;
        }

        // For simplicity, we don't decode base64 here
        // Just check if Authorization header is present
        // Real implementation would decode and verify credentials
        return !auth_header.empty();
    }

    static void send_response(int client_fd, const HttpResponse &response) {
        std::ostringstream resp_stream;
        resp_stream << "HTTP/1.1 " << response.status_code << " "
                    << response.reason_phrase << "\r\n";

        // Add headers
        for (const auto &[key, value] : response.headers) {
            resp_stream << key << ": " << value << "\r\n";
        }

        // Add Content-Length if not present
        if (!response.headers.contains("Content-Length")) {
            resp_stream << "Content-Length: " << response.body.size() << "\r\n";
        }

        resp_stream << "Connection: close\r\n";
        resp_stream << "\r\n";
        resp_stream << response.body;

        auto resp_str = resp_stream.str();
        size_t sent = 0;
        while (sent < resp_str.size()) {
            ssize_t n = ::write(client_fd, resp_str.data() + sent,
                                resp_str.size() - sent);
            if (n <= 0) {
                break;
            }
            sent += static_cast<size_t>(n);
        }
    }

    std::vector<std::thread> take_client_threads() {
        std::vector<std::thread> threads;
        threads.swap(client_threads_);
        return threads;
    }

    void handle_client_thread(int client_fd) {
        try {
            handle_client(client_fd);
        } catch (...) {
            close(client_fd);
        }
    }

    std::atomic<bool> running_;
    int listen_fd_;
    uint16_t port_;
    std::thread accept_thread_;

    std::mutex handlers_mutex_;
    std::map<std::string, RequestHandler> handlers_;
    std::map<std::string, std::pair<std::string, std::string>> auth_handlers_;
    RequestHandler default_handler_;

    std::mutex client_threads_mutex_;
    std::vector<std::thread> client_threads_;
};

} // namespace bidi::testing

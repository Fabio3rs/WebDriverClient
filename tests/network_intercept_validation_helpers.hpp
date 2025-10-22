// Declarations for test helpers used to validate network interception via JS
#pragma once

#include "bidi/client.hpp"
#include <memory>
#include <string>
#include <vector>

namespace bidi {
class Client;
namespace testing {

struct FetchResult {
    bool is_success = false;
    int status_code = 0;
    std::string status_text;
    std::string body;
    // headers stored as vector of pairs (name, value)
    std::vector<std::pair<std::string, std::string>> headers;

    [[nodiscard]] bool has_header(const std::string &name,
                                  const std::string &value) const {
        for (const auto &entry : headers) {
            // Case-insensitive comparison for header names
            if (std::ranges::equal(entry.first, name,
                                   [](unsigned char lhs, unsigned char rhs) {
                                       return std::tolower(lhs) ==
                                              std::tolower(rhs);
                                   }) &&
                entry.second == value) {
                return true;
            }
        }
        return false;
    }
};

// Helpers implemented in network_intercept_validation_helpers.cpp
// Helpers implemented in network_intercept_validation_helpers.cpp
auto make_fetch_request(const std::shared_ptr<bidi::Client> &client,
                        std::string_view context_id, std::string_view url)
    -> bidi::Task<FetchResult>;

auto make_verify_request_blocked(const std::shared_ptr<bidi::Client> &client,
                                 std::string_view context_id,
                                 std::string_view url) -> bidi::Task<bool>;

auto make_verify_response_body(const std::shared_ptr<bidi::Client> &client,
                               std::string_view context_id,
                               std::string_view url,
                               std::string_view expected_body)
    -> bidi::Task<bool>;

auto make_verify_response_headers(
    const std::shared_ptr<bidi::Client> &client, std::string_view context_id,
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>> &expected_headers)
    -> bidi::Task<bool>;

auto make_verify_response_status(const std::shared_ptr<bidi::Client> &client,
                                 std::string_view context_id,
                                 std::string_view url, int expected_status)
    -> bidi::Task<bool>;

} // namespace testing
} // namespace bidi

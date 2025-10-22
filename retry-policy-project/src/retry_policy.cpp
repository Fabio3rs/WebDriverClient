#include "retry_policy.hpp"

namespace retry_policy {

RetryPolicy::RetryPolicy(int attempts, std::chrono::milliseconds delay)
    : max_attempts(attempts), base_delay(delay) {}

std::optional<std::chrono::milliseconds>
RetryPolicy::get_delay(int attempt_number) const {
    if (attempt_number <= 0 || attempt_number > max_attempts) {
        return std::nullopt;
    }
    return base_delay * (1 << (attempt_number - 1)); // Exponential backoff
}

} // namespace retry_policy
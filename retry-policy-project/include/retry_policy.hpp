#pragma once
#include <chrono>

struct RetryPolicy {
    int max_attempts = 3;
    std::chrono::milliseconds base_delay{100};

    static auto exponential(int attempts) -> RetryPolicy {
        RetryPolicy policy;
        policy.max_attempts = attempts;
        return policy;
    }
};
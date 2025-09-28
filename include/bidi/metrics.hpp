#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace bidi::metrics {

// Simple thread-safe counter
class Counter {
  public:
    Counter() noexcept : value_(0) {}
    void inc(std::uint64_t n = 1) noexcept {
        value_.fetch_add(n, std::memory_order_relaxed);
    }
    void dec(std::uint64_t n = 1) noexcept {
        value_.fetch_sub(n, std::memory_order_relaxed);
    }
    std::uint64_t value() const noexcept {
        return value_.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<std::uint64_t> value_;
};

// Lightweight histogram buckets (power-of-two-ish) for latency in microseconds
class Histogram {
  public:
    Histogram() = default;
    void observe(std::uint64_t usec) {
        std::lock_guard<std::mutex> lk(m_);
        auto idx = bucket_index(usec);
        if (idx >= buckets_.size()) {
            buckets_.resize(idx + 1);
        }
        ++buckets_[idx];
        ++count_;
        total_ += usec;
    }

    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t total() const noexcept { return total_; }

    // Return a snapshot of buckets (index -> count)
    std::vector<std::uint64_t> snapshot() const {
        std::lock_guard<std::mutex> lk(m_);
        return buckets_;
    }

  private:
    static std::size_t bucket_index(std::uint64_t usec) noexcept {
        // buckets: [0-1), [1-2), [2-4), [4-8), ...
        std::size_t idx = 0;
        std::uint64_t v = (usec > 0) ? usec : 0;
        while (v >>= 1) {
            ++idx;
        }
        return idx;
    }

    mutable std::mutex m_;
    std::vector<std::uint64_t> buckets_;
    std::uint64_t count_ = 0;
    std::uint64_t total_ = 0;
};

// Registry for named metrics (very small, safe for examples)
class Registry {
  public:
    Counter &counter(const std::string &name) {
        std::lock_guard<std::mutex> lk(m_);
        return counters_[name];
    }

    Histogram &histogram(const std::string &name) {
        std::lock_guard<std::mutex> lk(m_);
        return histograms_[name];
    }

  private:
    std::mutex m_;
    std::map<std::string, Counter> counters_;
    std::map<std::string, Histogram> histograms_;
};

} // namespace bidi::metrics

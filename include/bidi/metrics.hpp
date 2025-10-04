#pragma once
/**
 * @file metrics.hpp
 * @brief Lightweight metrics primitives used by pools and tests.
 *
 * The goal is a tiny, dependency-free metrics surface suitable for examples
 * and unit tests: counters, histograms (bucketed), and a centralized
 * Registry. PoolMetrics is the agreed-upon shape for all pool instrumentation
 * and is used by `PendingEntryPoolVec` and others to expose capacity, in_use
 * and cumulative counters.
 */

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace bidi::metrics {

// Unificação de métricas de pools (P0-3):
// Estrutura padronizada para todos os pools de recursos.
// Campos cumulativos onde aplicável; in_use é instantâneo.
struct PoolMetrics {
    std::size_t capacity{0}; // slots configurados ou capacidade lógica
    std::size_t in_use{0};   // slots atualmente emprestados/ocupados
    std::size_t acquired{0}; // total de aquisições (inclui reused + created)
    std::size_t reused{
        0}; // aquisições servidas a partir de objeto já inicializado
    std::size_t created{0};  // construções efetivas de objeto
    std::size_t fallback{0}; // vezes que recorreu a heap/out-of-pool
    std::size_t failures{
        0}; // falhas de construção ou outros erros transitórios
};

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
        std::size_t index = 0;
        std::uint64_t value_for_shift = usec;
        while (value_for_shift > 1) {
            value_for_shift >>= 1;
            ++index;
        }
        return index;
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

#pragma once

#include "bidi/core.hpp"
#include "bidi/metrics.hpp"
#include "utils/PoolVec.hpp"
#include <boost/json.hpp>

#include <chrono>
#include <memory>
#include <optional>

namespace bidi::core {

// PendingEntry variant for PoolVec-backed pool (stores promise in-place)
struct PendingEntryVec {
    id_type id{0};
    std::string method;
    boost::json::object params;
    std::chrono::steady_clock::time_point created;
    // promise constructed on demand; policy Recreate will emplace/reset it
    std::optional<std::promise<boost::json::object>> promise;

    void reset() {
        id = 0;
        method.clear();
        params.clear();
        created = std::chrono::steady_clock::time_point{};
        promise.reset();
    }
};

// RAII handle returned to callers. If using pool-backed entry, the PoolHandle
// will return slot on destruction; if fallback heap entry is used, unique_ptr
// owns it.
class PendingEntryHandleVec {
  public:
    PendingEntryHandleVec() noexcept = default;

    explicit PendingEntryHandleVec(
        utils::PoolHandle<PendingEntryVec> handle) noexcept
        : pool_handle_(std::move(handle)) {}

    explicit PendingEntryHandleVec(
        std::unique_ptr<PendingEntryVec> fallback) noexcept
        : fallback_(std::move(fallback)) {}

    PendingEntryHandleVec(PendingEntryHandleVec &&) noexcept = default;
    PendingEntryHandleVec &
    operator=(PendingEntryHandleVec &&) noexcept = default;

    ~PendingEntryHandleVec() noexcept = default;

    PendingEntryHandleVec(const PendingEntryHandleVec &) = delete;
    PendingEntryHandleVec &operator=(const PendingEntryHandleVec &) = delete;

    PendingEntryVec *operator->() noexcept {
        if (pool_handle_) {
            return pool_handle_.operator->();
        }
        if (fallback_) {
            return fallback_.get();
        }
        return nullptr;
    }

    PendingEntryVec &operator*() noexcept { return *operator->(); }

    explicit operator bool() const noexcept {
        return static_cast<bool>(pool_handle_) || static_cast<bool>(fallback_);
    }

    // Release ownership of fallback (if any). Returns nullptr if this is a
    // pooled handle.
    std::unique_ptr<PendingEntryVec> release_fallback() noexcept {
        return std::move(fallback_);
    }

  private:
    // PoolHandle has private members; access internals here (friend not
    // needed because PoolHandle<T> exposes ptr_ etc as public in current
    // implementation). We'll store the PoolHandle by value.
    utils::PoolHandle<PendingEntryVec> pool_handle_;
    std::unique_ptr<PendingEntryVec> fallback_;
};

// Pool adapter using utils::PoolVec
class PendingEntryPoolVec {
  public:
    static constexpr std::size_t kDefaultPendingPool = 64;
    explicit PendingEntryPoolVec(std::size_t initial_size = kDefaultPendingPool)
        : pool_(initial_size,
                utils::PoolVec<PendingEntryVec>::Policy::Recreate),
          capacity_(initial_size) {}

    ~PendingEntryPoolVec() = default;

    PendingEntryPoolVec(const PendingEntryPoolVec &) = delete;
    PendingEntryPoolVec &operator=(const PendingEntryPoolVec &) = delete;
    PendingEntryPoolVec(PendingEntryPoolVec &&) = delete;
    PendingEntryPoolVec &operator=(PendingEntryPoolVec &&) = delete;

    // Acquire: non-blocking attempt; if pool exhausted, return a heap-allocated
    // fallback to preserve functionality without blocking.
    [[nodiscard]] PendingEntryHandleVec acquire_nonblocking() noexcept {
        auto handle = pool_.borrow(std::chrono::milliseconds(0));
        if (handle) {
            // reset state (Pool policy Recreate already reconstructed object)
            handle->reset();
            handle->created = std::chrono::steady_clock::now();
            metrics_acquired_.fetch_add(1, std::memory_order_relaxed);
            // Recreate policy always constructed fresh => count as created
            metrics_created_.fetch_add(1, std::memory_order_relaxed);
            // reused: zero for Recreate (no objeto persistente)
            return PendingEntryHandleVec(std::move(handle));
        }
        // fallback heap path
        auto fallback = std::make_unique<PendingEntryVec>();
        fallback->created = std::chrono::steady_clock::now();
        metrics_acquired_.fetch_add(1, std::memory_order_relaxed);
        metrics_fallback_.fetch_add(1, std::memory_order_relaxed);
        return PendingEntryHandleVec(std::move(fallback));
    }

    // Blocking acquire with timeout
    [[nodiscard]] PendingEntryHandleVec
    acquire_for(std::chrono::milliseconds timeout) noexcept {
        auto handle = pool_.borrow(timeout);
        if (handle) {
            handle->reset();
            handle->created = std::chrono::steady_clock::now();
            metrics_acquired_.fetch_add(1, std::memory_order_relaxed);
            metrics_created_.fetch_add(1, std::memory_order_relaxed);
            return PendingEntryHandleVec(std::move(handle));
        }
        // timeout => failure semantic
        metrics_failures_.fetch_add(1, std::memory_order_relaxed);
        return PendingEntryHandleVec();
    }

    // Stats wrapper
    // Mantém struct antiga para compat (poderá ser removida após migração)
    struct Stats {
        std::size_t available{0};
        std::size_t acquired{0};
        std::size_t reused{0};
        std::size_t created{0};
        std::size_t returned{0};
        std::size_t fallback{0};
        std::size_t failures{0};
    };

    [[nodiscard]] Stats get_stats() const noexcept {
        const auto metrics_snapshot = get_metrics();
        Stats legacy{};
        legacy.available = metrics_snapshot.capacity - metrics_snapshot.in_use;
        legacy.acquired = metrics_snapshot.acquired;
        legacy.reused = metrics_snapshot.reused;
        legacy.created = metrics_snapshot.created;
        legacy.returned =
            metrics_snapshot.acquired > metrics_snapshot.in_use
                ? (metrics_snapshot.acquired - metrics_snapshot.in_use)
                : 0; // aproximação
        legacy.fallback = metrics_snapshot.fallback;
        legacy.failures = metrics_snapshot.failures;
        return legacy;
    }

    [[nodiscard]] bidi::metrics::PoolMetrics get_metrics() const noexcept {
        bidi::metrics::PoolMetrics pm{};
        pm.capacity = capacity_;
        pm.in_use = pool_.borrowed_count();
        pm.acquired = metrics_acquired_.load(std::memory_order_relaxed);
        pm.reused = metrics_reused_.load(std::memory_order_relaxed);
        pm.created = metrics_created_.load(std::memory_order_relaxed);
        pm.fallback = metrics_fallback_.load(std::memory_order_relaxed);
        pm.failures = metrics_failures_.load(std::memory_order_relaxed) +
                      pool_.failures();
        return pm;
    }

  private:
    utils::PoolVec<PendingEntryVec> pool_;
    std::size_t capacity_;
    // Contadores atômicos (cumulativos)
    std::atomic<std::size_t> metrics_acquired_{0};
    std::atomic<std::size_t> metrics_reused_{
        0}; // sempre 0 para Policy::Recreate (placeholder para futuro
            // Policy::Keep)
    std::atomic<std::size_t> metrics_created_{0};
    std::atomic<std::size_t> metrics_fallback_{0};
    std::atomic<std::size_t> metrics_failures_{0};
};

// Factory overload to match existing convenience
inline PendingEntryHandleVec make_pending_entry(PendingEntryPoolVec &pool) {
    return pool.acquire_nonblocking();
}

} // namespace bidi::core

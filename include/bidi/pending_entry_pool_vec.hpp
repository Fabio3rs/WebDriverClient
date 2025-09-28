#pragma once

#include "bidi/core.hpp"
#include <boost/json.hpp>
#include "utils/PoolVec.hpp"

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

    explicit PendingEntryHandleVec(utils::PoolHandle<PendingEntryVec> handle) noexcept
        : pool_handle_(std::move(handle)) {}

    explicit PendingEntryHandleVec(std::unique_ptr<PendingEntryVec> fallback) noexcept
        : fallback_(std::move(fallback)) {}

    PendingEntryHandleVec(PendingEntryHandleVec &&) noexcept = default;
    PendingEntryHandleVec &operator=(PendingEntryHandleVec &&) noexcept = default;

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

    explicit operator bool() const noexcept { return static_cast<bool>(pool_handle_) || static_cast<bool>(fallback_); }

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
        : pool_(initial_size, utils::PoolVec<PendingEntryVec>::Policy::Recreate) {}

    ~PendingEntryPoolVec() = default;

    PendingEntryPoolVec(const PendingEntryPoolVec &) = delete;
    PendingEntryPoolVec &operator=(const PendingEntryPoolVec &) = delete;
    PendingEntryPoolVec(PendingEntryPoolVec &&) = delete;
    PendingEntryPoolVec &operator=(PendingEntryPoolVec &&) = delete;

    // Acquire: non-blocking attempt; if pool exhausted, return a heap-allocated
    // fallback to preserve functionality without blocking.
    [[nodiscard]] PendingEntryHandleVec acquire_nonblocking() noexcept {
        auto ph = pool_.borrow(std::chrono::milliseconds(0));
        if (ph) {
            // ensure clean state
            ph->reset();
            ph->created = std::chrono::steady_clock::now();
            return PendingEntryHandleVec(std::move(ph));
        }

        // fallback: allocate on heap
        auto fallback = std::make_unique<PendingEntryVec>();
        fallback->created = std::chrono::steady_clock::now();
        return PendingEntryHandleVec(std::move(fallback));
    }

    // Blocking acquire with timeout
    [[nodiscard]] PendingEntryHandleVec acquire_for(std::chrono::milliseconds timeout) noexcept {
        auto ph = pool_.borrow(timeout);
        if (ph) {
            ph->reset();
            ph->created = std::chrono::steady_clock::now();
            return PendingEntryHandleVec(std::move(ph));
        }

        // if timed out, return empty handle
        return PendingEntryHandleVec();
    }

    // Stats wrapper
    struct Stats {
        std::size_t available{0};
        std::size_t acquired{0};
        std::size_t reused{0};
        std::size_t created{0};
        std::size_t returned{0};
    };

    [[nodiscard]] Stats get_stats() const noexcept {
        Stats stats{};
        // Provide conservative metrics: capacity as available to avoid
        // accidental negative values; detailed counters not tracked yet.
        stats.available = pool_.capacity();
        return stats;
    }

  private:
    utils::PoolVec<PendingEntryVec> pool_;
};

// Factory overload to match existing convenience
inline PendingEntryHandleVec make_pending_entry(PendingEntryPoolVec &pool) {
    return pool.acquire_nonblocking();
}

} // namespace bidi::core

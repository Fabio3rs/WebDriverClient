#pragma once

// Aggregated header that exposes the various resource pools used across the
// BiDi implementation. The goal is to provide a single include for pools and
// a small facade to access them for instrumentation and testing.

#include "bidi/buffer_pool.hpp"
#include "bidi/promise_pool.hpp"
#include "bidi/pending_entry_pool_vec.hpp"

#include <memory>

namespace bidi::core {

// Lightweight facade that holds shared pool instances for tests or demos.
// Not intended to force global state in production; used as a convenience
// for instrumentation and examples.
struct ResourcePools {
        static constexpr std::size_t kDefaultPromisePool = 64;
        static constexpr std::size_t kDefaultPendingPool = 128;
        static constexpr std::size_t kDefaultBufferPool = 16;

        ResourcePools()
                : promise_pool(std::make_unique<PromisePool<boost::json::object>>(kDefaultPromisePool)),
                    pending_pool(std::make_unique<PendingEntryPoolVec>(kDefaultPendingPool)),
                    buffer_pool(std::make_unique<BufferPool>(kDefaultBufferPool)) {}

        ~ResourcePools() = default;

    // Non-copyable
    ResourcePools(const ResourcePools &) = delete;
    ResourcePools &operator=(const ResourcePools &) = delete;

    // Moveable
    ResourcePools(ResourcePools &&) = default;
    ResourcePools &operator=(ResourcePools &&) = default;

    PromisePool<boost::json::object> &get_promise_pool() {
        return *promise_pool;
    }
    PendingEntryPoolVec &get_pending_pool() { return *pending_pool; }
    BufferPool &get_buffer_pool() { return *buffer_pool; }

    // Stats snapshot combining pool stats into a simple struct
    struct Stats {
        PromisePool<boost::json::object>::Stats promise_stats{};
    // PendingEntryPoolVec has a simpler Stats; keep a placeholder struct
    PendingEntryPoolVec::Stats pending_stats{};
        BufferPool::Stats buffer_stats{};
    };

    Stats snapshot() const {
        Stats snapshot{};
        snapshot.promise_stats = promise_pool->get_stats();
        snapshot.pending_stats = pending_pool->get_stats();
        snapshot.buffer_stats = buffer_pool->get_stats();
        return snapshot;
    }

  private:
    std::unique_ptr<PromisePool<boost::json::object>> promise_pool;
    std::unique_ptr<PendingEntryPoolVec> pending_pool;
    std::unique_ptr<BufferPool> buffer_pool;
};

// Convenience accessor for tests/demos
inline ResourcePools &get_default_resource_pools() {
    static ResourcePools pools;
    return pools;
}

} // namespace bidi::core

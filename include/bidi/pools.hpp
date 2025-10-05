#pragma once
/**
 * @file pools.hpp
 * @brief Convenience facade exposing shared resource pools for examples/tests.
 *
 * Notes:
 * - `ResourcePools` is intended for examples and unit tests where a single
 *  , easily-accessible set of pools simplifies wiring and assertions. It is
 *   not meant to mandate global state for production use; prefer injecting
 *   pool instances where possible.
 */

// Aggregated header that exposes the various resource pools used across the
// BiDi implementation. The goal is to provide a single include for pools and
// a small facade to access them for instrumentation and testing.

#include "bidi/buffer_pool_vec.hpp"
#include "bidi/pending_entry_pool_vec.hpp"

#include <memory>

namespace bidi::core {

// Lightweight facade that holds shared pool instances for tests or demos.
// Not intended to force global state in production; used as a convenience
// for instrumentation and examples.
struct ResourcePools {
    static constexpr std::size_t kDefaultPendingPool = 128;
    static constexpr std::size_t kDefaultBufferPool = 16;

    ResourcePools()
        : pending_pool(
              std::make_unique<PendingEntryPoolVec>(kDefaultPendingPool)),
          buffer_pool(std::make_unique<BufferPoolVec>(kDefaultBufferPool)) {}

    ~ResourcePools() = default;

    // Non-copyable
    ResourcePools(const ResourcePools &) = delete;
    auto operator=(const ResourcePools &) -> ResourcePools & = delete;

    // Moveable
    ResourcePools(ResourcePools &&) = default;
    auto operator=(ResourcePools &&) -> ResourcePools & = default;

    auto get_pending_pool() -> PendingEntryPoolVec & { return *pending_pool; }
    auto get_buffer_pool() -> BufferPoolVec & { return *buffer_pool; }

    // Stats snapshot combining pool stats into a simple struct
    struct Stats {
        PendingEntryPoolVec::Stats pending_stats{};
        BufferPool::Stats buffer_stats{}; // manter nome para compatibilidade
    };

    [[nodiscard]] auto snapshot() const -> Stats {
        Stats snapshot{};
        snapshot.pending_stats = pending_pool->get_stats();
        snapshot.buffer_stats = buffer_pool->get_stats();
        return snapshot;
    }

  private:
    std::unique_ptr<PendingEntryPoolVec> pending_pool;
    std::unique_ptr<BufferPoolVec> buffer_pool;
};

// Convenience accessor for tests/demos
inline auto get_default_resource_pools() -> ResourcePools & {
    static ResourcePools pools;
    return pools;
}

} // namespace bidi::core

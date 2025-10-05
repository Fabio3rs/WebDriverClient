#pragma once
/**
 * @file buffer_pool_vec.hpp
 * @brief PoolVec-backed buffer pool for zero-copy WebSocket operations.
 *
 * Architectural rationale:
 * - Zero-copy operations: Buffers are reused across multiple WebSocket
 *   write operations, eliminating per-message allocations.
 * - Size class optimization: Four size classes (Small/Medium/Large/XLarge)
 *   minimize wasted memory while covering typical BiDi message sizes.
 * - Cache locality: PoolVec stores buffer slots contiguously, improving
 *   cache hit rates during high-throughput operations.
 * - RAII semantics: BufferHandle automatically returns buffer to pool on
 *   destruction, preventing leaks and simplifying error handling.
 * - Non-blocking fallback: When pool exhausted, falls back to heap allocation
 *   to preserve functionality without blocking (metrics expose fallback count).
 *
 * Performance characteristics:
 * - Eliminates allocation overhead for steady-state operations
 * - Reduces memory fragmentation from repeated alloc/free cycles
 * - Metrics tracking enables pool sizing validation
 *
 * Integration with architecture:
 * - Used by WebSocket write queue for serialized message sends
 * - Supports Beast's buffer sequence interface for zero-copy async_write
 * - Metrics tracked via BufferPool::Stats for monitoring
 */

#include "../utils/PoolVec.hpp"
#include "buffer_pool.hpp"

#include <functional>
#include <memory>
#include <string_view>

namespace bidi::core {

// Internal slot type stored in PoolVec. Contains the actual PooledBuffer.
class PooledBufferSlot {
  public:
    explicit PooledBufferSlot(std::size_t capacity) : buf_{capacity} {}

    auto get() noexcept -> PooledBuffer & { return buf_; }

    void reset() noexcept { buf_.reset(); }

  private:
    PooledBuffer buf_;
};

class BufferPoolVec {
  public:
    explicit BufferPoolVec(std::size_t initial_count_per_size = 16)
        : small_pool_{initial_count_per_size,
                      utils::PoolVec<PooledBufferSlot>::Policy::Recreate},
          medium_pool_{initial_count_per_size,
                       utils::PoolVec<PooledBufferSlot>::Policy::Recreate},
          large_pool_{initial_count_per_size,
                      utils::PoolVec<PooledBufferSlot>::Policy::Recreate},
          xlarge_pool_{initial_count_per_size,
                       utils::PoolVec<PooledBufferSlot>::Policy::Recreate} {}

    ~BufferPoolVec() = default;

    // Acquire buffer of specific size. Preserve API by returning BufferHandle
    // which wraps a shared_ptr with custom deleter that returns slot to pool.
    [[nodiscard]] auto acquire_buffer(BufferSize size) -> BufferHandle {
        auto &pool = pool_for(size);
        auto handle =
            pool.borrow(std::chrono::milliseconds(0), capacity_for(size));
        if (handle) {
            // Create aliasing shared_ptr that points to the inner PooledBuffer
            // but holds an owner object that will return the slot on
            // destruction.
            struct Owner {
                utils::PoolHandle<PooledBufferSlot> slot_handle;
                BufferPoolVec *parent{nullptr};
            };

            // Allocate owner on heap and create shared_ptr managing it
            auto owner = std::make_shared<Owner>();
            owner->slot_handle = std::move(handle);
            owner->parent = this;

            // Create shared_ptr<PooledBuffer> that points to the slot's
            // PooledBuffer
            std::shared_ptr<PooledBuffer> buffer_shared(
                owner, // control block
                &owner->slot_handle->get());

            // Custom return_fn: when last shared_ptr goes away, owner is freed
            // and slot_handle destructor will return slot via PoolHandle's
            // reset().
            auto return_fn = [](const std::shared_ptr<PooledBuffer> & /*buf*/) {
                // nothing to do: owner destructor will release slot
            };

            return BufferHandle{std::move(buffer_shared), std::move(return_fn)};
        }

        // fallback: create heap buffer and return with noop return_fn
        auto heap_buf = std::make_shared<PooledBuffer>(capacity_for(size));
        auto return_fn = [](const std::shared_ptr<PooledBuffer> & /*buf*/) {};
        return BufferHandle{std::move(heap_buf), std::move(return_fn)};
    }

    [[nodiscard]] auto get_stats() const -> BufferPool::Stats {
        BufferPool::Stats s{};
        s.small_available =
            small_pool_.capacity() - small_pool_.borrowed_count();
        s.medium_available =
            medium_pool_.capacity() - medium_pool_.borrowed_count();
        s.large_available =
            large_pool_.capacity() - large_pool_.borrowed_count();
        s.xlarge_available =
            xlarge_pool_.capacity() - xlarge_pool_.borrowed_count();
        return s;
    }

  private:
    auto pool_for(BufferSize size) -> utils::PoolVec<PooledBufferSlot> & {
        switch (size) {
        case BufferSize::Small:
            return small_pool_;
        case BufferSize::Medium:
            return medium_pool_;
        case BufferSize::Large:
            return large_pool_;
        case BufferSize::XLarge:
            return xlarge_pool_;
        default:
            return medium_pool_;
        }
    }

    static auto capacity_for(BufferSize size) -> std::size_t {
        return static_cast<std::size_t>(size);
    }

    utils::PoolVec<PooledBufferSlot> small_pool_;
    utils::PoolVec<PooledBufferSlot> medium_pool_;
    utils::PoolVec<PooledBufferSlot> large_pool_;
    utils::PoolVec<PooledBufferSlot> xlarge_pool_;
};

} // namespace bidi::core

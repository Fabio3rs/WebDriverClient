// include/bidi/buffer_pool_vec.hpp — PoolVec-backed BufferPool POC
#pragma once

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

    PooledBuffer &get() noexcept { return buf_; }

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
    [[nodiscard]] BufferHandle acquire_buffer(BufferSize size) {
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
            auto return_fn = [this](std::shared_ptr<PooledBuffer> /*buf*/) {
                // nothing to do: owner destructor will release slot
            };

            return BufferHandle{std::move(buffer_shared), std::move(return_fn)};
        }

        // fallback: create heap buffer and return with noop return_fn
        auto heap_buf = std::make_shared<PooledBuffer>(capacity_for(size));
        auto return_fn = [](std::shared_ptr<PooledBuffer> /*buf*/) {};
        return BufferHandle{std::move(heap_buf), std::move(return_fn)};
    }

    [[nodiscard]] BufferPool::Stats get_stats() const {
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
    utils::PoolVec<PooledBufferSlot> &pool_for(BufferSize size) {
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

    static std::size_t capacity_for(BufferSize size) {
        return static_cast<std::size_t>(size);
    }

    utils::PoolVec<PooledBufferSlot> small_pool_;
    utils::PoolVec<PooledBufferSlot> medium_pool_;
    utils::PoolVec<PooledBufferSlot> large_pool_;
    utils::PoolVec<PooledBufferSlot> xlarge_pool_;
};

} // namespace bidi::core

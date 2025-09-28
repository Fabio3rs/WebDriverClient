// src/bidi_buffer_pool.cpp — Buffer pool implementation
#include "bidi/buffer_pool.hpp"
#include <algorithm>

namespace bidi::core {

PooledBuffer::PooledBuffer(std::size_t capacity) : data_(capacity) {}

void PooledBuffer::write_data(std::string_view data) {
    write_data(data.data(), data.size());
}

void PooledBuffer::write_data(const char *data, std::size_t length) {
    if (used_size_ + length > data_.size()) {
        // Could resize, but for now just truncate to avoid unexpected
        // allocations
        length = data_.size() - used_size_;
    }

    if (length > 0) {
        const auto *first = data;
        const auto *last = data + length; // pointer arithmetic in-bounds
        auto out = data_.begin();
        std::advance(out, static_cast<std::ptrdiff_t>(used_size_));
        std::copy(first, last, out);
        used_size_ += length; // length fits (checked above)
    }
}

BufferPool::BufferPool(std::size_t initial_count_per_size) {
    // Preallocate initial buffers for each size class
    preallocate(BufferSize::Small, initial_count_per_size);
    preallocate(BufferSize::Medium, initial_count_per_size);
    preallocate(BufferSize::Large,
                initial_count_per_size / 2); // Fewer large buffers
    preallocate(BufferSize::XLarge,
                initial_count_per_size / 4); // Even fewer XL buffers
}

auto BufferPool::acquire_buffer(BufferSize size) -> BufferHandle {
    std::shared_ptr<PooledBuffer> buffer;

    switch (size) {
    case BufferSize::Small: {
        std::lock_guard<std::mutex> lock(small_mutex_);
        if (!small_buffers_.empty()) {
            buffer = std::move(small_buffers_.back());
            small_buffers_.pop_back();
        }
        break;
    }
    case BufferSize::Medium: {
        std::lock_guard<std::mutex> lock(medium_mutex_);
        if (!medium_buffers_.empty()) {
            buffer = std::move(medium_buffers_.back());
            medium_buffers_.pop_back();
        }
        break;
    }
    case BufferSize::Large: {
        std::lock_guard<std::mutex> lock(large_mutex_);
        if (!large_buffers_.empty()) {
            buffer = std::move(large_buffers_.back());
            large_buffers_.pop_back();
        }
        break;
    }
    case BufferSize::XLarge: {
        std::lock_guard<std::mutex> lock(xlarge_mutex_);
        if (!xlarge_buffers_.empty()) {
            buffer = std::move(xlarge_buffers_.back());
            xlarge_buffers_.pop_back();
        }
        break;
    }
    }

    // Create new buffer if pool was empty
    if (!buffer) {
        buffer = create_buffer(size);
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.total_created;
    }

    // Reset buffer for reuse
    buffer->reset();

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.total_acquired;
    }

    // Return handle with automatic return function
    return BufferHandle{buffer,
                        [this, size](std::shared_ptr<PooledBuffer> buf) {
                            return_buffer(std::move(buf), size);
                        }};
}

void BufferPool::return_buffer(std::shared_ptr<PooledBuffer> buffer,
                               BufferSize size) {
    if (!buffer) {
        return;
    }

    // Return to appropriate pool
    switch (size) {
    case BufferSize::Small: {
        std::lock_guard<std::mutex> lock(small_mutex_);
        small_buffers_.push_back(std::move(buffer));
        break;
    }
    case BufferSize::Medium: {
        std::lock_guard<std::mutex> lock(medium_mutex_);
        medium_buffers_.push_back(std::move(buffer));
        break;
    }
    case BufferSize::Large: {
        std::lock_guard<std::mutex> lock(large_mutex_);
        large_buffers_.push_back(std::move(buffer));
        break;
    }
    case BufferSize::XLarge: {
        std::lock_guard<std::mutex> lock(xlarge_mutex_);
        xlarge_buffers_.push_back(std::move(buffer));
        break;
    }
    }

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.total_returned;
    }
}

auto BufferPool::create_buffer(BufferSize size)
    -> std::shared_ptr<PooledBuffer> {
    return std::make_shared<PooledBuffer>(static_cast<std::size_t>(size));
}

void BufferPool::preallocate(BufferSize size, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        auto buffer = create_buffer(size);
        return_buffer(std::move(buffer), size);
    }
}

auto BufferPool::get_stats() const -> BufferPool::Stats {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats current_stats = stats_;

    // Update available counts
    {
        std::lock_guard<std::mutex> small_lock(small_mutex_);
        current_stats.small_available = small_buffers_.size();
    }
    {
        std::lock_guard<std::mutex> medium_lock(medium_mutex_);
        current_stats.medium_available = medium_buffers_.size();
    }
    {
        std::lock_guard<std::mutex> large_lock(large_mutex_);
        current_stats.large_available = large_buffers_.size();
    }
    {
        std::lock_guard<std::mutex> xlarge_lock(xlarge_mutex_);
        current_stats.xlarge_available = xlarge_buffers_.size();
    }

    return current_stats;
}

StreamingBuffer::StreamingBuffer(BufferPool &pool, std::size_t chunk_size)
    : pool_{pool}, chunk_size_{chunk_size} {}

void StreamingBuffer::write_chunk(std::string_view data) {
    while (!data.empty()) {
        // Get or create current chunk
        if (chunks_.empty() || !chunks_.back()->can_fit(data.size())) {
            // Determine appropriate buffer size based on remaining data
            BufferSize size = BufferSize::Medium;
            if (data.size() <= static_cast<std::size_t>(BufferSize::Small)) {
                size = BufferSize::Small;
            } else if (data.size() >
                       static_cast<std::size_t>(BufferSize::Medium)) {
                size = BufferSize::Large;
            }

            chunks_.push_back(pool_.acquire_buffer(size));
        }

        // Write as much as possible to current chunk
        auto &current_chunk = chunks_.back();
        std::size_t available =
            current_chunk->capacity() - current_chunk->size();
        std::size_t to_write = std::min(data.size(), available);

        current_chunk->write_data(data.substr(0, to_write));
        total_size_ += to_write;
        data = data.substr(to_write);
    }
}

auto StreamingBuffer::get_asio_buffers() const
    -> std::vector<boost::asio::const_buffer> {
    std::vector<boost::asio::const_buffer> buffers;
    buffers.reserve(chunks_.size());

    for (const auto &chunk : chunks_) {
        if (!chunk->empty()) {
            buffers.push_back(chunk->as_asio_buffer());
        }
    }

    return buffers;
}

auto StreamingBuffer::total_size() const noexcept -> std::size_t {
    return total_size_;
}

void StreamingBuffer::reset() {
    chunks_.clear();
    total_size_ = 0;
}

} // namespace bidi::core

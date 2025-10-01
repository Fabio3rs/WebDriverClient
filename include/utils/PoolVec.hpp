#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace utils {

// PoolVec: POC de um pool de objetos com slots contíguos e RAII handle.
// - Prealoca N slots de armazenamento bruto.
// - Cada slot tem um estado atômico (0 = free, 1 = used).
// - borrow() faz CAS para marcar slot usado; retorna Handle que faz
// destroy+free no dtor.
// - Constrói o objeto no slot via placement-new quando emprestado.

template <class T> class PoolVec;

template <class T> class PoolHandle {
  public:
    PoolHandle() noexcept = default;
    PoolHandle(PoolHandle &&other) noexcept { *this = std::move(other); }
    ~PoolHandle() noexcept { reset(); }

    PoolHandle &operator=(PoolHandle &&other) noexcept {
        if (this != std::addressof(other)) {
            reset();
            pool_ = other.pool_;
            index_ = other.index_;
            ptr_ = other.ptr_;
            other.pool_ = nullptr;
            other.index_ = npos;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    PoolHandle(const PoolHandle &) = delete;
    PoolHandle &operator=(const PoolHandle &) = delete;

    T *operator->() noexcept { return ptr_; }
    const T *operator->() const noexcept { return ptr_; }
    T &operator*() noexcept { return *ptr_; }
    const T &operator*() const noexcept { return *ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void reset() noexcept {
        if (pool_ && index_ != npos) {
            pool_->destroy_and_free(index_);
        }
        pool_ = nullptr;
        index_ = npos;
        ptr_ = nullptr;
    }

  private:
    friend class PoolVec<T>;
    PoolVec<T> *pool_{nullptr};
    std::size_t index_{npos};
    T *ptr_{nullptr};
    constexpr static std::size_t npos = static_cast<std::size_t>(-1);
};

template <class T> class PoolVec {
  public:
    enum class Policy : std::uint8_t { Recreate = 0, Keep = 1 };

    explicit PoolVec(std::size_t capacity, Policy policy = Policy::Recreate)
        : capacity_(capacity), storage_(capacity), states_(capacity),
          initialized_(capacity), first_free_(npos), borrowed_count_(0),
          policy_(policy) {
        for (std::size_t i = 0; i < capacity_; ++i) {
            states_[i].store(0);
            storage_[i].reset();
            initialized_[i].store(0);
        }
    }

    PoolVec(const PoolVec &) = delete;
    PoolVec &operator=(const PoolVec &) = delete;
    PoolVec(PoolVec &&) = delete;
    PoolVec &operator=(PoolVec &&) = delete;

    ~PoolVec() noexcept { /* não destrói objetos que ainda possam estar em uso
                           */
    }

    // Tenta pegar um objeto; se sucesso constrói T(in-place) e retorna handle.
    // timeout = 0 => tentativa única (não-blocking)
    template <class... Args>
    PoolHandle<T> borrow(std::chrono::milliseconds timeout,
                         Args &&...args) noexcept {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        auto idx = get_valid_index(std::forward<Args>(args)...);
        while (idx == npos) {
            if (timeout.count() == 0) {
                return PoolHandle<T>();
            }
            std::unique_lock<std::mutex> lk(wait_mtx_);
            if (wait_cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
                return PoolHandle<T>();
            }
            idx = get_valid_index(std::forward<Args>(args)...);
        }

        // garantimos que o objeto está construído conforme política
        construct_if_needed(idx, std::forward<Args>(args)...);

        PoolHandle<T> handle;
        handle.pool_ = this;
        handle.index_ = idx;
        handle.ptr_ = std::addressof(*storage_[idx]);
        return handle;
    }

    // destroy_and_free é chamado pelo Handle no reset/destrutor.
    void destroy_and_free(std::size_t idx) noexcept {
        if (idx >= capacity_) {
            return;
        }

        if (policy_ == Policy::Recreate) {
            storage_[idx].reset();
            initialized_[idx].store(0);
        }

        states_[idx].store(0);
        // atualiza hint e contador
        first_free_.store(idx);
        borrowed_count_.fetch_sub(1, std::memory_order_relaxed);
        wait_cv_.notify_one();
    }

    std::size_t capacity() const noexcept { return capacity_; }

    // Number of currently borrowed slots (useful for pool monitoring)
    std::size_t borrowed_count() const noexcept {
        return borrowed_count_.load(std::memory_order_relaxed);
    }

  private:
    // Data arranged for locality: hot per-slot vectors first, then counters and
    // policy, then synchronization primitives. Order must match ctor init
    // order to avoid -Wreorder warnings.
    std::size_t capacity_{0};
    std::vector<std::optional<T>> storage_;
    // Use compact atomics per-slot to reduce memory/cache pressure.
    // states_: 0 free, 1 used
    std::vector<std::atomic<unsigned char>> states_;
    // initialized_ flags: 0 not-initialized, 1 initialized
    std::vector<std::atomic<unsigned char>> initialized_;

    // Hints / counters and policy (frequently accessed)
    std::atomic<std::size_t> first_free_;
    // borrowed_count_ uses relaxed ordering for increments/decrements
    std::atomic<std::size_t> borrowed_count_;
    // rotating probe index to avoid starting scans at 0 on contention
    std::atomic<std::size_t> next_probe_{0};
    Policy policy_;

    constexpr static std::size_t npos = static_cast<std::size_t>(-1);

    // Synchronization primitives at the end
    std::mutex wait_mtx_;
    std::condition_variable wait_cv_;

    // Tenta reclamar o slot indicado pelo hint (se houver). Retorna index ou
    // npos
    std::size_t try_claim_hint() noexcept {
        std::size_t hint = first_free_.exchange(npos);
        if (hint == npos || hint >= capacity_) {
            return npos;
        }
        unsigned char expected = 0;
        // weak CAS ok for brief spinning; use acquire-release semantics
        if (!states_[hint].compare_exchange_weak(expected, 1,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed)) {
            return npos;
        }
        // reservamos o slot; a construção será feita separadamente
        borrowed_count_.fetch_add(1, std::memory_order_relaxed);
        return hint;
    }

    std::size_t try_claim_scan() noexcept {
        // Start probing from a rotating index to reduce contention on low
        // indices. Try up to capacity_ slots.
        std::size_t start =
            next_probe_.fetch_add(1, std::memory_order_relaxed) % capacity_;
        for (std::size_t offset = 0; offset < capacity_; ++offset) {
            std::size_t i = (start + offset);
            if (i >= capacity_) {
                i -= capacity_ * (i / capacity_);
            }
            unsigned char expected = 0;
            if (states_[i].compare_exchange_weak(expected, 1,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed)) {
                borrowed_count_.fetch_add(1, std::memory_order_relaxed);
                return i;
            }
        }
        return npos;
    }

    template <class... Args>
    std::size_t get_valid_index(Args &&.../*ctor_args*/) noexcept {
        // tenta hint primeiro
        auto idx = try_claim_hint();
        if (idx != npos) {
            return idx;
        }
        // fallback scan
        return try_claim_scan();
    }

    template <class... Args>
    void construct_if_needed(std::size_t idx, Args &&...args) noexcept {
        if (policy_ == Policy::Recreate) {
            storage_[idx].emplace(std::forward<Args>(args)...);
            initialized_[idx].store(1, std::memory_order_release);
            return;
        }
        unsigned char inited =
            initialized_[idx].load(std::memory_order_acquire);
        if (!inited) {
            storage_[idx].emplace(std::forward<Args>(args)...);
            initialized_[idx].store(1, std::memory_order_release);
        }
    }
};

} // namespace utils

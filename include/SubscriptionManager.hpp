#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace bidi::ws {

// Simple RAII subscription handle. When destroyed, invokes the unsubscribe
// callback.
class SubscriptionHandle {
  public:
    SubscriptionHandle() = default;
    SubscriptionHandle(std::function<void()> unsub)
        : unsub_(std::move(unsub)) {}
    SubscriptionHandle(SubscriptionHandle &&o) noexcept
        : unsub_(std::move(o.unsub_)) {
        o.unsub_ = nullptr;
    }
    SubscriptionHandle &operator=(SubscriptionHandle &&o) noexcept {
        unsub_ = std::move(o.unsub_);
        o.unsub_ = nullptr;
        return *this;
    }
    ~SubscriptionHandle() {
        if (unsub_)
            unsub_();
    }

    void cancel() {
        if (unsub_) {
            unsub_();
            unsub_ = nullptr;
        }
    }

    bool valid() const { return static_cast<bool>(unsub_); }

  private:
    std::function<void()> unsub_{nullptr};
};

// SubscriptionManager: hold callbacks mapped by string topic -> id
class SubscriptionManager {
  public:
    using topic_t = std::string;
    using callback_t = std::function<void(std::string_view)>;

    SubscriptionManager() = default;

    // Subscribe: returns a handle that will remove the subscription on
    // destruction
    SubscriptionHandle subscribe(topic_t topic, callback_t cb) {
        std::lock_guard lock(m_);
        auto id = next_id_++;
        callbacks_.emplace(id, std::make_pair(std::move(topic), std::move(cb)));
        // create unsubscribe function
        auto unsub = [this, id]() {
            std::lock_guard lock2(m_);
            callbacks_.erase(id);
        };
        return SubscriptionHandle(unsub);
    }

    // dispatch a payload to subscribers that match topic (exact match for now)
    void dispatch(std::string_view topic, std::string_view payload) {
        std::vector<callback_t> list;
        {
            std::lock_guard lock(m_);
            for (auto &kv : callbacks_) {
                if (kv.second.first == topic)
                    list.push_back(kv.second.second);
            }
        }
        for (auto &f : list)
            f(payload);
    }

  private:
    std::mutex m_;
    std::map<std::uint64_t, std::pair<topic_t, callback_t>> callbacks_;
    std::uint64_t next_id_{1};
};

} // namespace bidi::ws

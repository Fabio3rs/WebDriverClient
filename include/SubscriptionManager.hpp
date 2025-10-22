#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include <boost/assert.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bidi::ws {

namespace detail {
struct subscription_entry {
    std::string topic;
    std::function<void(std::string_view)> callback;
};

struct subscription_state {
    explicit subscription_state(boost::asio::any_io_executor executor)
        : strand(boost::asio::make_strand(std::move(executor))) {}

    boost::asio::strand<boost::asio::any_io_executor> strand;
    std::unordered_map<std::uint64_t, subscription_entry> callbacks;
    std::uint64_t next_id{1};
};
} // namespace detail

class SubscriptionHandle {
  public:
    SubscriptionHandle() = default;
    SubscriptionHandle(std::shared_ptr<detail::subscription_state> state,
                       std::uint64_t id) noexcept;
    SubscriptionHandle(SubscriptionHandle &&) noexcept = default;
    auto
    operator=(SubscriptionHandle &&) noexcept -> SubscriptionHandle & = default;
    ~SubscriptionHandle();

    void cancel() noexcept;

    [[nodiscard]] auto valid() const noexcept -> bool;

  private:
    std::weak_ptr<detail::subscription_state> state_;
    std::uint64_t id_{0};
};

// SubscriptionManager: holds callbacks mapped by string topic -> id and runs
// them through a strand-backed executor to align with the project's threading
// model.
class SubscriptionManager {
  public:
    using topic_t = std::string;
    using callback_t = std::function<void(std::string_view)>;

    explicit SubscriptionManager(boost::asio::any_io_executor executor);
    explicit SubscriptionManager(boost::asio::io_context &context)
        : SubscriptionManager(context.get_executor()) {}

    [[nodiscard]] auto
    executor() const -> boost::asio::strand<boost::asio::any_io_executor>;

    [[nodiscard]] auto subscribe(topic_t topic,
                                 callback_t cb) -> SubscriptionHandle;
    void dispatch(std::string_view topic, std::string_view payload);

  private:
    std::shared_ptr<detail::subscription_state> state_;
};

inline SubscriptionHandle::SubscriptionHandle(
    std::shared_ptr<detail::subscription_state> state,
    std::uint64_t id) noexcept
    : state_(std::move(state)), id_(id) {}

inline SubscriptionHandle::~SubscriptionHandle() { cancel(); }

inline void SubscriptionHandle::cancel() noexcept {
    if (id_ == 0) {
        return;
    }
    if (auto state = state_.lock()) {
        auto strand = state->strand;
        auto id = id_;
        boost::asio::dispatch(strand, [state = std::move(state), id]() {
            state->callbacks.erase(id);
        });
    }
    state_.reset();
    id_ = 0;
}

inline auto SubscriptionHandle::valid() const noexcept -> bool {
    return id_ != 0 && !state_.expired();
}

inline SubscriptionManager::SubscriptionManager(
    boost::asio::any_io_executor executor)
    : state_(
          std::make_shared<detail::subscription_state>(std::move(executor))) {}

inline auto SubscriptionManager::executor() const
    -> boost::asio::strand<boost::asio::any_io_executor> {
    BOOST_ASSERT(state_);
    return state_->strand;
}

inline auto SubscriptionManager::subscribe(topic_t topic, callback_t cb)
    -> SubscriptionHandle {
    BOOST_ASSERT(state_);
    BOOST_ASSERT(state_->strand.running_in_this_thread() &&
                 "SubscriptionManager::subscribe must run on its strand");
    auto &state = *state_;
    const auto id = state.next_id++;
    state.callbacks.emplace(
        id, detail::subscription_entry{std::move(topic), std::move(cb)});
    return SubscriptionHandle{state_, id};
}

inline void SubscriptionManager::dispatch(std::string_view topic,
                                          std::string_view payload) {
    BOOST_ASSERT(state_);
    auto state = state_;
    auto topic_copy = std::string(topic);
    auto payload_copy = std::string(payload);
    boost::asio::dispatch(
        state->strand, [state = std::move(state), topic = std::move(topic_copy),
                        payload = std::move(payload_copy)]() mutable {
            std::vector<std::uint64_t> matching_ids;
            matching_ids.reserve(state->callbacks.size());
            for (const auto &[id, entry] : state->callbacks) {
                if (entry.topic == topic) [[likely]] {
                    matching_ids.push_back(id);
                }
            }

            std::string_view payload_view{payload};
            for (auto id : matching_ids) {
                auto it = state->callbacks.find(id);
                if (it != state->callbacks.end()) [[likely]] {
                    it->second.callback(payload_view);
                }
            }
        });
}

} // namespace bidi::ws

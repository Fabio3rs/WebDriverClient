#pragma once
#include "bidi/client.hpp"
#include <memory>

namespace bidi {

class ContextGuard {
  public:
    ContextGuard(std::shared_ptr<Client> client, std::string context_id)
        : client_{std::move(client)}, context_id_{std::move(context_id)} {}

    ~ContextGuard() {
        try {
            if (!context_id_.empty() && client_) {
                // Close context using new API (fire-and-forget)
                auto close_task = client_->close_context(context_id_);
                // Task destructor will handle cleanup
            }
        } catch (...) {
            // Ignore cleanup errors in destructor
        }
    }

    // Non-copyable, movable
    ContextGuard(const ContextGuard &) = delete;
    ContextGuard &operator=(const ContextGuard &) = delete;

    ContextGuard(ContextGuard &&other) noexcept
        : client_{std::move(other.client_)},
          context_id_{std::move(other.context_id_)} {
        other.context_id_.clear();
    }

    ContextGuard &operator=(ContextGuard &&other) noexcept {
        if (this != &other) {
            client_ = std::move(other.client_);
            context_id_ = std::move(other.context_id_);
            other.context_id_.clear();
        }
        return *this;
    }

    const std::string &id() const { return context_id_; }

  private:
    std::shared_ptr<Client> client_;
    std::string context_id_;
};

} // namespace bidi

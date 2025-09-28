#pragma once
#include <memory>
#include "bidi.hpp"

namespace bidi {

class ContextGuard {
  public:
    ContextGuard(std::shared_ptr<BidiClient> cli, std::string ctx)
    : cli_{std::move(cli)}, ctx_{std::move(ctx)} { }

    ~ContextGuard() {
      try {
        if (!ctx_.empty()) {
          // Envia fechamento "fire-and-forget" (padrão seguro; pode-se trocar por Async<void>).
          cli_->async_send("browsingContext.close",
                           { {"context", ctx_} });
        }
      } catch (...) { }
    }

    auto id() const -> const std::string& { return ctx_; }

  private:
    std::shared_ptr<BidiClient> cli_;
    std::string ctx_;
};

} // namespace bidi

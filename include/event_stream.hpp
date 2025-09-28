// event_stream.hpp — versão com Shared-State (copiável/movível)
#pragma once
#include <boost/asio.hpp>
#include <atomic>
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace bidi {

template <class E>
class EventStream {
  public:
    using Handler = std::function<void(const E&)>;

    struct Subscription {
        std::function<void()> cancel;
    };

    struct StopToken {
        std::atomic<bool> stop{false};
        void request_stop() { stop.store(true, std::memory_order_relaxed); }
    };

  private:
    struct State {
        explicit State(boost::asio::any_io_executor ex_) : ex(std::move(ex_)) {}
        boost::asio::any_io_executor ex;
        std::mutex mx;
        std::vector<std::pair<std::uint64_t, Handler>> subs;
        std::vector<Subscription> holds; // para manter encadeamentos vivos
        std::atomic<std::uint64_t> next_id{1};
    };

    std::shared_ptr<State> st_;

  public:
    EventStream() = default;
    explicit EventStream(boost::asio::any_io_executor ex)
        : st_(std::make_shared<State>(std::move(ex))) {}

    // cópia/move default OK (só move/copia o shared_ptr)
    EventStream(const EventStream&) = default;
    EventStream(EventStream&&) noexcept = default;
    EventStream& operator=(const EventStream&) = default;
    EventStream& operator=(EventStream&&) noexcept = default;

    bool valid() const { return (bool)st_; }
    boost::asio::any_io_executor get_executor() const { return st_->ex; }

    // publica um evento para todos os assinantes (thread-safe)
    void push(const E& e) const {
        std::vector<Handler> cbs;
        {
            std::scoped_lock lk(st_->mx);
            cbs.reserve(st_->subs.size());
            for (auto& p : st_->subs) { cbs.push_back(p.second); }
        }
        for (auto& cb : cbs) {
            boost::asio::post(st_->ex, [cb, e]() { cb(e); });
        }
    }

    // assinar => devolve Subscription com cancel
    Subscription subscribe(Handler h) const {
        const auto id = st_->next_id.fetch_add(1, std::memory_order_relaxed);
        {
            std::scoped_lock lk(st_->mx);
            st_->subs.emplace_back(id, std::move(h));
        }
        std::weak_ptr<State> w = st_;
        return Subscription{[w, id]() {
            if (auto s = w.lock()) {
                std::scoped_lock lk(s->mx);
                auto& v = s->subs;
                v.erase(std::remove_if(v.begin(), v.end(),
                                       [id](auto& p) { return p.first == id; }),
                        v.end());
            }
        }};
    }

    // operadores funcionais — retornam novos streams (copiáveis/movíveis)

    EventStream<E> take_until(const std::shared_ptr<StopToken>& stop) const {
        EventStream<E> out(st_->ex);
        auto sub = subscribe([out, stop](const E& e) mutable {
            if (!stop->stop.load(std::memory_order_relaxed)) { out.push(e); }
        });
        out.st_->holds.push_back(std::move(sub)); // mantém a ligação viva
        return out;
    }

    EventStream<E> filter(std::function<bool(const E&)> pred) const {
        EventStream<E> out(st_->ex);
        auto sub = subscribe([out, pred = std::move(pred)](const E& e) mutable {
            if (pred(e)) { out.push(e); }
        });
        out.st_->holds.push_back(std::move(sub));
        return out;
    }

    template <class F, class R = std::invoke_result_t<F, const E&>>
    EventStream<R> map(F f) const {
        EventStream<R> out(st_->ex);
        auto sub = subscribe([out, f = std::move(f)](const E& e) mutable {
            out.push(f(e));
        });
        out.st_->holds.push_back(std::move(sub));
        return out;
    }
};

} // namespace bidi

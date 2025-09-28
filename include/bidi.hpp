// bidi.hpp — C++20 (Boost 1.84+)
#pragma once
#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace bidi {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace ws = beast::websocket;
using tcp = net::ip::tcp;
namespace json = boost::json;

using ContextId = std::string;

struct RemoteElement {
    json::value handle;
};
struct BidiError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ======================== WsSession ========================
class WsSession : public std::enable_shared_from_this<WsSession> {
  public:
    using strand_t = net::strand<net::io_context::executor_type>;

    explicit WsSession(net::io_context &ioc)
        : strand_(net::make_strand(ioc)), resolver_(strand_), stream_(strand_) {
    }

    net::any_io_executor get_executor() const {
        return strand_.get_inner_executor();
    }

    // resolve -> connect -> handshake
    template <class CompletionToken>
    auto async_connect(std::string host, std::string port, std::string target,
                       CompletionToken &&token) {
        return net::async_initiate<CompletionToken,
                                   void(boost::system::error_code)>(
            [self = shared_from_this(), host = std::move(host),
             port = std::move(port),
             target = std::move(target)](auto &&handler) mutable {
                self->host_ = host;
                self->target_ = target;

                self->resolver_.async_resolve(
                    self->host_, port,
                    [self, handler = std::move(handler)](
                        boost::system::error_code ec,
                        tcp::resolver::results_type res) mutable {
                        if (ec) {
                            std::move(handler)(ec);
                            return;
                        }

                        beast::get_lowest_layer(self->stream_)
                            .async_connect(res, [self,
                                                 handler = std::move(handler)](
                                                    boost::system::error_code
                                                        ec,
                                                    tcp::endpoint) mutable {
                                if (ec) {
                                    std::move(handler)(ec);
                                    return;
                                }

                                self->stream_.set_option(
                                    ws::stream_base::timeout::suggested(
                                        boost::beast::role_type::client)); // ✅
                                self->stream_.set_option(
                                    ws::stream_base::decorator(
                                        [](ws::request_type &req) {
                                            req.set(
                                                beast::http::field::user_agent,
                                                "bidi/beast");
                                        }));

                                self->stream_.async_handshake(
                                    self->host_, self->target_,
                                    [handler = std::move(handler)](
                                        boost::system::error_code ec) mutable {
                                        std::move(handler)(ec);
                                    });
                            });
                    });
            },
            token);
    }

    void start_read_loop() { do_read(); }
    void on_text(std::function<void(std::string)> cb) {
        on_text_ = std::move(cb);
    }

    // Fila de escrita: 1 write ativo de cada vez
    template <class CompletionToken>
    auto async_write(std::string msg, CompletionToken &&token) {
        return net::async_initiate<CompletionToken,
                                   void(boost::system::error_code)>(
            [self = shared_from_this(),
             msg = std::move(msg)](auto &&handler) mutable {
                using Handler = std::decay_t<decltype(handler)>;
                auto hptr = std::make_shared<Handler>(std::move(handler));

                net::post(self->strand_, [self, msg = std::move(msg),
                                          hptr]() mutable {
                    self->write_q_.emplace_back(PendingWrite{
                        std::move(msg),
                        [self, hptr](boost::system::error_code ec) {
                            net::post(self->strand_,
                                      [hptr, ec]() mutable { (*hptr)(ec); });
                        }});
                    if (!self->writing_)
                        self->do_write();
                });
            },
            token);
    }

  private:
    void do_read() {
        stream_.async_read(
            read_buf_, [self = shared_from_this()](boost::system::error_code ec,
                                                   std::size_t) {
                if (ec) { /* TODO: retry/reconnect policy */
                    return;
                }
                auto data = beast::buffers_to_string(self->read_buf_.data());
                self->read_buf_.consume(self->read_buf_.size());
                if (self->on_text_)
                    self->on_text_(std::move(data));
                self->do_read();
            });
    }

    struct PendingWrite {
        std::string data;
        std::function<void(boost::system::error_code)> done;
    };

    void do_write() {
        if (write_q_.empty() || writing_)
            return;
        writing_ = true;

        stream_.async_write(
            net::buffer(write_q_.front().data),
            [self = shared_from_this()](boost::system::error_code ec,
                                        std::size_t) mutable {
                auto done = std::move(self->write_q_.front().done);
                self->write_q_.pop_front();
                self->writing_ = false;

                if (done)
                    done(ec);
                if (!ec && !self->write_q_.empty())
                    self->do_write();
            });
    }

    strand_t strand_;
    tcp::resolver resolver_;
    ws::stream<beast::tcp_stream> stream_;
    std::string host_, target_;
    beast::flat_buffer read_buf_;
    std::function<void(std::string)> on_text_;
    bool writing_{false};
    std::deque<PendingWrite> write_q_;
};

// ======================== BidiClient ========================
class BidiClient : public std::enable_shared_from_this<BidiClient> {
  public:
    explicit BidiClient(std::shared_ptr<WsSession> ws) : ws_(std::move(ws)) {
        ws_->on_text([this](std::string s) { this->on_raw(std::move(s)); });
    }

    net::any_io_executor get_executor() const { return ws_->get_executor(); }

    // Envia {id,method,params} → future<json::object> com "result"/"value"
    std::future<json::object> async_send(std::string method,
                                         json::object params) {
        auto id = next_id_++;
        json::object cmd{{"id", id},
                         {"method", std::move(method)},
                         {"params", std::move(params)}};

        std::promise<json::object> p;
        auto fut = p.get_future();
        {
            std::scoped_lock lk(mx_);
            pending_.emplace(id, std::move(p));
        }
        ws_->async_write(json::serialize(cmd), net::detached);
        return fut;
    }

    // Espera próximo evento "method"
    template <class CompletionToken>
    auto async_next_event(std::string method, CompletionToken &&token) {
        return net::async_initiate<
            CompletionToken, void(boost::system::error_code, json::object)>(
            [self = shared_from_this(),
             m = std::move(method)](auto &&handler) mutable {
                std::scoped_lock lk(self->mx_);
                self->waiters_[m].push_back(
                    [h = std::move(handler)](json::object evt) mutable {
                        std::move(h)({}, std::move(evt));
                    });
            },
            token);
    }

  private:
    void on_raw(std::string s) {
        json::value v;
        try {
            v = json::parse(s);
        } catch (...) {
            return;
        }
        auto *obj = v.if_object();
        if (!obj)
            return;

        // Eventos: { type:"event", method, params }
        if (auto *t = obj->if_contains("type");
            t && t->is_string() && json::value_to<std::string>(*t) == "event") {
            auto m = json::value_to<std::string>((*obj)["method"]);
            json::object params = (*obj)["params"].as_object();
            std::vector<std::function<void(json::object)>> cbs;
            {
                std::scoped_lock lk(mx_);
                if (auto it = waiters_.find(m);
                    it != waiters_.end() && !it->second.empty())
                    cbs.swap(it->second);
            }
            for (auto &cb : cbs)
                cb(params);
            return;
        }

        // Respostas de comando: têm "id" + ("result"|"value") OU
        // ("error","message")
        if (auto *idp = obj->if_contains("id")) {
            const auto id = json::value_to<std::int64_t>(*idp);

            auto pop_promise =
                [this, id]() -> std::optional<std::promise<json::object>> {
                std::scoped_lock lk(mx_);
                if (auto it = pending_.find(id); it != pending_.end()) {
                    auto p = std::move(it->second);
                    pending_.erase(it);
                    return p;
                }
                return std::nullopt;
            };

            if (auto *r = obj->if_contains("result"); r && r->is_object()) {
                if (auto p = pop_promise())
                    p->set_value(r->as_object());
                return;
            }
            if (auto *val = obj->if_contains("value");
                val && val->is_object()) {
                if (auto p = pop_promise())
                    p->set_value(val->as_object());
                return;
            }
            if (auto *e = obj->if_contains("error")) {
                std::string code = json::value_to<std::string>(*e);
                std::string msg =
                    obj->if_contains("message")
                        ? json::value_to<std::string>((*obj)["message"])
                        : "unknown BiDi error";
                if (auto p = pop_promise())
                    p->set_exception(std::make_exception_ptr(
                        std::runtime_error(code + ": " + msg)));
                return;
            }
        }
    }

    std::shared_ptr<WsSession> ws_;
    std::atomic<std::int64_t> next_id_{1};
    std::mutex mx_;
    std::unordered_map<std::int64_t, std::promise<json::object>> pending_;
    std::unordered_map<std::string,
                       std::vector<std::function<void(json::object)>>>
        waiters_;
};

// ===== helper p/ script.callFunction (espera elemento sem polling) =====
inline json::object make_wait_elem_call(const std::string &by,
                                        const std::string &sel, int timeout_ms,
                                        const std::string &ctx_id) {
    static const char *kFunc = R"JS(
    async function(by, selector, timeout) {
      function getEl(by, s) {
        if (by === "css")   return document.querySelector(s);
        if (by === "xpath") return document
          .evaluate(s, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null)
          .singleNodeValue;
        throw new Error("unsupported selector: " + by);
      }
      const existing = getEl(by, selector);
      if (existing) return existing;

      return await new Promise((resolve, reject) => {
        const obs = new MutationObserver(() => {
          const el = getEl(by, selector);
          if (el) { obs.disconnect(); resolve(el); }
        });
        obs.observe(document, {subtree:true, childList:true, attributes:true});
        setTimeout(() => {
          obs.disconnect();
          const el = getEl(by, selector);
          if (el) resolve(el); else reject(new Error("timeout"));
        }, timeout);
      });
    }
  )JS";

    using json::array;
    using json::object;

    object params{{"functionDeclaration", kFunc},
                  {"awaitPromise", true},
                  {"target", object{{"context", ctx_id}}},
                  {"resultOwnership", "root"},
                  {"arguments",
                   array{object{{"type", "string"}, {"value", by}},
                         object{{"type", "string"}, {"value", sel}},
                         object{{"type", "number"}, {"value", timeout_ms}}}}};
    return params;
}

// ================= async_wait_element (CompletionToken) =================
template <class CompletionToken>
auto async_wait_element(std::shared_ptr<BidiClient> cli, ContextId ctx,
                        std::string by, std::string selector,
                        std::chrono::milliseconds timeout,
                        CompletionToken &&token) {
    using Sig = void(boost::system::error_code, RemoteElement);

    return net::async_initiate<CompletionToken, Sig>(
        [cli = std::move(cli), ctx = std::move(ctx), by = std::move(by),
         selector = std::move(selector), timeout](auto &&raw_handler) mutable {
            using Handler = std::decay_t<decltype(raw_handler)>;

            auto ex =
                net::get_associated_executor(raw_handler, cli->get_executor());
            auto handler = std::make_shared<Handler>(std::move(raw_handler));
            auto done = std::make_shared<std::atomic_bool>(false);
            auto timer = std::make_shared<net::steady_timer>(ex);

            using namespace std::chrono_literals;
            timer->expires_after(timeout + 200ms); // margem extra

            // 1) timeout → chama handler uma única vez
            timer->async_wait(
                [handler, done, ex](boost::system::error_code ec) {
                    if (ec) {
                        return;
                    } // cancelado; ignore
                    if (!done->exchange(true)) {
                        net::post(ex, [handler]() mutable {
                            (*handler)(
                                make_error_code(boost::system::errc::timed_out),
                                RemoteElement{});
                        });
                    }
                });

            // 2) script.callFunction → executa a função e aguarda a Promise
            //    (arguments são LocalValue; ver spec)
            json::object params{
                {"functionDeclaration",
                 R"js(
function waitForElement(selector, selectorType, timeout = 5000) {
  return new Promise((resolve, reject) => {
    function getElement(sel, type) {
      if (type === "css" || type === "css selector") {
        return document.querySelector(sel);
      } else if (type === "xpath") {
        return document.evaluate(
          sel, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null
        ).singleNodeValue;
      }
      throw new Error("Unsupported selector type: " + type);
    }

    const existing = getElement(selector, selectorType);
    if (existing) { resolve(existing); return; }

    const observer = new MutationObserver(() => {
      const el = getElement(selector, selectorType);
      if (el) {
        if (timeoutId) { clearTimeout(timeoutId); }
        observer.disconnect();
        resolve(el);
      }
    });

    observer.observe(document.body || document.documentElement, {
      childList: true, subtree: true
    });

    const timeoutId = setTimeout(() => {
      observer.disconnect();
      const el = getElement(selector, selectorType);
      if (el) { resolve(el); return; }
      reject(new Error(`Timeout reached: Element "${selector}" not found.`));
    }, timeout);
  });
}
)js"},
                {"target", json::object{{"context", ctx}}},
                {"awaitPromise", true},
                {"resultOwnership", "root"},
                {"arguments",
                 json::array{
                     // ordem: (selector, selectorType, timeout)
                     json::object{{"type", "string"}, {"value", selector}},
                     json::object{{"type", "string"}, {"value", by}},
                     json::object{
                         {"type", "number"},
                         {"value", static_cast<double>(timeout.count())}}}}};

            auto fut =
                cli->async_send("script.callFunction", std::move(params));

            // 3) espera o Future em thread separada e entrega no executor
            std::thread([f = std::move(fut), handler, done, timer,
                         ex]() mutable {
                try {
                    // 'result' aqui já é o objeto "result" do BiDi (inner)
                    auto result = f.get();

                    // Pode vir "exceptionDetails" quando a função/rejeição
                    // lança
                    if (auto *exd = result.if_contains("exceptionDetails");
                        exd) {
                        if (!done->exchange(true)) {
                            timer->cancel();
                            net::post(ex, [handler]() mutable {
                                auto errorc = make_error_code(
                                    boost::system::errc::operation_canceled);
                                (*handler)(errorc, RemoteElement{});
                            });
                        }
                        return;
                    }

                    // Espera um RemoteValue em result["result"] com type "node"
                    // e handle
                    auto &rv = result.at("result");
                    if (!rv.is_object()) {
                        throw std::runtime_error("Unexpected result shape");
                    }

                    if (!done->exchange(true)) {
                        timer->cancel();
                        RemoteElement el{
                            rv}; // mantém o RemoteValue (inclui 'handle')
                        net::post(ex, [handler, el = std::move(el)]() mutable {
                            (*handler)({}, std::move(el));
                        });
                    }
                } catch (...) {
                    if (!done->exchange(true)) {
                        timer->cancel();
                        net::post(ex, [handler]() mutable {
                            (*handler)(
                                make_error_code(
                                    boost::system::errc::operation_canceled),
                                RemoteElement{});
                        });
                    }
                }
            }).detach();
        },
        token);
}

} // namespace bidi

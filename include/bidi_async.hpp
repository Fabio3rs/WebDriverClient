#pragma once
#include "asyncx.hpp"
#include "bidi.hpp"

namespace bidi_async {
namespace json = boost::json;
namespace net = boost::asio;
using asyncx::Async;

// 1) adaptar std::future<json::object> -> Async<json::object>
inline Async<json::object> send(bidi::BidiClient &cli, net::any_io_executor ex,
                                std::string method, json::object params) {
    return Async<json::object>::from_future(
        ex, cli.async_send(std::move(method), std::move(params)));
}

// 2) adaptar CompletionToken (wait_element) -> Async<RemoteElement>
inline Async<bidi::RemoteElement>
wait_element(std::shared_ptr<bidi::BidiClient> cli, bidi::ContextId ctx,
             std::string by, std::string sel, std::chrono::milliseconds to) {
    auto ex = cli->get_executor();
    return Async<bidi::RemoteElement>::from_callback(
        ex, [cli, ctx = std::move(ctx), by = std::move(by),
             sel = std::move(sel), to](auto complete, std::stop_token) {
            bidi::async_wait_element(cli, ctx, by, sel, to,
                                     [complete](boost::system::error_code ec,
                                                bidi::RemoteElement el) {
                                         // complete tem operator() const
                                         // (graças à correção no asyncx.hpp)
                                         complete(ec, std::move(el));
                                     });
        });
}

} // namespace bidi_async

#pragma once
#include <boost/json.hpp>
#include "event_stream.hpp"
#include "bidi.hpp"      // seu WsSession/BidiClient
                         // BidiClient expõe: async_send, async_next_event(method, token)
namespace bidi {
namespace json = boost::json;
namespace net  = boost::asio;

// Tipos mínimos (exemplos) — veja a spec p/ campos completos:
struct LogEntry {
  std::string level;   // "info" | "error" | ...
  std::string text;    // mensagem do console
};

struct BeforeRequest {
  std::string requestId;
  std::string url;
  std::string method;
};

inline auto decode_log_entry(json::object p) -> LogEntry {
  // Estrutura conforme "log.entryAdded" em BiDi (params.entry.*).
  // Campos exatos estão na spec e nos guias do Selenium/WebdriverIO. :contentReference[oaicite:1]{index=1}
  LogEntry out{};
  try {
    auto entry = p.at("entry").as_object();
    out.level  = json::value_to<std::string>(entry.at("level"));
    if (auto* t = entry.if_contains("text")) {
      out.text = json::value_to<std::string>(*t);
    }
  } catch (...) { /* tolerante */ }
  return out;
}

inline auto decode_before_request(json::object p) -> BeforeRequest {
  // Estrutura conforme "network.beforeRequestSent" (params.request.*). :contentReference[oaicite:2]{index=2}
  BeforeRequest out{};
  try {
    auto req = p.at("request").as_object();
    out.requestId = json::value_to<std::string>(req.at("request"));
    out.url       = json::value_to<std::string>(req.at("url"));
    out.method    = json::value_to<std::string>(req.at("method"));
  } catch (...) { }
  return out;
}

// Helper: cria stream tipado a partir de um "method" BiDi
template<class T>
auto make_event_stream(std::shared_ptr<BidiClient> cli,
                       std::string method,
                       std::function<T(json::object)> decode) -> std::shared_ptr<EventStream<T>> {
  auto ex = cli->get_executor();
  auto stream = std::make_shared<EventStream<T>>(ex);

  // 1) Assina o evento (session.subscribe).
  // A assinatura de eventos é parte do protocolo BiDi. :contentReference[oaicite:3]{index=3}
  json::array evs; evs.emplace_back(method);
  cli->async_send("session.subscribe",
                  { {"events", evs} });

  // 2) Encaminha params decodificados para o stream
  cli->async_next_event(method,
    [stream, decode](boost::system::error_code ec, json::object params) {
      if (!ec) { stream->push(decode(std::move(params))); }
    });

  return stream;
}

} // namespace bidi

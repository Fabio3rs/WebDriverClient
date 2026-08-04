# Visão geral
Nome (codinome): bidi-x
Propósito: cliente WebDriver BiDi minimalista, de alto desempenho em CPU/RAM e com DX moderna (lazy, monádica e/ou corrotinas).
Alvo: C++20/23 (corrotinas opcionais), pronto para Senders/Receivers (C++26).
Transporte: WebSocket (Boost.Beast/Asio).
Padrão externo: W3C WebDriver BiDi (métodos/eventos como browsingContext.navigate, script.evaluate, session.subscribe, log.entryAdded, network.beforeRequestSent etc.). (W3C)
Necessidade e objetivos
Evitar custo ocioso: tudo lazy por padrão; nada inicia sem um terminal (e.g., .finally, co_await, .sync).
Event loop invisível e único: io_context + executor_work_guard + strand (ordem de writes sem locks). (boost.org)
Sem detach: zero std::thread().detach(); bordas bloqueantes vão para thread_pool com post de volta. (Aderente às C++ Core Guidelines.) (ISOC++)
Timeout determinístico: toda operação é uma corrida “operação OU steady_timer” com cancel cooperativo. (The C++ Alliance)
Assinaturas corretas (RAII): subscribe/unsubscribe com refcount (global e por contexto), auto-resubscribe após reconexão (copiando a semântica de global event set e browsing context event map da própria spec). (pr-preview.s3.amazonaws.com)
Intercept de rede completo: addIntercept/removeIntercept, continue*, provideResponse, failRequest, com filtros por URL/context. (Espelhado na documentação do Selenium.) (Selenium)
Compat & detecção: checagem de capacidades e mensagens de UnsupportedOperation amigáveis (diferenças entre engines/browsers). (webdriver.io)
Orçamento de CPU/RAM agressivo: fast-path no roteamento (id/method) sem DOM, arenas/PMR por mensagem e serialização sem DOM no envio. (boost.org)
Não-objetivos: implementar automação de UI completa tipo “framework de testes” — o foco é o cliente BiDi de base; DSL de página é opcional.
Diretrizes de projeto
Assíncrono canônico (Boost.Asio): CompletionToken/handlers; opção de corrotinas com awaitable, co_spawn, use_awaitable. (boost.org)
Guidelines: seguir CppCoreGuidelines (RAII, evitar detach, std::jthread/stop_token em bordas), e práticas Beast/Asio para não rodar operações concorrentes do mesmo tipo. (ISOC++)
Timeout/cancel: uso de steady_timer.cancel()/cancel tokens; evitar races na anulação de timers. (StudyRaid)
Confiabilidade: erro modelado em std::error_code + ErrorDetail {method,id,params,duration,raw}; exceções só nas fachadas sync.
Nomes canônicos: constantes 1:1 com a spec para métodos/eventos/valores (sem literais soltos). (W3C)
Arquitetura
Camadas
Transporto WS (Beast): handshake, async_read loop, fila de async_write serializada no strand. (boost.org)
BidiClient:
gera id, mantém pending (id → completion + timer), resolve respostas e publica eventos;
async_send(method, params, token); RAII de Subscription.
Módulos (tipados, nomes da spec):
BrowsingContext, Script, Log, Network, … com decoders fortemente tipados;
Session::subscribe/unsubscribe (global/por contexto). (pr-preview.s3.amazonaws.com)
DX Functional (opcional): Async<T> (lazy): map/then/on_error/timeout/retry/await_all; ou corrotinas (awaitable).
Modelo assíncrono (mecanismos)
1) Operação lazy → hot (envio BiDi)
Somente quando houver terminal (e.g., .finally, co_await) a operação:
aloca id;
registra em pending com timer;
escreve {id,method,params} no WS;
read loop resolve pela resposta (ou o timer cancela → erro).
O read loop diferencia resposta (id) de evento (method). (Terminologia e eventos conforme a spec, ex.: log.entryAdded.) (W3C)
Esboço — async_send (CompletionToken)
template<class CompletionToken>
auto BidiClient::async_send(std::string method,
                            boost::json::object params,
                            CompletionToken&& token)
{
  return boost::asio::async_initiate<CompletionToken,
      void(boost::system::error_code, boost::json::object)>(
    [self=shared_from_this(), method=std::move(method), params=std::move(params)]
    (auto&& handler) mutable {
      boost::asio::dispatch(self->strand_, [self, m=std::move(method), p=std::move(params),
                                            h=std::forward<decltype(handler)>(handler)]() mutable {
        auto id = self->next_id_++;
        auto tm = std::make_shared<boost::asio::steady_timer>(self->strand_);
        tm->expires_after(self->default_timeout_);
        self->pending_.emplace_back(id, Pending{std::move(h), tm});
        tm->async_wait([self, id](auto ec){ if(!ec) self->fail_timeout(id); });

        // enviar sem DOM (stringify direto)
        self->tx_.send_cmd(id, m, [&](fmt::memory_buffer& b){
          // escrever params em JSON minimal
        });
      });
    }, token);
}
Timeout como corrida (timer vs. resposta), no strand. (The C++ Alliance)
2) Corrotinas (opcional, C++20)
awaitable<T> + co_spawn e use_awaitable deixam o fluxo linear; exemplo oficial de Asio mostra o padrão. (boost.org)
boost::asio::awaitable<void> scenario(BidiClient& cli) {
  auto r   = co_await send_awaitable(cli,"browsingContext.create", {{"type","window"}});
  auto ctx = boost::json::value_to<std::string>(r.at("context"));
  co_await send_awaitable(cli, "browsingContext.navigate",
                          {{"context",ctx},{"url",url},{"wait","complete"}});
}
API pública (esqueleto)
Constantes canônicas
namespace bidi::ids {
  namespace methods {
    inline constexpr auto session_subscribe   = "session.subscribe";
    inline constexpr auto session_unsubscribe = "session.unsubscribe";
    inline constexpr auto bc_create           = "browsingContext.create";
    inline constexpr auto bc_navigate         = "browsingContext.navigate";
    inline constexpr auto script_evaluate     = "script.evaluate";
    inline constexpr auto net_addIntercept    = "network.addIntercept";
    inline constexpr auto net_failRequest     = "network.failRequest";
    inline constexpr auto net_provideResponse = "network.provideResponse";
  }
  namespace events {
    inline constexpr auto log_entryAdded        = "log.entryAdded";
    inline constexpr auto net_beforeRequestSent = "network.beforeRequestSent";
    inline constexpr auto net_responseStarted   = "network.responseStarted";
    inline constexpr auto net_responseCompleted = "network.responseCompleted";
  }
}
(terminologia 1:1 com a spec; eventos/métodos conferem com docs de ecossistemas como WebdriverIO/Selenium) (W3C)

Superfície de módulos
struct BrowsingContext {
  asyncx::Async<std::string> create_window();
  asyncx::Async<void> navigate(std::string ctx, std::string url,
                               /* Readiness */ std::string wait="complete");
};

struct Script {
  template<class T=boost::json::value>
  asyncx::Async<T> evaluate(std::string ctx, std::string js,
                            bool await_promise=true, std::string ownership="root");
};

struct Network {
  struct Intercept { std::string id; /* RAII: remove no dtor */ };
  asyncx::Async<Intercept> add_intercept(std::string phase,
                                         std::vector<std::string> url_patterns,
                                         std::optional<std::string> context);
  asyncx::Async<void> provide_response(std::string request_id, /* status, headers, body... */);
  asyncx::Async<void> fail_request(std::string request_id, std::string reason="aborted");
};
(Fases e comandos em linha com documentação do Selenium BiDi: beforeRequestSent / responseStarted / responseCompleted; provideResponse/failRequest etc.) (Selenium)

DX funcional
auto job =
  bc.create_window()
    .then([&](auto ctx){ return bc.navigate(ctx, url).then([=]{ return ctx; }); })
    .then([&](auto ctx){ return sc.evaluate<int>(ctx, "40+2", true); })
    .timeout(5s).retry({.tries=2})
    .on_error([](std::error_code ec){ return 0; });

// materializa:
job.finally([](auto ok, auto, auto){ if (ok) std::println("ans={}", *ok); });
DX em corrotinas
boost::asio::awaitable<int> flow() {
  auto ctx = co_await bc.create_window();
  co_await bc.navigate(ctx, url);
  co_return co_await sc.evaluate<int>(ctx, "40+2", true);
}
Subscrições e eventos
RAII + refcount: primeira assinatura de um evento (global/ctx) envia session.subscribe; a última sendo destruída dispara session.unsubscribe. Reconexão: auto-resubscribe (reaplica o conjunto atual). Semântica baseada em global event set e context event map da spec. (pr-preview.s3.amazonaws.com)
Eventos supportados: log.entryAdded, network.*, browsingContext.*, … conforme espec. e documentação Selenium. (Selenium)
Modelo de erros e telemetria
namespace bidi {
  enum class errc { timeout=1, transport, server_error, decode_error, cancelled, unsupported };
  struct ErrorDetail {
    std::error_code           ec;
    std::string               method;
    std::int64_t              id{};
    boost::json::object       params;
    boost::json::value        raw;
    std::chrono::milliseconds duration{};
  };
}
Respostas type:error → errc::server_error, com raw preservado.
Timeout e cancel são distintos (errc::timeout vs errc::cancelled).
Observabilidade leve (contadores, tempos por método).
Performance (CPU/RAM)
Transporte e escrita
Fila de writes no strand (sem async_write concorrente). È a prática recomendada com Beast/Asio. (GitHub)
Serialização sem DOM: construir JSON de comando diretamente em fmt::memory_buffer (menos alocações/cópias).
Leitura e roteamento
Fast-path sem DOM para extrair id/method (scan leve) e decidir: resposta vs evento.
Quando precisar de DOM, usar arena/PMR por mensagem (Boost.JSON storage_ptr/monotonic_resource) para alloc/free em bloco. (boost.org)
Opcional: integrar simdjson para parse “on-demand” em rotas críticas (até GB/s), quando o custo do DOM virar gargalo. (simdjson.org)
Memória
PMR/arena por frame (ex.: 64–256 KiB): parse → usa → arena “zera” ao fim do handler. (Boa com JSON de leitura, como aponta Boost.JSON.) (boost.org)
flat_buffer com limite; freelist de buffers de envio (memory_buffer) para reutilização.
Evitar guardar string_view além do ciclo de vida do frame.
Compatibilidade e detecção
Habilitar BiDi via webSocketUrl: true na criação da sessão; tratar diferenças de implementação e retornar errc::unsupported com mensagem direcionada. (webdriver.io)
Para rede, espelhar limitações conhecidas (e.g., provideResponse em certos drivers) e orientar fallback. (GitHub)
Hipóteses de código (trechos)
await_all (agregação lazy de N operações)
auto a = sc.evaluate<int>(ctx, "21+21");
auto b = sc.evaluate<std::string>(ctx, "document.title");
asyncx::await_all(a, b).then([](auto tup){
  auto& [sum, title] = tup; /* ... */
});
Intercept “mock/ban”
auto ic = net.add_intercept("beforeRequestSent", {"*tracker*", "*doubleclick.net*"}, ctx);
cli.on_event(ids::events::net_beforeRequestSent, [&](const json::object& e){
  auto req_id = /* decode */;
  auto url    = /* decode */;
  if (url.contains("tracker"))  (void) net.fail_request(req_id, "aborted");
  if (url.ends_with("/api/prices")) {
    /* … */ (void) net.provide_response(req_id /* status=200, headers, body */);
  }
});
(Fluxos e nomes como na doc de Network BiDi do Selenium.) (Selenium)
Testes e validação
Conformidade de wire: snapshot tests de comandos/eventos contra a spec (IDs/métodos/formatos). (W3C)
Robustez: injetar respostas fora de ordem, respostas tardias, perda de conexão, e validar que timeouts e cancelamento funcionam (Asio patterns). (The C++ Alliance)
Perf: microbenchmarks de roteamento (fast-scan), parse DOM em arena, fila de writes.
Compat: matrix (Chromium/Gecko/Edge) com webSocketUrl: true. (webdriver.io)
Manutenção e versionamento
Gerar ids.hpp a partir da spec (scraper do índice de métodos/eventos) para reduzir drift. (W3C)
Feature-flags para capacidades opcionais (ex.: goog:channel quando suportado).
Semântica estável: breaking changes somente em major.
Apêndice A — Mapeamento de nomes (exemplos)
browsingContext.create/navigate/close → BrowsingContext::{create_window,navigate,close}. (W3C)
script.evaluate → Script::evaluate<T>(ctx, js, await, ownership). (W3C)
session.subscribe/unsubscribe → BidiClient::subscribe(events[,context]). (Global vs. por contexto como define a spec.) (pr-preview.s3.amazonaws.com)
log.entryAdded, network.beforeRequestSent/responseStarted/responseCompleted → eventos tipados em Log/Network. (W3C)
Referências principais
WebDriver BiDi – W3C (nomes/semântica de métodos/eventos). (W3C)
“Global event set / browsing context event map” (modelo de subscrição). (pr-preview.s3.amazonaws.com)
Boost.Asio corrotinas e exemplos (awaitable, co_spawn, timeouts/cancel). (boost.org)
Boost.Beast WebSocket async client (padrões de leitura/escrita). (boost.org)
Selenium docs (BiDi Network/Logging/JS API). (Selenium)
WebdriverIO BiDi enablement (capability webSocketUrl). (webdriver.io)
Boost.JSON storage_ptr/monotonic_resource (arena por mensagem). (boost.org)
std::pmr::monotonic_buffer_resource (C++17 PMR). (en.cppreference.com)
TL;DR
O projeto entrega cliente BiDi com:

lazy total, sem detach, timeout como corrida e cancel cooperativo;
assinaturas RAII (global/ctx) com auto-resubscribe;
wire minimalista (serialize sem DOM, parse com arena, fast-path de roteamento);
DX de ponta (monádico/await_all e/ou corrotinas);
aderência estrita aos nomes da spec e guidelines de C++/Asio/Beast/Selenium.
Esse conjunto é o que permite superar CPU/RAM ao mesmo tempo em que mantém API confortável.

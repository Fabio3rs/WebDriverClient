# Guia técnico para LLM/Copilot — projeto **bidi-x** (cliente WebDriver BiDi C++)

> **Meta**: orientar qualquer LLM/Copilot a gerar *somente* código alinhado ao objetivo do projeto: **cliente BiDi minimalista**, **lazy por padrão**, **assíncrono canônico com Boost.Asio/Beast**, **economia agressiva de CPU/RAM**, **DX funcional/monádica e/ou corrotinas**, e **adesão estrita à especificação W3C**. Nada de *busy loops*, *polling* ou *detach*; toda espera deve ser por primitivas assíncronas (timers, handlers, awaitable) e cancelamento cooperativo.

---

## 0) Escopo, não-objetivos e nomes canônicos

* **Escopo**: transporte WebSocket (Beast/Asio), *router* de mensagens BiDi, *pending map* `{id → handler + timer}`, comandos essenciais (`browsingContext.*`, `script.*`, `session.*`) e eventos tipados (`log.*`, `network.*`). **Sem** DSL/UI automation completa — o foco é um cliente de base sólido. **Sempre** usar os nomes **idênticos** aos da TR/ED do W3C (evitar “strings soltas” no código).

* **Constantes 1:1 com a spec** e (idealmente) *codegen* de `ids.hpp` a partir do índice de métodos/eventos para reduzir *drift*.

---

## 1) Modelo assíncrono e *threading* (por que e como)

* **Event loop único**: `io_context` + `executor_work_guard`. Operações no WebSocket **sempre** serializadas via `strand` (ou *implicit strand*), garantindo **um `async_read`** e **um `async_write`** ativos por vez. Fila de *writes* no `strand` é o **padrão abençoado** pelo Beast. **Nunca** dispare `async_write` concorrentes.

* **Sem `std::thread().detach()`**: qualquer fronteira bloqueante (ex.: *future.get()* de algo externo) deve ir para *thread pool* e postar de volta no *executor*; isso evita *liveness leaks* e baixa a latência média. **Use** `std::jthread/stop_token` quando necessário.

* **Timeout determinístico = corrida (operação × timer)**: para **toda** solicitação com `id`, armar um `steady_timer` e registrar em `pending`. Ganha quem completar primeiro (resposta → cumprir; timer → `errc::timeout` + retirar do `pending`). A resposta tardia deve ser ignorada (id não existe mais).

* **Lazy por padrão**: nada dispara sem *terminal* (`.finally`, `co_await`, `.sync`). Ao “materializar”: gerar `id`, inserir em `pending`, iniciar timer e **só então** enviar `{id, method, params}`. O *read loop* resolve por `id` (resposta) ou publica evento por `method`.

**Motivo das decisões**: o `strand` elimina *locks* e *false sharing*; a corrida operação×timer dá semântica previsível a cancelamento/timeout; o *lazy* evita custo ocioso e mantém *throughput* alto no *hot path*. Tudo isso reduz clocks, alocações e *context switches*.

---

## 2) Relação com WebSocket (Beast) e roteamento de mensagens

* **Transporte WS**: *handshake*, *read loop* contínuo, *deque* de *writes*. No *read*: distinguir rápido **resposta** (tem `id`) de **evento** (tem `method`). **Fast-path sem DOM** para extrair `id/method`; quando precisar materializar JSON, use *arena/PMR por frame*.

* **`pending` por `id`**: *hash map* O(1) e função `fail_timeout(id)` que (1) remove, (2) completa com `errc::timeout`, (3) tolera respostas tardias. **Backpressure**: *high-water mark* para fila TX (bytes) — negue novos envios quando exceder.

* **ID seguro**: o projeto define `bidi::core::id_type` como `std::uint64_t` para armazenamento interno; porém, por segurança de interoperabilidade com consumidores que usam IEEE-754 (por exemplo, runtimes JavaScript), IDs serializados no wire não devem ultrapassar `2^53−1` (MAX_SAFE_ID). Quando necessário, o cliente deve documentar a estratégia de serialização (por exemplo, serializar IDs maiores como string) ou evitar gerar IDs acima deste limite.

---

## 3) API e DX (monádica e/ou corrotinas)

* **DX funcional**: `Async<T>` **lazy** com `.map/.and_then/.on_error/.timeout/.retry/.finally`. Cadeias não alocam sem necessidade; resultados e erros são propagados sem exceção; *tap* de erro não deve lançar. **Use** terminadores para materializar.

* **Corrotinas (opcional)**: `awaitable<T>` (+ `co_spawn`, `use_awaitable`) para fluxo linear quando conveniente; ainda assim, o núcleo permanece em *CompletionToken* por compat/perf. **Nunca** misture com chamadas *sync* bloqueantes dentro do *strand*.

* **Lazy real**: *somente* ao `.finally`/`co_await` a operação é registrada no `pending` e enviada. Nada deve gerar trabalho sem *terminal*.

**Motivo**: fornecer duas faces (funcional/corrotinas) sem duplicar semântica; *CompletionToken* é interoperável com Asio/Boost; *awaitable* dá ergonomia sem sacrificar controle de *scheduling*.

---

## 4) Especificação BiDi (o que validar no *wire*)

* **Mensagens**: `{id, method, params}` / `{id, result|error}`. Validar *shape* antes de despachar; mapear `type:error` em domínios (`timeout/transport/server_error/decode/unsupported`) e **preservar** `raw` em `ErrorDetail`.

* **Comandos críticos**:

  * `browsingContext.navigate` com `wait: "none"|"interactive"|"complete"`;
  * `script.evaluate` com `awaitPromise` e `resultOwnership: "root"|"none"`.
    Use as formas canônicas da spec (e das *bindings*).

* **Eventos & subscrição (RAII)**: `session.subscribe/unsubscribe` com **refcount** global/por contexto; *reconnect* reprovisiona o “*global event set* / *context map*”.

**Motivo**: *fast-fail* precoce e mensagens de erro previsíveis melhoram DX e simplificam *debugging* (com *telemetry leve*).

---

## 5) Economia de CPU/RAM (parsing/serialização, PMR, *hot paths*)

* **Leitura**: *fast-scan* para roteamento e **arena/PMR por frame** quando precisar de DOM (`boost::json::monotonic_resource` + `storage_ptr`). O ciclo é “parse → usa → libera em bloco” (arena esvazia no fim do handler). **Nunca** exponha `string_view` para fora do *lifetime* da arena.

* **Escrita**: **evite stringify manual** propenso a *escaping*; **use** `boost::json::serializer/serialize` direto para o *buffer* de TX. Reutilize *buffers* (freelist) e limite `flat_buffer`.

* **Escalonamento de parse**: quando JSON virar gargalo, *opt-in* para `simdjson` On-Demand em rotas quentes.

**Motivo**: PMR reduz *malloc/free* fragmentados; `serializer` evita cópias e *escaping* incorreto; *fast-path* e buffers reaproveitados cortam latência e uso de RAM.

---

## 6) Padrões de espera **sem polling** (ex.: *wait for element*)

* **Jamais** rode *loops ativos* consultando estado. Para DOM, **injete** uma *Promise* que combina *cheque imediato* + `MutationObserver` + *timeout* (rejeita com mensagem descritiva). Chamadas via `script.callFunction` com `awaitPromise=true` e `resultOwnership="root"`.

* **Erros descritivos**: propague a mensagem do *reject* (ex.: `Timeout reached: Element "#sel" not found.`) via `ErrorDetail` — **não** resuma tudo para `timed_out`; guarde `raw` para *diagnostics*.

**Motivo**: zero *busy-wait*, clareza de *debug* e portabilidade (funciona igual em Chromium/Gecko).

---

## 7) Cancelamento, *stop_token* e *finally*

* **Cancel cooperativo**: `steady_timer.cancel()` e *flags*/`stop_token` para encerrar cadeias pendentes de forma limpa. *finally* **nunca** lança; se *tap* de erro existe, ele não pode propagar exceções.

* **Separação clara**: `errc::timeout` (timer ganhou), `errc::cancelled` (cancel do usuário/transporte), `errc::transport` (WS), `errc::server_error` (resposta `type:error`), `errc::decode_error` (parse). Sempre inclua `method/id/duration/raw`.

---

## 8) Compatibilidade e detecção

* Habilite BiDi com `webSocketUrl: true` (capability). **Mensagens de UnsupportedOperation** devem ser claras e instruir *fallbacks* por *engine*.

---

## 9) Testabilidade e *harness* de *races*

* **Snapshot de *wire***: comandos/eventos comparados com a TR/ED (IDs, campos). **Smoke cross-engine**: Chrome/Edge/Firefox. **Perf micro**: roteador (scan), throughput de *writes*, parse com arena.

* **Harness de corrida**: simular respostas fora de ordem, atrasadas e *drop* de conexão para validar `pending` + *timeouts* + *cancel*.

---

## 10) Diretrizes prescritivas (o que **pode** / **não pode** o LLM sugerir)

**Pode sugerir**

1. Implementar *write queue* no `strand`, *read loop* contínuo, `pending{id→(handler,timer)}` e corrida operação×timer.
2. `Async<T>` *lazy* com `.map/.and_then/.on_error/.timeout/.retry/.finally` e versão *awaitable* (C++20).
3. Uso de PMR/arena por mensagem, `boost::json::serializer` para TX, *fast-path* para roteamento RX.
4. RAII de *subscriptions* (`session.subscribe/unsubscribe`), *auto-resubscribe* após reconexão.

**Não pode sugerir**
A. *Polling* em *threads* dedicadas (“já terminou?”) — **use** timers/handlers/awaitable.
B. `std::thread().detach()` para “simplificar” — **proibido**. Use *strand* e *thread pool* quando estritamente necessário.
C. Multiplicar `async_write` simultâneos — **sempre** serializar no `strand`.
D. Serialização JSON manual com *escaping* ad-hoc — **usar** `serializer/serialize`.
E. Estourar `id` para além de `2^53−1` — Evite gerar IDs acima de `2^53−1` ao serializar; internamente use `bidi::core::id_type` (`std::uint64_t`) e documente uma estratégia de serialização (por exemplo, enviar IDs maiores como string) se realmente precisar exceder `MAX_SAFE_ID`.

---

## 11) *Design* para baixo overhead (stack-first)

* **Cadeias *lazy***: não aloque se a cadeia não materializou (*terminal* ausente). Propagar valores/erros por **SBO/`variant`** e postar *conts* no executor (sem bloquear).

* **Capturas pequenas e `constexpr`**: nomes canônicos, *traits* e *tag types* como `constexpr` para permitir *inlining* e reduzir *heap*. Gere tabelas de *dispatch* (quando fizer sentido) em `constexpr`.

* **Buffers reutilizáveis**: *freelist* para TX, `flat_buffer` com limite, arenas PMR para RX.

---

## 12) Rede (*intercept*), compat e mensagens de erro

* **`Network` completo**: `addIntercept/continue*/provideResponse/failRequest` com fases `beforeRequestSent/responseStarted/responseCompleted`. Lidar com *quirks* e devolver `unsupported` com instrução de *fallback*.

---

## Logging estruturado com std::source_location

- Objetivo: capturar automaticamente o call-site (arquivo, linha, coluna, função) sem macros.
- API: `log_error`, `log_info`, `log_warning`, `log_debug` aceitam `const std::source_location& loc = std::source_location::current()`.
- JSON: o builder inclui um objeto `source` com `{file, line, column, function}` por padrão.
- Stripping opcional: defina `WEBDRIVER_STRIP_LOG_LOCATION=ON` no CMake para omitir o campo `source` dos logs.

Exemplos:

```cpp
using bidi::logging::log_error;

// Captura o ponto de chamada automaticamente
log_error("Read error: " + ec.message());

// Em wrappers, preserve a origem repassando 'loc'
inline void log_transport_error(const std::string& op,
                                const std::error_code& ec,
  log_error("Transport error during " + op + ": " + ec.message(), ec, nullptr, {}, loc);
}
```

Boas práticas:
- Não use macros para logging; prefira o parâmetro defaultado de `std::source_location`.
- Em funções helper, sempre exponha `loc` com default e repasse para `log_*` para preservar o call-site do chamador.
- Para builds de produção sensíveis a metadados, habilite `WEBDRIVER_STRIP_LOG_LOCATION`.

Validação:
- Teste `tests/logging_source_location_test.cpp` verifica presença do campo `source` por padrão e ausência quando a flag de strip está ON.

## Regras prescritivas adicionais (do review de qualidade)

### [[nodiscard]] em operações lazy
- **Marque todos os métodos Task<T>** (lazy) com `[[nodiscard]]` para prevenir perda acidental de operações não materializadas.
- Exemplo: `client->navigate(...)` sem `.finally()` ou `co_await` gera warning de compilação.

### Semântica de conclusão de write
- Handlers de `async_send` são chamados **após** o write real no WebSocket, não imediatamente (fire-and-forget foi corrigido para rastreamento adequado).

### Thread safety explícita
- Classes com strand (ex.: `WebSocketClient`, `BiDiSession`) têm documentação Doxygen clara sobre acesso STRAND-ONLY.
- `get_executor()` retorna por valor intencionalmente para segurança de captura em lambdas.

---

### TL;DR para o Copilot/LLM

> Siga *estritamente* o padrão Asio/Beast: `strand` para serializar WS, **uma** leitura e **uma** escrita ativas, fila TX, `pending{id→handler+timer}` e **timeout por corrida**. Modele erros com `std::error_code + ErrorDetail`. DX: `Async<T>` *lazy* (monádico) **ou** `awaitable` (corrotinas), mas **nunca** gere trabalho sem *terminal*. Memória: *fast-path* no roteador, PMR/arena por frame, pools alocadas/reutilizadas, `serializer` no TX, **sem** *polling*/*detach*. Nomes e *shape* **idênticos** à spec W3C.

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
```cpp
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
```
Timeout como corrida (timer vs. resposta), no strand. (The C++ Alliance)
2) Corrotinas (opcional, C++20)

```cpp
awaitable<T> + co_spawn e use_awaitable deixam o fluxo linear; exemplo oficial de Asio mostra o padrão. (boost.org)
boost::asio::awaitable<void> scenario(BidiClient& cli) {
  auto r   = co_await send_awaitable(cli,"browsingContext.create", {{"type","window"}});
  auto ctx = boost::json::value_to<std::string>(r.at("context"));
  co_await send_awaitable(cli, "browsingContext.navigate",
                          {{"context",ctx},{"url",url},{"wait","complete"}});
}
```

API pública (esqueleto)
Constantes canônicas
```cpp
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
```
(terminologia 1:1 com a spec; eventos/métodos conferem com docs de ecossistemas como WebdriverIO/Selenium) (W3C)

Superfície de módulos

```cpp
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
```
(Fases e comandos em linha com documentação do Selenium BiDi: beforeRequestSent / responseStarted / responseCompleted; provideResponse/failRequest etc.) (Selenium)

DX funcional
```cpp
auto job =
  bc.create_window()
    .then([&](auto ctx){ return bc.navigate(ctx, url).then([=]{ return ctx; }); })
    .then([&](auto ctx){ return sc.evaluate<int>(ctx, "40+2", true); })
    .timeout(5s).retry({.tries=2})
    .on_error([](std::error_code ec){ return 0; });

// materializa:
job.finally([](auto ok, auto, auto){ if (ok) std::println("ans={}", *ok); });
```
DX em corrotinas
```cpp
boost::asio::awaitable<int> flow() {
  auto ctx = co_await bc.create_window();
  co_await bc.navigate(ctx, url);
  co_return co_await sc.evaluate<int>(ctx, "40+2", true);
}
```
Subscrições e eventos
RAII + refcount: primeira assinatura de um evento (global/ctx) envia session.subscribe; a última sendo destruída dispara session.unsubscribe. Reconexão: auto-resubscribe (reaplica o conjunto atual). Semântica baseada em global event set e context event map da spec. (pr-preview.s3.amazonaws.com)
Eventos supportados: log.entryAdded, network.*, browsingContext.*, … conforme espec. e documentação Selenium. (Selenium)
Modelo de erros e telemetria
```cpp
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
```
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

```cpp
auto a = sc.evaluate<int>(ctx, "21+21");
auto b = sc.evaluate<std::string>(ctx, "document.title");
asyncx::await_all(a, b).then([](auto tup){
  auto& [sum, title] = tup; /* ... */
});
```

Intercept “mock/ban”

```cpp
auto ic = net.add_intercept("beforeRequestSent", {"*tracker*", "*doubleclick.net*"}, ctx);
cli.on_event(ids::events::net_beforeRequestSent, [&](const json::object& e){
  auto req_id = /* decode */;
  auto url    = /* decode */;
  if (url.contains("tracker"))  (void) net.fail_request(req_id, "aborted");
  if (url.ends_with("/api/prices")) {
    /* … */ (void) net.provide_response(req_id /* status=200, headers, body */);
  }
});
```

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


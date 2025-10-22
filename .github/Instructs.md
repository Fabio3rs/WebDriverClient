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

* **ID seguro**: internamente o código usa `bidi::core::id_type` (`std::uint64_t`) para evitar truncamento em implementações internas; contudo, por interoperabilidade com runtimes que usam IEEE-754 (p.ex. JavaScript), IDs transmitidos devem ficar dentro de `2^53−1` (MAX_SAFE_ID). Se o projeto precisar usar IDs maiores, documente claramente a estratégia de serialização (por exemplo, enviar o `id` como string no JSON) e avalie compatibilidade com consumers.

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

### TL;DR para o Copilot/LLM

> Siga *estritamente* o padrão Asio/Beast: `strand` para serializar WS, **uma** leitura e **uma** escrita ativas, fila TX, `pending{id→handler+timer}` e **timeout por corrida**. Modele erros com `std::error_code + ErrorDetail`. DX: `Async<T>` *lazy* (monádico) **ou** `awaitable` (corrotinas), mas **nunca** gere trabalho sem *terminal*. Memória: *fast-path* no roteador, PMR/arena por frame, pools alocadas/reutilizadas, `serializer` no TX, **sem** *polling*/*detach*. Nomes e *shape* **idênticos** à spec W3C.

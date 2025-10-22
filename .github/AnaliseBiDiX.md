# Análise técnica do projeto **bidi-x** (cliente WebDriver BiDi C++)

A proposta está muito boa: foco em *lazy eval*, modelo assíncrono canônico com Asio/Beast, RAII para *subscriptions*, e orçamento de CPU/RAM agressivo. Abaixo trago uma revisão crítica com pontos de validação contra as referências (W3C/Asio/Beast/Selenium/WebdriverIO) e recomendações práticas para você sair com um design “difícil de quebrar”.

---

## 1) Wire protocol & compat

**Formato das mensagens.** O BiDi define mensagens `{id, method, params}` e respostas com `result|error`. O `id` é inteiro ≥ 0; a presença de `method` valida contra a lista de comandos. Use isso para validação e *fast-fail* no roteador. ([pr-preview.s3.amazonaws.com][1])

**Limite seguro para `id`.** Evite `id` acima de `2^53-1` (limite de *double* IEEE-754 usado nos parsers). Há bugs reportados em Chromium quando o `id` “vira float”. O projeto padroniza internamente `bidi::core::id_type` como `std::uint64_t` para flexibilidade, mas recomenda que IDs serializados não excedam `2^53-1` (MAX_SAFE_ID). Caso o sistema gere IDs maiores, considere serializá-los como strings no JSON ou adotar um esquema de mapeamento que preserve interoperabilidade com runtimes baseados em IEEE-754.

**Habilitando BiDi.** Sua detecção de capacidades via `webSocketUrl: true` está correta e é a maneira recomendada (Selenium, MDN, WebdriverIO). ([Selenium][3])

**Cobertura de navegadores.** Hoje, Chrome/Edge/Firefox têm suporte robusto; Safari segue em progresso/descompasso. Planeje mensagens de `UnsupportedOperation` claras e *feature flags* por *engine*. ([Testplane][4])

**Eventos e assinatura.** O modelo “*global event set*” e “*browsing context event map*” existe na spec: sua ideia de RAII+refcount para `session.subscribe/unsubscribe` está alinhada; no *reconnect*, reprojete o conjunto atual. ([pr-preview.s3.amazonaws.com][1])

**Parâmetros canônicos.** Exemplos úteis para *shape checking*:
– `browsingContext.navigate` com `wait = "none"|"interactive"|"complete"`. ([pr-preview.s3.amazonaws.com][5])
– `script.evaluate` com `awaitPromise` e `resultOwnership = "root"|"none"`. ([Searchfox][6])

**Intercept de rede.** Os comandos `network.addIntercept/… provideResponse/failRequest` estão documentados e usados ativamente; há *quirks* e bugs históricos (especialmente em `provideResponse`). Mantenha *fallbacks* e mensagens de erro que sugiram alternativas. ([Selenium][7])

---

## 2) Transporte WS (Beast/Asio) e concorrência

**Um `async_read` e um `async_write` ativos por vez.** Beast deixa claro que *stream não é thread-safe*; serialize **todas** as operações via `strand` (ou o *implicit strand* do executor) e mantenha fila de *writes* para evitar `async_write` concorrente. Isso casa com sua proposta de “fila no `strand`”. ([Boost][8])

**Event loop invisível.** Use `executor_work_guard` para não deixar o `io_context::run()` cair quando não houver trabalho (o que você já previu). ([Boost][9])

**Timeout determinístico (corrida operação×timer).** O padrão recomendado em Asio é cronometrar e, ao expirar, acionar *cancellation* na operação (ou falhar “localmente” e ignorar resposta tardia). Os exemplos da C++ Alliance e do Boost.MySQL com *coroutines* são uma boa base. Evite reusar *timer* sem “geração” para não confundir *callbacks* após *cancel*. ([The C++ Alliance][10])

**Cancel cooperativo.** Se oferecer *cancel tokens*, encadeie com `bind_cancellation_slot`/`cancellation_signal` ou componha operações. (O seu RAII + `steady_timer.cancel()` já leva 90% do caminho; só cuide das *races*.) ([The C++ Alliance][10])

---

## 3) Parsing/serialização e orçamento de CPU/RAM

**Roteamento “fast-path”.** Sua ideia de *scan* só para `id`/`method` antes de decidir “resposta vs evento” é ótima. Considere `boost::json::stream_parser` para extrair chaves de *top-level* incrementalmente, evitando DOM completo quando possível. ([Boost][11])

**DOM sob demanda e arenas.** Para quando precisar materializar, `boost::json::monotonic_resource`/`storage_ptr` dão parse rápido e *free* em bloco — encaixa exatamente no seu ciclo “por frame”. ([Boost][12])

**Serialização sem DOM (lado TX).** Em vez de “stringify manual” com `fmt` (arriscado por *escaping*), use `boost::json::serializer`/`serialize(obj)` que já emite compacto, correto e pode escrever direto em *buffer/ostream* sem DOM pesado. ([Boost][13])

**Altas taxas de parse.** Se/Quando JSON virar gargalo, `simdjson` *On-Demand* é um *upgrade* plausível (GB/s). Tenha um caminho *opt-in* para rotas quentes. ([GitHub][14])

**Cuidados de *lifetime*.** Com PMR por frame, nunca exponha `string_view` que aponte para a arena após o retorno do handler — regra de ouro para evitar *UAF*. (Compatível com sua seção “evitar guardar string_view”.) ([Boost][12])

---

## 4) Modelo assíncrono e DX

**Asio “puro” + *awaitable*.** Seu `async_initiate` + opção `awaitable` segue o *gold standard* do Asio. Aplique `co_spawn` no *strand* do *client* para manter a linearidade e o *ordering*. ([Boost][15])

**Senders/Receivers (C++26).** Ótima ideia já nascer “pronto para S/R”. Com P2300 aprovado para C++26, vale modelar seu `Async<T>` como *adaptor* de *senders*: `async_send` ↔ *sender*; `.then/.on_error/.timeout/.retry` ↔ *algorithms*. Isso evita *lock/alloc* extras em cadeias síncronas. Prototipe com `stdexec`. ([Sutter’s Mill][16])

**Erros.** Mantenha `std::error_code` + `ErrorDetail` (muito bom). Mapeie `type:error` da resposta BiDi para domínios (`timeout/transport/server_error/decode/unsupported`) e preserve o `raw` para *diagnostics*. ([W3C][17])

---

## 5) Rede: intercept total, fases e limitações

**API e fases.** `beforeRequestSent`, `responseStarted`, `responseCompleted`, além de *continue*/`failRequest`/`provideResponse`, estão documentados e com exemplos multi-linguagem em Selenium; espelhe isso no seu módulo `Network`. ([Selenium][7])

**Gaps reais.** Houve (e ainda surgem) *issues* envolvendo `provideResponse` e *continue* em alguns stacks. Trate erros de estado e informe claramente (*e.g.*, “driver não suporta X neste contexto; tente Y”). ([GitHub][18])

---

## 6) Robustez do `BidiClient`

**Fila de *writes* no `strand`.** Mantenha um `deque<tx>`; se o *stream* não está escrevendo, *kick off* um `async_write`; ao completar, *pop_front* e dispare o próximo. (Padrão abençoado por Beast.) ([Boost][8])

**`pending` por `id`.** Prefira `flat_hash_map`/`unordered_map` para O(1). O `fail_timeout(id)` precisa:

1. retirar do mapa,
2. completar handler com `errc::timeout`,
3. opcionalmente cancelar leitura correlata se o protocolo local permitir. (A resposta tardia deve ser ignorada — *id* não existe mais.)

**Reconexão.** No *reconnect*, (a) reabra *read loop*, (b) *drain* a fila de *writes* só após handshake, (c) reprograme *subscriptions* globais e por contexto conforme o “*event set/map*” atual. ([pr-preview.s3.amazonaws.com][1])

**Backpressure.** Se o *peer* engasgar, seu *queue* cresce. Estabeleça *high-water mark* (tamanho total em bytes) e falhe pedidos novos com `errc::cancelled`/“busy” para não explodir RAM.

---

## 7) Testes, conformidade e perf

**Conformidade de *wire*.** Faça *snapshot tests* comparando comandos/eventos com a TR/ED da spec e com *bindings* (WebdriverIO lista comandos/campos atualizados). ([W3C][17])

**Matrix de engines.** Rode *smoke* em Chrome/Edge/Firefox com `webSocketUrl: true`. Acompanhe docs de troubleshooting e nuvens (BrowserStack/TestingBot) para validar intercept/console/logs. ([Selenium][3])

**Perf.** Microbenchmarks do roteador (scan `id/method`), parse com arena e throughput de *writes*. Se precisar, avalie rota *opt-in* com `simdjson` On-Demand. ([simdjson.org][19])

---

## 8) Ajustes finos de API (sugestões)

* **`ids.hpp` gerado:** automatize extração de nomes/códigos da *spec living* (evita *drift*). Baseie-se na lista de comandos de *docs* (p.ex., WebdriverIO gera a partir da *living spec*). ([WebdriverIO][20])
* **Tipos fortes para nomes:** `constexpr std::string_view` + *namespaces* como você propôs.
* **`Subscription` RAII:** *global* vs *per-context* com *refcount* interno; `~Subscription()` dispara *unsubscribe* quando zera — e participa do *auto-resubscribe*. ([pr-preview.s3.amazonaws.com][1])
* **Timeout por operação:** valor padrão configurável + `per-call override`; exponha *policy* (cancelar só local ou também fechar WS). ([The C++ Alliance][10])
* **`resultOwnership` em `Script`:** exponha `root|none` explicitamente no template. ([W3C][17])

---

## 9) Riscos e *foot-guns* a evitar

* **IDs exagerados:** *wrap* em 32 bits para não trombar em *float64* do lado do *remote*. ([issues.chromium.org][2])
* **`async_write` concorrente:** nunca. Centralize no `strand` com fila. ([Boost][8])
* **Timers reusados sem “geração”:** pode disparar *callback* antigo “sem cancel”. Use *generation counter* se reaproveitar timers. ([Reddit][21])
* **Stringify manual de JSON:** *escaping* é traiçoeiro; prefira `boost::json::serializer/serialize`. ([Boost][13])
* **Suposições de suporte de rede:** `provideResponse/continue*` variam entre *drivers*; trate *unsupported* com mensagem concreta e *fallbacks*. ([GitHub][18])

---

## 10) Roadmap proposto (objetivo → evidência)

1. **MVP transporte:** WS + fila de *writes* no `strand`, `async_read` contínuo, `pending{id→handler+timer}` com *race* bem definida (oper × timer). ([Boost][8])
2. **MVP comandos:** `browsingContext.create/navigate/close`, `script.evaluate` (await/resultOwnership), `session.subscribe/unsubscribe`. ([pr-preview.s3.amazonaws.com][5])
3. **Eventos:** `log.entryAdded`, `network.*`, `browsingContext.*` com RAII de *subscriptions*. ([Selenium][22])
4. **Intercept de rede:** `addIntercept/continue*/provideResponse/failRequest` + *compat matrix* e *feature flags*. ([Selenium][7])
5. **Perf + memória:** *fast-scan* + `stream_parser` + arenas PMR; *opt-in* simdjson em *hot paths*. ([Boost][11])
6. **DX avançada:** *awaitable* + *adapters* Senders/Receivers (pré-C++26), mantendo assinatura `CompletionToken`. ([wg21.link][23])

---

### Conclusão

O **bidi-x** está muito bem direcionado: fundamentos de Asio/Beast corretos, *lazy by default*, RAII de *subscriptions*, e preocupações reais de CPU/RAM. Com os ajustes acima (IDs seguros, fila de *writes* canônica, *timeouts* com cancel, `serializer/stream_parser` no lugar de *stringify* manual, *feature flags* por engine), você fica com um cliente BiDi enxuto, previsível e pronto para C++26/`std::execution`. ([Boost][8])

Se quiser, na próxima iteração posso revisar o *skeleton* de `BidiClient::async_send`/fila de *writes* com trechos compiláveis (Asio + Beast) e um *harness* de testes de *race* operação×timer.

[1]: https://pr-preview.s3.amazonaws.com/w3c/webdriver-bidi/pull/153.html?utm_source=chatgpt.com "WebDriver BiDi"
[2]: https://issues.chromium.org/issues/352467343?utm_source=chatgpt.com "BiDi - Huge id of command does turn into a floating number ..."
[3]: https://www.selenium.dev/documentation/webdriver/bidi/?utm_source=chatgpt.com "BiDirectional functionality"
[4]: https://testplane.io/blog/support-bidi-protocol/?utm_source=chatgpt.com "WebDriver BiDi protocol support"
[5]: https://pr-preview.s3.amazonaws.com/w3c/webdriver-bidi/395/9904e83...402d879.html?utm_source=chatgpt.com "WebDriver BiDi"
[6]: https://searchfox.org/firefox-main/source/remote/webdriver-bidi/modules/root/script.sys.mjs?utm_source=chatgpt.com "script.sys.mjs - mozsearch - Searchfox"
[7]: https://www.selenium.dev/documentation/webdriver/bidi/w3c/network/?utm_source=chatgpt.com "Network"
[8]: https://www.boost.org/doc/libs/1_73_0/libs/beast/doc/html/beast/using_websocket/notes.html?utm_source=chatgpt.com "Notes"
[9]: https://www.boost.org/doc/libs/master/doc/html/boost_asio/reference/io_context.html?utm_source=chatgpt.com "io_context"
[10]: https://cppalliance.org/asio/2023/01/02/Asio201Timeouts.html?utm_source=chatgpt.com "Asio 201 - timeouts, cancellation & custom tokens"
[11]: https://www.boost.org/doc/libs/1_87_0/libs/json/doc/html/json/input_output.html?utm_source=chatgpt.com "Input/Output"
[12]: https://www.boost.org/doc/libs/1_85_0/libs/json/doc/html/json/ref/boost__json__monotonic_resource.html?utm_source=chatgpt.com "monotonic_resource"
[13]: https://www.boost.org/doc/libs/1_87_0/boost/json/serializer.hpp?utm_source=chatgpt.com "boost/json/serializer.hpp"
[14]: https://github.com/simdjson/simdjson?utm_source=chatgpt.com "simdjson/simdjson: Parsing gigabytes of JSON per second"
[15]: https://www.boost.org/doc/libs/1_89_0_beta1/doc/html/boost_asio/overview/core/strands.html?utm_source=chatgpt.com "Strands: Use Threads Without Explicit Locking"
[16]: https://herbsutter.com/2024/07/?utm_source=chatgpt.com "July 2024 – Sutter's Mill"
[17]: https://www.w3.org/TR/webdriver-bidi/?utm_source=chatgpt.com "WebDriver BiDi"
[18]: https://github.com/SeleniumHQ/selenium/issues/14443?utm_source=chatgpt.com "Selenium BIDI network interception facing an issue with the ..."
[19]: https://simdjson.org/api/0.6.0/md_doc_ondemand.html?utm_source=chatgpt.com "A Better Way to Parse Documents? - Simdjson"
[20]: https://webdriver.io/docs/api/webdriverBidi/?utm_source=chatgpt.com "WebDriver Bidi Protocol"
[21]: https://www.reddit.com/r/cpp/comments/jdy2gd/asio_users_how_do_you_deal_with_cancellation/?utm_source=chatgpt.com "Asio users, how do you deal with cancellation? : r/cpp"
[22]: https://www.selenium.dev/documentation/webdriver/bidi/logging/?utm_source=chatgpt.com "WebDriver BiDi Logging Features"
[23]: https://wg21.link/P2300?utm_source=chatgpt.com "P2300R10: `std::execution` - WG21 Links"


# boost::asio::io_context — Guia rápido

Este documento explica o propósito de `boost::asio::io_context`, padrões de uso no projeto BiDi, e exemplos práticos.

O que é `io_context`?
- `boost::asio::io_context` (ou `asio::io_context`) é o laço de eventos central do Boost.Asio. Ele gerencia filas de handlers (timers, sockets, async ops) e dispatch/executor para execução de callbacks.

Padrões adotados neste projeto
- Um `io_context` por aplicação (ou por componente de alto nível). A biblioteca BiDi baseia-se no padrão "single io_context + strand" para serializar operações relacionadas ao WebSocket/Session.
- Use `boost::asio::make_work_guard(ioc.get_executor())` (ou `make_work_guard(ioc)`) quando for necessário manter o `io_context` vivo enquanto houver trabalho assíncrono em background.
- Para modelos concorrentes, rode `io_context.run()` em múltiplas threads para aumentar paralelismo; para aplicações simples, rode em uma thread principal.
- Use `strand` quando múltiplas threads chamam handlers que acessam recursos não thread-safe para garantir serialização sem locks.

Boas práticas e receita de `main()`
1. Crie o `io_context` no `main()` ou encapsule-o em um RAII helper.
2. Prepare quaisquer recursos síncronos (ex: obter WebSocket URL via HTTP) antes de iniciar o loop de eventos, ou use um `SessionGuard`/`ConnectionBuilder` que encapsule isso.
3. Lance as coroutines via `co_spawn(ioc, my_task(), use_future|detached|use_awaitable)` conforme o padrão desejado.
4. Use `make_work_guard(ioc.get_executor())` se existir uma thread que precise manter o loop rodando mesmo que não haja handlers imediatos.
5. Quando quiser parar ordenadamente, chame `ioc.stop()` ou solte o work guard e deixe `run()` terminar naturalmente; então faça `join()` nas threads que executaram `run()`.

Exemplo mínimo usando coroutine e `co_spawn`:

```cpp
boost::asio::io_context ioc;
// opcional: auto guard = boost::asio::make_work_guard(ioc.get_executor());
boost::asio::co_spawn(ioc, my_coroutine(), boost::asio::detached);
ioc.run();
```

Exemplo com `use_future` e watchdog:

```cpp
boost::asio::io_context ioc;
auto fut = boost::asio::co_spawn(ioc, run_flow(), boost::asio::use_future);
// start timer/watchdog that calls ioc.stop() se tempo exceder
ioc.run();
int result = fut.get();
```

Encapsulamento recomendado (IoContextRunner)
- Para scripts simples, preferir um helper RAII que cria o `io_context`, adiciona `work_guard`, inicia um `std::jthread` que chama `run()` e expõe um executor. Isso reduz a verbosidade em exemplos sem esconder a semântica.

Exemplo usando `bidi::IoContextRunner` (syntactic sugar leve):

```cpp
// Header: include/bidi/io_context_runner.hpp
// Cria runner que inicia background thread com io_context
bidi::IoContextRunner runner;
auto &ioc = runner.get();
boost::asio::co_spawn(ioc, my_coroutine(), boost::asio::detached);
// runner será destruído no final do escopo e fará join das threads
```

Notas sobre `operator()()` e materialização
- Muitas operações na biblioteca retornam `Task<T>` (lazy). Para usar em coroutines com Boost.Asio é necessário converter para `awaitable<T>` — isto é feito por `operator()()` em `Async`/`Task`.
- Chamar `task()` converte a operação para `awaitable` e também faz tradução de exceções para tipos específicos do projeto.

Referências no repositório
- `examples/flow/*` — padrões de uso reais
- `include/bidi/connection_builder.hpp` — builder para conectividade
- `include/asyncx.hpp` — explicação detalhada do modelo Async e `operator()()`

Sugestão prática: ao escrever exemplos, siga o padrão visto em `examples/flow/example_bidi_flow_minimal.cpp` — crie o `io_context`, co_spawn a coroutine `main` e rode `ioc.run()` no `main()`.

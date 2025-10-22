abaixo estão **exemplos hipotéticos** usando um *wrapper* chamado `bidicli` (nome ilustrativo) com **métodos de alto nível**. Cada chamada tem um comentário curto mapeando para o comando BiDi equivalente (ex.: `// calls "script.evaluate"`). A ideia é servir de guia de DX.

> **Suposições do wrapper `bidicli`**
>
> * Retorna `Async<T>` (teu tipo monádico), com `.map()`, `.and_then()`, `.finally()`, `.on_error()`, `.within()`, etc.
> * `Async<T>` é diretamente *awaitable* - pode usar `co_await` direto sem conversão adicional.
> * Usa executor do `io_context` internamente (sem *polling* e sem *busy-wait*).
> * Strings "mágicas" já viram **enums/consts** (ex.: `Wait::Complete`, `Selector::Css`).

---

# Tabela rápida de métodos (DX → BiDi)

```cpp
// Criação & navegação
bidicli.create_window()                // calls "browsingContext.create" {type:"window"} -> Async<ContextId>
bidicli.create_tab()                   // calls "browsingContext.create" {type:"tab"}    -> Async<ContextId>
bidicli.navigate(ctx, url, Wait::Complete)  // calls "browsingContext.navigate"

// Script
bidicli.evaluate(ctx, js, EvaluateOpts{.await_promise=true, .ownership=Ownership::Root})
                                       // calls "script.evaluate"
bidicli.call_function(ctx, fnDecl, args, EvaluateOpts{...})
                                       // calls "script.callFunction"

// DOM helpers (açúcar)
bidicli.wait_css(ctx, "#selector", 5s) // built on "script.callFunction" + Promise + MutationObserver
bidicli.wait_xpath(ctx, "//div[1]", 5s)

// Eventos (streams)
bidicli.events().logs()                // subscribes "log.entryAdded"
bidicli.events().network().before_request()
                                       // subscribes "network.beforeRequestSent"

// Input
bidicli.input().mouse(ctx).click(x,y)  // calls "input.dispatchMouseEvent"
bidicli.input().keys(ctx).type("hello")// calls "input.insertText" / "input.dispatchKeyEvent"

// Outros
bidicli.screenshot(ctx)                // calls "browsingContext.captureScreenshot"
bidicli.get_tree(ctx)                  // calls "browsingContext.getTree"
bidicli.close(ctx)                     // calls "browsingContext.close"
```

> Nomes como `Ownership::Root`, `Wait::Complete`, `Selector::Css` são **constantes/enum classes** para evitar “stringly typed”.

---

## 1) Pipeline mínimo: criar → navegar → esperar CSS

```cpp
{
    bidicli.create_window() // calls "browsingContext.create"
    .and_then([&](ContextId ctx) {
        return bidicli.navigate(ctx, "https://example.com", Wait::Complete); // calls "browsingContext.navigate"
    })
    .and_then([&](auto /*navRes*/) {
        return bidicli.wait_css(/*ctx preserved via capture*/, "#login", 5s); // built on "script.callFunction"
    })
    .finally([](std::optional<RemoteElement> ok, std::optional<EC> ec, std::exception_ptr ep) {
        if (ok) { std::print("OK: #login => {}\n", boost::json::serialize(ok->handle)); }
        else if (ec) { std::print("Erro: {}\n", ec->message()); }
        else { try { if (ep) { std::rethrow_exception(ep); } } catch (const std::exception& e) { std::print("Exceção: {}\n", e.what()); } }
    });
}
```

---

## 2) Versão coroutine: `co_await` direto

```cpp
{
    boost::asio::co_spawn(ex,
        [&]() -> boost::asio::awaitable<void> {
            auto ctx = co_await bidicli.create_window();                           // "browsingContext.create"
            co_await bidicli.navigate(ctx, "https://duckduckgo.com", Wait::Complete); // "browsingContext.navigate"
            auto el  = co_await bidicli.wait_css(ctx, "#searchbox_input", 5s);       // "script.callFunction" helper
            std::print("OK: {}\n", boost::json::serialize(el.handle));
            co_return;
        },
        boost::asio::detached);
}
```

---

## 3) Avaliar JS e pegar valor

```cpp
{
    auto js = R"js((() => document.title)())js";
    bidicli.evaluate(ctx, js, EvaluateOpts{.await_promise=true, .ownership=Ownership::None}) // calls "script.evaluate"
    .map([](const boost::json::object& r) {
        const auto& v = r.at("result").as_object().at("value");
        std::print("Title: {}\n", boost::json::serialize(v));
        return r;
    });
}
```

---

## 4) `call_function` com Promise + argumentos

```cpp
{
    auto fn = R"js(
        async function waitId(id, ms) {
            return await new Promise((resolve, reject) => {
                const el = document.getElementById(id);
                if (el) { resolve(el); return; }
                const obs = new MutationObserver(() => {
                    const e = document.getElementById(id);
                    if (e) { obs.disconnect(); resolve(e); }
                });
                obs.observe(document.body || document.documentElement, {childList:true, subtree:true});
                setTimeout(() => { obs.disconnect(); reject(new Error("Timeout: #" + id)); }, ms);
            });
        }
    )js";

    bidicli.call_function(ctx, fn, ArgList{"login", 3000}, EvaluateOpts{.await_promise=true, .ownership=Ownership::Root}) // calls "script.callFunction"
    .finally([](std::optional<boost::json::object> ok, std::optional<EC> ec, std::exception_ptr){
        if (ok) { std::print("Elemento via fn: {}\n", boost::json::serialize(ok->at("result"))); }
        if (ec) { std::print("Erro: {}\n", ec->message()); }
    });
}
```

---

## 5) Screenshot

```cpp
{
    bidicli.screenshot(ctx) // calls "browsingContext.captureScreenshot"
    .map([](const std::string& base64Png) {
        std::print("PNG (base64) size: {}\n", base64Png.size());
        return base64Png;
    });
}
```

---

## 6) Enviar texto e clique

```cpp
{
    bidicli.input().click(ctx, 200, 150)   // calls "input.dispatchMouseEvent"
    .and_then([&](auto){ return bidicli.input().type(ctx, "hello world"); }) // calls "input.insertText" / "input.dispatchKeyEvent"
    .finally([](auto, auto ec, auto){ if (ec) { std::print("input erro: {}\n", ec->message()); } });
}
```

---

## 7) Retry/backoff açucarado

```cpp
{
    retry([&]{
        return bidicli.navigate(ctx, "https://example.com", Wait::Complete); // calls "browsingContext.navigate"
    }, ex, RetryBackoffOpts{.max_tries=5, .initial_delay=200ms, .factor=2.0})
    .on_error([](EC ec){ std::print("Navigate falhou: {}\n", ec.message()); });
}
```

---

## 8) Timeout total `.within()`

```cpp
{
    bidicli.wait_css(ctx, "#login", 5s) // "script.callFunction" helper
    .within(ex, 7s)                     // wrapper: cancela se exceder 7s
    .finally([](auto ok, auto ec, auto){
        if (ec) { std::print("Timeout total: {}\n", ec->message()); }
    });
}
```

---

## 9) “Corrida” (primeiro que completar vence)

```cpp
{
    auto A = bidicli.wait_css(ctx, "#ok", 5s);                    // "script.callFunction" helper
    auto B = bidicli.events().logs().first(10s);                  // subscribes "log.entryAdded", resolve first event
    asyncx::first_of(std::move(A), std::move(B))                  // combinator utilitário
    .finally([](auto ok, auto ec, auto){
        if (ok) {
            std::visit([](auto&& v){
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, RemoteElement>) { std::print("Ganhou o elemento\n"); }
                else { std::print("Ganhou o log: {}\n", v.text); }
            }, *ok);
        }
    });
}
```

---

## 10) Streams de eventos com filtros

```cpp
{
    auto errors = bidicli.events().logs()                          // subscribes "log.entryAdded"
                 .filter([](const LogEntry& e){ return e.level=="error"; })
                 .take_until(bidicli.stop_token());

    errors.subscribe([](const LogEntry& e){ std::print("[console.error] {}\n", e.text); });
}
```

---

## 11) RAII de contexto

```cpp
{
    AutoContext win{ bidicli.create_window() }; // wraps "browsingContext.create" + "browsingContext.close" no dtor
    bidicli.navigate(win.ctx(), "https://example.com", Wait::Complete); // "browsingContext.navigate"
    // Ao sair do escopo, fecha a janela (DX segura).
}
```

---

## 12) Batch de comandos

```cpp
{
    bidicli.batch({
        BiDiCall{"browsingContext.navigate", {{"context",ctx},{"url","https://a"},{"wait","none"}}},
        BiDiCall{"browsingContext.navigate", {{"context",ctx},{"url","https://b"},{"wait","none"}}},
    }) // calls N vezes (com preservação de ordem)
    .finally([](auto ok, auto ec, auto){ if (ok) { std::print("Batch OK: {}\n", ok->size()); } });
}
```

---

## 13) Child contexts (frames)

```cpp
{
    bidicli.get_tree(ctx) // calls "browsingContext.getTree"
    .map([](const boost::json::object& tree){
        std::print("Tree: {}\n", boost::json::serialize(tree));
        return tree;
    });
}
```

---

## 14) Erro “rico”: mensagem do JS (exceptionDetails)

```cpp
{
    bidicli.wait_css(ctx, "#does-not-exist", 2s) // "script.callFunction" helper
    .on_error([](EC ec, const RichError* rich){
        if (rich != nullptr) { std::print("Erro JS: {}\n", rich->message); } // ex: “Timeout reached: …”
        else { std::print("Erro: {}\n", ec.message()); }
    });
}
```

---

## 15) Compondo com `map` / `and_then`

```cpp
{
    bidicli.create_tab() // "browsingContext.create"
    .and_then([&](ContextId ctx) {
        return bidicli.navigate(ctx, "data:text/html,ok", Wait::Complete); // "browsingContext.navigate"
    })
    .map([](auto){ return 42; })
    .finally([](auto ok, auto, auto){ if (ok) { std::print("Ans: {}\n", *ok); } });
}
```

---

## 16) Cancelamento com `std::stop_token`

```cpp
{
    std::stop_source cancel;
    bidicli.wait_css(ctx, "#slow", 30s) // "script.callFunction" helper
    .cancellable_with(cancel.get_token())
    .finally([](auto, auto ec, auto){
        if (ec && *ec == boost::system::errc::operation_canceled) { std::print("Cancelado.\n"); }
    });

    // Em outro momento:
    cancel.request_stop();
}
```

---

## 17) `co_await` + retry

```cpp
{
    boost::asio::co_spawn(ex,
        [&]() -> boost::asio::awaitable<void> {
            auto ctx = co_await bidicli.create_window(); // "browsingContext.create"
            co_await retry([&]{ return bidicli.navigate(ctx, "https://unstable", Wait::Complete); }, ex,
                           {.max_tries=4,.initial_delay=150ms,.factor=1.8}); // "browsingContext.navigate"
            co_return;
        },
        boost::asio::detached);
}
```

---

## 18) “goto_and_wait”: açúcar

```cpp
{
    bidicli.goto_and_wait(ctx, "https://app.local", Selector::Css, "#ready", 6s) // navigate + wait; "browsingContext.navigate" + helper
    .finally([](auto ok, auto ec, auto){ if (ok) { std::print("App pronto.\n"); } });
}
```

---

## 19) Paralelismo: `all` (join)

```cpp
{
    asyncx::all(
        bidicli.wait_css(ctx, "#a", 5s), // helper
        bidicli.wait_css(ctx, "#b", 5s)  // helper
    )
    .finally([](auto ok, auto ec, auto){
        if (ok) { std::print("A e B prontos.\n"); }
    });
}
```

---

## 20) DSL de seletor

```cpp
{
    bidicli.wait(ctx, Selector::Css,   "#login", 5s);  // helper => "script.callFunction"
    bidicli.wait(ctx, Selector::Xpath, "//button[@id='login']", 5s);
}
```

---

### Observações finais (para orientar a LLM):

* **Cada método comenta o comando BiDi exato**; o wrapper serve só de camada DX (lazy, zero busy-wait, *error-first*, *cancel-friendly*).
* **Sem polling**: use `steady_timer`, *strand*, *stop_token*.
* **Propague `exceptionDetails`** como `RichError` para mensagens JS úteis.
* **Constantes/Enums** para `wait`, `ownership`, `selector kind` e afins (evitar strings).
* **Açúcares** (`goto_and_wait`, `within`, `first_of`, `all`, `retry`) são *wrappers* finos sobre teu `Async<T>`.
* **RAII** (`AutoContext`) garante que recursos do browser sejam limpos no dtor (sem leaks).

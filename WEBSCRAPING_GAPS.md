# WebScraping Implementation Gaps

Este documento complementa `SPEC_TODO.md` com foco específico em funcionalidades **críticas para webscraping** que estão faltando ou parcialmente implementadas.

---

## Índice

1. [Sumário Executivo](#sumário-executivo)
2. [Comandos BiDi Faltantes (P0/P1)](#comandos-bidi-faltantes)
3. [Tipos W3C Não Implementados](#tipos-w3c-não-implementados)
4. [APIs de Alto Nível para Scraping](#apis-de-alto-nível-para-scraping)
5. [Helpers e Utilitários](#helpers-e-utilitários)
6. [Plano de Ação Priorizado](#plano-de-ação-priorizado)

---

## Sumário Executivo

### Status Atual
✅ **Implementado:**
- WebSocket BiDi transport com multiplexação
- Comandos básicos: `navigate`, `create`, `evaluate`, `close`
- Sistema async com `asyncx::Async<T>`
- Subscription RAII para eventos

❌ **Faltando (crítico para webscraping):**
- `browsingContext.locateNodes` (localizar elementos)
- `input.performActions` (click, type, scroll)
- `browsingContext.captureScreenshot`
- Tipos: `Locator`, `RemoteValue` completo, `Cookie`
- Helpers: `wait_for_element`, `wait_for_condition`, `retry_until`

### Impacto
Sem esses comandos, usuários precisam usar `script.evaluate` com JavaScript manual para:
- Localizar elementos (ineficiente, sem retry nativo)
- Interagir com elementos (não dispara eventos do browser)
- Esperar condições (busy-wait ou polling manual)

---

## Comandos BiDi Faltantes

### P0 - Crítico (impossível fazer scraping avançado sem isso)

#### 1. `browsingContext.locateNodes` ⭐⭐⭐
**Status:** ❌ Não implementado

**Spec:** [7.3.4.5 browsingContext.locateNodes](https://w3c.github.io/webdriver-bidi/#command-browsingContext-locateNodes)

**Descrição:**
Localiza elementos no DOM usando vários tipos de locators (CSS, XPath, innerText, accessibility).

**Tipos necessários:**
```cpp
namespace bidi::browsing_context {

// Locator types (união)
struct CssLocator { std::string value; };
struct XPathLocator { std::string value; };
struct InnerTextLocator {
    std::string value;
    std::optional<bool> ignoreCase;
    enum class MatchType { full, partial };
    std::optional<MatchType> matchType;
    std::optional<uint64_t> maxDepth;
};
struct AccessibilityLocator {
    std::optional<std::string> name;
    std::optional<std::string> role;
};

using Locator = std::variant<
    CssLocator,
    XPathLocator,
    InnerTextLocator,
    AccessibilityLocator
>;

// Request params
struct LocateNodesParams {
    std::string context;            // browsing context ID
    Locator locator;                // selector
    std::optional<uint64_t> maxNodeCount;
    std::optional<std::string> ownership; // script.ResultOwnership
    std::optional<std::vector<std::string>> sandbox;
    std::optional<std::string> serializationOptions;
    std::optional<std::string> startNodes; // script.SharedReference[]
};

// Response
struct LocateNodesResult {
    std::vector<std::string> nodes; // script.RemoteValue[] (NodeRemoteValue)
};

}
```

**Arquivos a modificar:**
- `include/bidi/commands.hpp` - adicionar `browsingContext::locate_nodes()`
- `src/bidi_commands.cpp` - implementar builder
- `include/bidi/client.hpp` - adicionar `Client::locate_nodes()` → `Task<std::vector<RemoteValue>>`
- `src/bidi_client.cpp` - implementar método

**Teste:**
```cpp
TEST(BrowsingContext, LocateNodesCSS) {
    auto nodes = co_await client->locate_nodes(ctx, CssLocator{"#submit-btn"})();
    EXPECT_GE(nodes.size(), 1);
}

TEST(BrowsingContext, LocateNodesInnerText) {
    auto nodes = co_await client->locate_nodes(
        ctx,
        InnerTextLocator{
            .value = "Click me",
            .ignoreCase = true,
            .matchType = InnerTextLocator::MatchType::partial
        }
    )();
    EXPECT_GE(nodes.size(), 1);
}
```

**Prioridade:** P0 - Blocker para scraping real

---

#### 2. `input.performActions` ⭐⭐⭐
**Status:** ❌ Não implementado

**Spec:** [7.4 input Module](https://w3c.github.io/webdriver-bidi/#module-input)

**Descrição:**
Simula interações do usuário: click, type, scroll, drag-and-drop.

**Tipos necessários:**
```cpp
namespace bidi::input {

// Action types
struct PauseAction {
    std::optional<uint64_t> duration; // milissegundos
};

struct KeyDownAction {
    std::string value; // single character or key name
};

struct KeyUpAction {
    std::string value;
};

struct PointerDownAction {
    uint8_t button; // 0=left, 1=middle, 2=right
    std::optional<uint32_t> width;
    std::optional<uint32_t> height;
    std::optional<double> pressure;
};

struct PointerUpAction {
    uint8_t button;
};

struct PointerMoveAction {
    int64_t x;
    int64_t y;
    std::optional<uint64_t> duration;
    enum class Origin { viewport, pointer };
    std::optional<Origin> origin;
};

struct WheelScrollAction {
    int64_t deltaX;
    int64_t deltaY;
    std::optional<uint64_t> duration;
};

// Action sequence
struct ActionSequence {
    std::string id; // identificador da fonte (ex: "mouse1", "key1")
    enum class Type { key, pointer, wheel, none };
    Type type;
    std::vector<std::variant<
        PauseAction,
        KeyDownAction,
        KeyUpAction,
        PointerDownAction,
        PointerUpAction,
        PointerMoveAction,
        WheelScrollAction
    >> actions;
};

// Request
struct PerformActionsParams {
    std::string context;
    std::vector<ActionSequence> actions;
};

}
```

**Arquivos a modificar:**
- `include/bidi/commands.hpp` - namespace `input` com builders
- `include/bidi/client.hpp` - adicionar `Client::click()`, `Client::type()`, `Client::scroll()`
- Criar `include/bidi/input.hpp` para tipos de ação
- Criar `src/bidi_input.cpp` com implementação

**API de alto nível desejada:**
```cpp
// Helpers simples
auto click(Client& client, std::string_view context,
           std::string_view element_selector) -> asyncx::Async<void>;

auto type_text(Client& client, std::string_view context,
               std::string_view element_selector,
               std::string_view text) -> asyncx::Async<void>;

auto scroll_to_element(Client& client, std::string_view context,
                       std::string_view element_selector) -> asyncx::Async<void>;
```

**Prioridade:** P0 - Blocker para automação interativa

---

#### 3. `browsingContext.captureScreenshot` ⭐⭐
**Status:** ⚠️ Parcialmente declarado (bidi_methods.hpp), não implementado

**Spec:** [7.3.4.1 browsingContext.captureScreenshot](https://w3c.github.io/webdriver-bidi/#command-browsingContext-captureScreenshot)

**Tipos necessários:**
```cpp
namespace bidi::browsing_context {

struct BoxClipRectangle {
    double x, y, width, height;
};

struct ElementClipRectangle {
    std::string sharedId; // script.SharedReference
};

using ClipRectangle = std::variant<BoxClipRectangle, ElementClipRectangle>;

struct ImageFormat {
    std::string type; // "image/png" | "image/jpeg"
    std::optional<double> quality; // 0.0-1.0 para JPEG
};

struct CaptureScreenshotParams {
    std::string context;
    std::optional<ClipRectangle> clip;
    std::optional<ImageFormat> format;
    std::optional<std::string> origin; // "viewport" | "document"
};

struct CaptureScreenshotResult {
    std::string data; // base64 encoded image
};

}
```

**Arquivos a modificar:**
- `include/bidi/commands.hpp` - adicionar `capture_screenshot()`
- `include/bidi/client.hpp` - `Client::capture_screenshot()` → `Task<std::string>`

**Prioridade:** P1 - Importante para debugging e visual testing

---

### P1 - Importante (melhora UX significativamente)

#### 4. `browsingContext.print` ⭐
**Status:** ❌ Não implementado

**Spec:** [7.3.4.7 browsingContext.print](https://w3c.github.io/webdriver-bidi/#command-browsingContext-print)

**Use case:** Gerar PDFs de páginas scrapadas

**Tipos:**
```cpp
struct PrintParams {
    std::string context;
    std::optional<std::string> background;  // bool
    std::optional<double> margin_top;
    std::optional<double> margin_bottom;
    std::optional<double> margin_left;
    std::optional<double> margin_right;
    std::optional<std::string> orientation; // "portrait" | "landscape"
    std::optional<std::string> page_ranges;
    std::optional<double> scale;
    std::optional<bool> shrinkToFit;
};

struct PrintResult {
    std::string data; // base64 encoded PDF
};
```

**Prioridade:** P1

---

#### 5. `browsingContext.setViewport` ⭐
**Status:** ⚠️ Declarado em bidi_methods.hpp, não implementado

**Spec:** [7.3.4.8 browsingContext.setViewport](https://w3c.github.io/webdriver-bidi/#command-browsingContext-setViewport)

**Use case:** Mobile scraping, responsive testing

```cpp
struct Viewport {
    uint64_t width;
    uint64_t height;
};

struct SetViewportParams {
    std::string context;
    std::optional<Viewport> viewport; // null = reset
    std::optional<double> devicePixelRatio;
};
```

**Prioridade:** P1

---

#### 6. `storage.getCookies` / `storage.setCookie` ⭐⭐
**Status:** ❌ Não implementado

**Spec:** [7.9 storage Module](https://w3c.github.io/webdriver-bidi/#module-storage)

**Use case:** Login persistence, session management

**Tipos:**
```cpp
namespace bidi::storage {

enum class SameSite { none, lax, strict };

struct Cookie {
    std::string name;
    std::string value;      // BytesValue na spec (string ou base64)
    std::string domain;
    std::string path;
    uint64_t size;
    bool httpOnly;
    bool secure;
    SameSite sameSite;
    std::optional<uint64_t> expiry; // epoch seconds
};

struct GetCookiesParams {
    std::optional<std::string> filter; // CookieFilter
    std::optional<std::string> partition; // StorageKeyPartitionDescriptor
};

struct GetCookiesResult {
    std::vector<Cookie> cookies;
};

struct SetCookieParams {
    Cookie cookie;
    std::optional<std::string> partition;
};

}
```

**Prioridade:** P1 - Essencial para scraping autenticado

---

### P2 - Nice to Have

#### 7. `network.addIntercept` / `network.continueRequest` ⭐
**Status:** ⚠️ Parcial (`continue_request` e `fail_request` existem)

**Use case:** Mock responses, modificar headers, bloquear recursos

**Tipos faltantes:**
```cpp
namespace bidi::network {

enum class InterceptPhase {
    beforeRequestSent,
    responseStarted,
    authRequired
};

struct AddInterceptParams {
    std::vector<InterceptPhase> phases;
    std::optional<std::vector<std::string>> contexts;
    std::optional<std::vector<std::string>> urlPatterns;
};

struct AddInterceptResult {
    std::string intercept; // id opaco
};

}
```

**Arquivos a modificar:**
- `include/bidi/commands.hpp` - namespace `network` (já existe parcial)
- Adicionar `add_intercept()`, `remove_intercept()`

**Prioridade:** P2

---

## Tipos W3C Não Implementados

### P0 - Essencial

#### 1. `script.RemoteValue` (completo)
**Status:** ⚠️ Usado mas não tipado fortemente

**Necessário para:** `locateNodes` response, `evaluate` response

**Tipos faltantes:**
```cpp
namespace bidi::script {

// Base types
using Handle = std::string;
using InternalId = std::string;

// Remote value variants
struct PrimitiveValue {
    enum class Type { undefined, null_type, string, number, boolean, bigint };
    Type type;
    std::optional<std::string> value; // para string/bigint
    std::optional<double> number;     // para number
    std::optional<bool> boolean;      // para boolean
};

struct SymbolRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internalId;
};

struct ArrayRemoteValue {
    std::optional<Handle> handle;
    std::optional<std::vector<RemoteValue>> value; // recursivo!
};

struct ObjectRemoteValue {
    std::optional<Handle> handle;
    std::optional<std::map<std::string, RemoteValue>> value;
};

struct NodeRemoteValue {
    std::optional<Handle> handle;
    std::optional<InternalId> internalId;
    std::optional<std::string> nodeType;      // "element", "text", etc
    std::optional<uint64_t> childNodeCount;
    std::optional<std::string> localName;     // tag name
    std::optional<std::string> namespaceURI;
    std::optional<std::string> nodeValue;
    std::optional<std::map<std::string, std::string>> attributes;
};

using RemoteValue = std::variant<
    PrimitiveValue,
    SymbolRemoteValue,
    ArrayRemoteValue,
    ObjectRemoteValue,
    NodeRemoteValue
    // ... + Function, RegExp, Date, Map, Set, Error, etc
>;

}
```

**Arquivos:**
- Criar `include/bidi/remote_value.hpp`
- Criar `src/bidi_remote_value.cpp` com parsers/serializers

**Prioridade:** P0

---

#### 2. `script.SharedReference`
**Status:** ❌ Não implementado

**Usado em:** `locateNodes` para passar elementos como startNodes

```cpp
namespace bidi::script {

using SharedId = std::string;

struct SharedReference {
    SharedId sharedId;
    std::optional<Handle> handle;
};

struct RemoteObjectReference {
    Handle handle;
    std::optional<SharedId> sharedId;
};

using RemoteReference = std::variant<SharedReference, RemoteObjectReference>;

}
```

**Prioridade:** P0

---

### P1 - Importante

#### 3. `browsingContext.Info` (completo)
**Status:** ⚠️ Uso parcial (só context ID)

**Usado em:** `getTree` response

```cpp
namespace bidi::browsing_context {

struct Info {
    std::string context;                    // ID do contexto
    std::string url;
    std::string userContext;
    std::optional<std::string> parent;
    std::optional<std::string> originalOpener;
    std::vector<Info> children;             // recursivo
    std::string clientWindow;
};

}
```

**Prioridade:** P1

---

## APIs de Alto Nível para Scraping

### Helpers Assíncronos (asyncx-based)

**Arquivo:** `include/bidi/scraping_helpers.hpp`

```cpp
namespace bidi::helpers {

// Wait for element to exist
asyncx::Async<script::RemoteValue>
wait_for_element(std::shared_ptr<Client> client,
                 std::string_view context,
                 browsing_context::Locator locator,
                 std::chrono::milliseconds timeout = 10s);

// Wait for element to be visible
asyncx::Async<script::RemoteValue>
wait_for_visible(std::shared_ptr<Client> client,
                 std::string_view context,
                 std::string_view element_selector,
                 std::chrono::milliseconds timeout = 10s);

// Wait for condition (polling)
template<typename Predicate>
asyncx::Async<void>
wait_until(std::shared_ptr<Client> client,
           std::string_view context,
           Predicate condition,
           std::chrono::milliseconds timeout = 10s,
           std::chrono::milliseconds poll_interval = 100ms);

// Retry operation with exponential backoff
template<typename Operation>
auto retry_with_backoff(Operation op,
                        int max_retries = 5,
                        std::chrono::milliseconds initial_delay = 100ms)
    -> decltype(op());

// Wait for page to be idle (no pending XHR/fetch)
asyncx::Async<void>
wait_for_page_idle(std::shared_ptr<Client> client,
                   std::string_view context,
                   std::chrono::milliseconds timeout = 30s);

// Scroll element into view
asyncx::Async<void>
scroll_into_view(std::shared_ptr<Client> client,
                 std::string_view context,
                 std::string_view element_selector);

// Extract text from multiple elements in parallel
asyncx::Async<std::vector<std::string>>
extract_texts(std::shared_ptr<Client> client,
              std::string_view context,
              std::vector<std::string> selectors);

}
```

**Prioridade:** P0 - Tornam o scraping prático

---

## Helpers e Utilitários

### 1. Conversões de tipos

**Arquivo:** `include/bidi/type_conversions.hpp`

```cpp
namespace bidi::conversions {

// RemoteValue → C++ primitives
std::optional<std::string> to_string(const script::RemoteValue& rv);
std::optional<double> to_number(const script::RemoteValue& rv);
std::optional<bool> to_bool(const script::RemoteValue& rv);

// C++ → LocalValue (para argumentos)
boost::json::object to_local_value(std::string_view str);
boost::json::object to_local_value(double num);
boost::json::object to_local_value(bool b);
boost::json::object to_local_value(std::nullptr_t);

// Locator helpers
browsing_context::CssLocator css(std::string_view selector);
browsing_context::XPathLocator xpath(std::string_view expression);
browsing_context::InnerTextLocator text(std::string_view content,
                                         bool ignore_case = false);

}
```

---

### 2. Fluent API (opcional, P2)

**Arquivo:** `include/bidi/fluent.hpp`

```cpp
namespace bidi::fluent {

class ElementHandle {
    std::shared_ptr<Client> client_;
    std::string context_;
    script::RemoteValue value_;

public:
    // Encadeamento fluente
    asyncx::Async<void> click();
    asyncx::Async<void> type(std::string_view text);
    asyncx::Async<std::string> get_text();
    asyncx::Async<std::string> get_attribute(std::string_view name);
    asyncx::Async<bool> is_visible();
    asyncx::Async<ElementHandle> find_child(std::string_view selector);
};

class PageHandle {
    std::shared_ptr<Client> client_;
    std::string context_;

public:
    asyncx::Async<ElementHandle> find(browsing_context::Locator locator);
    asyncx::Async<std::vector<ElementHandle>> find_all(browsing_context::Locator locator);
    asyncx::Async<void> navigate(std::string_view url);
    asyncx::Async<std::string> screenshot();
    asyncx::Async<void> wait_for_load();
};

}
```

---

## Plano de Ação Priorizado

### Phase 1: Core Scraping (P0) - 2-3 semanas

**Semana 1-2:**
1. ✅ Implementar `browsingContext.locateNodes`
   - Tipos: `Locator` variants
   - Command builder
   - Client API: `locate_nodes()`
   - Testes: CSS, XPath, InnerText

2. ✅ Implementar `script.RemoteValue` completo
   - Criar `include/bidi/remote_value.hpp`
   - Parser para todos os subtipos
   - Conversões para C++ primitives

**Semana 2-3:**
3. ✅ Implementar `input.performActions` (básico)
   - Tipos de ação: Pointer, Key
   - Builders para click, type, scroll
   - Client helpers: `click()`, `type()`, `scroll()`

4. ✅ Helpers de scraping
   - `wait_for_element()`
   - `wait_until()`
   - `retry_with_backoff()`

**Entregáveis:**
- Exemplo funcional: `examples/scraping/basic_form_fill.cpp`
- Testes de integração: localizar + clicar + extrair texto

---

### Phase 2: Avançado (P1) - 1-2 semanas

**Semana 4:**
1. ✅ `browsingContext.captureScreenshot`
2. ✅ `storage.getCookies` / `storage.setCookie`
3. ✅ `browsingContext.setViewport`

**Semana 5:**
4. ✅ Helpers avançados:
   - `wait_for_page_idle()`
   - `scroll_into_view()`
   - `extract_texts()` (paralelo)

**Entregáveis:**
- Exemplo: `examples/scraping/e_commerce_scraper.cpp`
- Exemplo: `examples/scraping/authenticated_scraping.cpp`

---

### Phase 3: Polimento (P2) - 1 semana

1. ✅ `browsingContext.print` (PDF generation)
2. ✅ `network.addIntercept` (request mocking)
3. ✅ Fluent API (opcional)

**Entregáveis:**
- Documentação completa de scraping patterns
- Performance benchmarks

---

## Checklist de Implementação

### Comandos
- [ ] `browsingContext.locateNodes`
- [ ] `input.performActions`
- [ ] `browsingContext.captureScreenshot`
- [ ] `browsingContext.print`
- [ ] `browsingContext.setViewport`
- [ ] `storage.getCookies`
- [ ] `storage.setCookie`
- [ ] `network.addIntercept`
- [ ] `network.removeIntercept`

### Tipos
- [ ] `Locator` (CSS, XPath, InnerText, Accessibility)
- [ ] `RemoteValue` completo (com todos os subtipos)
- [ ] `SharedReference`
- [ ] `NodeRemoteValue`
- [ ] `ActionSequence` (input)
- [ ] `Cookie`
- [ ] `Viewport`
- [ ] `ClipRectangle`

### Helpers
- [ ] `wait_for_element()`
- [ ] `wait_for_visible()`
- [ ] `wait_until()`
- [ ] `retry_with_backoff()`
- [ ] `wait_for_page_idle()`
- [ ] `scroll_into_view()`
- [ ] `extract_texts()`

### Exemplos
- [ ] `basic_form_fill.cpp`
- [ ] `e_commerce_scraper.cpp`
- [ ] `authenticated_scraping.cpp`
- [ ] `spa_infinite_scroll.cpp`

---

## Arquivos a Criar/Modificar

### Novos arquivos
```
include/bidi/
├── locator.hpp              # Tipos Locator
├── remote_value.hpp         # RemoteValue completo
├── input.hpp                # Tipos input.performActions
├── scraping_helpers.hpp     # Helpers async para scraping
├── type_conversions.hpp     # Conversões RemoteValue ↔ C++
└── fluent.hpp               # API fluente (opcional)

src/
├── bidi_locator.cpp
├── bidi_remote_value.cpp
├── bidi_input.cpp
├── bidi_scraping_helpers.cpp
└── bidi_type_conversions.cpp

examples/scraping/
├── basic_form_fill.cpp
├── e_commerce_scraper.cpp
├── authenticated_scraping.cpp
└── spa_infinite_scroll.cpp

tests/
├── locator_test.cpp
├── remote_value_test.cpp
├── input_test.cpp
└── scraping_helpers_test.cpp
```

### Arquivos a modificar
```
include/bidi/commands.hpp    # Adicionar builders
include/bidi/client.hpp      # Adicionar métodos de alto nível
src/bidi_commands.cpp        # Implementar builders
src/bidi_client.cpp          # Implementar client methods
```

---

## Referências

- [W3C WebDriver BiDi Spec](https://w3c.github.io/webdriver-bidi/)
- TIPOS_W3C.md (este repositório)
- SPEC_TODO.md (gaps técnicos)
- CLAUDE.md (instruções do projeto)

---

**Gerado em:** 2025-10-03
**Próxima revisão:** Após implementação de Phase 1

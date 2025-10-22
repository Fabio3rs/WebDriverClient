especificação “WebDriver BiDi” mais recente no W3C e transformei os tipos principais (os “types” do protocolo WebSocket) em modelos C++. Abaixo estão: o envelope de mensagens do BiDi, e os tipos mais usados dos módulos `session`, `browsingContext`, `script` e `network`. Usei `std::optional` para campos opcionais e `std::variant` onde o BiDi usa uniões. (Se quiser, troco `std::variant` por ponteiros base + herança.)

> As referências da spec estão logo após cada grupo de tipos.

---

### Envelope de mensagens (WebSocket)

```cpp
// =========================
// Núcleo do protocolo BiDi
// =========================
#include <string>
#include <optional>
#include <variant>
#include <vector>
#include <map>

namespace bidi {

// --- Códigos de erro padronizados do BiDi (subset fiel à spec) ---
enum class ErrorCode {
  InvalidArgument,
  InvalidSelector,
  InvalidSessionId,
  InvalidWebExtension,
  MoveTargetOutOfBounds,
  NoSuchAlert,
  NoSuchNetworkCollector,
  NoSuchElement,
  NoSuchFrame,
  NoSuchHandle,
  NoSuchHistoryEntry,
  NoSuchIntercept,
  NoSuchNetworkData,
  NoSuchNode,
  NoSuchRequest,
  NoSuchScript,
  NoSuchStoragePartition,
  NoSuchUserContext,
  NoSuchWebExtension,
  SessionNotCreated,
  UnableToCaptureScreen,
  UnableToCloseBrowser,
  UnableToSetCookie,
  UnableToSetFileInput,
  UnavailableNetworkData,
  UnderspecifiedStoragePartition,
  UnknownCommand,
  UnknownError,
  UnsupportedOperation
};

// --- Pedido de comando enviado pelo cliente (local end) ---
struct CommandRequest {
  // inteiro arbitrário controlado pelo "local end"
  long long id;
  // "module.method" (ex.: "browsingContext.navigate")
  std::string method;
  // parâmetros específicos do comando (use seu próprio modelo em vez de JSON cru se preferir)
  std::map<std::string, std::string> params_raw; // placeholder
};

// --- Respostas possíveis do servidor (remote end) ---
struct SuccessResponse {
  std::string type = "success";
  long long id;
  // corpo específico do comando — modele com structs por comando
  std::map<std::string, std::string> result_raw; // placeholder
};

struct ErrorResponse {
  std::string type = "error";
  long long id;
  ErrorCode error;
  std::string message;
  std::optional<std::string> stacktrace;
};

// --- Eventos emitidos pelo servidor ---
struct EventMessage {
  std::string type = "event";
  std::string method; // "module.eventName"
  std::map<std::string, std::string> params_raw; // payload do evento
};

using Message = std::variant<SuccessResponse, ErrorResponse, EventMessage>;

} // namespace bidi
```

Isto reflete o modelo do BiDi: comandos assíncronos identificados por `id`, respostas de sucesso/erro, e eventos com `method` + `params`. A camada de transporte é WebSocket. ([W3C][1])

---

### Módulo `session` (tipos essenciais)

```cpp
namespace bidi::session {

// ---- Tipos de capacidades (handshake) ----
struct CapabilityRequest {
  std::optional<bool> acceptInsecureCerts;
  std::optional<std::string> browserName;
  std::optional<std::string> browserVersion;
  std::optional<std::string> platformName;

  // Proxy
  struct SocksProxyConfiguration {
    std::string socksProxy;
    uint8_t socksVersion;
  };

  struct ProxyConfiguration {
    enum class Type { Autodetect, Direct, Manual, Pac, System };
    Type proxyType;

    // Manual
    std::optional<std::string> httpProxy;
    std::optional<std::string> sslProxy;
    std::optional<SocksProxyConfiguration> socks;
    std::optional<std::vector<std::string>> noProxy;

    // PAC
    std::optional<std::string> proxyAutoconfigUrl;
  };

  std::optional<ProxyConfiguration> proxy;

  // Prompts
  enum class UserPromptHandlerType { Accept, Dismiss, Ignore };
  struct UserPromptHandler {
    std::optional<UserPromptHandlerType> alert;
    std::optional<UserPromptHandlerType> beforeUnload;
    std::optional<UserPromptHandlerType> confirm;
    std::optional<UserPromptHandlerType> def;   // "default"
    std::optional<UserPromptHandlerType> file;  // file picker
    std::optional<UserPromptHandlerType> prompt;
  };
  std::optional<UserPromptHandler> unhandledPromptBehavior;
};

struct CapabilitiesRequest {
  std::optional<CapabilityRequest> alwaysMatch;
  std::optional<std::vector<CapabilityRequest>> firstMatch;
};

// Inscrição em eventos
using Subscription = std::string;

struct SubscriptionRequest {
  std::vector<std::string> events;
  std::optional<std::vector<std::string>> contexts;     // browsingContext ids
  std::optional<std::vector<std::string>> userContexts; // browser user contexts
};

struct UnsubscribeByIDRequest {
  std::vector<Subscription> subscriptions;
};

struct UnsubscribeByAttributesRequest {
  std::vector<std::string> events;
};

} // namespace bidi::session
```

Estes mapeiam os tipos `CapabilitiesRequest`, `CapabilityRequest`, `ProxyConfiguration`, `UserPromptHandler/Type` e os de inscrição/remoção de eventos da spec. ([W3C][1])

---

### Módulo `browsingContext` (tipos de contexto, navegação e localização de nós)

```cpp
namespace bidi::browsingContext {

// Um ID opaco de context (navegável)
using BrowsingContext = std::string;

// Informações de um contexto
struct Info {
  // lista de filhos (ou null na spec; aqui, use vetor vazio para "null")
  std::vector<Info> children;
  std::string clientWindow;            // browser.ClientWindow (id opaco)
  BrowsingContext context;             // id do contexto
  std::optional<BrowsingContext> originalOpener;
  std::string url;
  std::string userContext;             // browser.UserContext (id opaco)
  std::optional<BrowsingContext> parent;
};

// Estratégias de localização de nós
struct AccessibilityLocator { std::optional<std::string> name, role; };
struct CssLocator { std::string value; };
struct ContextLocator { BrowsingContext context; };
struct InnerTextLocator {
  std::string value;
  std::optional<bool> ignoreCase;
  enum class MatchType { Full, Partial };
  std::optional<MatchType> matchType;
  std::optional<uint64_t> maxDepth;
};
struct XPathLocator { std::string value; };

using Locator = std::variant<
  AccessibilityLocator, CssLocator, ContextLocator, InnerTextLocator, XPathLocator>;

// Navegação
using Navigation = std::string;

// Snapshot de navegação (para eventos/retornos)
struct NavigationInfo {
  BrowsingContext context;
  std::optional<Navigation> navigation; // pode ser null
  uint64_t timestamp;                   // epoch millis
  std::string url;
};

// Quando considerar “concluído”
enum class ReadinessState { None, Interactive, Complete };

// Tipos de prompts do usuário
enum class UserPromptType { Alert, BeforeUnload, Confirm, Prompt };

// Captura de screenshot (tipos auxiliares)
struct ElementClipRectangle { std::string elementSharedRef; /* script.SharedReference */ };
struct BoxClipRectangle { double x, y, width, height; };
using ClipRectangle = std::variant<ElementClipRectangle, BoxClipRectangle>;

struct ImageFormat { std::string type; std::optional<double> quality; };

} // namespace bidi::browsingContext
```

Campos conforme `Info`, `Locator`, `Navigation*`, `ReadinessState`, `UserPromptType`, e tipos usados por `captureScreenshot` (`ImageFormat`, `ClipRectangle`). ([W3C][1])

---

### Módulo `script` (valores JS remotos, exceções, realms)

```cpp
namespace bidi::script {

// Identificadores / referências
using Handle     = std::string; // válido apenas no Realm correspondente
using InternalId = std::string; // id durante a serialização para objetos duplicados
using Realm      = std::string; // id opaco

// Compartilhamento de referências
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

// Tipos de realm
enum class RealmType {
  Window, DedicatedWorker, SharedWorker, ServiceWorker,
  Worker, PaintWorklet, AudioWorklet, Worklet
};

// Info de realm (varia conforme tipo)
struct BaseRealmInfo { Realm realm; std::string origin; };
// Para simplificar, exponho apenas a base; variantes específicas (WindowRealmInfo etc.)
// podem estender com campos adicionais conforme necessário.
struct RealmInfo { BaseRealmInfo base; RealmType type; };

// Valores remotos (espelho do runtime JS)
struct SymbolRemoteValue { std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct ArrayRemoteValue  { std::optional<Handle> handle; std::optional<InternalId> internalId; /* std::vector<RemoteValue> value; */ };
struct ObjectRemoteValue { std::optional<Handle> handle; std::optional<InternalId> internalId; /* mapping[string|RemoteValue -> RemoteValue] */ };
struct FunctionRemoteValue{ std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct RegExpRemoteValue { std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct DateRemoteValue   { std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct MapRemoteValue    { std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct SetRemoteValue    { std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct WeakMapRemoteValue{ std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct WeakSetRemoteValue{ std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct GeneratorRemoteValue{ std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct ErrorRemoteValue  { std::optional<Handle> handle; std::optional<InternalId> internalId; };
struct ProxyRemoteValue  { std::optional<Handle> handle; std::optional<InternalId> internalId; };
// plus WindowProxy/Node/TypedArray/HTMLCollection/NodeList... idem

// Primitivos "JSON-representable" + especiais (NaN, -0, Infinity...) ficam na union abaixo
struct PrimitiveProtocolValue {
  // Você pode modelar como std::variant<std::nullptr_t, bool, double, std::string, SpecialNumber>
  // conforme §7.6.3.10. Omitido aqui para brevidade.
};

using RemoteValue = std::variant<
  PrimitiveProtocolValue,
  SymbolRemoteValue, ArrayRemoteValue, ObjectRemoteValue, FunctionRemoteValue,
  RegExpRemoteValue, DateRemoteValue, MapRemoteValue, SetRemoteValue,
  WeakMapRemoteValue, WeakSetRemoteValue, GeneratorRemoteValue, ErrorRemoteValue,
  ProxyRemoteValue
  // ... e demais tipos listados na spec (WindowProxyRemoteValue, NodeRemoteValue, TypedArray, etc.)
>;

// Stack trace / exceções
struct StackFrame {
  uint64_t columnNumber;
  std::string functionName;
  uint64_t lineNumber;
  std::string url;
};

struct StackTrace { std::vector<StackFrame> callFrames; };

struct ExceptionDetails {
  uint64_t columnNumber;
  RemoteValue exception;
  uint64_t lineNumber;
  StackTrace stackTrace;
  std::string text;
};

} // namespace bidi::script
```

Isto cobre `Handle`, `RemoteReference`, `RealmType/RealmInfo`, a união `RemoteValue` (com as categorias principais), além de `ExceptionDetails`, `StackFrame` e `StackTrace`. ([W3C][1])

---

### Módulo `network` (subset prático para automação)

```cpp
namespace bidi::network {

// Representação de bytes (spec permite string UTF-8 ou base64)
enum class BytesKind { Utf8, Base64 };
struct BytesValue {
  BytesKind kind;
  std::string value; // se Base64, está em base64
};

// Cookie
enum class SameSite { None, Lax, Strict };

struct Cookie {
  std::string name;
  BytesValue value;
  std::string domain;
  std::string path;
  uint64_t size;
  bool httpOnly;
  bool secure;
  SameSite sameSite;
  std::optional<uint64_t> expiry; // epoch seconds
};

// Ids opacos
using Request = std::string;

// Dados do request/response (simplificado ao essencial; acrescente campos conforme sua necessidade)
struct RequestData {
  Request request;  // id opaco
  std::string url;
  std::string method;
  // headers/body etc. podem ser modelados conforme a spec
};

struct ResponseData {
  // status, headers, url final, protocol etc.
  // Preencha conforme campos que você consumir (ex.: status, statusText, headers, fromCache, ...)
};

} // namespace bidi::network
```

Os modelos acima seguem `network.Cookie` e a ideia de `BytesValue` (string vs base64); `RequestData`/`ResponseData` podem ser detalhados conforme os campos que você realmente usa no cliente. ([W3C][1])

---

## Observações rápidas

* **Nomes de string → enum**: onde a spec usa literais (p.ex. `"interactive"`, `"complete"`), converti para `enum class` em C++ (documentei o mapeamento em cada bloco). ([W3C][1])
* **Campos opcionais**: usei `std::optional` para os `?` da CDDL.
* **Uniões**: onde a CDDL diz `A / B / C`, mapeei com `std::variant<A, B, C>`.
* **Payload “raw”**: no envelope deixei `*_raw` como `std::map<std::string, std::string>` só para ilustrar; no seu cliente real, substitua por **structs concretas por comando/evento** (ex.: `browsingContext.NavigateResult`, `session.StatusResult` etc.). A própria spec lista o tipo de retorno de cada comando. ([W3C][1])

[1]: https://www.w3.org/TR/webdriver-bidi/ "WebDriver BiDi"

Boa! No **WebDriver BiDi**, o `network.ResponseData` tem campos bem específicos. Em C++, ele pode ficar assim (com os tipos auxiliares usados pela spec):

```cpp
#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <cstdint>

namespace bidi::network {

// BytesValue = StringValue | Base64Value
struct StringValue { std::string value; };   // type: "string"
struct Base64Value { std::string value; };   // type: "base64" (conteúdo em Base64)
using BytesValue = std::variant<StringValue, Base64Value>; // network.BytesValue

// Header
struct Header {
  std::string name;    // ex.: "Content-Type"
  BytesValue value;    // valor pode não ser UTF-8 → vira Base64
};

// Conteúdo “decodificado” (a spec expõe só o tamanho aqui)
struct ResponseContent {
  std::uint64_t size;  // bytes do corpo após decodificação (ex.: gzip → descomprimido)
};

// Desafios de autenticação (401/407)
struct AuthChallenge {
  std::string scheme;  // ex.: "Basic", "Digest", "Bearer", "Negotiate"
  std::string realm;   // realm textual (pode ser vazio)
};

// O próprio ResponseData
struct ResponseData {
  std::string url;                         // URL final
  std::string protocol;                    // ex.: "h2", "http/1.1", "data"...
  std::uint64_t status;                    // código HTTP (js-uint)
  std::string statusText;                  // ex.: "OK", "Not Found"
  bool fromCache;                          // se veio do cache
  std::vector<Header> headers;             // lista de cabeçalhos (nome+bytes)
  std::string mimeType;                    // ex.: "text/html; charset=utf-8"
  std::uint64_t bytesReceived;             // bytes totais recebidos na resposta
  std::optional<std::uint64_t> headersSize;// tamanho dos headers (ou null)
  std::optional<std::uint64_t> bodySize;   // tamanho do corpo bruto (ou null)
  ResponseContent content;                 // info do corpo decodificado (size)
  std::optional<std::vector<AuthChallenge>> authChallenges; // quando 401/407
};

} // namespace bidi::network
```

### De onde vêm esses campos (spec)

* A definição de **`network.ResponseData`** traz exatamente: `url`, `protocol`, `status`, `statusText`, `fromCache`, `headers` (`[Header]`), `mimeType`, `bytesReceived`, `headersSize` (`js-uint | null`), `bodySize` (`js-uint | null`), `content` (`ResponseContent`) e opcional `authChallenges` (`[AuthChallenge]`). ([W3C][1])
* **`Header`** é `{ name, value }` e `value` é um **`BytesValue`** (ou string UTF-8 ou Base64). ([W3C][1])
* **`ResponseContent`** hoje expõe apenas `size` (tamanho decodificado). Para obter o corpo, usa-se coletor + `network.getData`. ([W3C][1])
* **`AuthChallenge`** tem `{ scheme, realm }` e só aparece em respostas 401/407 (WWW-Authenticate/Proxy-Authenticate). ([W3C][1])
* O campo **`protocol`** é derivado do ALPN negociado; se ausente/“unknown”, cai no *scheme* da URL (ex.: `http/1.1`, `h2`, `data`). ([W3C][1])
* Este `ResponseData` é exatamente o payload entregue em `network.responseStarted` e `network.responseCompleted`. ([W3C][1])

[1]: https://www.w3.org/TR/webdriver-bidi/ "WebDriver BiDi"


# WebDriver BiDi (WebSocket) — LLM-friendly Spec (Markdown)

> This is a concise, implementation-oriented summary of the **WebDriver BiDi** protocol as standardized by W3C, tailored for LLM consumption. It focuses on the **WebSocket transport**, the **message envelope**, and the **most-used commands/events** (with examples). Citations point to the official W3C TR.

---

## 1) What is WebDriver BiDi?

WebDriver **Bi**-**Di** is the modern, bidirectional browser automation protocol. Unlike classic WebDriver (HTTP), BiDi uses a **WebSocket** to carry JSON messages both ways (commands, responses, and events).

---

## 2) Transport & connection

* **Establishing the WS URL:** The “local end” (client) typically gets a `webSocketUrl` from a classic WebDriver **New Session** (HTTP) response. That URL is then used to open the BiDi WebSocket connection.
* **High-level flow:** “The local end obtains the WebSocket URL (e.g., from New Session), opens a WebSocket connection, and starts exchanging BiDi messages.”

---

## 3) Message envelope (top-level JSON)

BiDi defines **three top-level message kinds**:

1. **Command request** — sent by the client to the browser:

```json
{ "id": <number>, "method": "<domain.command>", "params": { ... } }
```

Commands are initiated by the local end and identified by `method`/`params`; an `id` correlates request/response.

2. **Success response** — from the browser to the client:

```json
{ "id": <number>, "type": "success", "result": { ... } }
```

A successful completion carries a `result` object.

3. **Error response** — from the browser to the client:

```json
{
  "id": <number>,
  "type": "error",
  "error": "<error code>",
  "message": "<human-readable message>"
}
```

On failure, the remote end **must** send an *error response* with `error` and `message`.

4. **Event** — pushed by the browser to the client:

```json
{ "type": "event", "method": "<domain.eventName>", "params": { ... } }
```

Events are asynchronous notifications.

---

## 4) Core domains & frequently used commands

BiDi is partitioned into **domains** (modules). Below are the high-leverage ones.

### A) `session.*`

* **Subscribe to events**
  **Command:** `session.subscribe`
  **Shape:**

  ```json
  { "id": 1, "method": "session.subscribe", "params": { "events": ["log.entryAdded","network.beforeRequestSent"], "contexts": ["<context-id>"] } }
  ```

  **Returns:** `{ "subscription": { ... } }` (enables event delivery).

* **Unsubscribe from events:** `session.unsubscribe` (disables event delivery for given scopes).

### B) `browsingContext.*`

* **Create** a new top-level context (tab or window):
  `browsingContext.create` → returns a `context` id.
* **Navigate** an existing context:
  `browsingContext.navigate` with `{ "context": "<id>", "url": "...", "wait": "complete" | "interactive" | "none" }`.

### C) `script.*`

* **Evaluate** script in a realm/context:
  `script.evaluate` supports key params such as:

  * `awaitPromise` (boolean): wait for returned Promise to settle.
  * `resultOwnership` (`"root"` or `"none"`): how returned remote values are owned/garbage-collected.
* **Call a function** (with arguments):
  `script.callFunction` — similar execution semantics, including `awaitPromise` and value ownership.

**Note on exceptions (script):** Script commands may return **`exceptionDetails`** if the evaluated code throws or a Promise rejects; clients should inspect this to surface rich error context (line/column/stack).

### D) `log.*`

* **Event:** `log.entryAdded` — console logs, errors, etc. Subscribe via `session.subscribe` to receive.

### E) `network.*`

* **Event:** `network.beforeRequestSent` — emitted before a network request leaves. Subscribe via `session.subscribe`.

---

## 5) Minimal end-to-end example (messages)

**(1) Create a window/tab**

```json
{ "id": 1, "method": "browsingContext.create", "params": { "type": "window" } }
```

**Success**

```json
{ "id": 1, "type": "success", "result": { "context": "<ctx-id>", ... } }
```

**(2) Navigate**

```json
{
  "id": 2,
  "method": "browsingContext.navigate",
  "params": { "context": "<ctx-id>", "url": "https://example.com", "wait": "complete" }
}
```

**Success**

```json
{ "id": 2, "type": "success", "result": { ... } }
```

**(3) Evaluate script (wait for a selector)**

```json
{
  "id": 3,
  "method": "script.evaluate",
  "params": {
    "expression": "document.querySelector('#login')",
    "target": { "context": "<ctx-id>" },
    "awaitPromise": true,
    "resultOwnership": "root"
  }
}
```

If the expression throws or a returned Promise rejects, expect `exceptionDetails` in the result.

---

## 6) Events & subscriptions (quick view)

1. Enable event delivery (globally or for chosen contexts):
   `session.subscribe` → specify event names and optional scopes (contexts).
2. Handle incoming **event** messages of the shape:

   ```json
   { "type": "event", "method": "log.entryAdded", "params": { ... } }
   ```

   or

   ```json
   { "type": "event", "method": "network.beforeRequestSent", "params": { ... } }
   ```

---

## 7) Errors

* **Command errors** return:

  ```json
  { "id": <number>, "type": "error", "error": "<code>", "message": "<text>" }
  ```

  (e.g., invalid params, wrong state, etc.).
* **Script errors** can include rich `exceptionDetails` (line/column/stack), even if the outer command itself is a success container; check the returned object.

---

## 8) Glossary (operational)

* **Browsing context** — a top-level window/tab or nested frame; created via `browsingContext.create` and navigated via `browsingContext.navigate`.
* **Realm** — a JS execution environment (per document/worker) used by `script.*` commands; controls where expressions/functions run and how values are marshaled.

---

## 9) Pointers to the spec sections

* **Transport & Connection:** “Establishing a Connection” (WebSocket URL & opening the connection).
* **Protocol & Messages:** Commands, success/error responses, events.
* **`session.subscribe` / `session.unsubscribe`:** Enabling/disabling event delivery.
* **`browsingContext.create` / `browsingContext.navigate`:** Creating and navigating contexts.
* **`script.evaluate` / `script.callFunction`:** Running code, `awaitPromise`, `resultOwnership`, and exception details.
* **Events:** `log.entryAdded`, `network.beforeRequestSent`.

---

### Notes for implementers

* The **WebSocket endpoint** is decoupled from the classic HTTP session but is typically **discovered via** the classic **New Session** `webSocketUrl`. You **only** need the WS URL to speak BiDi.
* Treat **script exceptions** specially—surface `exceptionDetails` to users for better DX.
* Prefer **`awaitPromise: true`** to turn async JS into synchronous results at the protocol boundary; choose **`resultOwnership: "root"`** if you want to manage remote references (and release them later), or `"none"` for marshaled values only.

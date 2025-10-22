
# 0) Setup rápido (Chrome ou Firefox)

```python
from selenium import webdriver
from selenium.webdriver.chrome.options import Options as ChromeOptions
from selenium.webdriver.firefox.options import Options as FirefoxOptions

def start_chrome():
    opts = ChromeOptions()
    opts.enable_bidi = True           # habilita WebDriver BiDi
    return webdriver.Chrome(options=opts)

def start_firefox():
    opts = FirefoxOptions()
    opts.enable_bidi = True           # idem p/ Firefox
    return webdriver.Firefox(options=opts)

driver = start_chrome()               # ou start_firefox()
```

> `options.enable_bidi = True` ativa o canal WebSocket/ BiDi (pré-requisito para os recursos abaixo). ([Selenium][1])

---

# 1) Logs em tempo real + exceções JS (BiDi: `driver.script`)

```python
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait

console = []
js_errors = []

# Registra handlers BiDi
console_id = driver.script.add_console_message_handler(console.append)
errors_id  = driver.script.add_javascript_error_handler(js_errors.append)

driver.get("https://www.selenium.dev/selenium/web/bidi/logEntryAdded.html")

# Gera um console.log e captura
driver.find_element(By.ID, "consoleLog").click()
WebDriverWait(driver, 5).until(lambda d: len(console) > 0)
print("console[0]:", console[0].text)         # -> "Hello, world!"

# Gera uma exceção JS e captura
driver.find_element(By.ID, "jsException").click()
WebDriverWait(driver, 5).until(lambda d: len(js_errors) > 0)
print("error[0]:", js_errors[0].text)         # -> "Error: Not working"

# Boas práticas: remover handlers quando não precisar mais
driver.script.remove_console_message_handler(console_id)
driver.script.remove_javascript_error_handler(errors_id)
```

As APIs acima e o exemplo de página de teste são os mesmos da documentação oficial (Python). ([Selenium][2])

---

# 2) Interceptar rede: bloquear, alterar headers, medir, autenticar (BiDi: `driver.network`)

Este exemplo instala 3 handlers de rede:

* **before_request**: pode bloquear (`fail_request`) ou continuar a request alterando **método**, **URL**, **headers**, **cookies** ou **body** via `continue_request`.
* **response_started / response_completed**: ajuda a medir/registrar o que passou pelo wire.
* **add_auth_handler**: responde prompts de **Basic/Digest Auth** sem “embedar” credenciais na URL. ([Selenium][3])

```python
from selenium.webdriver.common.bidi.network import Network
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait

net = driver.network  # atalho de alto nível ao módulo BiDi de rede

blocked, touched, completed = [], [], []

def on_before_request(req):
    # 1) BLOQUEAR certos recursos (ex.: imagens PNG dos testes)
    if (req.url or "").endswith("/image/png"):
        blocked.append(req.url)
        req.fail_request()                   # cancela a chamada
        return

    # 2) INJETAR HEADERS em chamadas "normais"
    headers = req.headers or {}
    headers["X-Learning-BiDi"] = "true"
    req.continue_request(headers=headers)    # segue com a chamada alterada

def on_response_started(req):
    # chamado quando a resposta começou
    touched.append(req.url)

def on_response_completed(req):
    # chamado quando a resposta terminou
    completed.append(req.url)

# Assinando eventos (note os nomes dos eventos de alto nível)
cb_before = net.add_request_handler("before_request", on_before_request, url_patterns=["*"])
cb_started = net.add_request_handler("response_started", on_response_started)
cb_done    = net.add_request_handler("response_completed", on_response_completed)

# 3) BASIC AUTH automática
auth_id = net.add_auth_handler("user", "passwd")

# --- Exercitar as interceptações ---

# a) Header custom deve aparecer no JSON do httpbin:
driver.get("https://httpbin.org/headers")
WebDriverWait(driver, 5).until(lambda d: '"X-Learning-BiDi": "true"' in d.find_element(By.TAG_NAME, "body").text)
print("Header injetado OK")

# b) Autenticação básica sem prompt:
driver.get("https://httpbin.org/basic-auth/user/passwd")
WebDriverWait(driver, 5).until(lambda d: '"authenticated": true' in d.find_element(By.TAG_NAME, "body").text)
print("Basic Auth OK")

# c) Disparar uma request que será BLOQUEADA
driver.execute_script("fetch('https://httpbin.org/image/png').catch(()=>{})")
WebDriverWait(driver, 5).until(lambda d: len(blocked) > 0)
print("Bloqueadas:", len(blocked))

# --- Limpeza (boa prática) ---
net.remove_request_handler("before_request", cb_before)
net.remove_request_handler("response_started", cb_started)
net.remove_request_handler("response_completed", cb_done)
net.remove_auth_handler(auth_id)

print("Interceptadas:", len(touched), "Completadas:", len(completed))
```

* A lista de **EVENTS** aceitos (`before_request`, `response_started`, `response_completed`, `auth_required`, etc.), os métodos `continue_request`/`fail_request`, e os helpers de auth estão documentados na API Python e no código-fonte (útil pra checar assinaturas exatas). ([Selenium][3])
* Páginas **Network** e **BiDi** do site do Selenium explicam o modelo e o enablement. ([Selenium][4])

> Observação: o módulo de rede vem evoluindo rápido; se algo não bater 100% com a sua versão, confira as notas/Issues do projeto (p.ex., #13993 e relatórios recentes). ([GitHub][5])

---

# 3) “Pacotão” de smoke test: tudo junto em ~50 linhas

```python
from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait

def start():
    opts = Options()
    opts.enable_bidi = True
    return webdriver.Chrome(options=opts)

def main():
    d = start()

    # ----- LOGGING -----
    logs, errs = [], []
    log_id = d.script.add_console_message_handler(logs.append)
    err_id = d.script.add_javascript_error_handler(errs.append)

    # ----- NETWORK -----
    net = d.network
    seen = {"blocked":0,"touched":0,"done":0}

    def on_before(req):
        if (req.url or "").endswith("/image/png"):
            seen["blocked"] += 1
            req.fail_request()
            return
        headers = (req.headers or {}) | {"X-Learning-BiDi":"true"}
        req.continue_request(headers=headers)

    def on_started(_):  seen["touched"] += 1
    def on_done(_):     seen["done"]    += 1

    cb_b = net.add_request_handler("before_request", on_before, url_patterns=["*"])
    cb_s = net.add_request_handler("response_started", on_started)
    cb_d = net.add_request_handler("response_completed", on_done)
    auth = net.add_auth_handler("user","passwd")

    # ----- Exercícios -----
    d.get("https://www.selenium.dev/selenium/web/bidi/logEntryAdded.html")
    d.find_element(By.ID,"consoleLog").click()
    d.find_element(By.ID,"jsException").click()
    WebDriverWait(d,5).until(lambda x: logs and errs)

    d.get("https://httpbin.org/headers")
    WebDriverWait(d,5).until(lambda x: '"X-Learning-BiDi": "true"' in x.find_element(By.TAG_NAME,"body").text)

    d.get("https://httpbin.org/basic-auth/user/passwd")
    WebDriverWait(d,5).until(lambda x: '"authenticated": true' in x.find_element(By.TAG_NAME,"body").text)

    d.execute_script("fetch('https://httpbin.org/image/png').catch(()=>{})")
    WebDriverWait(d,5).until(lambda x: seen["blocked"] > 0)

    print("console[0]:", logs[0].text, "| js_error[0]:", errs[0].text)
    print("net:", seen)

    # ----- Limpeza -----
    net.remove_request_handler("before_request", cb_b)
    net.remove_request_handler("response_started", cb_s)
    net.remove_request_handler("response_completed", cb_d)
    net.remove_auth_handler(auth)
    d.script.remove_console_message_handler(log_id)
    d.script.remove_javascript_error_handler(err_id)
    d.quit()

if __name__ == "__main__":
    main()
```

Tudo que o script usa — **enable BiDi**, **console/error handlers**, **intercept de request** (com `continue_request`/`fail_request`), **auth handler** — está nas páginas oficiais do Selenium (BiDi + Logging + Network + API Python). ([Selenium][1])

---

## Dicas finais

* Se algo falhar, confirme a **versão do Selenium** (4.36+ recomendável) e compare com as **assinaturas/eventos** da sua instalação (a página de API exibe a versão no topo). ([Selenium][3])
* Evite bloquear a **navegação principal** com `fail_request`; use filtros de URL que atinjam só recursos “secundários” (como no `fetch` do exemplo). (Prática geral; as APIs permitem falhar qualquer request.) ([Selenium][3])

[1]: https://www.selenium.dev/documentation/webdriver/bidi/ "BiDirectional functionality | Selenium"
[2]: https://www.selenium.dev/documentation/webdriver/bidi/logging/ "WebDriver BiDi Logging Features | Selenium"
[3]: https://www.selenium.dev/selenium/docs/api/py/selenium_webdriver_common_bidi/selenium.webdriver.common.bidi.network.html "selenium.webdriver.common.bidi.network — Selenium 4.36.0 documentation"
[4]: https://www.selenium.dev/documentation/webdriver/bidi/network/?utm_source=chatgpt.com "WebDriver BiDi Network Features"
[5]: https://github.com/SeleniumHQ/selenium/issues/13993?utm_source=chatgpt.com "[🚀 Feature]: Implement high level BiDi network commands ..."

#pragma once

/**
 * @brief WebDriverClient definitions, based on the WebDriver protocol and
 * selenium https://github.com/SeleniumHQ/selenium
 *
 * Disclaimer: The code provided is currently under development and has not been
 * thoroughly tested. Consequently, it might contain bugs or not function as
 * expected in certain scenarios. Users are advised to proceed with caution and
 * conduct their own thorough testing before using it in any critical systems or
 * production environments. The code is provided "as is" without any warranties
 * or guarantees of any kind. Users assume all risks associated with its use.
 */

#include "CurlRAII.hpp"
#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <chrono>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_map>

struct WebDriver {
    using elementType = Poco::Dynamic::Var;

    auto jsonToString(const Poco::JSON::Object::Ptr &obj) {
        std::stringstream ss;
        obj->stringify(ss);
        return ss.str();
    }

    auto analyzeError(const Poco::JSON::Object::Ptr &obj) {
        /*
        {
    "value": {
        "error": "no such element",
        "message": "no such element: Unable to locate element: {\"method\":\"css
selector\",\"selector\":\"input[name=secret]\"}\n  (Session info:
chrome=129.0.6668.70)", "stacktrace": "#0 0x5dd8a5bff10a \u003Cunknown>\n#1
0x5dd8a58e55e0 \u003Cunknown>\n#2 0x5dd8a5934be8 \u003Cunknown>\n#3
0x5dd8a5934e81 \u003Cunknown>\n#4 0x5dd8a597b8c4 \u003Cunknown>\n#5
0x5dd8a5959b4d \u003Cunknown>\n#6 0x5dd8a5978d7d \u003Cunknown>\n#7
0x5dd8a59598c3 \u003Cunknown>\n#8 0x5dd8a59276b3 \u003Cunknown>\n#9
0x5dd8a592868e \u003Cunknown>\n#10 0x5dd8a5bc9b0b \u003Cunknown>\n#11
0x5dd8a5bcda91 \u003Cunknown>\n#12 0x5dd8a5bb6305 \u003Cunknown>\n#13
0x5dd8a5bce612 \u003Cunknown>\n#14 0x5dd8a5b9b46f \u003Cunknown>\n#15
0x5dd8a5bee008 \u003Cunknown>\n#16 0x5dd8a5bee1d3 \u003Cunknown>\n#17
0x5dd8a5bfdf5c \u003Cunknown>\n#18 0x72580a09ca94 \u003Cunknown>\n#19
0x72580a129c3c \u003Cunknown>\n"
    }
}
        */
        if (!obj->has("value") || obj->isNull("value")) {
            return;
        }

        auto value = obj->getObject("value");

        if (value.isNull()) {
            return;
        }

        if (!value->has("error")) {
            return;
        }

        auto error = value->getValue<std::string>("error");

        auto message = value->get("message").toString();
        std::cerr << "Error: " << message << std::endl;
        throw std::runtime_error("Error: " + error + "\n" + message);
    }

    void connect(const Poco::JSON::Array::Ptr &args = {}) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        Poco::JSON::Object::Ptr capabilities = new Poco::JSON::Object();
        Poco::JSON::Object::Ptr alwaysMatch = new Poco::JSON::Object();
        alwaysMatch->set("browserName", "chrome");

        if (!args.isNull() && args->size() > 0) {
            Poco::JSON::Object::Ptr chromeOptions = new Poco::JSON::Object();
            chromeOptions->set("args", args);
            alwaysMatch->set("goog:chromeOptions", chromeOptions);
        }

        capabilities->set("alwaysMatch", alwaysMatch);
        obj->set("capabilities", capabilities);

        auto reqStr = jsonToString(obj);

        std::cout << reqStr << std::endl;

        auto URL = webDriverUrl + "/session";
        auto value = callUrlDriver("POST", URL, reqStr)
                         .extract<Poco::JSON::Object::Ptr>();
        sessionId = value->get("sessionId").toString();
    }

    void gotoUrl(const std::string &url) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("url", url);

        auto reqStr = jsonToString(obj);

        std::cout << reqStr << std::endl;

        auto URL = webDriverUrl + "/session/" + sessionId + "/url";
        callUrlDriver("POST", URL, reqStr);
    }

    void sendKeysToElement(const std::string &elementId,
                           const std::string &keys) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("text", keys);

        auto reqStr = jsonToString(obj);

        std::cout << reqStr << std::endl;

        auto URL = webDriverUrl + "/session/" + sessionId + "/element/" +
                   elementId + "/value";
        callUrlDriver("POST", URL, reqStr);
    }

    static auto getIdFromElement(const Poco::Dynamic::Var &element) {
        return element.extract<Poco::JSON::Object::Ptr>()
            ->getValue<std::string>("element-6066-11e4-a52e-4f735466cecf");
    }

    void sendKeysToElement(const Poco::Dynamic::Var &element,
                           const std::string &keys) {
        auto elementId =
            element.extract<Poco::JSON::Object::Ptr>()->getValue<std::string>(
                "element-6066-11e4-a52e-4f735466cecf");
        sendKeysToElement(elementId, keys);
    }

    auto selectElement(const std::string &usingSelector,
                       const std::string &value) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("using", usingSelector);
        obj->set("value", value);

        auto reqStr = jsonToString(obj);

        std::cout << reqStr << std::endl;

        auto URL = webDriverUrl + "/session/" + sessionId + "/element";
        return callUrlDriver("POST", URL, reqStr);
    }

    template <class... T>
    auto executeSyncScript(const std::string &script, const T &...args) {
        Poco::JSON::Array::Ptr argsjs = new Poco::JSON::Array;
        (argsjs->add(args), ...);

        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("script", script);
        obj->set("args", argsjs);

        auto reqStr = jsonToString(obj);

        auto URL = webDriverUrl + "/session/" + sessionId + "/execute/sync";
        return callUrlDriver("POST", URL, reqStr);
    }

    auto submitElement(const Poco::Dynamic::Var &elementId) {
        std::string script = R"js(/* submitForm */var form = arguments[0];
while (form.nodeName != "FORM" && form.parentNode) {
  form = form.parentNode;
}
if (!form) { throw Error('Unable to find containing form element'); }
if (!form.ownerDocument) { throw Error('Unable to find owning document'); }
var e = form.ownerDocument.createEvent('Event');
e.initEvent('submit', true, true);
if (form.dispatchEvent(e)) { HTMLFormElement.prototype.submit.call(form); }
)js";

        auto res = executeSyncScript(script, elementId);

        if (res.isEmpty()) {
            return;
        }

        std::cout << "Response: " << res.toString() << std::endl;
    }

    auto uploadFile(const std::string &filePath) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("file", filePath);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/se/file";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto setUserVerified(const std::string &authenticatorId) {
        std::string path = "/session/" + sessionId +
                           "/webauthn/authenticator/" + authenticatorId + "/uv";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto removeAllCredentials(const std::string &authenticatorId) {
        std::string path = "/session/" + sessionId +
                           "/webauthn/authenticator/" + authenticatorId +
                           "/credentials";

        return callUrlDriver("DELETE", webDriverUrl + path, "{}");
    }

    auto getCredentials(const std::string &authenticatorId) {
        std::string path = "/session/" + sessionId +
                           "/webauthn/authenticator/" + authenticatorId +
                           "/credentials";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto addCredential(const std::string &authenticatorId) {
        std::string path = "/session/" + sessionId +
                           "/webauthn/authenticator/" + authenticatorId +
                           "/credential";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto removeVirtualAuthenticator(const std::string &authenticatorId) {
        std::string path = "/session/" + sessionId +
                           "/webauthn/authenticator/" + authenticatorId;

        return callUrlDriver("DELETE", webDriverUrl + path, "{}");
    }

    auto printPage() {
        std::string path = "/session/" + sessionId + "/print";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto minimizeWindow() {
        std::string path = "/session/" + sessionId + "/window/minimize";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto fullscreenWindow() {
        std::string path = "/session/" + sessionId + "/window/fullscreen";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto getAvailableLogTypes() {
        std::string path = "/session/" + sessionId + "/se/log/types";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getLog() {
        std::string path = "/session/" + sessionId + "/se/log";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto getNetworkConnection() {
        std::string path = "/session/" + sessionId + "/network_connection";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getElementRect(const std::string &id) {
        std::string path = "/session/" + sessionId + "/element/" + id + "/rect";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto isElementEnabled(const std::string &id) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/enabled";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto setTimeouts(const int implicit, const int pageLoad, const int script) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("implicit", implicit);
        obj->set("pageLoad", pageLoad);
        obj->set("script", script);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/timeouts";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getElementTagName(const std::string &id) {
        std::string path = "/session/" + sessionId + "/element/" + id + "/name";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getTimeouts() {
        std::string path = "/session/" + sessionId + "/timeouts";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getDownloadableFiles() {
        std::string path = "/session/" + sessionId + "/se/files";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto clickElement(const std::string &id) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/click";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto screenshot() {
        std::string path = "/session/" + sessionId + "/screenshot";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto findChildElement(const std::string &id,
                          const std::string &usingSelector,
                          const std::string &value) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("using", usingSelector);
        obj->set("value", value);

        auto reqStr = jsonToString(obj);

        std::string path =
            "/session/" + sessionId + "/element/" + id + "/element";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto w3cGetActiveElement() {
        std::string path = "/session/" + sessionId + "/element/active";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto elementScreenshot(const std::string &id) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/screenshot";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto findElements(const std::string &usingSelector,
                      const std::string &value) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("using", usingSelector);
        obj->set("value", value);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/elements";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto findChildElements(const std::string &id,
                           const std::string &usingSelector,
                           const std::string &value) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("using", usingSelector);
        obj->set("value", value);

        auto reqStr = jsonToString(obj);

        std::string path =
            "/session/" + sessionId + "/element/" + id + "/elements";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto findElement(const std::string &usingSelector,
                     const std::string &value) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("using", usingSelector);
        obj->set("value", value);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/element";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto addVirtualAuthenticator() {
        std::string path = "/session/" + sessionId + "/webauthn/authenticator";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto w3cSetAlertValue(const std::string &text) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("text", text);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/alert/text";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getElementAriaRole(const std::string &id) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/computedrole";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto newSession() {
        std::string path = "/session";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto isElementSelected(const std::string &id) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/selected";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getCookie(const std::string &name) {
        std::string path = "/session/" + sessionId + "/cookie/" + name;

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto removeCredential(const std::string &authenticatorId,
                          const std::string &credentialId) {
        std::string path = "/session/" + sessionId +
                           "/webauthn/authenticator/" + authenticatorId +
                           "/credentials/" + credentialId;

        return callUrlDriver("DELETE", webDriverUrl + path, "{}");
    }

    auto w3cGetCurrentWindowHandle() {
        std::string path = "/session/" + sessionId + "/window";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto w3cDismissAlert() {
        std::string path = "/session/" + sessionId + "/alert/dismiss";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto goBack() {
        std::string path = "/session/" + sessionId + "/back";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto w3cGetWindowHandles() {
        std::string path = "/session/" + sessionId + "/window/handles";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getCurrentContextHandle() {
        std::string path = "/session/" + sessionId + "/context";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto setScreenOrientation(const std::string &orientation) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("orientation", orientation);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/orientation";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto goForward() {
        std::string path = "/session/" + sessionId + "/forward";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto close() {
        std::string path = "/session/" + sessionId + "/window";

        return callUrlDriver("DELETE", webDriverUrl + path, "{}");
    }

    auto refresh() {
        std::string path = "/session/" + sessionId + "/refresh";

        return callUrlDriver("POST", webDriverUrl + path, "{}");
    }

    auto switchToContext(const std::string &name) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("name", name);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/context";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getWindowRect() {
        std::string path = "/session/" + sessionId + "/window/rect";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getCurrentUrl() {
        std::string path = "/session/" + sessionId + "/url";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    template <class... T>
    auto w3cExecuteScript(const std::string &script, const T &...args) {
        Poco::JSON::Array::Ptr argsjs = new Poco::JSON::Array;
        (argsjs->add(args), ...);

        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("script", script);
        obj->set("args", argsjs);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/execute/sync";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getElementText(const std::string &id) {
        std::string path = "/session/" + sessionId + "/element/" + id + "/text";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getElementText(const Poco::Dynamic::Var &id) {
        auto elementId = getIdFromElement(id);

        return getElementText(elementId);
    }

    template <class... T>
    auto w3cExecuteScriptAsync(const std::string &script, const T &...args) {
        Poco::JSON::Array::Ptr argsjs = new Poco::JSON::Array;
        (argsjs->add(args), ...);

        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("script", script);
        obj->set("args", argsjs);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/execute/async";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getTitle() {
        std::string path = "/session/" + sessionId + "/title";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto get(const std::string &url) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("url", url);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/url";
        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getElementAriaLabel(const std::string &id) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/computedlabel";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getPageSource() {
        std::string path = "/session/" + sessionId + "/source";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto newWindow(const std::string &type) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("type", type);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/window/new";
        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto setWindowRect(int x, int y, int width, int height) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("x", x);
        obj->set("y", y);
        obj->set("width", width);
        obj->set("height", height);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/window/rect";
        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getShadowRoot(const std::string &id) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/shadow";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto switchToFrame(const Poco::Dynamic::Var &frameId) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("id", frameId);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/frame";
        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto clearActionState() {
        std::string path = "/session/" + sessionId + "/actions";

        return callUrlDriver("DELETE", webDriverUrl + path);
    }

    auto findElementFromShadowRoot(const std::string &shadowId,
                                   const std::string &usingSelector,
                                   const std::string &value) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("using", usingSelector);
        obj->set("value", value);

        auto reqStr = jsonToString(obj);

        std::string path =
            "/session/" + sessionId + "/shadow/" + shadowId + "/element";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getCookies() {
        std::string path = "/session/" + sessionId + "/cookie";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto deleteCookie(const std::string &name) {
        std::string path = "/session/" + sessionId + "/cookie/" + name;

        return callUrlDriver("DELETE", webDriverUrl + path);
    }

    auto deleteDownloadableFiles() {
        std::string path = "/session/" + sessionId + "/se/files";

        return callUrlDriver("DELETE", webDriverUrl + path);
    }

    auto findElementsFromShadowRoot(const std::string &shadowId,
                                    const std::string &usingSelector,
                                    const std::string &value) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("using", usingSelector);
        obj->set("value", value);

        auto reqStr = jsonToString(obj);

        std::string path =
            "/session/" + sessionId + "/shadow/" + shadowId + "/elements";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto addCookie(const Poco::JSON::Object::Ptr &cookieJson) {
        auto reqStr = jsonToString(cookieJson);

        std::string path = "/session/" + sessionId + "/cookie";
        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getElementAttribute(const std::string &id, const std::string &name) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/attribute/" + name;

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto getElementProperty(const std::string &id, const std::string &name) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/property/" + name;

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto quit() {
        std::string path = "/session/" + sessionId;

        return callUrlDriver("DELETE", webDriverUrl + path);
    }

    auto switchToParentFrame() {
        std::string path = "/session/" + sessionId + "/frame/parent";

        return callUrlDriver("POST", webDriverUrl + path);
    }

    auto switchToWindow(const std::string &handle) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("handle", handle);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/window";
        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto setNetworkConnection(int connectionType) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("network", connectionType);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/network_connection";
        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getScreenOrientation() {
        std::string path = "/session/" + sessionId + "/orientation";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    template <class... T>
    auto executeAsyncScript(const std::string &script, const T &...args) {
        Poco::JSON::Array::Ptr argsjs = new Poco::JSON::Array;
        (argsjs->add(args), ...);

        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("script", script);
        obj->set("args", argsjs);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/execute_async";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto getContextHandles() {
        std::string path = "/session/" + sessionId + "/contexts";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto clearElement(const std::string &id) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/clear";

        return callUrlDriver("POST", webDriverUrl + path);
    }

    auto getElementValueOfCssProperty(const std::string &id,
                                      const std::string &propertyName) {
        std::string path =
            "/session/" + sessionId + "/element/" + id + "/css/" + propertyName;

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto w3cAcceptAlert() {
        std::string path = "/session/" + sessionId + "/alert/accept";

        return callUrlDriver("POST", webDriverUrl + path);
    }

    auto downloadFile(const std::string &fileId) {
        Poco::JSON::Object::Ptr obj = new Poco::JSON::Object();
        obj->set("fileId", fileId);

        auto reqStr = jsonToString(obj);

        std::string path = "/session/" + sessionId + "/se/files";

        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto w3cGetAlertText() {
        std::string path = "/session/" + sessionId + "/alert/text";

        return callUrlDriver("GET", webDriverUrl + path);
    }

    auto deleteAllCookies() {
        std::string path = "/session/" + sessionId + "/cookie";

        return callUrlDriver("DELETE", webDriverUrl + path);
    }

    auto actions(const Poco::JSON::Object::Ptr &actionsJson) {
        auto reqStr = jsonToString(actionsJson);

        std::string path = "/session/" + sessionId + "/actions";
        return callUrlDriver("POST", webDriverUrl + path, reqStr);
    }

    auto w3cMaximizeWindow() {
        std::string path = "/session/" + sessionId + "/window/maximize";

        return callUrlDriver("POST", webDriverUrl + path);
    }

    auto callUrlDriver(const std::string &verb, const std::string &url,
                       const std::string &body = "") -> Poco::Dynamic::Var {
        auto &req = CurlRAII::instance();

        auto res =
            body.empty() ? req.request(verb, url) : req.postJson(url, body);

        std::cout << "Response: " << res.buffer << std::endl;

        if (res.curl_perfm_res != CURLE_OK) {
            throw std::runtime_error("Error: " + std::string(curl_easy_strerror(
                                                     res.curl_perfm_res)));
        }

        if (res.buffer.empty()) {
            return {};
        }

        Poco::JSON::Object::Ptr resObj =
            Poco::JSON::Parser()
                .parse(res.buffer)
                .extract<Poco::JSON::Object::Ptr>();

        analyzeError(resObj);

        return resObj->get("value");
    }

    ~WebDriver() {
        if (sessionId.empty()) {
            return;
        }

        auto &req = CurlRAII::instance();

        auto res =
            req.request("DELETE", webDriverUrl + "/session/" + sessionId);

        std::cout << "Response: " << res.response_code << std::endl;
    }

    std::string webDriverUrl = "http://localhost:9515";
    std::string sessionId;
};

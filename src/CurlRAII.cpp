/**
 *@file CurlRAII.cpp
 * @author Fabio Rossini Sluzala ()
 * @brief CurlRAII definitions
 * @version 0.1
 *
 *
 */
#include "CurlRAII.hpp"

CurlRAII::CurlRAII() { curl_global_init(CURL_GLOBAL_DEFAULT); }

CurlRAII::~CurlRAII() { curl_global_cleanup(); }

CurlRAII &CurlRAII::instance() {
    static CurlRAII inst;
    return inst;
}

auto CurlRAII::postJson(const std::string &url, const std::string &json)
    -> curlCallBack {

    curlCallBack result;
    curlraii_t curl = make_curl_easy();

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json.c_str());

    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION,
                     std::addressof(result.cb));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, std::addressof(result));

    curlslitraii_t headers;

    CurlRAII::curl_slist_append_raii(headers, "Content-Type: application/json");

    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

    result.curl_perfm_res = curl_easy_perform(curl.get());

    if (result.curl_perfm_res == CURLE_OK) {
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE,
                          std::addressof(result.response_code));
    }

    return result;
}

auto CurlRAII::request(const std::string &httpVerb, const std::string &url,
                       const std::string &body) -> curlCallBack {
    curlCallBack result;
    curlraii_t curl = make_curl_easy();

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, httpVerb.c_str());

    if (!body.empty()) {
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    }

    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION,
                     std::addressof(result.cb));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, std::addressof(result));

    result.curl_perfm_res = curl_easy_perform(curl.get());

    if (result.curl_perfm_res == CURLE_OK) {
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE,
                          std::addressof(result.response_code));
    }

    return result;
}

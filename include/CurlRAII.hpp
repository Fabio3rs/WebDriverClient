/**
 *@file CurlRAII.hpp
 * @author Fabio Rossini Sluzala ()
 * @brief RAII encapsulated cURL library functions
 * @version 0.1
 *
 *
 */
#pragma once
#ifndef CURL_RAII_HPP
#define CURL_RAII_HPP
#include <curl/curl.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief RAII curl CURL* deallocator curl_easy_cleanup
 */
class curlraii {
  public:
    void operator()(CURL *curl) { curl_easy_cleanup(curl); }
};

/**
 * @brief RAII curl curl_slist deallocator curl_slist_free_all
 */
class curlslitraii {
  public:
    void operator()(curl_slist *curl) { curl_slist_free_all(curl); }
};

/**
 * @brief RAII curl typedefs with unique_ptr
 */
typedef std::unique_ptr<CURL, curlraii> curlraii_t;
typedef std::unique_ptr<curl_slist, curlslitraii> curlslitraii_t;

/**
 * @brief RAII curl callback class
 */
class curlCallBack {
  public:
    std::string buffer;
    long response_code{};
    CURLcode curl_perfm_res{};
    bool storedata{true};

    /**
     * @brief CURL callback definition, if userp->storedata is false it will
     * store nothing
     * @param[in] data Input data
     * @param[in] size Size of the elements
     * @param[in] nmemb Number of elements
     * @param[in] userp User pointer, in this case curlCallBack*
     *
     * @return Size written in bytes
     */
    static size_t cb(void *data, size_t size, size_t nmemb,
                     curlCallBack *userp) {
        size_t realsize = size * nmemb;

        try {
            if (userp->storedata)
                userp->buffer.insert(
                    userp->buffer.end(), reinterpret_cast<const char *>(data),
                    reinterpret_cast<const char *>(data) + realsize);
        } catch (const std::exception &e) {
            std::cerr << "Error in curlCallBack::cb: " << e.what() << std::endl;
        }

        return realsize;
    }

    curlCallBack() : response_code(0), curl_perfm_res(CURLE_OK) {}
};

class CurlRAII {
    CurlRAII();
    ~CurlRAII();

    CurlRAII(const CurlRAII &) = delete;
    CurlRAII(CurlRAII &&) = delete;

  public:
    /**
     * @brief similar to make_unique but with cURL curl_easy_init and curlraii_
     * @return curlraii_t object
     */
    static inline curlraii_t make_curl_easy() {
        return curlraii_t(curl_easy_init());
    }

    /**
     * @brief RAII version of curl_slist_append, create and append headers for
     * cURL
     * @param[out] ptr The curlslitraii_t variable to create/append
     */
    static inline void curl_slist_append_raii(curlslitraii_t &ptr,
                                              const char *str) {
        curl_slist *tmp = curl_slist_append(ptr.get(), str);

        if (tmp == nullptr) {
            throw std::runtime_error("Curl fail to run curl_slist_append");
        }

        if (!ptr) {
            ptr = curlslitraii_t(tmp);
        }

        /*
        cURL returns the first element from the curl_slist_append, that's what I
        am expecting in this function logic
        */
        if (ptr.get() != tmp) {
            throw std::logic_error(
                "curl curl_slist_append return ptr different");
        }
    }

    /**
     * @brief singleton of the CurlRAII class to call automatically
     * curl_global_init
     * @return CurlRAII& instance
     */
    static CurlRAII &instance();

    auto postJson(const std::string &url, const std::string &json)
        -> curlCallBack;

    auto request(const std::string &httpVerb, const std::string &url,
                 const std::string &body = "") -> curlCallBack;
};

#endif

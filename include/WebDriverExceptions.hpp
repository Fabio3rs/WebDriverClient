#pragma once
#include <stdexcept>
#include <string>

class WebDriverException : public std::runtime_error {
  public:
    explicit WebDriverException(const std::string &msg)
        : std::runtime_error(msg) {}
};

class WebDriverConnectionException : public WebDriverException {
  public:
    explicit WebDriverConnectionException(const std::string &msg)
        : WebDriverException(msg) {}
};

class WebDriverSessionException : public WebDriverException {
  public:
    explicit WebDriverSessionException(const std::string &msg)
        : WebDriverException(msg) {}
};

class WebDriverTimeoutException : public WebDriverException {
  public:
    explicit WebDriverTimeoutException(const std::string &msg)
        : WebDriverException(msg) {}
};

class WebDriverNoSuchElementException : public WebDriverException {
  public:
    explicit WebDriverNoSuchElementException(const std::string &msg)
        : WebDriverException(msg) {}
};

class WebDriverInvalidArgumentException : public WebDriverException {
  public:
    explicit WebDriverInvalidArgumentException(const std::string &msg)
        : WebDriverException(msg) {}
};

class WebDriverUnknownErrorException : public WebDriverException {
  public:
    explicit WebDriverUnknownErrorException(const std::string &msg)
        : WebDriverException(msg) {}
};

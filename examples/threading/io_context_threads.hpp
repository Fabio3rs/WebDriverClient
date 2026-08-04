#pragma once

#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <thread>
#include <vector>

namespace bidi::examples {

class IoContextThreads {
  public:
    IoContextThreads(boost::asio::io_context &io_context,
                     std::size_t thread_count)
        : io_context_(io_context) {
        threads_.reserve(thread_count);
        for (std::size_t index = 0; index < thread_count; ++index) {
            threads_.emplace_back([this] { io_context_.run(); });
        }
    }

    ~IoContextThreads() { io_context_.stop(); }

    IoContextThreads(const IoContextThreads &) = delete;
    auto operator=(const IoContextThreads &) -> IoContextThreads & = delete;
    IoContextThreads(IoContextThreads &&) = delete;
    auto operator=(IoContextThreads &&) -> IoContextThreads & = delete;

  private:
    boost::asio::io_context &io_context_;
    std::vector<std::jthread> threads_;
};

} // namespace bidi::examples

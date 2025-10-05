// ThreadPoolWrapper.hpp — injectable RAII wrapper around
// boost::asio::thread_pool
#pragma once

#include <algorithm>
#include <boost/asio/thread_pool.hpp>
#include <memory>
#include <thread>

namespace webdriver {

struct ThreadPool {
    // if pool_shared is null, this object does not own the pool
    std::shared_ptr<boost::asio::thread_pool> pool_shared;

    ThreadPool()
        : pool_shared(std::make_shared<boost::asio::thread_pool>(
              std::max<std::size_t>(1, std::thread::hardware_concurrency()))) {}

    explicit ThreadPool(std::shared_ptr<boost::asio::thread_pool> external)
        : pool_shared(std::move(external)) {}

    auto get() -> boost::asio::thread_pool & { return *pool_shared; }
};

} // namespace webdriver

// ThreadPool.hpp — small RAII accessor for a global thread_pool used for
// blocking work
#pragma once

#include <algorithm>
#include <boost/asio/thread_pool.hpp>
#include <thread>

namespace webdriver {

inline boost::asio::thread_pool &global_thread_pool() {
    static std::size_t n = std::max<std::size_t>(
        1, std::max(1u, std::thread::hardware_concurrency()));
    static boost::asio::thread_pool pool(static_cast<std::size_t>(n));
    return pool;
}

} // namespace webdriver

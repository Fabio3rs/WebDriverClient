// ThreadPool.hpp — small RAII accessor for a global thread_pool used for
// blocking work
#pragma once

#include <algorithm>
#include <boost/asio/thread_pool.hpp>
#include <thread>

namespace webdriver {

inline auto global_thread_pool() -> boost::asio::thread_pool & {
    static std::size_t n =
        std::max<std::size_t>({static_cast<unsigned long>(1), 1U,
                               std::thread::hardware_concurrency()});
    static boost::asio::thread_pool pool(n);
    return pool;
}

} // namespace webdriver

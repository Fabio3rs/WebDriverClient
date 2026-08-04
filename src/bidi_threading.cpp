// src/bidi_threading.cpp — Threading context implementation
#include "bidi/logging.hpp"
#include "bidi/threading.hpp"
#include <iostream>

namespace bidi::core {

ThreadingContext::ThreadingContext(std::size_t io_threads,
                                   std::size_t cpu_threads)
    : io_context_{std::make_unique<boost::asio::io_context>()},
      io_work_guard_{boost::asio::make_work_guard(*io_context_)},
      cpu_pool_{std::make_unique<boost::asio::thread_pool>(cpu_threads)},
      ws_strand_{boost::asio::make_strand(*io_context_)} {
    // Spawn I/O threads that suspend on kernel primitives (epoll/kqueue/IOCP)
    io_threads_.reserve(io_threads);
    for (std::size_t i = 0; i < io_threads; ++i) {
        bidi::logging::log_info(
            std::format("Spawning I/O thread {} (suspends on kernel I/O)", i));
        io_threads_.emplace_back([this]() {
            // Thread suspends here until I/O events via epoll/kqueue/IOCP
            io_context_->run();
        });
    }

    bidi::logging::log_info(std::string("Created CPU thread pool with ") +
                            std::to_string(cpu_threads) + " threads");
    bidi::logging::log_info(
        "Threading context ready - zero busy-wait guaranteed");
}

ThreadingContext::~ThreadingContext() {
    try {
        stop();
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("~ThreadingContext exception: ") +
                                 e.what());
    } catch (...) {
        bidi::logging::log_error("~ThreadingContext unknown exception");
    }
}

void ThreadingContext::stop() {
    if (io_work_guard_.owns_work()) {
        bidi::logging::log_info("Stopping threading context...");

        // Stop work guard to allow io_context to exit
        io_work_guard_.reset();
        io_context_->stop();

        // Join I/O threads (they'll wake from kernel suspension)
        for (auto &thread : io_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        // Stop and join CPU pool
        cpu_pool_->stop();
        cpu_pool_->join();

        bidi::logging::log_info("Threading context stopped");
    }
}

} // namespace bidi::core

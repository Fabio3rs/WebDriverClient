// src/bidi_timer_pool.cpp — Timer wheel implementation
#include "bidi/logging.hpp"
#include "bidi/timer_pool.hpp"
#include <algorithm>

namespace bidi::core {

TimerWheel::TimerWheel(boost::asio::io_context &ioc,
                       std::chrono::milliseconds tick_interval)
    : io_context_{ioc},
      tick_timer_{std::make_unique<boost::asio::steady_timer>(ioc)},
      tick_interval_{tick_interval} {}

TimerWheel::~TimerWheel() {
    try {
        stop();
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("~TimerWheel exception: ") +
                                 e.what());
    } catch (...) {
        bidi::logging::log_error("~TimerWheel unknown exception");
    }
}

auto TimerWheel::schedule_timeout(std::chrono::milliseconds duration,
                                  TimeoutHandler handler) -> TimeoutId {
    auto id = next_id_.fetch_add(1);
    auto expiry = std::chrono::steady_clock::now() + duration;

    timeouts_.emplace(id, TimeoutEntry{id, expiry, std::move(handler)});

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.active_timeouts;
        ++stats_.total_scheduled;
    }

    return id;
}

void TimerWheel::cancel_timeout(TimeoutId id) {
    auto it = timeouts_.find(id);
    if (it != timeouts_.end() && !it->second.cancelled) {
        it->second.cancelled = true;

        std::lock_guard<std::mutex> lock(stats_mutex_);
        --stats_.active_timeouts;
        ++stats_.cancelled_timeouts;
        ++stats_.total_cancelled;
    }
}

void TimerWheel::start() {
    running_.store(true);
    schedule_next_tick();
}

void TimerWheel::stop() {
    running_.store(false);
    if (tick_timer_) {
        tick_timer_->cancel();
    }
}

void TimerWheel::schedule_next_tick() {
    if (!running_.load()) {
        return;
    }

    tick_timer_->expires_after(tick_interval_);
    tick_timer_->async_wait([this](boost::system::error_code ec) {
        if (!ec && running_.load()) {
            process_tick();
            schedule_next_tick();
        }
    });
}

void TimerWheel::process_tick() {
    auto now = std::chrono::steady_clock::now();
    process_expired_timeouts(now);
}

void TimerWheel::process_expired_timeouts(
    std::chrono::steady_clock::time_point now) {
    std::vector<TimeoutEntry> expired_entries;

    // Collect expired timeouts
    auto it = timeouts_.begin();
    while (it != timeouts_.end()) {
        if (it->second.cancelled) {
            // Remove cancelled timeouts (lazy cleanup)
            it = timeouts_.erase(it);
            std::lock_guard<std::mutex> lock(stats_mutex_);
            --stats_.cancelled_timeouts;
        } else if (it->second.expiry <= now) {
            // Collect expired timeout
            expired_entries.push_back(std::move(it->second));
            it = timeouts_.erase(it);
        } else {
            ++it;
        }
    }

    // Execute expired timeouts
    for (auto &entry : expired_entries) {
        if (entry.handler) {
            try {
                entry.handler(entry.id);
            } catch (...) {
                // Ignore handler exceptions to prevent timer wheel corruption
            }
        }
    }

    // Update stats
    if (!expired_entries.empty()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.active_timeouts -= expired_entries.size();
        stats_.total_expired += expired_entries.size();
    }
}

auto TimerWheel::get_stats() const -> TimerWheel::Stats {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace bidi::core

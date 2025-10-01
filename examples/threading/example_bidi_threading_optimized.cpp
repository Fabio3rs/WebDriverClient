// Example: High-performance threading optimizations (timer wheel, pools, queue)
// Origin: migrated from demo_optimized_threading.cpp

#include "bidi/buffer_pool_vec.hpp"
#include "bidi/logging.hpp"
#include "bidi/memory_pool.hpp"
#include "bidi/message_queue.hpp"
#include "bidi/metrics.hpp"
#include "bidi/pools.hpp"
#include "bidi/threading.hpp"
#include "bidi/timer_pool.hpp"
#include "bidi_methods.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace bidi::core;

namespace {
constexpr int kArenaParseIterations = 10000;
constexpr int kMessageQueueCapacity = 512;
constexpr int kMessageCount = 1000;
constexpr int kDequeueSleepUs = 10;
constexpr int kTimerTickMs = 5;
constexpr int kTimeoutCount = 1000;
constexpr int kTimeoutBaseMs = 10;
constexpr int kTimeoutModulo = 100;
constexpr int kRunIoMs = 200;
constexpr int kBufferCycles = 100;
constexpr int kIoTasks = 20;
constexpr int kCpuTasks = 50;
constexpr int kThreadWaitMs = 10;
} // namespace

// Functions adapted from original optimized threading demo (trimmed comments)
static void demonstrate_memory_arena();
static void demonstrate_message_queue();
static void demonstrate_timer_wheel();
static void demonstrate_buffer_pool();
static void demonstrate_threading_context();

static void demonstrate_memory_arena() {
    bidi::logging::log_info("Testing JSON Arena Allocation");
    static bidi::metrics::Registry reg;
    auto &parse_counter = reg.counter("memory_arena.parses");
    auto &parse_latency = reg.histogram("memory_arena.parse_latency_us");
    auto start = std::chrono::high_resolution_clock::now();
    constexpr int kContextModulo = 8;
    for (int i = 0; i < kArenaParseIterations; ++i) {
        std::string json = std::format(
            R"({{"id":{},"method":"{}","params":{{"expression":"document.title","target":{{"context":"ctx-{}"}}}}}})",
            i, bidi::ids::methods::script_evaluate, (i % kContextModulo));
        auto parser = JsonParser::create(json);
        auto parsed = parser.parse();
        if (parsed.is_object()) {
            auto &obj = parsed.as_object();
            if (auto *id = obj.if_contains("id")) {
                [[maybe_unused]] auto id_val = id->as_int64();
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    parse_counter.inc(kArenaParseIterations);
    parse_latency.observe(
        static_cast<std::uint64_t>(duration.count() / kArenaParseIterations));
}

static void demonstrate_message_queue() {
    bidi::logging::log_info("Testing Lock-Free Message Queue");
    static bidi::metrics::Registry reg;
    auto &mq_counter = reg.counter("message_queue.messages_processed");
    MessageQueue queue{kMessageQueueCapacity};
    std::atomic<int> messages_processed{0};
    std::thread producer([&queue]() {
        for (int i = 0; i < kMessageCount; ++i) {
            std::string message = std::format(
                R"({{"type":"event","method":"{}","params":{{"level":"info","text":"Message {}"}}}})",
                bidi::ids::events::log_entryAdded, i);
            while (!queue.try_enqueue(message, static_cast<id_type>(i))) {
                std::this_thread::yield();
            }
        }
    });
    std::thread consumer([&queue, &messages_processed, &mq_counter]() {
        QueuedMessage msg{"", 0};
        while (messages_processed.load() < kMessageCount) {
            if (queue.try_dequeue(msg)) {
                [[maybe_unused]] auto data = msg.as_string_view();
                messages_processed.fetch_add(1);
                mq_counter.inc();
            } else {
                std::this_thread::sleep_for(
                    std::chrono::microseconds{kDequeueSleepUs});
            }
        }
    });
    producer.join();
    consumer.join();
}

static void demonstrate_timer_wheel() {
    bidi::logging::log_info("Testing Timer Wheel Scalability");
    boost::asio::io_context ioc;
    TimerWheel timer_wheel{ioc, std::chrono::milliseconds{kTimerTickMs}};
    std::atomic<int> timeouts_fired{0};
    timer_wheel.start();
    std::vector<TimeoutId> timeout_ids;
    timeout_ids.reserve(kTimeoutCount);
    for (int i = 0; i < kTimeoutCount; ++i) {
        auto duration =
            std::chrono::milliseconds{kTimeoutBaseMs + (i % kTimeoutModulo)};
        auto id = timer_wheel.schedule_timeout(
            duration,
            [&timeouts_fired](TimeoutId) { timeouts_fired.fetch_add(1); });
        timeout_ids.push_back(id);
    }
    std::thread io_thread(
        [&ioc]() { ioc.run_for(std::chrono::milliseconds{kRunIoMs}); });
    io_thread.join();
    timer_wheel.stop();
}

static void demonstrate_buffer_pool() {
    bidi::logging::log_info("Testing Zero-Copy Buffer Pool");
    auto &pools = bidi::core::get_default_resource_pools();
    auto &pool = pools.get_buffer_pool();
    for (int i = 0; i < kBufferCycles; ++i) {
        auto buffer = pool.acquire_buffer(BufferSize::Medium);
        std::string message = R"({"id":)" + std::to_string(i) +
                              R"(,"result":{"value":"test response )" +
                              std::to_string(i) + R"("}})";
        buffer->write_data(message);
        [[maybe_unused]] auto asio_buffer = buffer->as_asio_buffer();
        [[maybe_unused]] auto string_view = buffer->as_string_view();
    }
    auto stats = pool.get_stats();
    (void)stats; // trimmed logging
}

static void demonstrate_threading_context() {
    bidi::logging::log_info("Testing Threading Context");
    static bidi::metrics::Registry reg;
    auto &io_counter = reg.counter("threading.io_tasks");
    auto &cpu_counter = reg.counter("threading.cpu_tasks");
    ThreadingContext threading{1, std::thread::hardware_concurrency()};
    std::atomic<int> io_tasks_completed{0};
    std::atomic<int> cpu_tasks_completed{0};
    for (int i = 0; i < kIoTasks; ++i) {
        threading.post_io([&io_tasks_completed, &io_counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            io_tasks_completed.fetch_add(1);
            io_counter.inc();
        });
    }
    for (int i = 0; i < kCpuTasks; ++i) {
        threading.post_cpu([&cpu_tasks_completed, &cpu_counter]() {
            JsonParser parser{R"({"heavy": "computational work here"})"};
            [[maybe_unused]] auto parsed = parser.parse();
            cpu_tasks_completed.fetch_add(1);
            cpu_counter.inc();
        });
    }
    while (io_tasks_completed.load() < kIoTasks ||
           cpu_tasks_completed.load() < kCpuTasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds{kThreadWaitMs});
    }
    threading.stop();
}

auto main() -> int {
    try {
        demonstrate_threading_context();
        demonstrate_memory_arena();
        demonstrate_message_queue();
        demonstrate_timer_wheel();
        demonstrate_buffer_pool();
        return 0;
    } catch (const std::exception &e) {
        bidi::logging::log_error(std::string("Demo failed: ") + e.what());
        return 1;
    }
}

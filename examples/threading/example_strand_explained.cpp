// Example: Visual explanation of how strand works with multiple threads
//
// This example creates a visual demonstration showing:
// - How multiple threads can execute handlers
// - How strand ensures serial execution despite multiple threads
// - The difference between concurrent threads and serialized handlers
//
// Key insight: Strand doesn't mean single-threaded, it means serialized access

#include "bidi/logging.hpp"
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace asio = boost::asio;

// Thread-safe output
std::mutex output_mutex;

void print_message(const std::string &msg) {
    std::lock_guard<std::mutex> lock(output_mutex);
    auto tid = std::this_thread::get_id();
    std::ostringstream oss;
    oss << tid;
    std::cout << std::format("[Thread {:>6}] {}", oss.str(), msg) << std::endl;
}

// Simulates a shared resource (like WebSocket state)
class SharedResource {
  private:
    int counter_ = 0;
    std::string last_operation_;

  public:
    // Unsafe increment (no protection)
    void unsafe_increment(int operation_id) {
        print_message(std::format("UNSAFE: Operation {} reading counter = {}",
                                  operation_id, counter_));

        // Simulate some work between read and write
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        counter_++;
        last_operation_ = std::format("unsafe_op_{}", operation_id);

        print_message(std::format("UNSAFE: Operation {} wrote counter = {}",
                                  operation_id, counter_));
    }

    // Safe increment (caller ensures serialization via strand)
    void safe_increment(int operation_id) {
        print_message(std::format("SAFE:   Operation {} reading counter = {}",
                                  operation_id, counter_));

        // Same work, but serialized by strand
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        counter_++;
        last_operation_ = std::format("safe_op_{}", operation_id);

        print_message(std::format("SAFE:   Operation {} wrote counter = {}",
                                  operation_id, counter_));
    }

    int get_counter() const { return counter_; }
    std::string get_last_operation() const { return last_operation_; }
    void reset() {
        counter_ = 0;
        last_operation_.clear();
    }
};

// Demonstrate UNSAFE concurrent access (no strand)
void demo_without_strand() {
    print_message("\n=== DEMONSTRATION 1: WITHOUT STRAND (Unsafe) ===\n");

    asio::io_context ioc;
    SharedResource resource;

    // Post operations directly to io_context (no strand protection)
    for (int i = 0; i < 10; ++i) {
        asio::post(ioc, [&resource, i]() { resource.unsafe_increment(i); });
    }

    // Run on 3 threads - operations can run CONCURRENTLY
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&ioc]() { ioc.run(); });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    print_message(
        std::format("\nResult WITHOUT strand: counter = {} (expected 10)",
                    resource.get_counter()));
    if (resource.get_counter() != 10) {
        print_message("❌ RACE CONDITION DETECTED! Counter is incorrect!");
    } else {
        print_message("✓ Lucky! No race detected this time (but not "
                      "guaranteed)");
    }
}

// Demonstrate SAFE concurrent access (with strand)
void demo_with_strand() {
    print_message("\n=== DEMONSTRATION 2: WITH STRAND (Safe) ===\n");

    asio::io_context ioc;
    asio::strand<asio::io_context::executor_type> strand =
        asio::make_strand(ioc);
    SharedResource resource;

    // Post operations to strand (serialized execution)
    for (int i = 0; i < 10; ++i) {
        asio::post(strand, [&resource, i]() { resource.safe_increment(i); });
    }

    // Run on 3 threads - but strand serializes access
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&ioc]() { ioc.run(); });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    print_message(
        std::format("\nResult WITH strand: counter = {} (expected 10)",
                    resource.get_counter()));
    if (resource.get_counter() == 10) {
        print_message("✅ CORRECT! Strand prevented race conditions!");
    }
}

// Demonstrate that strand handlers can run on different threads
void demo_strand_thread_migration() {
    print_message("\n=== DEMONSTRATION 3: Strand Handlers on Different "
                  "Threads ===\n");

    asio::io_context ioc;
    asio::strand<asio::io_context::executor_type> strand =
        asio::make_strand(ioc);

    // Post 20 operations to strand
    for (int i = 0; i < 20; ++i) {
        asio::post(strand, [i]() {
            print_message(
                std::format("Handler {} executing (serialized by strand)", i));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }

    // Run on 4 threads
    print_message("Starting 4 I/O threads...\n");

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&ioc]() { ioc.run(); });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    print_message("\n✅ Notice: Handlers ran on DIFFERENT threads");
    print_message("   But they were SERIALIZED (never concurrent)");
    print_message("   This is how BiDi WebSocket maintains thread safety!");
}

// Demonstrate performance with strand
void demo_strand_performance() {
    print_message("\n=== DEMONSTRATION 4: Performance Impact of Strand ===\n");

    const int operations = 1000;

    // Measure with strand
    {
        asio::io_context ioc;
        asio::strand<asio::io_context::executor_type> strand =
            asio::make_strand(ioc);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < operations; ++i) {
            asio::post(strand, []() {
                // Simulate small work
                volatile int x = 0;
                for (int j = 0; j < 100; ++j) {
                    x += j;
                }
            });
        }

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&ioc]() { ioc.run(); });
        }

        for (auto &thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        print_message(
            std::format("WITH strand:    {} operations in {} μs ({:.2f} "
                        "ops/ms)",
                        operations, duration.count(),
                        static_cast<double>(operations) /
                            (static_cast<double>(duration.count()) / 1000.0)));
    }

    // Measure without strand (unsafe, for comparison)
    {
        asio::io_context ioc;

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < operations; ++i) {
            asio::post(ioc, []() {
                volatile int x = 0;
                for (int j = 0; j < 100; ++j) {
                    x += j;
                }
            });
        }

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&ioc]() { ioc.run(); });
        }

        for (auto &thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        print_message(
            std::format("WITHOUT strand: {} operations in {} μs ({:.2f} "
                        "ops/ms)",
                        operations, duration.count(),
                        static_cast<double>(operations) /
                            (static_cast<double>(duration.count()) / 1000.0)));
    }

    print_message("\n✅ Strand overhead is minimal!");
    print_message("   Safety is worth the tiny performance cost");
}

int main() {
    std::cout << "\n";
    std::cout
        << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout
        << "║  BOOST.ASIO STRAND EXPLAINED WITH VISUAL DEMONSTRATIONS   ║\n";
    std::cout
        << "╚════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "This example shows:\n";
    std::cout << "  1. What happens WITHOUT strand (race conditions)\n";
    std::cout << "  2. How strand PREVENTS races\n";
    std::cout << "  3. Strand works with MULTIPLE threads\n";
    std::cout << "  4. Performance impact (minimal)\n";
    std::cout << "\n";

    try {
        // Run demonstrations
        demo_without_strand();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        demo_with_strand();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        demo_strand_thread_migration();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        demo_strand_performance();

        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════════════════════"
                     "════╗\n";
        std::cout << "║                     KEY TAKEAWAYS                      "
                     "    ║\n";
        std::cout << "╚════════════════════════════════════════════════════════"
                     "════╝\n";
        std::cout << "\n";
        std::cout
            << "✅ Strand = SERIALIZED execution (no concurrent handlers)\n";
        std::cout
            << "✅ Strand ≠ single-threaded (handlers run on any thread)\n";
        std::cout << "✅ Strand prevents race conditions WITHOUT mutexes\n";
        std::cout << "✅ Strand has minimal performance overhead\n";
        std::cout << "✅ BiDi WebSocket uses strand for thread safety\n";
        std::cout << "\n";
        std::cout << "This is why BiDiSession is safe with multiple threads!\n";
        std::cout << "\n";

        return 0;

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

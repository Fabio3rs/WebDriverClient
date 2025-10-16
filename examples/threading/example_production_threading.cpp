// Example: Production-ready multi-threaded BiDi architecture
//
// Demonstrates:
// - Proper thread lifecycle management
// - Graceful shutdown
// - Error handling across threads
// - Integration with ThreadingContext
// - Work queue pattern
// - Clean resource cleanup

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/logging.hpp"
#include "bidi/threading.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <format>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

namespace asio = boost::asio;

// Production task queue with proper synchronization
class TaskQueue {
  public:
    struct Task {
        std::string expression;
        int task_id;
    };

    void push(Task task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }

    auto try_pop() -> std::optional<Task> {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tasks_.empty()) {
            return std::nullopt;
        }
        auto task = std::move(tasks_.front());
        tasks_.pop();
        return task;
    }

    auto empty() const -> bool {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.empty();
    }

    auto size() const -> size_t {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

  private:
    mutable std::mutex mutex_;
    std::queue<Task> tasks_;
};

// Production BiDi worker with proper lifecycle
class BiDiWorker {
  private:
    std::shared_ptr<bidi::Client> client_;
    std::string context_id_;
    std::shared_ptr<TaskQueue> task_queue_;
    [[maybe_unused]] asio::io_context &ioc_;
    std::atomic<bool> running_{false};
    std::atomic<int> tasks_processed_{0};
    std::atomic<int> tasks_failed_{0};

  public:
    BiDiWorker(std::shared_ptr<bidi::Client> client, std::string context_id,
               std::shared_ptr<TaskQueue> task_queue, asio::io_context &ioc)
        : client_(std::move(client)), context_id_(std::move(context_id)),
          task_queue_(std::move(task_queue)), ioc_(ioc) {}

    // Start worker coroutine
    auto run() -> asio::awaitable<void> {
        running_ = true;
        bidi::logging::log_info("Worker started");

        // Backoff exponencial simples para períodos ociosos sem busy-wait
        std::chrono::milliseconds backoff{1};
        constexpr std::chrono::milliseconds backoff_max{50};
        while (running_) {
            // Try to get a task
            auto task = task_queue_->try_pop();

            if (!task) {
                // Nenhuma tarefa: ceder ao event loop e aplicar backoff
                co_await asio::post(asio::use_awaitable);
                std::this_thread::sleep_for(backoff);
                backoff = std::min(backoff * 2, backoff_max);
                continue;
            }

            // Reset backoff quando há trabalho
            backoff = std::chrono::milliseconds{1};

            // Process task
            try {
                bidi::logging::log_info(std::format(
                    "Processing task {}: {}", task->task_id, task->expression));

                auto result =
                    co_await client_->evaluate(task->expression, context_id_);

                tasks_processed_.fetch_add(1);
                bidi::logging::log_info(
                    std::format("Task {} completed: {}", task->task_id,
                                boost::json::serialize(result)));

            } catch (const std::exception &e) {
                tasks_failed_.fetch_add(1);
                bidi::logging::log_error(
                    std::format("Task {} failed: {}", task->task_id, e.what()));
            }
        }

        bidi::logging::log_info("Worker stopped");
    }

    void stop() { running_ = false; }

    [[nodiscard]] auto get_tasks_processed() const -> int {
        return tasks_processed_.load();
    }
    [[nodiscard]] auto get_tasks_failed() const -> int {
        return tasks_failed_.load();
    }
};

// Production application with proper lifecycle
class ProductionBiDiApp {
  public:
    auto ioc() -> asio::io_context & { return ioc_; }

  private:
    asio::io_context ioc_;
    std::shared_ptr<bidi::core::ThreadingContext> threading_;
    std::shared_ptr<bidi::Client> client_;
    std::shared_ptr<TaskQueue> task_queue_;
    std::shared_ptr<BiDiWorker> worker_;
    std::future<void> worker_future_;
    std::vector<std::thread> io_threads_;
    std::atomic<bool> shutdown_requested_{false};
    std::string context_id_;

  public:
    ProductionBiDiApp() : task_queue_(std::make_shared<TaskQueue>()) {
        // Create threading context with explicit thread counts
        threading_ = std::make_shared<bidi::core::ThreadingContext>(
            2,                                    // I/O threads
            std::thread::hardware_concurrency()); // CPU threads

        bidi::logging::log_info("Application initialized");
    }

    // Movível, não copiável (gerencia recursos de I/O)
    ProductionBiDiApp(const ProductionBiDiApp &) = delete;
    auto operator=(const ProductionBiDiApp &) -> ProductionBiDiApp & = delete;
    ProductionBiDiApp(ProductionBiDiApp &&) = delete;
    auto operator=(ProductionBiDiApp &&) -> ProductionBiDiApp & = delete;

    ~ProductionBiDiApp() = default; // shutdown é explícito no main

    // Initialize BiDi connection
    auto initialize(std::string websocket_url) -> asio::awaitable<bool> {
        try {
            bidi::logging::log_info("Initializing BiDi client");

            client_ = co_await bidi::Client::connect(ioc_, websocket_url);
            if (!client_) {
                bidi::logging::log_error("Failed to connect client");
                co_return false;
            }

            auto ctx = co_await client_->create_context();
            context_id_ = ctx; // guardar para shutdown explícito
            co_await client_->navigate(ctx, "https://example.com");

            // Create worker
            worker_ =
                std::make_shared<BiDiWorker>(client_, ctx, task_queue_, ioc_);

            bidi::logging::log_info("Initialization complete");
            co_return true;

        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("Initialization failed: {}", e.what()));
            co_return false;
        }
    }

    // Start processing
    void start() {
        bidi::logging::log_info("Starting application");

        // Start worker and keep future to synchronize shutdown
        worker_future_ = asio::co_spawn(ioc_, worker_->run(), asio::use_future);

        // Start I/O threads
        const int io_thread_count = 2;
        for (int i = 0; i < io_thread_count; ++i) {
            io_threads_.emplace_back([this, i]() {
                bidi::logging::log_info(
                    std::format("I/O thread {} started", i));
                try {
                    ioc_.run();
                } catch (const std::exception &e) {
                    bidi::logging::log_error(
                        std::format("I/O thread {} error: {}", i, e.what()));
                }
                bidi::logging::log_info(
                    std::format("I/O thread {} stopped", i));
            });
        }

        bidi::logging::log_info("Application started");
    }

    // Add task to queue
    void submit_task(const std::string &expression, int task_id) {
        task_queue_->push({.expression = expression, .task_id = task_id});
        bidi::logging::log_info(std::format("Task {} queued (queue size: {})",
                                            task_id, task_queue_->size()));
    }

    // Versão coroutine de shutdown para ordering seguro
    auto shutdown_async() -> asio::awaitable<void> {
        if (shutdown_requested_.exchange(true)) {
            co_return; // já em progresso
        }
        bidi::logging::log_info("Shutdown requested");

        // Sinaliza worker parar
        if (worker_) {
            worker_->stop();
        }
        // Aguardar worker concluído cooperativamente
        if (worker_future_.valid()) {
            while (worker_future_.wait_for(std::chrono::milliseconds(0)) !=
                   std::future_status::ready) {
                co_await asio::post(asio::use_awaitable);
            }
            try {
                worker_future_.get();
            } catch (const std::exception &e) {
                bidi::logging::log_error(
                    std::format("worker_future error: {}", e.what()));
            }
        }

        // close_context
        if (client_ && !context_id_.empty()) {
            try {
                co_await client_->close_context(context_id_);
            } catch (const std::exception &e) {
                bidi::logging::log_error(
                    std::format("close_context falhou: {}", e.what()));
            }
        }

        // disconnect
        if (client_) {
            try {
                client_->disconnect();
            } catch (const std::exception &e) {
                bidi::logging::log_error(
                    std::format("disconnect falhou: {}", e.what()));
            }
        }

        // loop turn para flush final
        co_await asio::post(asio::use_awaitable);

        // Stop io_context (join das threads será feito no main thread)
        ioc_.stop();
        bidi::logging::log_info("Shutdown coroutine complete");
    }

    // Mantém API antiga chamando a nova para compatibilidade (não usada no
    // refator main)
    void shutdown() {
        // Se já solicitado, retorna
        if (shutdown_requested_.load()) {
            return;
        }
        // Executa shutdown_async até concluir
        auto fut = asio::co_spawn(ioc_, shutdown_async(), asio::use_future);
        while (fut.wait_for(std::chrono::milliseconds(0)) !=
               std::future_status::ready) {
            if (!ioc_.stopped()) {
                ioc_.poll();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        try {
            fut.get();
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("shutdown() error: {}", e.what()));
        }
    }

    // Join de threads de I/O a partir da thread principal para evitar EDEADLK
    void join_io_threads_and_stop_threading() {
        for (auto &thread : io_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        if (threading_) {
            threading_->stop();
        }
    }

    // Wait for all tasks to complete
    void wait_for_completion() {
        bidi::logging::log_info("Waiting for task queue to drain");
        static constexpr auto kDrainSleep = std::chrono::milliseconds(50);
        while (!task_queue_->empty()) {
            // Executa qualquer handler pronto rapidamente
            (void)ioc_.poll();
            std::this_thread::sleep_for(kDrainSleep);
        }
        bidi::logging::log_info("All tasks completed");
    }

    // Get statistics
    void print_stats() const {
        if (worker_) {
            bidi::logging::log_info(
                std::format("Statistics: {} processed, {} failed, {} queued",
                            worker_->get_tasks_processed(),
                            worker_->get_tasks_failed(), task_queue_->size()));
        }
    }
};

// Main application
auto main() -> int {
    try {
        bidi::logging::log_info("=== Production Multi-threaded BiDi Example "
                                "===\n");

        // Get WebSocket URL
        WebDriver driver;
        driver.webDriverUrl = "http://localhost:9515";
        WebDriver::json args =
            WebDriver::json::array({"--headless", "--no-sandbox"});
        auto response = driver.connect(args, "chrome", true);

        if (!response.contains("capabilities") ||
            !response["capabilities"].contains("webSocketUrl")) {
            bidi::logging::log_error("No webSocketUrl");
            return 1;
        }

        std::string ws_url =
            response["capabilities"]["webSocketUrl"].get<std::string>();

        // Create application
        ProductionBiDiApp app;

        // Start minimal I/O threads early so initialization coroutine can
        // progress (semelhante ao exemplo basic onde o run acontece em threads
        // antes do get)
        std::vector<std::thread> early_threads;
        const unsigned early_count = 2;
        early_threads.reserve(early_count);
        for (unsigned i = 0; i < early_count; ++i) {
            early_threads.emplace_back([&app, i]() {
                bidi::logging::log_info(
                    std::format("[early] I/O thread {} started", i));
                app.ioc().run();
                bidi::logging::log_info(
                    std::format("[early] I/O thread {} finished", i));
            });
        }

        // Initialize (executará nos early threads)
        auto init_future =
            asio::co_spawn(app.ioc(), app.initialize(ws_url), asio::use_future);

        bool init_success = false;
        try {
            init_success = init_future.get();
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("Init exception: {}", e.what()));
            for (auto &thread_ref : early_threads) {
                if (thread_ref.joinable()) {
                    thread_ref.join();
                }
            }
            return 1;
        }

        if (!init_success) {
            bidi::logging::log_error("Initialization failed");
            for (auto &thread_ref : early_threads) {
                if (thread_ref.joinable()) {
                    thread_ref.join();
                }
            }
            return 1;
        }

        // Parar temporariamente esses threads para reorganizar (shutdown suave
        // do run atual)
        app.ioc().stop();
        for (auto &thread_ref : early_threads) {
            if (thread_ref.joinable()) {
                thread_ref.join();
            }
        }
        app.ioc().restart();

        // Start application (cria worker + threads definitivas)
        app.start();

        // Submit tasks from main thread
        bidi::logging::log_info("\nSubmitting tasks...\n");

        static constexpr int kTotalTasks = 20;
        static constexpr auto kSubmitDelay = std::chrono::milliseconds(20);
        for (int i = 0; i < kTotalTasks; ++i) {
            std::string expr = std::format(
                "console.log('Task {} executing'); {{ taskId: {} }}", i, i);
            app.submit_task(expr, i);
            std::this_thread::sleep_for(kSubmitDelay);
        }

        // Wait for completion
        app.wait_for_completion();

        // Print stats
        app.print_stats();

        // Graceful shutdown (awaitable)
        bidi::logging::log_info("\nShutting down gracefully...\n");
        auto shutdown_future =
            asio::co_spawn(app.ioc(), app.shutdown_async(), asio::use_future);
        // Run remaining events until shutdown coroutine finishes
        while (shutdown_future.wait_for(std::chrono::milliseconds(0)) !=
               std::future_status::ready) {
            app.ioc().poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        try {
            shutdown_future.get();
        } catch (const std::exception &e) {
            bidi::logging::log_error(
                std::format("shutdown_async exception: {}", e.what()));
        }

        // Join das threads de I/O e parada do threading context a partir da
        // thread principal
        app.join_io_threads_and_stop_threading();

        bidi::logging::log_info("Application terminated successfully");
        return 0;

    } catch (const std::exception &e) {
        bidi::logging::log_error(std::format("Fatal error: {}", e.what()));
        return 1;
    }
}

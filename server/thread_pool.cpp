#include "thread_pool.hpp"

ThreadPool::ThreadPool(size_t threads) {
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    condition.wait(lock, [this] { return stop || !tasks.empty(); });

                    if (stop && tasks.empty()) return;

                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();

    // Wait for all threads to finish
    for (std::thread &worker : workers) {
        if (worker.joinable()) {  // Add joinable check
            worker.join();
        }
    }

    // Clear any remaining tasks
    std::queue<std::function<void()>> empty;
    tasks.swap(empty);
}

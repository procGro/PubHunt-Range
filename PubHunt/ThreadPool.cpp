#include "ThreadPool.h"
#include <stdexcept> // For std::runtime_error in worker

ThreadPool::ThreadPool(size_t numThreads)
    : stop_pool(false), active_tasks(0) {
    for(size_t i = 0; i < numThreads; ++i)
        workers.emplace_back(
            [this] {
                for(;;) {
                    std::function<void()> task_to_execute;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this]{ return this->stop_pool || !this->tasks.empty(); });
                        if(this->stop_pool && this->tasks.empty())
                            return;
                        task_to_execute = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    try {
                        task_to_execute();
                    } catch (const std::exception& e) {
                        // Optionally log the exception e.g., using Logger
                        // std::cerr << "Exception in thread: " << e.what() << std::endl;
                    } catch (...) {
                        // Optionally log unknown exception
                        // std::cerr << "Unknown exception in thread." << std::endl;
                    }
                    active_tasks--; // Decrement when task is finished
                }
            }
        );
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop_pool = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

void ThreadPool::wait_for_tasks() {
    std::unique_lock<std::mutex> lock(queue_mutex); // Lock to access active_tasks and tasks queue safely
    condition.wait(lock, [this] { return tasks.empty() && active_tasks == 0; });
    // This simple version waits for queue to be empty AND active_tasks to be zero.
    // Note: active_tasks is incremented when task is enqueued, decremented when finished by worker.
    // A task is removed from queue before execution, so tasks.empty() alone isn't sufficient.
} 
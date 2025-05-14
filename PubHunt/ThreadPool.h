#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future> // For std::future and std::packaged_task

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::_Invoke_result_t<F, Args...>>;

    void wait_for_tasks(); // Wait for all queued tasks to complete

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop_pool;
    std::atomic<size_t> active_tasks; // Counter for active and queued tasks
};

// Template implementation must be in the header file or an included .tpp file

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::_Invoke_result_t<F, Args...>> {
    
    using return_type = typename std::_Invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        if(stop_pool) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        tasks.emplace([task](){ (*task)(); });
        active_tasks++; // Increment when task is added
    }
    condition.notify_one();
    return res;
}

#endif // THREADPOOL_H 
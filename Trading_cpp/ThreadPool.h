#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>

// Fixed-size thread pool for bounded concurrency
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    // Submit a task and get a future for the result
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    // Get number of threads
    size_t size() const { return workers_.size(); }

    // Check if pool is running
    bool isRunning() const { return !stop_; }

    // Wait for all tasks to complete (does not stop the pool)
    void waitAll();

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::condition_variable finishedCondition_;
    bool stop_;
    size_t activeTasks_;
};

// Implementation

inline ThreadPool::ThreadPool(size_t numThreads)
    : stop_(false), activeTasks_(0) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    condition_.wait(lock, [this] {
                        return stop_ || !tasks_.empty();
                    });

                    if (stop_ && tasks_.empty()) {
                        return;
                    }

                    task = std::move(tasks_.front());
                    tasks_.pop();
                    ++activeTasks_;
                }

                task();

                {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    --activeTasks_;
                    if (tasks_.empty() && activeTasks_ == 0) {
                        finishedCondition_.notify_all();
                    }
                }
            }
        });
    }
}

inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {

    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        if (stop_) {
            throw std::runtime_error("Cannot submit to stopped ThreadPool");
        }
        tasks_.emplace([task]() { (*task)(); });
    }

    condition_.notify_one();
    return result;
}

inline void ThreadPool::waitAll() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    finishedCondition_.wait(lock, [this] {
        return tasks_.empty() && activeTasks_ == 0;
    });
}

#pragma once

#include "i_blocking_executor.h"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

class StdThreadPoolExecutor final : public IBlockingExecutor {
public:
    explicit StdThreadPoolExecutor(std::size_t thread_count)
    {
        if (thread_count == 0) {
            throw std::invalid_argument("thread_count must be > 0");
        }

        workers_.reserve(thread_count);

        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] {
                worker_loop();
            });
        }
    }

    ~StdThreadPoolExecutor() override
    {
        stop();
    }

    StdThreadPoolExecutor(const StdThreadPoolExecutor&) = delete;
    StdThreadPoolExecutor& operator=(const StdThreadPoolExecutor&) = delete;

    void submit_void(std::function<void()> task) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (stopping_) {
                throw std::runtime_error("StdThreadPoolExecutor is stopping");
            }

            queue_.push(std::move(task));
        }

        cv_.notify_one();
    }

private:
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }

        cv_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void worker_loop()
    {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(mutex_);

                cv_.wait(lock, [this] {
                    return stopping_ || !queue_.empty();
                });

                if (stopping_ && queue_.empty()) {
                    return;
                }

                task = std::move(queue_.front());
                queue_.pop();
            }

            task();
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;

    std::mutex mutex_;
    std::condition_variable cv_;

    bool stopping_ = false;
};
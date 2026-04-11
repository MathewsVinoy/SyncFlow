// src/platform/thread_pool.cpp

#include <syncflow/platform/platform.h>
#include <syncflow/common/logger.h>
#include <queue>
#include <thread>
#include <condition_variable>
#include <mutex>

namespace syncflow::platform {

class ThreadPoolImpl : public ThreadPool {
public:
    explicit ThreadPoolImpl(size_t num_threads) : shutdown_(false), active_count_(0) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }
    
    ~ThreadPoolImpl() override {
        shutdown(true);
    }
    
    void enqueue(ThreadTask task) override {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (shutdown_) {
                LOG_WARN("ThreadPool", "Cannot enqueue task after shutdown");
                return;
            }
            tasks_.push(task);
        }
        condition_.notify_one();
    }
    
    void shutdown(bool wait) override {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        condition_.notify_all();
        
        if (wait) {
            for (auto& worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
        }
    }
    
    size_t active_count() const override {
        std::unique_lock<std::mutex> lock(mutex_);
        return active_count_;
    }
    
    size_t pending_count() const override {
        std::unique_lock<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    
private:
    void worker_loop() {
        while (true) {
            ThreadTask task;
            
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return !tasks_.empty() || shutdown_; });
                
                if (shutdown_ && tasks_.empty()) {
                    break;
                }
                
                if (tasks_.empty()) {
                    continue;
                }
                
                task = tasks_.front();
                tasks_.pop();
                active_count_++;
            }
            
            try {
                if (task) {
                    task();
                }
            } catch (const std::exception& e) {
                LOG_ERROR("ThreadPool", std::string("Task execution failed: ") + e.what());
            }
            
            {
                std::unique_lock<std::mutex> lock(mutex_);
                active_count_--;
            }
        }
    }
    
    std::vector<std::thread> workers_;
    std::queue<ThreadTask> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool shutdown_;
    size_t active_count_;
};

std::unique_ptr<ThreadPool> ThreadPool::create(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }
    return std::make_unique<ThreadPoolImpl>(num_threads);
}

} // namespace syncflow::platform

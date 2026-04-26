#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
public:
	explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency());
	~ThreadPool();

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	template <typename F, typename... Args>
	auto enqueue(F&& f, Args&&... args)
		-> std::future<std::invoke_result_t<F, Args...>> {
		using ReturnType = std::invoke_result_t<F, Args...>;

		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));

		std::future<ReturnType> future = task->get_future();
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (stopping_) {
				throw std::runtime_error("enqueue on stopped ThreadPool");
			}
			tasks_.emplace([task]() { (*task)(); });
		}
		condition_.notify_one();
		return future;
	}

	void stop();

private:
	void workerLoop();

	std::vector<std::thread> workers_;
	std::queue<std::function<void()>> tasks_;
	std::mutex mutex_;
	std::condition_variable condition_;
	bool stopping_ = false;
};

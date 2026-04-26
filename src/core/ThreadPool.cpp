#include "core/ThreadPool.h"

ThreadPool::ThreadPool(std::size_t threadCount) {
	if (threadCount == 0) {
		threadCount = 1;
	}

	workers_.reserve(threadCount);
	for (std::size_t i = 0; i < threadCount; ++i) {
		workers_.emplace_back([this]() { workerLoop(); });
	}
}

ThreadPool::~ThreadPool() {
	stop();
}

void ThreadPool::stop() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_) {
			return;
		}
		stopping_ = true;
	}

	condition_.notify_all();
	for (auto& worker : workers_) {
		if (worker.joinable()) {
			worker.join();
		}
	}
	workers_.clear();
}

void ThreadPool::workerLoop() {
	for (;;) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			condition_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
			if (stopping_ && tasks_.empty()) {
				return;
			}
			task = std::move(tasks_.front());
			tasks_.pop();
		}

		task();
	}
}
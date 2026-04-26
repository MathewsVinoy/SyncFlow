#pragma once

#include "core/ThreadPool.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

class SyncEngine {
public:
	struct FileEntry {
		std::uintmax_t size = 0;
		std::filesystem::file_time_type modifiedAt{};
	};

	explicit SyncEngine(std::filesystem::path sourceFolder,
	                   std::filesystem::path mirrorFolder = {});
	~SyncEngine();

	bool start();
	void stop();

	std::optional<std::string> pollEvent();
	std::filesystem::path sourceFolder() const;
	std::filesystem::path mirrorFolder() const;
	std::size_t trackedFileCount() const;

private:
	void runLoop();
	void scanAndSync();
	void recordEvent(const std::string& event);
	void copyFileAsync(const std::filesystem::path& source, const std::filesystem::path& destination);
	std::unordered_map<std::string, FileEntry> buildSnapshot() const;
	static bool isInsideMirror(const std::filesystem::path& mirrorRoot, const std::filesystem::path& candidate);

	std::filesystem::path sourceFolder_;
	std::filesystem::path mirrorFolder_;
	ThreadPool workers_;

	mutable std::mutex stateMutex_;
	bool running_ = false;
	std::thread loopThread_;
	std::deque<std::string> events_;
	std::unordered_map<std::string, FileEntry> lastSnapshot_;
	std::chrono::milliseconds interval_{1500};
};
#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <string>
#include <vector>

class FolderWatcher {
public:
	enum class EventType {
		Created,
		Modified,
		Deleted,
		Renamed
	};

	struct Event {
		EventType type = EventType::Modified;
		std::string relativePath;
		std::string previousRelativePath;
	};

	explicit FolderWatcher(std::filesystem::path root);
	~FolderWatcher();

	bool start();
	void stop();
	std::vector<Event> poll();

	const std::filesystem::path& root() const;
	bool running() const;

private:
	std::filesystem::path root_;
	bool running_ = false;

#ifdef __linux__
	int inotifyFd_ = -1;
	std::vector<char> readBuffer_;
	struct WatchRecord {
		int wd = -1;
		std::filesystem::path relativeDir;
	};
	std::vector<WatchRecord> watchRecords_;
	bool addWatchRecursive(const std::filesystem::path& absoluteDir, const std::filesystem::path& relativeDir);
	void removeAllWatches();
#endif

	void addSnapshotEvent(std::vector<Event>& events,
	                     EventType type,
	                     const std::filesystem::path& path,
	                     const std::filesystem::path& previous = {});
	std::vector<Event> pollSnapshotFallback();
	void refreshSnapshot();

	struct SnapshotEntry {
		bool isDirectory = false;
		std::uintmax_t size = 0;
		std::filesystem::file_time_type modifiedAt{};
	};
	std::unordered_map<std::string, SnapshotEntry> snapshot_;

	static std::string toRelative(const std::filesystem::path& root, const std::filesystem::path& path);
};

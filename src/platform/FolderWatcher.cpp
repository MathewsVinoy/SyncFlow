#include "platform/FolderWatcher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <unordered_map>

#ifdef __linux__
#include <fcntl.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace {
std::string genericRelative(const std::filesystem::path& root, const std::filesystem::path& path) {
	std::error_code ec;
	auto rel = std::filesystem::relative(path, root, ec);
	if (ec) {
		return {};
	}
	return rel.lexically_normal().generic_string();
}
}  // namespace

FolderWatcher::FolderWatcher(std::filesystem::path root) : root_(std::move(root)) {}

FolderWatcher::~FolderWatcher() {
	stop();
}

bool FolderWatcher::start() {
	if (running_) {
		return true;
	}

	std::error_code ec;
	std::filesystem::create_directories(root_, ec);
	if (ec) {
		return false;
	}

#ifdef __linux__
	inotifyFd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotifyFd_ >= 0) {
		watchRecords_.clear();
		if (!addWatchRecursive(root_, {})) {
			removeAllWatches();
			::close(inotifyFd_);
			inotifyFd_ = -1;
		} else {
			running_ = true;
			return true;
		}
	}
#endif

	refreshSnapshot();
	running_ = true;
	return true;
}

void FolderWatcher::stop() {
	if (!running_) {
		return;
	}

#ifdef __linux__
	removeAllWatches();
	if (inotifyFd_ >= 0) {
		::close(inotifyFd_);
		inotifyFd_ = -1;
	}
#endif
	snapshot_.clear();
	running_ = false;
}

std::vector<FolderWatcher::Event> FolderWatcher::poll() {
	if (!running_) {
		return {};
	}

#ifdef __linux__
	if (inotifyFd_ >= 0) {
		std::vector<Event> events;
		std::array<char, 16 * 1024> buffer{};
		for (;;) {
			const auto n = ::read(inotifyFd_, buffer.data(), buffer.size());
			if (n <= 0) {
				break;
			}
			std::size_t offset = 0;
			while (offset + sizeof(inotify_event) <= static_cast<std::size_t>(n)) {
				auto* ev = reinterpret_cast<inotify_event*>(buffer.data() + offset);
				std::filesystem::path relativeDir;
				for (const auto& record : watchRecords_) {
					if (record.wd == ev->wd) {
						relativeDir = record.relativeDir;
						break;
					}
				}

				std::filesystem::path relativePath = relativeDir;
				if (ev->len > 0 && ev->name[0] != '\0') {
					relativePath /= ev->name;
				}

				if (ev->mask & IN_ISDIR) {
					const auto absolute = root_ / relativePath;
					if (ev->mask & (IN_CREATE | IN_MOVED_TO)) {
						addWatchRecursive(absolute, relativePath);
						addSnapshotEvent(events, EventType::Created, relativePath);
					} else if (ev->mask & (IN_DELETE | IN_MOVED_FROM)) {
						addSnapshotEvent(events, EventType::Deleted, relativePath);
					}
				} else {
					if (ev->mask & (IN_CREATE | IN_MOVED_TO)) {
						addSnapshotEvent(events, EventType::Created, relativePath);
					} else if (ev->mask & IN_DELETE) {
						addSnapshotEvent(events, EventType::Deleted, relativePath);
					} else if (ev->mask & (IN_MOVE_SELF | IN_MOVED_FROM)) {
						addSnapshotEvent(events, EventType::Renamed, relativePath);
					} else if (ev->mask & (IN_MODIFY | IN_CLOSE_WRITE)) {
						addSnapshotEvent(events, EventType::Modified, relativePath);
					}
				}

				offset += sizeof(inotify_event) + ev->len;
			}
		}
		return events;
	}
#endif

	return pollSnapshotFallback();
}

const std::filesystem::path& FolderWatcher::root() const {
	return root_;
}

bool FolderWatcher::running() const {
	return running_;
}

void FolderWatcher::addSnapshotEvent(std::vector<Event>& events,
	                                 EventType type,
	                                 const std::filesystem::path& path,
	                                 const std::filesystem::path& previous) {
	Event event;
	event.type = type;
	event.relativePath = path.generic_string();
	event.previousRelativePath = previous.generic_string();
	events.push_back(std::move(event));
}

std::vector<FolderWatcher::Event> FolderWatcher::pollSnapshotFallback() {
	std::vector<Event> events;
	std::unordered_map<std::string, SnapshotEntry> current;
	std::error_code ec;
	if (!std::filesystem::exists(root_, ec)) {
		return events;
	}

	for (auto it = std::filesystem::recursive_directory_iterator(root_, ec);
	     it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		if (ec) {
			break;
		}
		const auto rel = toRelative(root_, it->path());
		if (rel.empty()) {
			continue;
		}
		SnapshotEntry entry;
		entry.isDirectory = it->is_directory(ec);
		if (!entry.isDirectory) {
			entry.size = it->file_size(ec);
			entry.modifiedAt = it->last_write_time(ec);
		}
		current[rel] = entry;
	}

	for (const auto& [path, entry] : current) {
		auto it = snapshot_.find(path);
		if (it == snapshot_.end()) {
			addSnapshotEvent(events, EventType::Created, path);
		} else if (it->second.isDirectory != entry.isDirectory || it->second.size != entry.size ||
		           it->second.modifiedAt != entry.modifiedAt) {
			addSnapshotEvent(events, EventType::Modified, path);
		}
	}

	for (const auto& [path, _] : snapshot_) {
		if (current.find(path) == current.end()) {
			addSnapshotEvent(events, EventType::Deleted, path);
		}
	}

	snapshot_ = std::move(current);
	return events;
}

void FolderWatcher::refreshSnapshot() {
	snapshot_.clear();
	std::error_code ec;
	if (!std::filesystem::exists(root_, ec)) {
		return;
	}

	for (auto it = std::filesystem::recursive_directory_iterator(root_, ec);
	     it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		if (ec) {
			break;
		}
		const auto rel = toRelative(root_, it->path());
		if (rel.empty()) {
			continue;
		}
		SnapshotEntry entry;
		entry.isDirectory = it->is_directory(ec);
		if (!entry.isDirectory) {
			entry.size = it->file_size(ec);
			entry.modifiedAt = it->last_write_time(ec);
		}
		snapshot_[rel] = entry;
	}
}

std::string FolderWatcher::toRelative(const std::filesystem::path& root, const std::filesystem::path& path) {
	return genericRelative(root, path);
}

#ifdef __linux__
bool FolderWatcher::addWatchRecursive(const std::filesystem::path& absoluteDir,
	                                  const std::filesystem::path& relativeDir) {
	if (inotifyFd_ < 0) {
		return false;
	}
	const uint32_t mask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE |
	                     IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF;
	const int wd = inotify_add_watch(inotifyFd_, absoluteDir.c_str(), mask);
	if (wd < 0) {
		return false;
	}
	watchRecords_.push_back(WatchRecord{wd, relativeDir});

	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(absoluteDir, ec)) {
		if (ec) {
			break;
		}
		if (entry.is_directory(ec)) {
			const auto rel = toRelative(root_, entry.path());
			addWatchRecursive(entry.path(), rel);
		}
	}
	return true;
}

void FolderWatcher::removeAllWatches() {
	if (inotifyFd_ < 0) {
		watchRecords_.clear();
		return;
	}
	for (const auto& record : watchRecords_) {
		if (record.wd >= 0) {
			inotify_rm_watch(inotifyFd_, record.wd);
		}
	}
	watchRecords_.clear();
}
#endif

#include "sync_engine/SyncEngine.h"

#include "core/Logger.h"
#include "sync_engine/HashUtils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace {
std::string toGenericString(const std::filesystem::path& path) {
	return path.generic_string();
}

bool copyFileContents(const std::filesystem::path& source, const std::filesystem::path& destination) {
	std::error_code ec;
	std::filesystem::create_directories(destination.parent_path(), ec);
	if (ec) {
		return false;
	}

	std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
	return !ec;
}
}  // namespace

SyncEngine::SyncEngine(std::filesystem::path sourceFolder, std::filesystem::path mirrorFolder)
	: sourceFolder_(std::move(sourceFolder)),
	  mirrorFolder_(mirrorFolder.empty() ? sourceFolder_ / ".syncflow_mirror" : std::move(mirrorFolder)),
	  versionFolder_(mirrorFolder_ / ".versions"),
	  workers_(2) {}

SyncEngine::~SyncEngine() {
	stop();
}

bool SyncEngine::start() {
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (running_) {
			return true;
		}
	}

	std::error_code ec;
	std::filesystem::create_directories(sourceFolder_, ec);
	if (ec) {
		recordEvent("sync engine failed to create source folder: " + sourceFolder_.string());
		return false;
	}

	std::filesystem::create_directories(mirrorFolder_, ec);
	if (ec) {
		recordEvent("sync engine failed to create mirror folder: " + mirrorFolder_.string());
		return false;
	}
	std::filesystem::create_directories(versionFolder_, ec);
	if (ec) {
		recordEvent("sync engine failed to create version folder: " + versionFolder_.string());
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		lastSnapshot_ = buildSnapshot();
		running_ = true;
		loopThread_ = std::thread([this]() { runLoop(); });
	}

	recordEvent("sync engine started: source=" + sourceFolder_.string() + " mirror=" + mirrorFolder_.string());
	return true;
}

void SyncEngine::stop() {
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (!running_) {
			return;
		}
		running_ = false;
	}

	if (loopThread_.joinable()) {
		loopThread_.join();
	}
	workers_.stop();
	recordEvent("sync engine stopped");
}

std::optional<std::string> SyncEngine::pollEvent() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (events_.empty()) {
		return std::nullopt;
	}

	std::string event = std::move(events_.front());
	events_.pop_front();
	return event;
}

std::filesystem::path SyncEngine::sourceFolder() const {
	return sourceFolder_;
}

std::filesystem::path SyncEngine::mirrorFolder() const {
	return mirrorFolder_;
}

std::size_t SyncEngine::trackedFileCount() const {
	std::lock_guard<std::mutex> lock(stateMutex_);
	return lastSnapshot_.size();
}

std::unordered_map<std::string, syncflow::engine::FileMetadata> SyncEngine::localMetadata(
	const std::string& localDeviceId) const {
	std::lock_guard<std::mutex> lock(stateMutex_);
	std::unordered_map<std::string, syncflow::engine::FileMetadata> out;
	out.reserve(lastSnapshot_.size());
	for (const auto& [path, entry] : lastSnapshot_) {
		syncflow::engine::FileMetadata item;
		item.relativePath = path;
		item.size = static_cast<std::uint64_t>(entry.size);
		item.modifiedUnixSeconds = entry.modifiedUnixSeconds;
		item.hash = entry.hash;
		item.lastEditorDeviceId = localDeviceId;
		item.deleted = false;
		out.emplace(path, std::move(item));
	}
	return out;
}

std::vector<syncflow::engine::SyncAction> SyncEngine::planAgainstRemote(
	const std::unordered_map<std::string, syncflow::engine::FileMetadata>& remote,
	const std::string& localDeviceId) const {
	syncflow::engine::SyncPlanner planner(localDeviceId);
	return planner.plan(localMetadata(localDeviceId), remote);
}

void SyncEngine::runLoop() {
	while (true) {
		{
			std::lock_guard<std::mutex> lock(stateMutex_);
			if (!running_) {
				break;
			}
		}

		scanAndSync();
		std::this_thread::sleep_for(interval_);
	}
}

void SyncEngine::scanAndSync() {
	auto snapshot = buildSnapshot();
	std::unordered_map<std::string, FileEntry> previous;
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		previous = lastSnapshot_;
	}

	for (const auto& [relString, entry] : snapshot) {
		auto prevIt = previous.find(relString);
		const bool isNew = prevIt == previous.end();
		const bool changed = isNew || prevIt->second.size != entry.size ||
		                     prevIt->second.modifiedAt != entry.modifiedAt || prevIt->second.hash != entry.hash;
		if (!changed) {
			continue;
		}

		const auto source = sourceFolder_ / std::filesystem::path(relString);
		const auto destination = mirrorFolder_ / std::filesystem::path(relString);
		if (!isNew) {
			archiveVersionIfExists(destination, relString);
		}
		copyFileAsync(source, destination);
		recordEvent(std::string(isNew ? "file created: " : "file updated: ") + relString);
		Logger::info("sync copy scheduled: " + source.string() + " -> " + destination.string());
	}

	for (const auto& [relString, deletedEntry] : previous) {
		if (snapshot.find(relString) == snapshot.end()) {
			const auto destination = mirrorFolder_ / std::filesystem::path(relString);
			archiveVersionIfExists(destination, relString);
			std::error_code ec;
			std::filesystem::remove(destination, ec);
			if (ec) {
				Logger::warn("sync failed removing mirrored file: " + destination.string());
			} else {
				recordEvent("file deleted: " + relString + " at " + std::to_string(deletedEntry.modifiedUnixSeconds));
			}
			Logger::info("sync removed mirrored file: " + destination.string());
		}
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		lastSnapshot_ = std::move(snapshot);
	}
}

void SyncEngine::recordEvent(const std::string& event) {
	std::lock_guard<std::mutex> lock(stateMutex_);
	events_.push_back(event);
	if (events_.size() > 256) {
		events_.pop_front();
	}
}

void SyncEngine::copyFileAsync(const std::filesystem::path& source, const std::filesystem::path& destination) {
	(void)workers_.enqueue([source, destination]() {
		if (!copyFileContents(source, destination)) {
			Logger::warn("sync copy failed: " + source.string() + " -> " + destination.string());
			return;
		}
		Logger::info("sync copied: " + source.string() + " -> " + destination.string());
	});
}

void SyncEngine::archiveVersionIfExists(const std::filesystem::path& mirroredFile, const std::string& relativePath) {
	std::error_code ec;
	if (!std::filesystem::exists(mirroredFile, ec) || ec) {
		return;
	}

	const auto now = std::chrono::system_clock::now();
	const auto unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
	const auto versionFile = versionFolder_ / (relativePath + "." + std::to_string(unixSeconds) + ".bak");
	std::filesystem::create_directories(versionFile.parent_path(), ec);
	if (ec) {
		Logger::warn("sync could not create version folder for: " + versionFile.string());
		return;
	}

	std::filesystem::copy_file(mirroredFile, versionFile, std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) {
		Logger::warn("sync could not archive old version: " + mirroredFile.string());
		return;
	}

	Logger::debug("sync archived version: " + versionFile.string());
}

std::unordered_map<std::string, SyncEngine::FileEntry> SyncEngine::buildSnapshot() const {
	std::unordered_map<std::string, FileEntry> snapshot;
	std::error_code ec;
	if (!std::filesystem::exists(sourceFolder_, ec)) {
		return snapshot;
	}

	for (auto it = std::filesystem::recursive_directory_iterator(sourceFolder_, ec);
	     it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		if (ec) {
			break;
		}

		const auto& entry = *it;
		if (!entry.is_regular_file(ec)) {
			continue;
		}

		if (isInsideMirror(mirrorFolder_, entry.path())) {
			continue;
		}

		const auto rel = std::filesystem::relative(entry.path(), sourceFolder_, ec);
		if (ec) {
			ec.clear();
			continue;
		}

		const auto size = entry.file_size(ec);
		if (ec) {
			ec.clear();
			continue;
		}

		const auto modified = entry.last_write_time(ec);
		if (ec) {
			ec.clear();
			continue;
		}

		const auto hash = syncflow::hash::hashFileFNV1a64(entry.path());
		if (hash.empty()) {
			continue;
		}

		snapshot.emplace(toGenericString(rel), FileEntry{size, modified, toUnixSeconds(modified), hash});
	}

	return snapshot;
}

bool SyncEngine::isInsideMirror(const std::filesystem::path& mirrorRoot, const std::filesystem::path& candidate) {
	if (mirrorRoot.empty()) {
		return false;
	}

	std::error_code ec;
	const auto mirrorCanonical = std::filesystem::weakly_canonical(mirrorRoot, ec);
	if (ec) {
		return false;
	}

	const auto candidateCanonical = std::filesystem::weakly_canonical(candidate, ec);
	if (ec) {
		return false;
	}

	const auto mismatch = std::mismatch(mirrorCanonical.begin(), mirrorCanonical.end(), candidateCanonical.begin(), candidateCanonical.end());
	return mismatch.first == mirrorCanonical.end();
}

std::int64_t SyncEngine::toUnixSeconds(std::filesystem::file_time_type timePoint) {
	using namespace std::chrono;
	const auto adjusted = timePoint - std::filesystem::file_time_type::clock::now() + system_clock::now();
	return duration_cast<seconds>(adjusted.time_since_epoch()).count();
}
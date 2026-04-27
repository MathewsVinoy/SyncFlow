#include "sync_engine/SyncEngine.h"

#include "core/Logger.h"
#include "sync_engine/HashUtils.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <optional>
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

std::string actionName(syncflow::engine::ActionType action) {
	switch (action) {
		case syncflow::engine::ActionType::Upload:
			return "upload";
		case syncflow::engine::ActionType::Download:
			return "download";
		case syncflow::engine::ActionType::DeleteRemote:
			return "delete_remote";
		case syncflow::engine::ActionType::DeleteLocal:
			return "delete_local";
		case syncflow::engine::ActionType::ConflictKeepBoth:
			return "conflict_keep_both";
		case syncflow::engine::ActionType::None:
		default:
			return "none";
	}
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
	transferDispatchThread_ = std::thread([this]() { dispatchTransferLoop(); });

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
	transferCv_.notify_all();
	if (transferDispatchThread_.joinable()) {
		transferDispatchThread_.join();
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

void SyncEngine::evaluateRemoteMetadata(
	const std::string& remoteDeviceId,
	const std::unordered_map<std::string, syncflow::engine::FileMetadata>& remote,
	const std::string& localDeviceId) {
	const std::string digest = buildRemoteDigest(remote);
	{
		std::lock_guard<std::mutex> lock(transferMutex_);
		auto it = remoteMetadataDigests_.find(remoteDeviceId);
		if (it != remoteMetadataDigests_.end() && it->second == digest) {
			Logger::debug("sync remote metadata skipped duplicate: device=" + remoteDeviceId);
			return;
		}
		remoteMetadataDigests_[remoteDeviceId] = digest;
	}

	auto actions = planAgainstRemote(remote, localDeviceId);
	const auto nowUnix = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch())
		                     .count();

	for (const auto& action : actions) {
		if (action.type == syncflow::engine::ActionType::None) {
			continue;
		}

		Logger::info("sync decision: device=" + remoteDeviceId + " action=" + actionName(action.type) +
		             " path=" + action.relativePath + " reason=" + action.reason);

		if ((action.type == syncflow::engine::ActionType::Upload ||
		     action.type == syncflow::engine::ActionType::DeleteRemote) &&
		    isRecentlyChangedLocally(action.relativePath)) {
			Logger::debug("sync loop-prevention skip: device=" + remoteDeviceId + " path=" + action.relativePath);
			continue;
		}

		if (action.type == syncflow::engine::ActionType::ConflictKeepBoth) {
			resolveConflictKeepBoth(action.relativePath, localDeviceId);
			TransferTask conflictDownload;
			conflictDownload.action = syncflow::engine::ActionType::Download;
			conflictDownload.remoteDeviceId = remoteDeviceId;
			conflictDownload.relativePath = action.relativePath;
			conflictDownload.reason = "conflict resolved via local rename, download remote";
			conflictDownload.decidedUnixSeconds = nowUnix;
			(void)enqueueTransferTask(conflictDownload);
			continue;
		}

		TransferTask task;
		task.action = action.type;
		task.remoteDeviceId = remoteDeviceId;
		task.relativePath = action.relativePath;
		task.reason = action.reason;
		task.decidedUnixSeconds = nowUnix;
		(void)enqueueTransferTask(task);
	}
}

void SyncEngine::setTransferHandler(TransferHandler handler) {
	std::lock_guard<std::mutex> lock(transferMutex_);
	transferHandler_ = std::move(handler);
}

std::optional<SyncEngine::TransferTask> SyncEngine::pollTransferTask() {
	std::lock_guard<std::mutex> lock(transferMutex_);
	if (transferQueue_.empty()) {
		return std::nullopt;
	}
	auto task = transferQueue_.front();
	transferQueue_.pop_front();
	queuedTransferKeys_.erase(task.remoteDeviceId + "|" + task.relativePath + "|" + actionName(task.action));
	return task;
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

void SyncEngine::dispatchTransferLoop() {
	for (;;) {
		TransferTask task;
		TransferHandler handler;
		{
			std::unique_lock<std::mutex> lock(transferMutex_);
			transferCv_.wait(lock, [this]() {
				std::lock_guard<std::mutex> stateLock(stateMutex_);
				return !transferQueue_.empty() || !running_;
			});

			if (transferQueue_.empty()) {
				std::lock_guard<std::mutex> stateLock(stateMutex_);
				if (!running_) {
					return;
				}
				continue;
			}

			task = transferQueue_.front();
			transferQueue_.pop_front();
			queuedTransferKeys_.erase(task.remoteDeviceId + "|" + task.relativePath + "|" + actionName(task.action));
			handler = transferHandler_;
		}

		if (!handler) {
			recordEvent("transfer queued (no handler): " + actionName(task.action) + " " + task.relativePath +
			            " -> " + task.remoteDeviceId);
			continue;
		}

		try {
			handler(task);
		} catch (...) {
			Logger::error("transfer handler threw exception: action=" + actionName(task.action) + " path=" +
			              task.relativePath + " device=" + task.remoteDeviceId);
		}
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
		markRecentLocalChange(relString);
		copyFileAsync(source, destination);
		recordEvent(std::string(isNew ? "file created: " : "file updated: ") + relString);
		Logger::info("sync copy scheduled: " + source.string() + " -> " + destination.string());
	}

	for (const auto& [relString, deletedEntry] : previous) {
		if (snapshot.find(relString) == snapshot.end()) {
			const auto destination = mirrorFolder_ / std::filesystem::path(relString);
			markRecentLocalChange(relString);
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

void SyncEngine::markRecentLocalChange(const std::string& relativePath) {
	std::lock_guard<std::mutex> lock(stateMutex_);
	const auto now = std::chrono::steady_clock::now();
	if (recentLocalChanges_.size() > 8192) {
		for (auto it = recentLocalChanges_.begin(); it != recentLocalChanges_.end();) {
			if ((now - it->second) > (loopPreventionWindow_ * 4)) {
				it = recentLocalChanges_.erase(it);
			} else {
				++it;
			}
		}
	}
	recentLocalChanges_[relativePath] = std::chrono::steady_clock::now();
}

bool SyncEngine::isRecentlyChangedLocally(const std::string& relativePath) const {
	std::lock_guard<std::mutex> lock(stateMutex_);
	auto it = recentLocalChanges_.find(relativePath);
	if (it == recentLocalChanges_.end()) {
		return false;
	}
	return (std::chrono::steady_clock::now() - it->second) <= loopPreventionWindow_;
}

bool SyncEngine::enqueueTransferTask(const TransferTask& task) {
	const std::string key = task.remoteDeviceId + "|" + task.relativePath + "|" + actionName(task.action);
	std::lock_guard<std::mutex> lock(transferMutex_);
	if (queuedTransferKeys_.find(key) != queuedTransferKeys_.end()) {
		return false;
	}

	if (transferQueue_.size() >= transferQueueMaxSize_) {
		const auto dropped = transferQueue_.front();
		queuedTransferKeys_.erase(dropped.remoteDeviceId + "|" + dropped.relativePath + "|" +
		                        actionName(dropped.action));
		transferQueue_.pop_front();
		Logger::warn("transfer queue full; dropping oldest task");
	}

	transferQueue_.push_back(task);
	queuedTransferKeys_.insert(key);
	transferCv_.notify_one();
	return true;
}

void SyncEngine::resolveConflictKeepBoth(const std::string& relativePath, const std::string& localDeviceId) {
	const auto original = sourceFolder_ / std::filesystem::path(relativePath);
	if (!std::filesystem::exists(original)) {
		return;
	}

	const auto now = std::chrono::system_clock::now();
	const auto unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
	const auto conflictPath = sourceFolder_ /
		(std::filesystem::path(relativePath).string() + ".conflict." + localDeviceId + "." +
		 std::to_string(unixSeconds));

	std::error_code ec;
	std::filesystem::create_directories(conflictPath.parent_path(), ec);
	ec.clear();
	std::filesystem::rename(original, conflictPath, ec);
	if (ec) {
		Logger::warn("sync conflict rename failed: " + original.string() + " -> " + conflictPath.string());
		return;
	}

	markRecentLocalChange(relativePath);
	Logger::warn("sync conflict resolved by keep-both rename: " + original.string() + " -> " +
	             conflictPath.string());
	recordEvent("sync conflict keep-both: " + relativePath);
}

std::string SyncEngine::buildRemoteDigest(
	const std::unordered_map<std::string, syncflow::engine::FileMetadata>& remote) const {
	std::vector<std::string> lines;
	lines.reserve(remote.size());
	for (const auto& [path, meta] : remote) {
		lines.push_back(path + "|" + std::to_string(meta.size) + "|" + std::to_string(meta.modifiedUnixSeconds) +
		                "|" + meta.hash + "|" + (meta.deleted ? "1" : "0"));
	}
	std::sort(lines.begin(), lines.end());
	std::string digest;
	for (const auto& line : lines) {
		digest += line;
		digest.push_back('\n');
	}
	return digest;
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
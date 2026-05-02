#include "core/Application.h"

#include "core/Logger.h"
#include "core/ThreadPool.h"
#include "networking/DeviceDiscovery.h"
#include "networking/TcpHandshake.h"

#include "platform/PlatformPaths.h"
#include "platform/PlatformSocket.h"

#include "security/AuthManager.h"

#include "sync_engine/RemoteSync.h"
#include "sync_engine/SyncEngine.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
std::atomic<bool> g_keepRunning{true};

void handleStopSignal(int) {
	g_keepRunning.store(false);
}

struct BasicFileMetadata {
	std::uint64_t size = 0;
	std::uint64_t modifiedMs = 0;
	std::string hash;
};

struct IncomingTransfer {
	std::filesystem::path targetPath;
	std::filesystem::path tempPath;
	std::unique_ptr<std::fstream> out;
	std::uint64_t expectedSize = 0;
	std::uint64_t expectedModifiedMs = 0;
	std::uint64_t written = 0;
	std::chrono::steady_clock::time_point lastActivity{};
};

struct OutboundTransfer {
	std::mutex mutex;
	std::condition_variable cv;
	std::uint64_t resumeOffset = 0;
	bool resumeKnown = false;
	bool cancelled = false;
	std::uint64_t fileSize = 0;
	std::uint64_t modifiedMs = 0;
	std::string relativePath;
	std::string peerId;
};

using MetadataSnapshot = std::map<std::string, BasicFileMetadata>;

std::optional<std::uint64_t> parseUnsigned64(const std::string& value) {
	try {
		std::size_t idx = 0;
		auto parsed = std::stoull(value, &idx);
		if (idx != value.size()) {
			return std::nullopt;
		}
		return static_cast<std::uint64_t>(parsed);
	} catch (...) {
		return std::nullopt;
	}
}

MetadataSnapshot buildLocalSnapshot(const std::filesystem::path& syncFolder, syncflow::engine::RemoteSync& remoteSync) {
	MetadataSnapshot snapshot;
	for (const auto& file : remoteSync.getLocalFileMetadata(syncFolder)) {
		if (file.isDirectory) {
			continue;
		}
		snapshot[file.path] = BasicFileMetadata{file.size, file.lastModifiedTime, file.hash};
	}
	return snapshot;
}

std::string serializeSnapshot(const MetadataSnapshot& snapshot) {
	std::string entries;
	for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
		const auto& [path, meta] = *it;
		if (path.find('|') != std::string::npos || path.find(',') != std::string::npos) {
			continue;
		}
		if (!entries.empty()) {
			entries.push_back(',');
		}
		entries += path + ":" + std::to_string(meta.size) + ":" + std::to_string(meta.modifiedMs) + ":" + meta.hash;
	}
	return entries;
}

std::string buildFilesMessage(const std::string& deviceId, const MetadataSnapshot& snapshot) {
	return "FILES|" + deviceId + "|" + serializeSnapshot(snapshot);
}

bool parseFilesMessage(const std::string& payload,
	                   std::string& deviceId,
	                   MetadataSnapshot& parsedSnapshot,
	                   std::string& entriesDigest) {
	if (payload.rfind("FILES|", 0) != 0) {
		return false;
	}

	const std::size_t firstPipe = payload.find('|');
	if (firstPipe == std::string::npos) {
		return false;
	}
	const std::size_t secondPipe = payload.find('|', firstPipe + 1);
	if (secondPipe == std::string::npos) {
		return false;
	}

	deviceId = payload.substr(firstPipe + 1, secondPipe - (firstPipe + 1));
	if (deviceId.empty()) {
		return false;
	}

	entriesDigest = payload.substr(secondPipe + 1);
	parsedSnapshot.clear();
	std::stringstream ss(entriesDigest);
	std::string token;
	while (std::getline(ss, token, ',')) {
		if (token.empty()) {
			continue;
		}

		const std::size_t lastColon = token.rfind(':');
		if (lastColon == std::string::npos) {
			continue;
		}
		const std::size_t secondLastColon = token.rfind(':', lastColon - 1);
		if (secondLastColon == std::string::npos) {
			continue;
		}
		const std::size_t thirdLastColon = token.rfind(':', secondLastColon - 1);
		if (thirdLastColon == std::string::npos) {
			continue;
		}

		const std::string path = token.substr(0, thirdLastColon);
		if (path.empty()) {
			continue;
		}

		auto size = parseUnsigned64(token.substr(thirdLastColon + 1, secondLastColon - thirdLastColon - 1));
		auto modified = parseUnsigned64(token.substr(secondLastColon + 1, lastColon - secondLastColon - 1));
		const std::string hash = token.substr(lastColon + 1);
		if (!size.has_value() || !modified.has_value() || hash.empty()) {
			continue;
		}

		parsedSnapshot[path] = BasicFileMetadata{*size, *modified, hash};
	}

	return true;
}

// Helper function to collect all files and directories to sync
std::vector<std::string> collectSyncPaths(const std::filesystem::path& syncFolder) {
	std::vector<std::string> paths;
	
	if (!std::filesystem::exists(syncFolder)) {
		return paths;
	}

	try {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(syncFolder)) {
			if (entry.is_regular_file()) {
				const auto relative = std::filesystem::relative(entry.path(), syncFolder);
				paths.push_back(relative.string());
			}
		}
	} catch (const std::exception& ex) {
		Logger::warn("Error scanning sync folder: " + std::string(ex.what()));
	}
	
	return paths;
}

void logSyncDecisions(const std::string& remoteDeviceId,
	                  const MetadataSnapshot& localSnapshot,
	                  const MetadataSnapshot& remoteSnapshot) {
	for (const auto& [path, local] : localSnapshot) {
		auto it = remoteSnapshot.find(path);
		if (it == remoteSnapshot.end()) {
			Logger::info("sync decision [upload missing]: " + path + " -> " + remoteDeviceId);
			continue;
		}

		const auto& remote = it->second;
		if (!local.hash.empty() && local.hash == remote.hash) {
			continue;
		}

		if (local.modifiedMs > remote.modifiedMs) {
			Logger::info("sync decision [upload content-changed]: " + path + " -> " + remoteDeviceId);
		} else if (remote.modifiedMs > local.modifiedMs) {
			Logger::info("sync decision [download content-changed]: " + path + " <- " + remoteDeviceId);
		} else {
			Logger::info("sync decision [conflict content mismatch]: " + path + " <-> " + remoteDeviceId);
		}
	}

	for (const auto& [path, remote] : remoteSnapshot) {
		(void)remote;
		if (localSnapshot.find(path) == localSnapshot.end()) {
			Logger::info("sync decision [download missing]: " + path + " <- " + remoteDeviceId);
		}
	}
}

bool isTransferPathSafe(const std::string& path) {
	if (path.empty() || path.size() > 4096) {
		return false;
	}
	if (path.find('|') != std::string::npos || path.find('\n') != std::string::npos ||
	    path.find('\r') != std::string::npos) {
		return false;
	}
	std::filesystem::path rel(path);
	if (rel.is_absolute()) {
		return false;
	}
	for (const auto& part : rel) {
		if (part == "..") {
			return false;
		}
	}
	return true;
}

std::filesystem::path resolveConfigRelativePath(const std::filesystem::path& value,
	                                           const std::optional<std::filesystem::path>& configPath) {
	if (value.is_absolute()) {
		return value;
	}

	std::filesystem::path baseDir = std::filesystem::current_path();
	if (configPath.has_value()) {
		const auto parent = configPath->parent_path();
		if (!parent.empty()) {
			baseDir = parent.is_absolute() ? parent : std::filesystem::absolute(parent);
		}
	}

	return std::filesystem::absolute(baseDir / value);
}

std::vector<std::string> splitByPipe(const std::string& value) {
	std::vector<std::string> out;
	std::stringstream ss(value);
	std::string token;
	while (std::getline(ss, token, '|')) {
		out.push_back(token);
	}
	return out;
}

std::string bytesToHex(const std::vector<char>& bytes) {
	static constexpr char hex[] = "0123456789abcdef";
	std::string out;
	out.reserve(bytes.size() * 2);
	for (unsigned char b : bytes) {
		out.push_back(hex[(b >> 4) & 0x0f]);
		out.push_back(hex[b & 0x0f]);
	}
	return out;
}

int hexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return 10 + (c - 'a');
	}
	if (c >= 'A' && c <= 'F') {
		return 10 + (c - 'A');
	}
	return -1;
}

std::optional<std::vector<char>> hexToBytes(const std::string& hex) {
	if (hex.size() % 2 != 0) {
		return std::nullopt;
	}
	std::vector<char> out;
	out.reserve(hex.size() / 2);
	for (std::size_t i = 0; i < hex.size(); i += 2) {
		const int hi = hexValue(hex[i]);
		const int lo = hexValue(hex[i + 1]);
		if (hi < 0 || lo < 0) {
			return std::nullopt;
		}
		out.push_back(static_cast<char>((hi << 4) | lo));
	}
	return out;
}

std::string transferKey(const std::string& peerId, const std::string& relPath) {
	return peerId + "|" + relPath;
}

std::string joinStrings(const std::vector<std::string>& values, const std::string& delimiter) {
	std::string out;
	for (const auto& value : values) {
		if (!out.empty()) {
			out += delimiter;
		}
		out += value;
	}
	return out;
}

std::string describePeer(const std::string& deviceId, const std::string& deviceName, const std::string& ip,
	                     std::uint16_t port) {
	std::string label = deviceName.empty() ? deviceId : deviceName;
	label += " (id=" + deviceId + ", ip=" + ip + ", port=" + std::to_string(port) + ")";
	return label;
}

std::string describePeer(const DeviceDiscovery::PeerInfo& peer) {
	return describePeer(peer.deviceId, peer.deviceName, peer.ip, peer.port);
}

std::string describePeer(const TcpHandshake::RemoteDevice& peer) {
	return describePeer(peer.deviceId, peer.deviceName, peer.ip, peer.port);
}

std::string tempTransferPath(const std::filesystem::path& targetPath, const std::string& peerId) {
	return targetPath.string() + ".part." + peerId;
}
}  // namespace

bool Application::init() {
	platform::PlatformPaths::initialize("syncflow");

	const bool configLoaded = config_.load("config.json");
	const auto configPath = config_.loadedPath();

	const std::string appName = config_.getString("app_name", "SyncFlow");
	platform::PlatformPaths::initialize(appName);

	auto defaultLogDir = platform::PlatformPaths::getLogDir().value_or(std::filesystem::path("log"));
	auto defaultDataDir = platform::PlatformPaths::getDataDir().value_or(std::filesystem::path("syncflow_data"));
	auto defaultSyncDir = defaultDataDir / "sync";

	auto configuredLogFolder = config_.getString("log_folder", defaultLogDir.string());
	std::filesystem::path logFolder(resolveConfigRelativePath(std::filesystem::path(configuredLogFolder), configPath));

	const std::string logLevel = config_.getString("log_level", "info");
	const bool syncDataOnlyLogs = config_.getInt("sync_data_only_logs", 0) != 0;
	Logger::init(logFolder.string());
	Logger::setLevel(logLevel);
	Logger::setSyncDataOnly(syncDataOnlyLogs);

	if (!configLoaded) {
		Logger::warn("config.json could not be loaded; using defaults");
	}

	const std::string deviceName = config_.getString("device_name", "unknown-device");
	const int configuredPort = config_.getInt("port", 8080);
	std::filesystem::path syncFolderPath(resolveConfigRelativePath(
		std::filesystem::path(config_.getString("sync_folder", defaultSyncDir.string())), configPath));
	const std::string syncFolder = syncFolderPath.string();
	const std::string securitySecret = config_.getString("security_shared_secret", "change-me-in-production");

	Logger::info("Application initialized");
	Logger::info("app_name: " + appName);
	Logger::info("device_name: " + deviceName);
	Logger::info("port: " + std::to_string(configuredPort));
	Logger::info("sync_folder: " + syncFolder);
	Logger::info("log_level: " + logLevel);
	const std::filesystem::path mirrorFolderPath = resolveConfigRelativePath(std::filesystem::path(config_.getString(
		"mirror_folder", (syncFolderPath / ".syncflow_mirror").string())), configPath);
	Logger::info("mirror_folder: " + mirrorFolderPath.string());
	if (securitySecret == "change-me-in-production") {
		Logger::warn("security_shared_secret is using default value; update it for production");
	}

	std::cout << "app_name: " << appName << '\n'
	          << "device_name: " << deviceName << '\n'
	          << "port: " << configuredPort << '\n'
	          << "sync_folder: " << syncFolder << '\n'
	          << "log_level: " << logLevel << std::endl;

	initialized_ = true;
	return true;
}

int Application::run() {
	if (!initialized_) {
		Logger::warn("Application::run() called before init()");
		return 1;
	}

	Logger::info("Application started: " + config_.getString("app_name", "SyncFlow"));
	const std::string deviceName = config_.getString("device_name", "unknown-device");
	const int configuredPort = config_.getInt("port", 8080);
	int broadcastIntervalMs = config_.getInt("broadcast_interval_ms", 2000);
	const std::string syncFolder = config_.getString("sync_folder", "./sync");
	const auto configPath = config_.loadedPath();
	std::filesystem::path syncFolderPath(resolveConfigRelativePath(std::filesystem::path(syncFolder), configPath));
	std::filesystem::path mirrorFolderPathRun(resolveConfigRelativePath(
		std::filesystem::path(config_.getString("mirror_folder", (syncFolderPath / ".syncflow_mirror").string())),
		configPath));
	const std::string mirrorFolder = mirrorFolderPathRun.string();
	const std::string securitySecret = config_.getString("security_shared_secret", "change-me-in-production");
	unsigned int hwThreads = std::thread::hardware_concurrency();
	if (hwThreads == 0) {
		hwThreads = 4;
	}
	int transferWorkerThreads =
		config_.getInt("transfer_worker_threads", static_cast<int>(hwThreads));
	if (transferWorkerThreads < 1) {
		transferWorkerThreads = 1;
	}
	if (broadcastIntervalMs < 200) {
		broadcastIntervalMs = 200;
	}
	Logger::info("Configured port: " + std::to_string(configuredPort));
	Logger::info("Broadcast interval (ms): " + std::to_string(broadcastIntervalMs));
	Logger::info("Transfer worker threads: " + std::to_string(transferWorkerThreads));
	Logger::info("Discovery is using broadcast probe routing and TCP verification");
	Logger::info("Sync source folder: " + syncFolder);
	Logger::info("Sync mirror folder: " + mirrorFolder);

	const auto nowSeconds = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch())
			.count());
	std::mt19937_64 rng(std::random_device{}());
	syncflow::security::AuthManager auth(securitySecret, 120);
	const auto startupToken = auth.issue(deviceName, nowSeconds, rng());
	if (!auth.verify(startupToken, nowSeconds)) {
		Logger::error("security self-check failed");
		return 1;
	}
	Logger::info("security self-check passed");

	DeviceDiscovery discovery(deviceName, static_cast<std::uint16_t>(configuredPort));
	const std::string localDeviceId = discovery.getDeviceId();
	Logger::info("Local device_id: " + localDeviceId);
	TcpHandshake tcp(localDeviceId, deviceName, static_cast<std::uint16_t>(configuredPort));
	const bool tcpStarted = tcp.start();
	if (!tcpStarted) {
		Logger::warn("TCP handshake listener failed to start on port " + std::to_string(configuredPort) +
		             "; continuing with local-only sync mode");
		Logger::info("Connection status: tcp_listener=disabled discovery=active connected_peers=0");
	} else {
		Logger::info("Connection status: tcp_listener=active discovery=active connected_peers=0");
	}

	SyncEngine syncEngine(syncFolder, mirrorFolder);
	if (!syncEngine.start()) {
		Logger::warn("Sync engine failed to start; continuing without file mirroring");
	}

	const auto syncFilesAtStartup = collectSyncPaths(syncFolderPath);
	Logger::info("Sync folder files at startup: " + std::to_string(syncFilesAtStartup.size()));
	if (syncFilesAtStartup.empty()) {
		Logger::info("Sync folder is empty at startup");
	}

	const auto localAddresses = platform::PlatformSocket::getLocalAddresses();
	if (localAddresses.has_value() && !localAddresses->empty()) {
		std::string joinedAddresses;
		for (const auto& address : *localAddresses) {
			if (!joinedAddresses.empty()) {
				joinedAddresses += ", ";
			}
			joinedAddresses += address;
		}
		Logger::info("Local network address(es): " + joinedAddresses);
	} else {
		Logger::warn("Local network address could not be detected; discovery may still work via broadcast");
	}
	Logger::info("Peer discovery status: waiting for another device");

	syncflow::engine::RemoteSync remoteSync;
	std::mutex remoteMetadataMutex;
	std::unordered_map<std::string, MetadataSnapshot> remoteMetadataByDevice;
	std::unordered_map<std::string, std::string> lastRemoteMetadataDigest;
	std::mutex transferMutex;
	std::unordered_map<std::string, std::shared_ptr<IncomingTransfer>> incomingTransfers;
	std::unordered_map<std::string, std::shared_ptr<OutboundTransfer>> outboundTransfers;
	std::unordered_set<std::string> outboundInProgress;
	std::unordered_set<std::string> pendingDownloadRequests;
	ThreadPool transferWorkerPool(static_cast<std::size_t>(transferWorkerThreads));

	g_keepRunning.store(true);
	std::signal(SIGINT, handleStopSignal);
	std::signal(SIGTERM, handleStopSignal);

	Logger::info("Discovery loop started. Press Ctrl+C to stop.");
	Logger::info("Waiting for another device to connect...");
	Logger::info("Connection status: known_devices=0 tcp_connected_peers=0 discovery=active");
	std::mutex knownDevicesMutex;
	std::unordered_map<std::string, DeviceDiscovery::PeerInfo> knownDevices;
	bool syncFolderInitialSyncTriggered = false;
	
	// Sync completion tracking
	std::mutex syncCompletionMutex;
	std::unordered_map<std::string, size_t> expectedFilesPerDevice;  // peerId -> count
	std::unordered_map<std::string, size_t> completedFilesPerDevice; // peerId -> count
	std::unordered_set<std::string> devicesSyncCompleted;            // track completed syncs

	std::thread senderThread([&discovery, broadcastIntervalMs]() {
		while (g_keepRunning.load()) {
			if (!discovery.sender()) {
				Logger::warn("Thread 1: Broadcast sender failed");
			} else {
				Logger::debug("Thread 1: Broadcast probe sent");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(broadcastIntervalMs));
		}
	});

	std::thread listenerThread([&discovery,
	                           &tcp,
	                           &knownDevicesMutex,
	                           &knownDevices,
	                           &remoteSync,
	                           &remoteMetadataMutex,
	                           &remoteMetadataByDevice,
	                           &lastRemoteMetadataDigest,
	                           &localDeviceId,
	                           &transferMutex,
	                           &incomingTransfers,
	                           &outboundInProgress,
	                           &pendingDownloadRequests,
	                           &outboundTransfers,
	                           &transferWorkerPool,
	                           &syncFolder,
	                           &syncFolderInitialSyncTriggered,
	                           &syncCompletionMutex,
	                           &expectedFilesPerDevice,
	                           &completedFilesPerDevice,
	                           &devicesSyncCompleted]() {
		auto nextMetadataBroadcast = std::chrono::steady_clock::now();
		auto nextForcedMetadataBroadcast = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		auto nextTransferSweep = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		auto nextPeerStatusLog = std::chrono::steady_clock::now();
		auto nextConnectionStatusLog = std::chrono::steady_clock::now();
		std::string lastLocalPayload;

		auto enqueueSendFile = [&](const std::string& peerId, const std::string& relPath) {
			if (!isTransferPathSafe(relPath)) {
				return;
			}
			const std::string key = transferKey(peerId, relPath);
			{
				std::lock_guard<std::mutex> lock(transferMutex);
				if (outboundInProgress.find(key) != outboundInProgress.end()) {
					return;
				}
				outboundInProgress.insert(key);
			}

			try {
				(void)transferWorkerPool.enqueue([&, peerId, relPath, key]() {
				auto release = [&](bool keepState = false) {
					std::lock_guard<std::mutex> lock(transferMutex);
					outboundInProgress.erase(key);
					if (!keepState) {
						outboundTransfers.erase(key);
					}
				};

				const std::filesystem::path target = std::filesystem::path(syncFolder) / relPath;
				if (!std::filesystem::exists(target) || !std::filesystem::is_regular_file(target)) {
					release();
					return;
				}

				std::error_code ec;
				const auto fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(target, ec));
				if (ec) {
					release();
					return;
				}
				auto info = remoteSync.getFileInfo(target);
				const std::uint64_t modifiedMs = info.lastModifiedTime;

				auto state = std::make_shared<OutboundTransfer>();
				state->fileSize = fileSize;
				state->modifiedMs = modifiedMs;
				state->relativePath = relPath;
				state->peerId = peerId;
				{
					std::lock_guard<std::mutex> lock(transferMutex);
					outboundTransfers[key] = state;
				}

				if (!tcp.sendMessage(peerId,
				                     "SEND|" + relPath + "|" + std::to_string(fileSize) + "|0|" +
				                         std::to_string(modifiedMs))) {
					Logger::warn("transfer send failed (SEND): " + relPath + " -> " + peerId);
					release();
					return;
				}

				std::uint64_t resumeOffset = 0;
				{
					std::unique_lock<std::mutex> lock(state->mutex);
					if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]() {
						return state->resumeKnown || state->cancelled || !g_keepRunning.load();
					})) {
						Logger::warn("transfer SEND timed out waiting for resume: " + relPath + " -> " + peerId);
						release();
						return;
					}
					if (state->cancelled || !g_keepRunning.load()) {
						Logger::warn("transfer SEND cancelled before streaming: " + relPath + " -> " + peerId);
						release();
						return;
					}
					resumeOffset = state->resumeOffset;
				}

				if (resumeOffset > fileSize) {
					Logger::warn("transfer send invalid resume offset: " + relPath + " -> " + peerId);
					release();
					return;
				}

				std::ifstream in(target, std::ios::binary);
				if (!in.is_open()) {
					Logger::warn("transfer send failed (open): " + target.string());
					release();
					return;
				}
				in.seekg(static_cast<std::streamoff>(resumeOffset), std::ios::beg);
				if (!in.good()) {
					Logger::warn("transfer send failed (seek): " + target.string());
					release();
					return;
				}

				constexpr std::size_t kChunkSize = 4096;
				std::vector<char> buf(kChunkSize);
				std::uint64_t offset = resumeOffset;
				while (in.good()) {
					in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
					const auto n = static_cast<std::size_t>(in.gcount());
					if (n == 0) {
						break;
					}
					std::vector<char> chunk(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
					if (!tcp.sendMessage(peerId,
					                     "DATA|" + relPath + "|" + std::to_string(offset) + "|" +
					                         bytesToHex(chunk))) {
						Logger::warn("transfer send failed (DATA): " + relPath + " -> " + peerId);
						release();
						return;
					}
					offset += static_cast<std::uint64_t>(n);
				}

				if (!tcp.sendMessage(peerId,
				                     "DONE|" + relPath + "|" + std::to_string(fileSize) + "|" +
				                         std::to_string(modifiedMs))) {
					Logger::warn("transfer send failed (DONE): " + relPath + " -> " + peerId);
					release();
					return;
				}

				Logger::info("transfer sent: " + relPath + " -> " + peerId + " bytes=" + std::to_string(fileSize) +
				             " resume=" + std::to_string(resumeOffset));
				release();
				});
			} catch (const std::exception& ex) {
				Logger::warn("transfer queue rejected: " + relPath + " -> " + peerId + " reason=" + ex.what());
				std::lock_guard<std::mutex> lock(transferMutex);
				outboundInProgress.erase(key);
				outboundTransfers.erase(key);
			}
		};

		while (g_keepRunning.load()) {
			const auto now = std::chrono::steady_clock::now();
			tcp.tick();
			while (auto evt = tcp.pollEvent()) {
				Logger::info(*evt);
			}

			if (now >= nextConnectionStatusLog) {
				const auto connectedPeers = tcp.getConnectedPeers();
				std::vector<std::string> connectedPeerLabels;
				connectedPeerLabels.reserve(connectedPeers.size());
				for (const auto& peer : connectedPeers) {
					connectedPeerLabels.push_back(describePeer(peer));
				}
				size_t knownDeviceCount = 0;
				{
					std::lock_guard<std::mutex> lock(knownDevicesMutex);
					knownDeviceCount = knownDevices.size();
				}

				Logger::info("Connection status: known_devices=" + std::to_string(knownDeviceCount) +
				             " tcp_connected_peers=" + std::to_string(connectedPeers.size()) +
				             " discovery=active connected_peer_list=[" +
				             joinStrings(connectedPeerLabels, ", ") + "]");
				if (connectedPeers.empty() && knownDeviceCount == 0) {
					Logger::info("Termux/mobile note: if no peers appear, broadcast discovery may be blocked by the network");
				}
				nextConnectionStatusLog = now + std::chrono::seconds(5);
			}

			while (auto inbound = tcp.pollMessage()) {
				const std::string& peerId = inbound->deviceId;
				const std::string& msg = inbound->payload;

				if (msg.rfind("READY|", 0) == 0 || msg.rfind("RESUME|", 0) == 0) {
					auto parts = splitByPipe(msg);
					if (parts.size() < 4 || !isTransferPathSafe(parts[1])) {
						Logger::warn("transfer READY/RESUME ignored (malformed): from=" + peerId);
						continue;
					}

					auto offset = parseUnsigned64(parts[2]);
					auto fileSize = parseUnsigned64(parts[3]);
					auto modifiedMs = parts.size() >= 5 ? parseUnsigned64(parts[4]) : std::optional<std::uint64_t>{};
					if (!offset.has_value() || !fileSize.has_value()) {
						Logger::warn("transfer READY/RESUME ignored (invalid numbers): from=" + peerId);
						continue;
					}

					const std::string key = transferKey(peerId, parts[1]);
					std::shared_ptr<OutboundTransfer> state;
					{
						std::lock_guard<std::mutex> lock(transferMutex);
						auto it = outboundTransfers.find(key);
						if (it != outboundTransfers.end()) {
							state = it->second;
						}
					}

					if (!state) {
						continue;
					}

					{
						std::lock_guard<std::mutex> lock(state->mutex);
						state->resumeOffset = *offset;
						state->resumeKnown = true;
						state->fileSize = *fileSize;
						if (modifiedMs.has_value()) {
							state->modifiedMs = *modifiedMs;
						}
					}
					state->cv.notify_all();
					Logger::info(std::string(msg.rfind("RESUME|", 0) == 0 ? "transfer resume accepted: " : "transfer ready: ") +
					             parts[1] + " <- " + peerId + " offset=" + std::to_string(*offset));
					continue;
				}

				if (msg.rfind("GET|", 0) == 0) {
					auto parts = splitByPipe(msg);
					if (parts.size() != 2 || !isTransferPathSafe(parts[1])) {
						Logger::warn("transfer GET ignored (malformed): from=" + peerId);
						continue;
					}
					enqueueSendFile(peerId, parts[1]);
					continue;
				}

				if (msg.rfind("SEND|", 0) == 0) {
					auto parts = splitByPipe(msg);
					if ((parts.size() != 4 && parts.size() != 5) || !isTransferPathSafe(parts[1])) {
						Logger::warn("transfer SEND ignored (malformed): from=" + peerId);
						continue;
					}

					auto fileSize = parseUnsigned64(parts[2]);
					auto senderOffset = parseUnsigned64(parts[3]);
					auto modifiedMs = parts.size() >= 5 ? parseUnsigned64(parts[4]) : std::optional<std::uint64_t>{0};
					if (!fileSize.has_value() || !senderOffset.has_value() || !modifiedMs.has_value()) {
						Logger::warn("transfer SEND ignored (invalid numbers): from=" + peerId);
						continue;
					}

					const auto relPath = std::filesystem::path(parts[1]);
					const auto targetPath = std::filesystem::path(syncFolder) / relPath;
					const auto tempPath = std::filesystem::path(tempTransferPath(targetPath, peerId));
					std::error_code ec;
					std::filesystem::create_directories(targetPath.parent_path(), ec);
					if (ec) {
						Logger::warn("transfer SEND ignored (mkdir failed): " + targetPath.parent_path().string());
						continue;
					}

					std::shared_ptr<IncomingTransfer> state;
					std::uint64_t resumeOffset = 0;
					{
						std::lock_guard<std::mutex> lock(transferMutex);
						auto& slot = incomingTransfers[transferKey(peerId, parts[1])];
						if (!slot) {
							slot = std::make_shared<IncomingTransfer>();
						}
						const bool sameTransfer = slot->targetPath == targetPath && slot->tempPath == tempPath &&
						                         slot->expectedSize == *fileSize && slot->expectedModifiedMs == *modifiedMs;

						if (!sameTransfer) {
							if (slot->out && slot->out->is_open()) {
								slot->out->close();
							}
							slot = std::make_shared<IncomingTransfer>();
							slot->targetPath = targetPath;
							slot->tempPath = tempPath;
							slot->expectedSize = *fileSize;
							slot->expectedModifiedMs = *modifiedMs;
						}

						std::error_code fileEc;
						const bool existed = std::filesystem::exists(tempPath, fileEc) && !fileEc;
						std::uint64_t existingSize = existed ? static_cast<std::uint64_t>(std::filesystem::file_size(tempPath, fileEc)) : 0;
						if (fileEc) {
							fileEc.clear();
						}

						if (!sameTransfer && existed) {
							std::filesystem::remove(tempPath, fileEc);
							existingSize = 0;
						}

						if (!slot->out || !slot->out->is_open()) {
							slot->out = std::make_unique<std::fstream>();
							slot->out->open(tempPath, std::ios::binary | std::ios::in | std::ios::out);
							if (!slot->out->is_open()) {
								slot->out->clear();
								slot->out->open(tempPath, std::ios::binary | std::ios::out | std::ios::trunc);
								slot->out->close();
								slot->out->open(tempPath, std::ios::binary | std::ios::in | std::ios::out);
							}
						}

						if (!slot->out || !slot->out->is_open()) {
							Logger::warn("transfer SEND ignored (temp open failed): " + tempPath.string());
							continue;
						}

						resumeOffset = std::max(existingSize, *senderOffset);
						if (resumeOffset > *fileSize) {
							resumeOffset = *fileSize;
						}

						slot->written = resumeOffset;
						slot->lastActivity = std::chrono::steady_clock::now();
						state = slot;
					}

					if (!state || !state->out || !state->out->is_open()) {
						Logger::warn("transfer SEND ignored (state unavailable): " + tempPath.string());
						continue;
					}

					state->out->seekp(static_cast<std::streamoff>(resumeOffset), std::ios::beg);
					if (!state->out->good()) {
						Logger::warn("transfer SEND ignored (seek failed): " + tempPath.string());
						continue;
					}

					const std::string responseType = resumeOffset > 0 ? "RESUME" : "READY";
					if (!tcp.sendMessage(peerId, responseType + "|" + parts[1] + "|" + std::to_string(resumeOffset) +
					                               "|" + std::to_string(*fileSize) + "|" + std::to_string(*modifiedMs))) {
						Logger::warn("transfer SEND response failed: " + parts[1] + " <- " + peerId);
						continue;
					}

					Logger::info("transfer send accepted: " + parts[1] + " <- " + peerId +
					             " offset=" + std::to_string(resumeOffset) +
					             " size=" + std::to_string(*fileSize));
					continue;
				}

				if (msg.rfind("DATA|", 0) == 0) {
					auto parts = splitByPipe(msg);
					if (parts.size() != 4 || !isTransferPathSafe(parts[1])) {
						Logger::warn("transfer DATA ignored (malformed): from=" + peerId);
						continue;
					}

					auto offset = parseUnsigned64(parts[2]);
					auto chunk = hexToBytes(parts[3]);
					if (!offset.has_value() || !chunk.has_value()) {
						Logger::warn("transfer DATA ignored (decode failed): from=" + peerId);
						continue;
					}

					const std::string key = transferKey(peerId, parts[1]);
					std::lock_guard<std::mutex> lock(transferMutex);
					auto it = incomingTransfers.find(key);
					if (it == incomingTransfers.end() || !it->second) {
						continue;
					}

					auto st = it->second;
					if (!st || !st->out || !st->out->is_open()) {
						Logger::warn("transfer DATA ignored (stream closed): " + parts[1] + " <- " + peerId);
						continue;
					}

					std::error_code diskEc;
					const auto diskSize = std::filesystem::exists(st->tempPath, diskEc) && !diskEc
						                      ? static_cast<std::uint64_t>(std::filesystem::file_size(st->tempPath, diskEc))
						                      : st->written;
					if (diskEc) {
						diskEc.clear();
					}

					if (*offset < st->written) {
						Logger::debug("transfer DATA duplicate chunk ignored: " + parts[1] + " <- " + peerId +
						              " offset=" + std::to_string(*offset));
						continue;
					}
					if (*offset != st->written && diskSize == *offset) {
						st->out->seekp(static_cast<std::streamoff>(*offset), std::ios::beg);
						st->written = *offset;
					}
					if (*offset != st->written) {
						Logger::warn("transfer DATA aborted (offset/state mismatch): " + parts[1] + " <- " + peerId);
						continue;
					}

					st->out->write(chunk->data(), static_cast<std::streamsize>(chunk->size()));
					if (!st->out->good()) {
						st->out->close();
						Logger::warn("transfer DATA aborted (write error): " + parts[1] + " <- " + peerId);
						continue;
					}

					st->written += static_cast<std::uint64_t>(chunk->size());
					st->lastActivity = std::chrono::steady_clock::now();
					continue;
				}

				if (msg.rfind("DONE|", 0) == 0) {
					auto parts = splitByPipe(msg);
					if (parts.size() != 4 || !isTransferPathSafe(parts[1])) {
						Logger::warn("transfer DONE ignored (malformed): from=" + peerId);
						continue;
					}

					auto fileSize = parseUnsigned64(parts[2]);
					auto modifiedMs = parseUnsigned64(parts[3]);
					if (!fileSize.has_value() || !modifiedMs.has_value()) {
						Logger::warn("transfer DONE ignored (invalid numbers): from=" + peerId);
						continue;
					}

					const std::string key = transferKey(peerId, parts[1]);
					std::shared_ptr<IncomingTransfer> st;
					bool ok = false;
					{
						std::lock_guard<std::mutex> lock(transferMutex);
						auto it = incomingTransfers.find(key);
						if (it != incomingTransfers.end() && it->second) {
							st = it->second;
							incomingTransfers.erase(it);
							ok = true;
						}
					}
					if (!ok) {
						continue;
					}

					if (!st || !st->out || !st->out->is_open()) {
						Logger::warn("transfer DONE ignored (stream closed): " + parts[1] + " <- " + peerId);
						continue;
					}
					st->out->flush();
					st->out->close();

					if (st->written != *fileSize || st->expectedSize != *fileSize) {
						std::error_code rmEc;
						std::filesystem::remove(st->tempPath, rmEc);
						Logger::warn("transfer DONE aborted (size mismatch): " + parts[1] + " <- " + peerId);
						continue;
					}

					std::error_code ec;
					if (std::filesystem::exists(st->targetPath, ec) && !ec) {
						std::filesystem::remove(st->targetPath, ec);
					}
					std::filesystem::rename(st->tempPath, st->targetPath, ec);
					if (ec) {
						std::filesystem::remove(st->tempPath, ec);
						Logger::warn("transfer DONE failed finalize: " + st->targetPath.string());
						continue;
					}

					{
						std::lock_guard<std::mutex> lock(transferMutex);
						pendingDownloadRequests.erase(key);
						incomingTransfers.erase(key);
					}
					Logger::info("transfer received: " + parts[1] + " <- " + peerId + " bytes=" +
					             std::to_string(*fileSize));
					
					// Track sync completion
					{
						std::lock_guard<std::mutex> lock(syncCompletionMutex);
						if (expectedFilesPerDevice.find(peerId) != expectedFilesPerDevice.end()) {
							completedFilesPerDevice[peerId]++;
							size_t completed = completedFilesPerDevice[peerId];
							size_t expected = expectedFilesPerDevice[peerId];
							
							// Show progress
							Logger::info("SYNC PROGRESS: [" + std::to_string(completed) + "/" + 
							             std::to_string(expected) + "] files completed from " + peerId);
							
							// Check if all files synced
							if (completed >= expected && devicesSyncCompleted.find(peerId) == devicesSyncCompleted.end()) {
								devicesSyncCompleted.insert(peerId);
								Logger::info("╔════════════════════════════════════════════╗");
								Logger::info("║  SYNC COMPLETE - All files transferred! ✅  ║");
								Logger::info("║  Device: " + peerId);
								Logger::info("║  Total: " + std::to_string(expected) + " files synced");
								Logger::info("║  Location: " + syncFolder);
								Logger::info("╚════════════════════════════════════════════╝");
							}
						}
					}
					continue;
				}

				std::string messageDeviceId;
				MetadataSnapshot remoteSnapshot;
				std::string digest;
				if (!parseFilesMessage(msg, messageDeviceId, remoteSnapshot, digest)) {
					Logger::warn("metadata message ignored (malformed envelope): from=" + peerId);
					continue;
				}

				if (messageDeviceId != peerId) {
					Logger::warn("metadata message ignored (device mismatch): from=" + peerId + " claims=" +
					             messageDeviceId);
					continue;
				}

				bool duplicate = false;
				{
					std::lock_guard<std::mutex> lock(remoteMetadataMutex);
					auto it = lastRemoteMetadataDigest.find(peerId);
					if (it != lastRemoteMetadataDigest.end() && it->second == digest) {
						duplicate = true;
					} else {
						lastRemoteMetadataDigest[peerId] = digest;
						remoteMetadataByDevice[peerId] = remoteSnapshot;
					}
				}

				if (duplicate) {
					continue;
				}

				auto localSnapshot = buildLocalSnapshot(std::filesystem::path(syncFolder), remoteSync);
				logSyncDecisions(peerId, localSnapshot, remoteSnapshot);

				for (const auto& [path, local] : localSnapshot) {
					auto rit = remoteSnapshot.find(path);
					if (rit == remoteSnapshot.end()) {
						enqueueSendFile(peerId, path);
						continue;
					}
					const auto& remote = rit->second;
					if (!local.hash.empty() && local.hash == remote.hash) {
						continue;
					}
					if (local.modifiedMs > remote.modifiedMs) {
						enqueueSendFile(peerId, path);
					} else if (remote.modifiedMs > local.modifiedMs) {
						const std::string key = transferKey(peerId, path);
						bool alreadyRequested = false;
						{
							std::lock_guard<std::mutex> lock(transferMutex);
							alreadyRequested = pendingDownloadRequests.find(key) != pendingDownloadRequests.end();
							if (!alreadyRequested) {
								pendingDownloadRequests.insert(key);
							}
						}
						if (!alreadyRequested) {
							(void)tcp.sendMessage(peerId, "GET|" + path);
						}
					}
				}

				for (const auto& [path, remote] : remoteSnapshot) {
					(void)remote;
					if (localSnapshot.find(path) == localSnapshot.end()) {
						const std::string key = transferKey(peerId, path);
						bool alreadyRequested = false;
						{
							std::lock_guard<std::mutex> lock(transferMutex);
							alreadyRequested = pendingDownloadRequests.find(key) != pendingDownloadRequests.end();
							if (!alreadyRequested) {
								pendingDownloadRequests.insert(key);
							}
						}
						if (!alreadyRequested) {
							(void)tcp.sendMessage(peerId, "GET|" + path);
						}
					}
				}
			}

			if (now >= nextTransferSweep) {
				std::vector<std::string> expiredKeys;
				{
					std::lock_guard<std::mutex> lock(transferMutex);
						for (const auto& [k, st] : incomingTransfers) {
							if (st && now - st->lastActivity > std::chrono::seconds(30)) {
								expiredKeys.push_back(k);
							}
						}
						for (const auto& k : expiredKeys) {
							auto it = incomingTransfers.find(k);
							if (it != incomingTransfers.end() && it->second) {
								if (it->second->out && it->second->out->is_open()) {
									it->second->out->close();
								}
								std::error_code rmEc;
								std::filesystem::remove(it->second->tempPath, rmEc);
								incomingTransfers.erase(it);
							}
						}
				}
				nextTransferSweep = now + std::chrono::seconds(5);
			}
			if (now >= nextMetadataBroadcast) {
				auto localSnapshot = buildLocalSnapshot(std::filesystem::path(syncFolder), remoteSync);
				const std::string payload = buildFilesMessage(localDeviceId, localSnapshot);
				const bool changed = payload != lastLocalPayload;

				if (changed || now >= nextForcedMetadataBroadcast) {
					auto peers = tcp.getConnectedPeers();
					for (const auto& peer : peers) {
						(void)tcp.sendMessage(peer.deviceId, payload);
					}
					lastLocalPayload = payload;
					nextForcedMetadataBroadcast = now + std::chrono::seconds(15);
				}

				nextMetadataBroadcast = now + std::chrono::seconds(3);
			}

			auto peer = discovery.receiver(800);
			if (!peer.has_value()) {
				if (now >= nextPeerStatusLog) {
					Logger::info("Waiting for another device to connect...");
					nextPeerStatusLog = now + std::chrono::seconds(10);
				}
				continue;
			}

			std::string event = "discovered";
			bool isNewDevice = false;
			{
				std::lock_guard<std::mutex> lock(knownDevicesMutex);
				auto it = knownDevices.find(peer->deviceId);
				if (it == knownDevices.end()) {
					knownDevices.emplace(peer->deviceId, *peer);
					isNewDevice = true;
				} else {
					event = "updated";
					it->second = *peer;
				}
			}

			Logger::info("Peer " + event + ": " + describePeer(*peer));
			Logger::info("Connection status: peer=" + describePeer(*peer) +
			             " known_devices=" + std::to_string(knownDevices.size()) +
			             " tcp_connected_peers=" + std::to_string(tcp.getConnectedPeers().size()));
			
			// Trigger initial sync folder scan and transfer when second device connects
			if (isNewDevice && knownDevices.size() == 2 && !syncFolderInitialSyncTriggered) {
				syncFolderInitialSyncTriggered = true;
				
				auto syncPaths = collectSyncPaths(std::filesystem::path(syncFolder));
				Logger::info("Initial sync folder scan triggered for " + describePeer(*peer) +
				             ". Found " + std::to_string(syncPaths.size()) +
				             " files to sync with the second connected device");
				
				if (!syncPaths.empty()) {
					Logger::info("File transfer status: preparing initial sync for " + describePeer(*peer));
					Logger::info("sync folder content to transfer:");
					for (const auto& path : syncPaths) {
						Logger::info("  - " + path);
					}
					
					// Track expected files for this peer
					{
						std::lock_guard<std::mutex> lock(syncCompletionMutex);
						expectedFilesPerDevice[peer->deviceId] = syncPaths.size();
						completedFilesPerDevice[peer->deviceId] = 0;
					}
				} else {
					Logger::info("No files to sync - sync folder is empty!");
					Logger::info("File transfer status: nothing to transfer for " + describePeer(*peer));
					{
						std::lock_guard<std::mutex> lock(syncCompletionMutex);
						devicesSyncCompleted.insert(peer->deviceId);
						Logger::info("╔════════════════════════════════════════════╗");
						Logger::info("║  SYNC COMPLETE - All files synced! ✅     ║");
						Logger::info("║  Folder: " + syncFolder);
						Logger::info("║  Status: 0/0 files (folder empty)         ║");
						Logger::info("╚════════════════════════════════════════════╝");
					}
				}
			}
			
			tcp.observePeer(TcpHandshake::RemoteDevice{peer->deviceId, peer->deviceName, peer->ip, peer->port});
		}
	});

	std::thread syncThread([&syncEngine]() {
		while (g_keepRunning.load()) {
			while (auto evt = syncEngine.pollEvent()) {
				Logger::info(*evt);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
	});

	std::thread cleanupThread([&discovery,
	                         &tcp,
	                         &knownDevicesMutex,
	                         &knownDevices,
	                         &remoteMetadataMutex,
	                         &remoteMetadataByDevice,
	                         &lastRemoteMetadataDigest,
	                         &transferMutex,
	                         &incomingTransfers,
	                         &outboundInProgress,
	                         &pendingDownloadRequests]() {
		while (g_keepRunning.load()) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			auto active = discovery.getActiveDevices(15000);
			std::unordered_map<std::string, DeviceDiscovery::PeerInfo> activeById;
			for (const auto& peer : active) {
				activeById.emplace(peer.deviceId, peer);
			}

			std::vector<DeviceDiscovery::PeerInfo> removedPeers;
			{
				std::lock_guard<std::mutex> lock(knownDevicesMutex);
				for (auto it = knownDevices.begin(); it != knownDevices.end();) {
					if (activeById.find(it->first) == activeById.end()) {
						removedPeers.push_back(it->second);
						it = knownDevices.erase(it);
					} else {
						++it;
					}
				}
			}

			for (const auto& peer : removedPeers) {
				Logger::info("Device removed: id=" + peer.deviceId + " name=" + peer.deviceName +
				             " ip=" + peer.ip + " port=" + std::to_string(peer.port));
				{
					std::lock_guard<std::mutex> lock(remoteMetadataMutex);
					remoteMetadataByDevice.erase(peer.deviceId);
					lastRemoteMetadataDigest.erase(peer.deviceId);
				}
				{
					std::lock_guard<std::mutex> lock(transferMutex);
					for (auto it = incomingTransfers.begin(); it != incomingTransfers.end();) {
						if (it->first.rfind(peer.deviceId + "|", 0) == 0) {
							if (it->second && it->second->out && it->second->out->is_open()) {
								it->second->out->close();
							}
							std::error_code rmEc;
							if (it->second) {
								std::filesystem::remove(it->second->tempPath, rmEc);
							}
							it = incomingTransfers.erase(it);
						} else {
							++it;
						}
					}

					for (auto it = outboundInProgress.begin(); it != outboundInProgress.end();) {
						if (it->rfind(peer.deviceId + "|", 0) == 0) {
							it = outboundInProgress.erase(it);
						} else {
							++it;
						}
					}

					for (auto it = pendingDownloadRequests.begin(); it != pendingDownloadRequests.end();) {
						if (it->rfind(peer.deviceId + "|", 0) == 0) {
							it = pendingDownloadRequests.erase(it);
						} else {
							++it;
						}
					}
				}
				tcp.removePeer(peer.deviceId);
			}
		}
	});

	senderThread.join();
	listenerThread.join();
	syncThread.join();
	cleanupThread.join();
	syncEngine.stop();
	tcp.stop();
	Logger::info("Discovery loop stopped");
	return 0;
}

void Application::shutdown() {
	if (!initialized_) {
		Logger::shutdown();
		return;
	}

	Logger::info("Application shutting down");
	Logger::shutdown();
	initialized_ = false;
}
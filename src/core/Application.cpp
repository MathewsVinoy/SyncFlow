#include "core/Application.h"

#include "core/Logger.h"
#include "networking/DeviceDiscovery.h"
#include "networking/TcpHandshake.h"

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
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
std::atomic<bool> g_keepRunning{true};

void handleStopSignal(int) {
	g_keepRunning.store(false);
}

std::string hexEncode(const std::string& input) {
	static constexpr char kHex[] = "0123456789abcdef";
	std::string out;
	out.reserve(input.size() * 2);
	for (unsigned char c : input) {
		out.push_back(kHex[(c >> 4) & 0x0F]);
		out.push_back(kHex[c & 0x0F]);
	}
	return out;
}

std::string hexEncodeBytes(const std::vector<char>& input) {
	static constexpr char kHex[] = "0123456789abcdef";
	std::string out;
	out.reserve(input.size() * 2);
	for (unsigned char c : input) {
		out.push_back(kHex[(c >> 4) & 0x0F]);
		out.push_back(kHex[c & 0x0F]);
	}
	return out;
}

int fromHexNibble(char c) {
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

std::optional<std::string> hexDecodeToString(const std::string& input) {
	if (input.size() % 2 != 0) {
		return std::nullopt;
	}
	std::string out;
	out.reserve(input.size() / 2);
	for (std::size_t i = 0; i < input.size(); i += 2) {
		int hi = fromHexNibble(input[i]);
		int lo = fromHexNibble(input[i + 1]);
		if (hi < 0 || lo < 0) {
			return std::nullopt;
		}
		out.push_back(static_cast<char>((hi << 4) | lo));
	}
	return out;
}

std::optional<std::vector<char>> hexDecodeToBytes(const std::string& input) {
	if (input.size() % 2 != 0) {
		return std::nullopt;
	}
	std::vector<char> out;
	out.reserve(input.size() / 2);
	for (std::size_t i = 0; i < input.size(); i += 2) {
		int hi = fromHexNibble(input[i]);
		int lo = fromHexNibble(input[i + 1]);
		if (hi < 0 || lo < 0) {
			return std::nullopt;
		}
		out.push_back(static_cast<char>((hi << 4) | lo));
	}
	return out;
}

std::vector<std::string> splitPipe(const std::string& text) {
	std::vector<std::string> parts;
	std::stringstream ss(text);
	std::string part;
	while (std::getline(ss, part, '|')) {
		parts.push_back(part);
	}
	return parts;
}

bool isSafeRelativePath(const std::filesystem::path& rel) {
	if (rel.empty() || rel.is_absolute()) {
		return false;
	}
	for (const auto& item : rel) {
		if (item == "..") {
			return false;
		}
	}
	return true;
}
}  // namespace

bool Application::init() {
	const bool configLoaded = config_.load();

	const std::string logFolder = config_.getString("log_folder", "log");
	const std::string logLevel = config_.getString("log_level", "info");
	const bool syncDataOnlyLogs = config_.getInt("sync_data_only_logs", 0) != 0;
	Logger::init(logFolder);
	Logger::setLevel(logLevel);
	Logger::setSyncDataOnly(syncDataOnlyLogs);

	if (!configLoaded) {
		Logger::warn("config.json could not be loaded; using defaults");
	}

	const std::string appName = config_.getString("app_name", "SyncFlow");
	const std::string deviceName = config_.getString("device_name", "unknown-device");
	const int configuredPort = config_.getInt("port", 8080);
	const std::string syncFolder = config_.getString("sync_folder", "./sync");
	const std::string securitySecret = config_.getString("security_shared_secret", "change-me-in-production");

	Logger::info("Application initialized");
	Logger::info("app_name: " + appName);
	Logger::info("device_name: " + deviceName);
	Logger::info("port: " + std::to_string(configuredPort));
	Logger::info("sync_folder: " + syncFolder);
	Logger::info("log_level: " + logLevel);
	Logger::info("mirror_folder: " + config_.getString("mirror_folder", syncFolder + "/.syncflow_mirror"));
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
	const std::string mirrorFolder = config_.getString("mirror_folder", syncFolder + "/.syncflow_mirror");
	const std::string securitySecret = config_.getString("security_shared_secret", "change-me-in-production");
	if (broadcastIntervalMs < 200) {
		broadcastIntervalMs = 200;
	}
	Logger::info("Configured port: " + std::to_string(configuredPort));
	Logger::info("Broadcast interval (ms): " + std::to_string(broadcastIntervalMs));
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
	Logger::info("Local device_id: " + discovery.getDeviceId());
	TcpHandshake tcp(discovery.getDeviceId(), deviceName, static_cast<std::uint16_t>(configuredPort));
	const bool tcpStarted = tcp.start();
	if (!tcpStarted) {
		Logger::warn("TCP handshake listener failed to start on port " + std::to_string(configuredPort) +
		             "; continuing with local-only sync mode");
	}

	SyncEngine syncEngine(syncFolder, mirrorFolder);
	if (!syncEngine.start()) {
		Logger::warn("Sync engine failed to start; continuing without file mirroring");
	}

	syncflow::engine::RemoteSync remoteSync;
	std::mutex pendingMutex;
	std::unordered_map<std::string, std::vector<char>> pendingFileBuffers;
	std::unordered_map<std::string, std::uint64_t> pendingFileTimestamps;

	auto sendLocalFileToPeer = [&](const std::string& peerId, const std::string& relativePath) {
		const std::filesystem::path relPath(relativePath);
		if (!isSafeRelativePath(relPath)) {
			return;
		}

		const auto fullPath = std::filesystem::path(syncFolder) / relPath;
		if (!std::filesystem::exists(fullPath) || !std::filesystem::is_regular_file(fullPath)) {
			return;
		}

		std::ifstream in(fullPath, std::ios::binary);
		if (!in.is_open()) {
			Logger::warn("sync send skipped, cannot open: " + fullPath.string());
			return;
		}

		std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		auto fileInfo = remoteSync.getFileInfo(fullPath);
		const std::uint64_t ts = fileInfo.lastModifiedTime;

		constexpr std::size_t kChunkBytes = 512;
		std::size_t offset = 0;
		const std::string encodedPath = hexEncode(relativePath);
		while (offset < data.size()) {
			const std::size_t n = std::min(kChunkBytes, data.size() - offset);
			std::vector<char> chunk(data.begin() + static_cast<std::ptrdiff_t>(offset),
			                       data.begin() + static_cast<std::ptrdiff_t>(offset + n));
			const bool isFinal = (offset + n) >= data.size();
			const std::string payload = "SYNC_FILE_CHUNK|" + encodedPath + "|" + std::to_string(ts) + "|" +
			                            std::to_string(offset) + "|" + (isFinal ? "1" : "0") + "|" +
			                            hexEncodeBytes(chunk);
			if (!tcp.sendMessage(peerId, payload)) {
				Logger::warn("sync send chunk failed: peer=" + peerId + " file=" + relativePath);
				return;
			}
			offset += n;
		}
		if (data.empty()) {
			const std::string payload = "SYNC_FILE_CHUNK|" + encodedPath + "|" + std::to_string(ts) + "|0|1|";
			tcp.sendMessage(peerId, payload);
		}
		Logger::info("sync pushed file to peer: " + relativePath + " -> " + peerId);
	};

	g_keepRunning.store(true);
	std::signal(SIGINT, handleStopSignal);
	std::signal(SIGTERM, handleStopSignal);

	Logger::info("Discovery loop started. Press Ctrl+C to stop.");
	std::mutex knownDevicesMutex;
	std::unordered_map<std::string, DeviceDiscovery::PeerInfo> knownDevices;

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
	                           &pendingMutex,
	                           &pendingFileBuffers,
	                           &pendingFileTimestamps,
	                           &sendLocalFileToPeer,
	                           &syncFolder]() {
		auto nextMetaRequest = std::chrono::steady_clock::now();
		while (g_keepRunning.load()) {
			tcp.tick();
			while (auto evt = tcp.pollEvent()) {
				Logger::info(*evt);
			}

			while (auto inbound = tcp.pollMessage()) {
				const std::string& peerId = inbound->deviceId;
				const std::string& msg = inbound->payload;

				if (msg == "SYNC_META_REQ") {
					auto localMeta = remoteSync.getLocalFileMetadata(std::filesystem::path(syncFolder));
					const std::string payload = "SYNC_META|" + remoteSync.encodeMetadataList(localMeta);
					(void)tcp.sendMessage(peerId, payload);
					continue;
				}

				if (msg.rfind("SYNC_META|", 0) == 0) {
					const std::string encodedMeta = msg.substr(std::string("SYNC_META|").size());
					auto remoteMeta = remoteSync.decodeMetadataList(encodedMeta);
					auto localMeta = remoteSync.getLocalFileMetadata(std::filesystem::path(syncFolder));
					auto plans = remoteSync.compareMeta(localMeta, remoteMeta, std::filesystem::path(syncFolder));

					for (const auto& p : plans) {
						if (p.action == syncflow::engine::RemoteSyncAction::DownloadFile) {
							const std::string req = "SYNC_FILE_REQ|" + hexEncode(p.remotePath);
							(void)tcp.sendMessage(peerId, req);
						} else if (p.action == syncflow::engine::RemoteSyncAction::UploadFile) {
							sendLocalFileToPeer(peerId, p.localPath);
						}
					}
					continue;
				}

				if (msg.rfind("SYNC_FILE_REQ|", 0) == 0) {
					auto decodedPath = hexDecodeToString(msg.substr(std::string("SYNC_FILE_REQ|").size()));
					if (!decodedPath.has_value()) {
						continue;
					}
					sendLocalFileToPeer(peerId, *decodedPath);
					continue;
				}

				if (msg.rfind("SYNC_FILE_CHUNK|", 0) == 0) {
					auto parts = splitPipe(msg);
					if (parts.size() != 6) {
						continue;
					}
					auto decodedPath = hexDecodeToString(parts[1]);
					auto decodedBytes = hexDecodeToBytes(parts[5]);
					if (!decodedPath.has_value() || !decodedBytes.has_value()) {
						continue;
					}

					const std::filesystem::path relPath(*decodedPath);
					if (!isSafeRelativePath(relPath)) {
						continue;
					}

					const std::uint64_t incomingTs = static_cast<std::uint64_t>(std::stoull(parts[2]));
					const std::size_t offset = static_cast<std::size_t>(std::stoull(parts[3]));
					const bool isFinal = parts[4] == "1";
					const std::string key = peerId + "|" + *decodedPath;

					bool finalizeNow = false;
					{
						std::lock_guard<std::mutex> lock(pendingMutex);
						auto& buffer = pendingFileBuffers[key];
						if (offset != buffer.size()) {
							buffer.clear();
							pendingFileTimestamps[key] = incomingTs;
						}
						buffer.insert(buffer.end(), decodedBytes->begin(), decodedBytes->end());
						pendingFileTimestamps[key] = incomingTs;
						finalizeNow = isFinal;
					}

					if (finalizeNow) {
						std::vector<char> allData;
						std::uint64_t ts = 0;
						{
							std::lock_guard<std::mutex> lock(pendingMutex);
							allData = std::move(pendingFileBuffers[key]);
							ts = pendingFileTimestamps[key];
							pendingFileBuffers.erase(key);
							pendingFileTimestamps.erase(key);
						}

						auto target = std::filesystem::path(syncFolder) / relPath;
						std::error_code ec;
						std::filesystem::create_directories(target.parent_path(), ec);
						if (!ec) {
							bool shouldWrite = true;
							if (std::filesystem::exists(target, ec) && !ec) {
								auto localInfo = remoteSync.getFileInfo(target);
								if (localInfo.lastModifiedTime >= ts) {
									shouldWrite = false;
								}
							}

							if (shouldWrite) {
								std::ofstream out(target, std::ios::binary | std::ios::trunc);
								if (out.is_open()) {
									out.write(allData.data(), static_cast<std::streamsize>(allData.size()));
									out.close();
									Logger::info("sync received from peer: " + relPath.string() + " <- " + peerId);
								}
							}
						}
					}
					continue;
				}
			}

			const auto now = std::chrono::steady_clock::now();
			if (now >= nextMetaRequest) {
				auto peers = tcp.getConnectedPeers();
				for (const auto& peer : peers) {
					(void)tcp.sendMessage(peer.deviceId, "SYNC_META_REQ");
				}
				nextMetaRequest = now + std::chrono::seconds(4);
			}

			auto peer = discovery.receiver(800);
			if (!peer.has_value()) {
				continue;
			}

			std::string event = "discovered";
			{
				std::lock_guard<std::mutex> lock(knownDevicesMutex);
				auto it = knownDevices.find(peer->deviceId);
				if (it == knownDevices.end()) {
					knownDevices.emplace(peer->deviceId, *peer);
				} else {
					event = "updated";
					it->second = *peer;
				}
			}

			std::cout << "Connected device: " << peer->deviceName << " (" << peer->ip << ':' << peer->port << ")"
			          << std::endl;
			Logger::info("Device " + event + ": id=" + peer->deviceId + " name=" + peer->deviceName +
			             " ip=" + peer->ip + " port=" + std::to_string(peer->port));
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

	std::thread cleanupThread([&discovery, &tcp, &knownDevicesMutex, &knownDevices]() {
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
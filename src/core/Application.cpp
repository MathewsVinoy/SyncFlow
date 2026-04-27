#include "core/Application.h"

#include "core/Logger.h"
#include "networking/DeviceDiscovery.h"
#include "networking/TcpHandshake.h"

#include "security/AuthManager.h"

#include "sync_engine/SyncEngine.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>

namespace {
std::atomic<bool> g_keepRunning{true};

void handleStopSignal(int) {
	g_keepRunning.store(false);
}
}  // namespace

bool Application::init() {
	const bool configLoaded = config_.load();

	const std::string logFolder = config_.getString("log_folder", "log");
	const std::string logLevel = config_.getString("log_level", "info");
	Logger::init(logFolder);
	Logger::setLevel(logLevel);

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

	std::thread listenerThread([&discovery, &tcp, &knownDevicesMutex, &knownDevices]() {
		while (g_keepRunning.load()) {
			tcp.tick();
			while (auto evt = tcp.pollEvent()) {
				Logger::info(*evt);
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
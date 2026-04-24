#include "core/Application.h"

#include "core/Logger.h"
#include "networking/DeviceDiscovery.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> g_keepRunning{true};

void handleStopSignal(int) {
	g_keepRunning.store(false);
}
}  // namespace

bool Application::init() {
	if (!config_.load()) {
		Logger::warn("config.json could not be loaded; using defaults");
		return false;
	}

	Logger::init();
	Logger::info("Application initialized: " + config_.getString("app_name", "SyncFlow"));
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
	Logger::info("Configured port: " + std::to_string(configuredPort));

	DeviceDiscovery discovery(deviceName, static_cast<std::uint16_t>(configuredPort));

	g_keepRunning.store(true);
	std::signal(SIGINT, handleStopSignal);
	std::signal(SIGTERM, handleStopSignal);

	Logger::info("Discovery loop started. Press Ctrl+C to stop.");

	std::thread senderThread([&discovery]() {
		while (g_keepRunning.load()) {
			if (!discovery.sender()) {
				Logger::warn("Thread 1: Broadcast sender failed");
			}
			std::this_thread::sleep_for(std::chrono::seconds(2));
		}
	});

	std::thread listenerThread([&discovery]() {
		while (g_keepRunning.load()) {
			auto peer = discovery.receiver(1000);
			if (!peer.has_value()) {
				continue;
			}

			std::cout << "Connected device: " << peer->deviceName << " (" << peer->ip << ':' << peer->port << ")"
			          << std::endl;
			Logger::info("Thread 2: Listener received from " + peer->ip + " as " + peer->deviceName +
			             " on port " + std::to_string(peer->port));
		}
	});

	senderThread.join();
	listenerThread.join();
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
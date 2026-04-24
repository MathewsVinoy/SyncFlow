#include "core/Application.h"

#include "core/Logger.h"
#include "networking/DeviceDiscovery.h"

#include <thread>

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

	std::thread senderThread([&discovery]() {
		if (!discovery.sender()) {
			Logger::warn("Broadcast sender failed");
			return;
		}
		Logger::info("Thread 1: Broadcast sender sent discovery probe");
	});

	std::thread listenerThread([&discovery]() {
		auto peer = discovery.receiver(5000);
		if (!peer.has_value()) {
			Logger::warn("Thread 2: Listener timed out or failed");
			return;
		}

		Logger::info("Thread 2: Listener received from " + peer->ip + " as " + peer->deviceName +
		             " on port " + std::to_string(peer->port));
	});

	senderThread.join();
	listenerThread.join();
	return 0;
}

void Application::shutdown() {
	if (!initialized_) {
		return;
	}

	Logger::info("Application shutting down");
	initialized_ = false;
}
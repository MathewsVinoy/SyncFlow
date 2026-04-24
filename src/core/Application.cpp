#include "core/Application.h"

#include "core/Logger.h"

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
	Logger::info("Configured port: " + std::to_string(config_.getInt("port", 0)));
	return 0;
}

void Application::shutdown() {
	if (!initialized_) {
		return;
	}

	Logger::info("Application shutting down");
	initialized_ = false;
}
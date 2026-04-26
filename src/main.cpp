#include "core/Application.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

constexpr const char* kDefaultConfigFile = "config.json";
constexpr const char* kDefaultPidFile = "/tmp/syncflow.pid";

std::filesystem::path resolveConfigPath(const std::string& fileName = kDefaultConfigFile) {
	std::filesystem::path path(fileName);
	if (path.is_absolute()) {
		return path;
	}

	if (std::filesystem::exists(path)) {
		return std::filesystem::absolute(path);
	}

	const auto filename = path.filename();
	for (auto current = std::filesystem::current_path(); ; current = current.parent_path()) {
		auto candidate = current / filename;
		if (std::filesystem::exists(candidate)) {
			return candidate;
		}
		if (!current.has_parent_path() || current == current.parent_path()) {
			break;
		}
	}

	return std::filesystem::absolute(path);
}

std::filesystem::path resolvePidFilePath() {
	auto configPath = resolveConfigPath();
	std::ifstream in(configPath);
	if (!in.is_open()) {
		return kDefaultPidFile;
	}

	try {
		auto json = nlohmann::json::parse(in);
		if (json.contains("pid_file") && json["pid_file"].is_string()) {
			return std::filesystem::path(json["pid_file"].get<std::string>());
		}
	} catch (...) {
	}

	return kDefaultPidFile;
}

void printUsage(const char* executable) {
	std::cout << "Usage:\n"
	          << "  " << executable << " start\n"
	          << "  " << executable << " stop\n"
	          << "  " << executable << " set-sync-path <path>\n"
	          << "  " << executable << " run\n";
}

int runApplication() {
	Application app;
	if (!app.init()) {
		app.shutdown();
		return 1;
	}

	const int exitCode = app.run();
	app.shutdown();
	return exitCode;
}

bool updateSyncPath(const std::string& newPath) {
	auto configPath = resolveConfigPath();
	nlohmann::json root;

	if (std::filesystem::exists(configPath)) {
		std::ifstream in(configPath);
		if (in.is_open()) {
			try {
				root = nlohmann::json::parse(in);
			} catch (...) {
				return false;
			}
		}
	}

	if (!root.is_object()) {
		root = nlohmann::json::object();
	}

	std::error_code ec;
	auto absoluteSyncPath = std::filesystem::absolute(std::filesystem::path(newPath), ec);
	if (ec) {
		return false;
	}
	std::filesystem::create_directories(absoluteSyncPath, ec);
	if (ec) {
		return false;
	}

	root["sync_folder"] = absoluteSyncPath.string();
	if (!root.contains("mirror_folder") || !root["mirror_folder"].is_string()) {
		root["mirror_folder"] = (absoluteSyncPath / ".syncflow_mirror").string();
	}

	std::ofstream out(configPath, std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out << root.dump(2) << '\n';
	return static_cast<bool>(out);
}

#ifndef _WIN32
bool isProcessRunning(pid_t pid) {
	if (pid <= 0) {
		return false;
	}
	return kill(pid, 0) == 0;
}

pid_t readPidFile(const std::filesystem::path& pidFile) {
	std::ifstream in(pidFile);
	if (!in.is_open()) {
		return -1;
	}
	pid_t pid = -1;
	in >> pid;
	return pid;
}

bool writePidFile(const std::filesystem::path& pidFile, pid_t pid) {
	std::error_code ec;
	if (pidFile.has_parent_path()) {
		std::filesystem::create_directories(pidFile.parent_path(), ec);
		if (ec) {
			return false;
		}
	}

	std::ofstream out(pidFile, std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out << pid << '\n';
	return static_cast<bool>(out);
}

int startDaemon(const std::filesystem::path& pidFile) {
	const pid_t existingPid = readPidFile(pidFile);
	if (isProcessRunning(existingPid)) {
		std::cerr << "syncflow is already running with PID " << existingPid << '\n';
		return 1;
	}
	if (existingPid > 0) {
		std::error_code ec;
		std::filesystem::remove(pidFile, ec);
	}

	pid_t pid = fork();
	if (pid < 0) {
		std::cerr << "failed to fork daemon process\n";
		return 1;
	}
	if (pid > 0) {
		for (int i = 0; i < 20; ++i) {
			if (!isProcessRunning(pid)) {
				std::cerr << "syncflow daemon failed to start\n";
				return 1;
			}

			const pid_t writtenPid = readPidFile(pidFile);
			if (writtenPid == pid && isProcessRunning(writtenPid)) {
				std::cout << "syncflow daemon started (PID " << pid << ")\n";
				return 0;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		std::cerr << "syncflow daemon startup timed out\n";
		return 1;
	}

	if (setsid() < 0) {
		_exit(1);
	}

	umask(0);

	const int nullFd = open("/dev/null", O_RDWR);
	if (nullFd >= 0) {
		dup2(nullFd, STDIN_FILENO);
		dup2(nullFd, STDOUT_FILENO);
		dup2(nullFd, STDERR_FILENO);
		if (nullFd > STDERR_FILENO) {
			close(nullFd);
		}
	}

	const pid_t daemonPid = getpid();
	if (!writePidFile(pidFile, daemonPid)) {
		_exit(1);
	}

	const int exitCode = runApplication();
	std::error_code ec;
	std::filesystem::remove(pidFile, ec);
	_exit(exitCode);
}

int stopDaemon(const std::filesystem::path& pidFile) {
	const pid_t pid = readPidFile(pidFile);
	if (pid <= 0) {
		std::cout << "syncflow daemon is not running\n";
		return 0;
	}

	if (!isProcessRunning(pid)) {
		std::error_code ec;
		std::filesystem::remove(pidFile, ec);
		std::cout << "stale PID file removed\n";
		return 0;
	}

	if (kill(pid, SIGTERM) != 0) {
		std::cerr << "failed to stop syncflow daemon\n";
		return 1;
	}

	for (int i = 0; i < 50; ++i) {
		if (!isProcessRunning(pid)) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	std::error_code ec;
	std::filesystem::remove(pidFile, ec);
	std::cout << "syncflow daemon stopped\n";
	return 0;
}
#endif

}  // namespace

int main(int argc, char* argv[]) {
	const std::vector<std::string> args(argv, argv + argc);
	if (args.size() <= 1) {
		return runApplication();
	}

	const std::string command = args[1];
	if (command == "run") {
		return runApplication();
	}

	if (command == "set-sync-path") {
		if (args.size() < 3) {
			std::cerr << "missing path argument\n";
			printUsage(argv[0]);
			return 1;
		}
		if (!updateSyncPath(args[2])) {
			std::cerr << "failed to update sync path in config.json\n";
			return 1;
		}
		std::cout << "sync path updated: " << std::filesystem::absolute(args[2]).string() << '\n';
		return 0;
	}

	if (command == "start" || command == "stop") {
#ifdef _WIN32
		std::cerr << "daemon start/stop commands are currently supported on Linux/macOS only\n";
		return 1;
#else
		const auto pidFile = resolvePidFilePath();
		if (command == "start") {
			return startDaemon(pidFile);
		}
		return stopDaemon(pidFile);
#endif
	}

	printUsage(argv[0]);
	return 1;
}


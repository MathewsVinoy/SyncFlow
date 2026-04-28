#include "platform/PlatformPaths.h"
#include "platform/PlatformSocket.h"

#include <cassert>
#include <filesystem>
#include <iostream>

void testPlatformPaths() {
	std::cout << "\n=== Testing PlatformPaths ===" << std::endl;

	platform::PlatformPaths::initialize("syncflow-test");

	std::cout << "OS: " << platform::PlatformPaths::getOSName() << std::endl;
	std::cout << "Is Windows: " << (platform::PlatformPaths::isWindows() ? "yes" : "no") << std::endl;
	std::cout << "Is Linux: " << (platform::PlatformPaths::isLinux() ? "yes" : "no") << std::endl;
	std::cout << "Is macOS: " << (platform::PlatformPaths::isMacOS() ? "yes" : "no") << std::endl;

	auto home = platform::PlatformPaths::getHomeDir();
	std::cout << "Home: " << (home ? home->string() : "not found") << std::endl;

	auto config = platform::PlatformPaths::getConfigDir();
	if (config) {
		std::cout << "Config dir: " << config->string() << std::endl;
		assert(std::filesystem::exists(*config));
	}

	auto logs = platform::PlatformPaths::getLogDir();
	if (logs) {
		std::cout << "Log dir: " << logs->string() << std::endl;
		assert(std::filesystem::exists(*logs));
	}

	auto data = platform::PlatformPaths::getDataDir();
	if (data) {
		std::cout << "Data dir: " << data->string() << std::endl;
		assert(std::filesystem::exists(*data));
	}

	auto cache = platform::PlatformPaths::getCacheDir();
	if (cache) {
		std::cout << "Cache dir: " << cache->string() << std::endl;
		assert(std::filesystem::exists(*cache));
	}

	auto configFile = platform::PlatformPaths::getConfigFile("test.json");
	std::cout << "Config file path: " << configFile.string() << std::endl;

	auto logFile = platform::PlatformPaths::getLogFile("test.log");
	std::cout << "Log file path: " << logFile.string() << std::endl;

	std::cout << "✓ PlatformPaths tests passed" << std::endl;
}

void testPlatformSocket() {
	std::cout << "\n=== Testing PlatformSocket ===" << std::endl;

	// Initialize socket system
	if (!platform::PlatformSocket::initializeSocketSystem()) {
		std::cerr << "Failed to initialize socket system" << std::endl;
		return;
	}

	std::cout << "Socket system initialized" << std::endl;

	// Test TCP socket creation
	auto tcpSock = platform::PlatformSocket::createTCP();
	assert(tcpSock.has_value() && "Failed to create TCP socket");
	std::cout << "✓ TCP socket created" << std::endl;

	// Test socket properties
	assert(tcpSock->isValid());
	std::cout << "✓ Socket is valid" << std::endl;

	// Test non-blocking
	assert(tcpSock->setNonBlocking(true));
	std::cout << "✓ Non-blocking set" << std::endl;

	// Test reuse address
	assert(tcpSock->setReuseAddress(true));
	std::cout << "✓ Reuse address set" << std::endl;

	// Test UDP socket
	auto udpSock = platform::PlatformSocket::createUDP();
	assert(udpSock.has_value() && "Failed to create UDP socket");
	std::cout << "✓ UDP socket created" << std::endl;

	// Test hostname
	auto hostname = platform::PlatformSocket::getHostname();
	if (hostname) {
		std::cout << "Hostname: " << *hostname << std::endl;
	}

	// Test local addresses
	auto addresses = platform::PlatformSocket::getLocalAddresses();
	if (addresses) {
		std::cout << "Local addresses: ";
		for (const auto& addr : *addresses) {
			std::cout << addr << " ";
		}
		std::cout << std::endl;
	}

	// Cleanup
	platform::PlatformSocket::shutdownSocketSystem();
	std::cout << "✓ Socket system cleaned up" << std::endl;

	std::cout << "✓ PlatformSocket tests passed" << std::endl;
}

int main() {
	try {
		std::cout << "=== Syncflow Cross-Platform Tests ===" << std::endl;

		testPlatformPaths();
		testPlatformSocket();

		std::cout << "\n=== All Cross-Platform Tests Passed ===" << std::endl;
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "Test failed: " << e.what() << std::endl;
		return 1;
	}
}

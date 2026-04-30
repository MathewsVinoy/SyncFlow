#include "networking/DeviceDiscovery.h"

#include "core/Logger.h"
#include "platform/PlatformSocket.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr const char* kResponsePrefix = "SYNCFLOW";

bool isDigitsOnly(const std::string& value) {
	if (value.empty()) {
		return false;
	}
	for (const unsigned char ch : value) {
		if (!std::isdigit(ch)) {
			return false;
		}
	}
	return true;
}

std::string buildProbeMessage(std::uint16_t discoveryPort) {
	return std::string("255.255.255.255:") + std::to_string(discoveryPort);
}
constexpr std::chrono::milliseconds kDefaultInactiveTimeout{15000};
}  // namespace

DeviceDiscovery::DeviceDiscovery(std::string deviceName, std::uint16_t servicePort, std::uint16_t discoveryPort)
	: deviceName_(std::move(deviceName)),
	  deviceId_(createDeviceId()),
	  servicePort_(servicePort),
	  discoveryPort_(discoveryPort) {}

bool DeviceDiscovery::sender() const {
	if (!platform::PlatformSocket::initializeSocketSystem()) {
		return false;
	}

	auto socket = platform::PlatformSocket::createUDP();
	if (!socket) {
		platform::PlatformSocket::shutdownSocketSystem();
		return false;
	}

	if (!socket->setBroadcast(true)) {
		platform::PlatformSocket::shutdownSocketSystem();
		return false;
	}

	const std::string payload = buildProbeMessage(discoveryPort_);
	int sentCount = 0;
	if (socket->sendTo("255.255.255.255", discoveryPort_, payload)) {
		++sentCount;
		Logger::info("Discovery TX probe: to=255.255.255.255:" + std::to_string(discoveryPort_) +
		             " payload='" + payload + "'");
	} else {
		Logger::warn("Discovery TX probe failed: to=255.255.255.255:" + std::to_string(discoveryPort_));
	}

	const std::string announce = std::string(kResponsePrefix) + "|" + deviceId_ + "|" + deviceName_ + "|" +
	                             std::to_string(servicePort_);
	if (socket->sendTo("255.255.255.255", discoveryPort_, announce)) {
		++sentCount;
		Logger::info("Discovery TX announce: to=255.255.255.255:" + std::to_string(discoveryPort_) +
		             " payload='" + announce + "'");
	} else {
		Logger::warn("Discovery TX announce failed: to=255.255.255.255:" + std::to_string(discoveryPort_));
	}

	platform::PlatformSocket::shutdownSocketSystem();
	return sentCount > 0;
}

std::optional<DeviceDiscovery::PeerInfo> DeviceDiscovery::receiver(int timeoutMs) {
	if (timeoutMs <= 0) {
		return std::nullopt;
	}

	if (!platform::PlatformSocket::initializeSocketSystem()) {
		return std::nullopt;
	}

	auto socket = platform::PlatformSocket::createUDP();
	if (!socket) {
		platform::PlatformSocket::shutdownSocketSystem();
		return std::nullopt;
	}

	if (!socket->setReuseAddress(true)) {
		platform::PlatformSocket::shutdownSocketSystem();
		return std::nullopt;
	}

	if (!socket->bind("0.0.0.0", discoveryPort_)) {
		platform::PlatformSocket::shutdownSocketSystem();
		return std::nullopt;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	std::optional<PeerInfo> discovered;

	while (std::chrono::steady_clock::now() < deadline) {
		const auto now = std::chrono::steady_clock::now();
		const auto remainingMs = static_cast<int>(
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
		if (remainingMs <= 0) {
			break;
		}

		auto packet = socket->receiveFrom(1024, remainingMs);
		if (!packet) {
			continue;
		}

		const std::string payload(packet->data.begin(), packet->data.end());
		Logger::info("Discovery RX packet: from=" + packet->address + ":" + std::to_string(packet->port) +
		             " payload='" + payload + "'");

		if (payload == buildProbeMessage(discoveryPort_)) {
			std::string response = std::string(kResponsePrefix) + "|" + deviceId_ + "|" + deviceName_ + "|" +
			                       std::to_string(servicePort_);
			if (socket->sendTo(packet->address, discoveryPort_, response)) {
				Logger::info("Discovery TX response: to=" + packet->address + ":" +
				             std::to_string(discoveryPort_) + " payload='" + response + "'");
			} else {
				Logger::warn("Discovery TX response failed: to=" + packet->address + ":" +
				             std::to_string(discoveryPort_));
			}
			continue;
		}

		auto parsed = parseMessage(payload, packet->address);
		if (!parsed.has_value() || parsed->deviceId == deviceId_) {
			continue;
		}

		const bool shouldNotify = upsertDevice(*parsed, kDefaultInactiveTimeout);
		if (shouldNotify) {
			Logger::info("Discovery RX parsed peer: id=" + parsed->deviceId + " name=" + parsed->deviceName +
			             " ip=" + parsed->ip + " port=" + std::to_string(parsed->port));
			discovered = parsed;
			break;
		}
	}

	platform::PlatformSocket::shutdownSocketSystem();
	return discovered;
}

std::vector<DeviceDiscovery::PeerInfo> DeviceDiscovery::getActiveDevices(int inactiveTimeoutMs) {
	if (inactiveTimeoutMs <= 0) {
		inactiveTimeoutMs = static_cast<int>(kDefaultInactiveTimeout.count());
	}

	const auto timeout = std::chrono::milliseconds(inactiveTimeoutMs);
	const auto now = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lock(devicesMutex_);
	removeInactiveLocked(now, timeout);

	std::vector<PeerInfo> active;
	active.reserve(devices_.size());
	for (const auto& [_, tracked] : devices_) {
		active.push_back(tracked.peer);
	}

	return active;
}

std::string DeviceDiscovery::getDeviceId() const {
	return deviceId_;
}

std::optional<DeviceDiscovery::PeerInfo> DeviceDiscovery::parseMessage(const std::string& payload,
	                                                                    const std::string& senderIp) {
	std::stringstream ss(payload);
	std::string part;
	std::vector<std::string> tokens;
	tokens.reserve(5);
	while (std::getline(ss, part, '|')) {
		tokens.push_back(part);
		if (tokens.size() > 5) {
			return std::nullopt;
		}
	}

	if (tokens.size() < 4 || tokens.size() > 5) {
		return std::nullopt;
	}

	if (tokens[0] != kResponsePrefix) {
		return std::nullopt;
	}

	if (!isValidDeviceId(tokens[1]) || !isValidDeviceName(tokens[2]) || !isDigitsOnly(tokens[3])) {
		return std::nullopt;
	}

	int portValue = 0;
	try {
		portValue = std::stoi(tokens[3]);
	} catch (...) {
		return std::nullopt;
	}

	if (portValue <= 0 || portValue > 65535) {
		return std::nullopt;
	}

	std::string peerIp = senderIp;
	if (tokens.size() == 5 && !tokens[4].empty() && tokens[4] != "0.0.0.0") {
		peerIp = tokens[4];
	}

	return PeerInfo{tokens[1],
	                tokens[2],
	                peerIp,
	                static_cast<std::uint16_t>(portValue),
	                std::chrono::steady_clock::now()};
}

bool DeviceDiscovery::isValidDeviceId(const std::string& deviceId) {
	if (deviceId.size() < 8 || deviceId.size() > 64) {
		return false;
	}

	for (const unsigned char ch : deviceId) {
		if (!(std::isalnum(ch) || ch == '-' || ch == '_')) {
			return false;
		}
	}

	return true;
}

bool DeviceDiscovery::isValidDeviceName(const std::string& deviceName) {
	if (deviceName.empty() || deviceName.size() > 128) {
		return false;
	}

	return deviceName.find('|') == std::string::npos;
}

std::string DeviceDiscovery::createDeviceId() {
	std::random_device rd;
	std::mt19937_64 rng(rd());
	std::uniform_int_distribution<std::uint32_t> dist(0, 255);

	std::ostringstream out;
	out << std::hex << std::setfill('0');
	for (int i = 0; i < 16; ++i) {
		out << std::setw(2) << dist(rng);
	}

	return out.str();
}

bool DeviceDiscovery::upsertDevice(const PeerInfo& peer, std::chrono::milliseconds inactiveTimeout) {
	const auto now = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lock(devicesMutex_);
	removeInactiveLocked(now, inactiveTimeout);

	auto it = devices_.find(peer.deviceId);
	if (it == devices_.end()) {
		devices_.emplace(peer.deviceId, TrackedDevice{peer});
		return true;
	}

	const bool changed =
		(it->second.peer.ip != peer.ip) || (it->second.peer.port != peer.port) ||
		(it->second.peer.deviceName != peer.deviceName);

	it->second.peer = peer;
	return changed;
}

void DeviceDiscovery::removeInactiveLocked(std::chrono::steady_clock::time_point now,
	                                       std::chrono::milliseconds inactiveTimeout) {
	for (auto it = devices_.begin(); it != devices_.end();) {
		const auto elapsed = now - it->second.peer.lastSeen;
		if (elapsed > inactiveTimeout) {
			it = devices_.erase(it);
		} else {
			++it;
		}
	}
}

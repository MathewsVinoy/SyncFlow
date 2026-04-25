#include "networking/DeviceDiscovery.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {
constexpr const char* kResponsePrefix = "SYNCFLOW";
constexpr const char* kProbePrefix = "SYNCFLOW_DISCOVER";

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLen = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
using SocketLen = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void closeSocket(SocketHandle socketFd) {
#ifdef _WIN32
	closesocket(socketFd);
#else
	close(socketFd);
#endif
}

#ifdef _WIN32
std::mutex g_winsockMutex;
int g_winsockRefCount = 0;
#endif

bool initSockets() {
#ifdef _WIN32
	std::lock_guard<std::mutex> lock(g_winsockMutex);
	if (g_winsockRefCount == 0) {
		WSADATA wsaData{};
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			return false;
		}
	}
	++g_winsockRefCount;
	return true;
#else
	return true;
#endif
}

void shutdownSockets() {
#ifdef _WIN32
	std::lock_guard<std::mutex> lock(g_winsockMutex);
	if (g_winsockRefCount > 0) {
		--g_winsockRefCount;
		if (g_winsockRefCount == 0) {
			WSACleanup();
		}
	}
#endif
}

std::vector<std::string> split(const std::string& text, char delimiter) {
	std::stringstream ss(text);
	std::string item;
	std::vector<std::string> parts;
	while (std::getline(ss, item, delimiter)) {
		parts.push_back(item);
}
	return parts;
}

std::string generateDeviceId() {
	std::random_device rd;
	std::mt19937_64 gen(rd());
	std::uniform_int_distribution<std::uint64_t> dist;

	const std::uint64_t a = dist(gen);
	const std::uint64_t b = dist(gen);

	std::ostringstream oss;
	oss << std::hex;
	oss.width(16);
	oss.fill('0');
	oss << a;
	oss.width(16);
	oss.fill('0');
	oss << b;
	return oss.str();
}

bool parsePort(const std::string& token, std::uint16_t& outPort) {
	int parsed = 0;
	try {
		parsed = std::stoi(token);
	} catch (...) {
		return false;
}

	if (parsed <= 0 || parsed > 65535) {
		return false;
}

	outPort = static_cast<std::uint16_t>(parsed);
	return true;
#endif
}
}  // namespace

DeviceDiscovery::DeviceDiscovery(std::string deviceName, std::uint16_t servicePort, std::uint16_t discoveryPort)
	: deviceId_(generateDeviceId()),
	  deviceName_(std::move(deviceName)),
	  servicePort_(servicePort),
	  discoveryPort_(discoveryPort) {}

bool DeviceDiscovery::sender() const {
	if (!initSockets()) {
		return false;
	}

	const SocketHandle socketFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socketFd == kInvalidSocket) {
		shutdownSockets();
		return false;
	}

	int enableBroadcast = 1;
	if (setsockopt(socketFd, SOL_SOCKET, SO_BROADCAST,
	               reinterpret_cast<const char*>(&enableBroadcast),
	               static_cast<SocketLen>(sizeof(enableBroadcast))) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return false;
	}

	sockaddr_in target{};
	target.sin_family = AF_INET;
	target.sin_port = htons(discoveryPort_);
	target.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	const std::string payload = std::string(kProbePrefix) + "|" + deviceId_ + "|" + std::to_string(discoveryPort_);
	const int sent = sendto(socketFd,
	                        payload.c_str(),
	                        static_cast<int>(payload.size()),
	                        0,
	                        reinterpret_cast<const sockaddr*>(&target),
	                        static_cast<SocketLen>(sizeof(target)));

	closeSocket(socketFd);
	shutdownSockets();
	return sent >= 0;
}

std::optional<DeviceDiscovery::PeerInfo> DeviceDiscovery::receiver(int timeoutMs) const {
	removeInactiveDevices();

	if (!initSockets()) {
		return std::nullopt;
	}

	const SocketHandle socketFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socketFd == kInvalidSocket) {
		shutdownSockets();
		return std::nullopt;
	}

		const std::string message(buffer.data());
		const auto parts = split(message, '|');

		char ipBuffer[INET_ADDRSTRLEN] = {0};
		const char* ipResult = inet_ntop(AF_INET, &senderAddr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
		const std::string senderIp = ipResult != nullptr ? std::string(ipBuffer) : std::string();

		if (parts.size() == 3 && parts[0] == kProbePrefix) {
			std::uint16_t requestedDiscoveryPort = 0;
			const bool validProbe = isValidDeviceId(parts[1]) && parsePort(parts[2], requestedDiscoveryPort);
			if (validProbe && parts[1] != deviceId_ && requestedDiscoveryPort == discoveryPort_) {
				const std::string response = std::string(kResponsePrefix) + "|" + deviceId_ + "|" + deviceName_ + "|" +
				                             std::to_string(servicePort_);
				const int sent = sendto(socketFd,
				                        response.c_str(),
				                        static_cast<int>(response.size()),
				                        0,
				                        reinterpret_cast<const sockaddr*>(&senderAddr),
				                        senderLen);
				if (sent < 0) {
					closeSocket(socketFd);
					shutdownSockets();
					return std::nullopt;
				}
			}

	               static_cast<SocketLen>(sizeof(reuse))) < 0) {
		closeSocket(socketFd);
			return std::nullopt;
		return std::nullopt;
	}
		closeSocket(socketFd);
		shutdownSockets();

		auto parsed = parseResponseMessage(message, senderIp);
		if (!parsed.has_value() || parsed->deviceId == deviceId_) {
			return std::nullopt;
	local.sin_port = htons(discoveryPort_);
	local.sin_addr.s_addr = htonl(INADDR_ANY);
		return upsertPeer(*parsed);
	sockaddr_in senderAddr{};
	SocketLen senderLen = static_cast<SocketLen>(sizeof(senderAddr));

	std::vector<DeviceDiscovery::PeerInfo> DeviceDiscovery::getActiveDevices() const {
		std::lock_guard<std::mutex> lock(peersMutex_);
		std::vector<PeerInfo> peers;
		peers.reserve(peersById_.size());
		for (const auto& entry : peersById_) {
			peers.push_back(entry.second);
		shutdownSockets();
		return peers;
	}
		return std::nullopt;

	void DeviceDiscovery::removeInactiveDevices(std::chrono::seconds maxAge) const {
		const auto now = std::chrono::steady_clock::now();
		std::lock_guard<std::mutex> lock(peersMutex_);
		for (auto it = peersById_.begin(); it != peersById_.end();) {
			if (now - it->second.lastSeen > maxAge) {
				it = peersById_.erase(it);
			} else {
				++it;
			}
	buffer[static_cast<std::size_t>(received)] = '\0';
	}
	const std::string request(buffer.data());

	const std::string& DeviceDiscovery::deviceId() const {
		return deviceId_;
	}


	std::optional<DeviceDiscovery::PeerInfo> DeviceDiscovery::upsertPeer(const PeerInfo& peer) const {
		std::lock_guard<std::mutex> lock(peersMutex_);
		const auto now = std::chrono::steady_clock::now();

		auto it = peersById_.find(peer.deviceId);
		if (it == peersById_.end()) {
			PeerInfo value = peer;
			value.lastSeen = now;
			peersById_[peer.deviceId] = value;
			return value;

		closeSocket(socketFd);
		const bool changed =
			it->second.deviceName != peer.deviceName || it->second.ip != peer.ip || it->second.port != peer.port;

		it->second.deviceName = peer.deviceName;
		it->second.ip = peer.ip;
		it->second.port = peer.port;
		it->second.lastSeen = now;

		if (!changed) {
		return parseMessage(request, senderIp);
	}

		return it->second;
	}


	std::optional<DeviceDiscovery::PeerInfo> DeviceDiscovery::parseResponseMessage(const std::string& payload,
		                                                                            const std::string& senderIp) const {
		const auto tokens = split(payload, '|');
		if (tokens.size() != 4 || tokens[0] != kResponsePrefix) {
			return std::nullopt;
		}

		if (!isValidDeviceId(tokens[1]) || !isValidDeviceName(tokens[2])) {
			return std::nullopt;
		}

		std::uint16_t port = 0;
		if (!parsePort(tokens[3], port)) {
			return std::nullopt;
		}

		return PeerInfo{tokens[1], tokens[2], senderIp, port, std::chrono::steady_clock::now()};
	}


	bool DeviceDiscovery::isValidDeviceId(const std::string& value) const {
		if (value.empty() || value.size() > 64) {
			return false;
		}

		for (const char ch : value) {
			const unsigned char u = static_cast<unsigned char>(ch);
			if (!(std::isalnum(u) || ch == '-' || ch == '_')) {
				return false;
			}
		}

		return true;
	}

	bool DeviceDiscovery::isValidDeviceName(const std::string& value) const {
		if (value.empty() || value.size() > 128) {
			return false;
		}

		return value.find('|') == std::string::npos;
	const int sent = sendto(socketFd,
	                        response.c_str(),
	                        static_cast<int>(response.size()),
	                        0,
	                        reinterpret_cast<const sockaddr*>(&senderAddr),
	                        senderLen);
	if (sent < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	char ipBuffer[INET_ADDRSTRLEN] = {0};
	const char* ipResult = inet_ntop(AF_INET, &senderAddr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
	const std::string senderIp = ipResult != nullptr ? std::string(ipBuffer) : std::string();

	closeSocket(socketFd);
	shutdownSockets();

	return PeerInfo{deviceName_, senderIp, servicePort_};
}

std::optional<DeviceDiscovery::PeerInfo> DeviceDiscovery::parseMessage(const std::string& payload,
	                                                                    const std::string& senderIp) {
	std::stringstream ss(payload);
	std::string part;
	std::array<std::string, 3> tokens;
	std::size_t idx = 0;

	while (std::getline(ss, part, '|') && idx < tokens.size()) {
		tokens[idx++] = part;
	}

	if (idx != 3 || tokens[0] != kPrefix) {
		return std::nullopt;
	}

	int portValue = 0;
	try {
		portValue = std::stoi(tokens[2]);
	} catch (...) {
		return std::nullopt;
	}

	if (portValue <= 0 || portValue > 65535) {
		return std::nullopt;
	}

	return PeerInfo{tokens[1], senderIp, static_cast<std::uint16_t>(portValue)};
}

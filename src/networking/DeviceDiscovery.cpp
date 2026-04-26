#include "networking/DeviceDiscovery.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
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
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {
constexpr const char* kResponsePrefix = "SYNCFLOW";

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

bool isValidIpv4(const std::string& ip) {
	in_addr addr{};
	return inet_pton(AF_INET, ip.c_str(), &addr) == 1;
}

std::string localIpForRemote(const sockaddr_in& remoteAddr) {
	const SocketHandle fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == kInvalidSocket) {
		return {};
	}

	sockaddr_in remote = remoteAddr;
	if (connect(fd, reinterpret_cast<const sockaddr*>(&remote), static_cast<SocketLen>(sizeof(remote))) < 0) {
		closeSocket(fd);
		return {};
	}

	sockaddr_in local{};
	SocketLen localLen = static_cast<SocketLen>(sizeof(local));
	if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &localLen) < 0) {
		closeSocket(fd);
		return {};
	}

	char ipBuffer[INET_ADDRSTRLEN] = {0};
	const char* ipResult = inet_ntop(AF_INET, &local.sin_addr, ipBuffer, INET_ADDRSTRLEN);
	closeSocket(fd);
	return ipResult != nullptr ? std::string(ipBuffer) : std::string();
}

std::string buildProbeMessage(std::uint16_t discoveryPort) {
	return std::string("255.255.255.255:") + std::to_string(discoveryPort);
}

std::vector<sockaddr_in> buildBroadcastTargets(std::uint16_t discoveryPort) {
	std::vector<sockaddr_in> targets;

	auto addTarget = [&](std::uint32_t ip) {
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(discoveryPort);
		addr.sin_addr.s_addr = htonl(ip);
		targets.push_back(addr);
	};

	addTarget(INADDR_BROADCAST);

#ifndef _WIN32
	struct ifaddrs* ifaddr = nullptr;
	if (getifaddrs(&ifaddr) == 0) {
		for (auto* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
				continue;
			}
			if ((ifa->ifa_flags & IFF_BROADCAST) == 0) {
				continue;
			}

			auto* broad = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
			if (broad == nullptr) {
				continue;
			}

			sockaddr_in addr = *broad;
			addr.sin_port = htons(discoveryPort);
			if (std::none_of(targets.begin(), targets.end(), [&](const sockaddr_in& existing) {
					return existing.sin_addr.s_addr == addr.sin_addr.s_addr;
				})) {
				targets.push_back(addr);
			}
		}
		freeifaddrs(ifaddr);
	}
#endif

	return targets;
}

constexpr std::chrono::milliseconds kDefaultInactiveTimeout{15000};

bool waitReadable(SocketHandle socketFd, int timeoutMs) {
	if (timeoutMs < 0) {
		timeoutMs = 0;
	}

	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(socketFd, &readSet);

	timeval timeout{};
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_usec = (timeoutMs % 1000) * 1000;

	const int ready = select(static_cast<int>(socketFd) + 1, &readSet, nullptr, nullptr, &timeout);
	return ready > 0;
}
}  // namespace

DeviceDiscovery::DeviceDiscovery(std::string deviceName, std::uint16_t servicePort, std::uint16_t discoveryPort)
	: deviceName_(std::move(deviceName)),
	  deviceId_(createDeviceId()),
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

	const std::string payload = buildProbeMessage(discoveryPort_);
	const auto targets = buildBroadcastTargets(discoveryPort_);
	int sentCount = 0;
	for (const auto& addr : targets) {
		const int sent = sendto(socketFd,
		                        payload.c_str(),
		                        static_cast<int>(payload.size()),
		                        0,
		                        reinterpret_cast<const sockaddr*>(&addr),
		                        static_cast<SocketLen>(sizeof(addr)));
		if (sent >= 0) {
			++sentCount;
		}
	}

	closeSocket(socketFd);
	shutdownSockets();
	return sentCount > 0;
}

std::optional<DeviceDiscovery::PeerInfo> DeviceDiscovery::receiver(int timeoutMs) {
	if (timeoutMs <= 0) {
		return std::nullopt;
	}

	if (!initSockets()) {
		return std::nullopt;
	}

	const SocketHandle socketFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socketFd == kInvalidSocket) {
		shutdownSockets();
		return std::nullopt;
	}

	int reuse = 1;
	if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR,
	               reinterpret_cast<const char*>(&reuse),
	               static_cast<SocketLen>(sizeof(reuse))) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_port = htons(discoveryPort_);
	local.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(socketFd, reinterpret_cast<const sockaddr*>(&local), static_cast<SocketLen>(sizeof(local))) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	std::optional<PeerInfo> discovered;

	while (std::chrono::steady_clock::now() < deadline) {
		const auto now = std::chrono::steady_clock::now();
		const auto remainingMs = static_cast<int>(
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
		if (remainingMs <= 0 || !waitReadable(socketFd, remainingMs)) {
			break;
		}

		std::array<char, 1024> buffer{};
		sockaddr_in senderAddr{};
		SocketLen senderLen = static_cast<SocketLen>(sizeof(senderAddr));
		const int received = recvfrom(socketFd,
		                              buffer.data(),
		                              static_cast<int>(buffer.size() - 1),
		                              0,
		                              reinterpret_cast<sockaddr*>(&senderAddr),
		                              &senderLen);

		if (received < 0) {
			continue;
		}

		buffer[static_cast<std::size_t>(received)] = '\0';
		const std::string payload(buffer.data());

		char ipBuffer[INET_ADDRSTRLEN] = {0};
		const char* ipResult = inet_ntop(AF_INET, &senderAddr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
		const std::string senderIp = ipResult != nullptr ? std::string(ipBuffer) : std::string();

		if (payload == buildProbeMessage(discoveryPort_)) {
			sockaddr_in responseTarget = senderAddr;
			responseTarget.sin_port = htons(discoveryPort_);
			const std::string advertisedIp = localIpForRemote(senderAddr);

			std::string response = std::string(kResponsePrefix) + "|" + deviceId_ + "|" + deviceName_ + "|" +
			                       std::to_string(servicePort_);
			if (!advertisedIp.empty()) {
				response += "|" + advertisedIp;
			}
			sendto(socketFd,
			       response.c_str(),
			       static_cast<int>(response.size()),
			       0,
			       reinterpret_cast<const sockaddr*>(&responseTarget),
			       static_cast<SocketLen>(sizeof(responseTarget)));
			continue;
		}

		auto parsed = parseMessage(payload, senderIp);
		if (!parsed.has_value() || parsed->deviceId == deviceId_) {
			continue;
		}

		const bool shouldNotify = upsertDevice(*parsed, kDefaultInactiveTimeout);
		if (shouldNotify) {
			discovered = parsed;
			break;
		}
	}

	closeSocket(socketFd);
	shutdownSockets();
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
	if (tokens.size() == 5 && isValidIpv4(tokens[4]) && tokens[4] != "0.0.0.0") {
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

#include "networking/DeviceDiscovery.h"

#include <array>
#include <cstring>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {
constexpr const char* kPrefix = "SYNCFLOW";

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void closeSocket(SocketHandle socketFd) {
#ifdef _WIN32
	closesocket(socketFd);
#else
	close(socketFd);
#endif
}

bool initSockets() {
#ifdef _WIN32
	WSADATA wsaData{};
	return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
	return true;
#endif
}

void shutdownSockets() {
#ifdef _WIN32
	WSACleanup();
#endif
}
}  // namespace

DeviceDiscovery::DeviceDiscovery(std::string deviceName, std::uint16_t servicePort, std::uint16_t discoveryPort)
	: deviceName_(std::move(deviceName)), servicePort_(servicePort), discoveryPort_(discoveryPort) {}

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
	               static_cast<socklen_t>(sizeof(enableBroadcast))) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return false;
	}

	sockaddr_in target{};
	target.sin_family = AF_INET;
	target.sin_port = htons(discoveryPort_);
	target.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	const std::string payload = std::string(kPrefix) + "|" + deviceName_ + "|" + std::to_string(servicePort_);
	const int sent = sendto(socketFd,
	                        payload.c_str(),
	                        static_cast<int>(payload.size()),
	                        0,
	                        reinterpret_cast<const sockaddr*>(&target),
	                        static_cast<socklen_t>(sizeof(target)));

	closeSocket(socketFd);
	shutdownSockets();
	return sent >= 0;
}

std::optional<DeviceDiscovery::PeerInfo> DeviceDiscovery::receiver(int timeoutMs) const {
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
	               static_cast<socklen_t>(sizeof(reuse))) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

#ifdef _WIN32
	const DWORD winTimeout = timeoutMs < 0 ? 0 : static_cast<DWORD>(timeoutMs);
	setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&winTimeout), sizeof(winTimeout));
#else
	const timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
	setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_port = htons(discoveryPort_);
	local.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(socketFd, reinterpret_cast<const sockaddr*>(&local), static_cast<socklen_t>(sizeof(local))) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	std::array<char, 1024> buffer{};
	sockaddr_in senderAddr{};
	socklen_t senderLen = static_cast<socklen_t>(sizeof(senderAddr));
	const int received = recvfrom(socketFd,
	                              buffer.data(),
	                              static_cast<int>(buffer.size() - 1),
	                              0,
	                              reinterpret_cast<sockaddr*>(&senderAddr),
	                              &senderLen);

	if (received < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	buffer[static_cast<std::size_t>(received)] = '\0';

	char ipBuffer[INET_ADDRSTRLEN] = {0};
	const char* ipResult = inet_ntop(AF_INET, &senderAddr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
	const std::string senderIp = ipResult != nullptr ? std::string(ipBuffer) : std::string();

	closeSocket(socketFd);
	shutdownSockets();

	return parseMessage(std::string(buffer.data()), senderIp);
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

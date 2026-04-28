#include "platform/PlatformSocket.h"
#include "core/Logger.h"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace platform {

int PlatformSocket::refCount_ = 0;

PlatformSocket::PlatformSocket() = default;

PlatformSocket::PlatformSocket(SocketHandle sock) : socket_(sock) {}

PlatformSocket::PlatformSocket(PlatformSocket&& other) noexcept
	: socket_(other.socket_) {
	other.socket_ = INVALID_SOCKET_HANDLE;
}

PlatformSocket& PlatformSocket::operator=(PlatformSocket&& other) noexcept {
	if (this != &other) {
		close();
		socket_ = other.socket_;
		other.socket_ = INVALID_SOCKET_HANDLE;
	}
	return *this;
}

PlatformSocket::~PlatformSocket() {
	close();
}

bool PlatformSocket::initializeSocketSystem() {
#ifdef _WIN32
	WSADATA wsaData{};
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Logger::error("PlatformSocket: WSAStartup failed");
		return false;
	}
	refCount_++;
#endif
	return true;
}

void PlatformSocket::shutdownSocketSystem() {
#ifdef _WIN32
	if (refCount_ > 0) {
		refCount_--;
		if (refCount_ == 0) {
			WSACleanup();
		}
	}
#endif
}

std::optional<PlatformSocket> PlatformSocket::createTCP() {
	SocketHandle sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET_HANDLE) {
		Logger::error("PlatformSocket: Failed to create TCP socket");
		return std::nullopt;
	}
	return PlatformSocket(sock);
}

std::optional<PlatformSocket> PlatformSocket::createUDP() {
	SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET_HANDLE) {
		Logger::error("PlatformSocket: Failed to create UDP socket");
		return std::nullopt;
	}
	return PlatformSocket(sock);
}

bool PlatformSocket::bind(const std::string& address, std::uint16_t port) {
	if (!isValid()) {
		Logger::error("PlatformSocket: Attempt to bind invalid socket");
		return false;
	}

	struct sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (address.empty() || address == "0.0.0.0") {
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	} else if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
		Logger::error("PlatformSocket: Invalid address: " + address);
		return false;
	}

	if (::bind(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
		Logger::error("PlatformSocket: Bind failed on " + address + ":" + std::to_string(port));
		return false;
	}

	return true;
}

bool PlatformSocket::listen(int backlog) {
	if (!isValid()) {
		Logger::error("PlatformSocket: Attempt to listen on invalid socket");
		return false;
	}

	if (::listen(socket_, backlog) < 0) {
		Logger::error("PlatformSocket: Listen failed");
		return false;
	}

	return true;
}

std::optional<PlatformSocket> PlatformSocket::accept() {
	if (!isValid()) {
		Logger::error("PlatformSocket: Attempt to accept on invalid socket");
		return std::nullopt;
	}

	struct sockaddr_in addr {};
	SocketLen addrLen = sizeof(addr);

	SocketHandle accepted = ::accept(socket_, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
	if (accepted == INVALID_SOCKET_HANDLE) {
		Logger::error("PlatformSocket: Accept failed");
		return std::nullopt;
	}

	return PlatformSocket(accepted);
}

bool PlatformSocket::connect(const std::string& address, std::uint16_t port) {
	if (!isValid()) {
		Logger::error("PlatformSocket: Attempt to connect with invalid socket");
		return false;
	}

	struct sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
		Logger::error("PlatformSocket: Invalid address: " + address);
		return false;
	}

	if (::connect(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
		if (WSAGetLastError() != WSAECONNREFUSED) {
			Logger::error("PlatformSocket: Connect failed");
		}
#else
		if (errno != ECONNREFUSED) {
			Logger::error("PlatformSocket: Connect failed");
		}
#endif
		return false;
	}

	return true;
}

std::optional<std::vector<std::uint8_t>> PlatformSocket::receive(std::size_t maxBytes, int timeoutMs) {
	if (!isValid()) {
		Logger::error("PlatformSocket: Attempt to receive on invalid socket");
		return std::nullopt;
	}

	if (timeoutMs >= 0) {
		setTimeout(timeoutMs);
	}

	std::vector<std::uint8_t> buffer(maxBytes);
	int received = ::recv(socket_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(maxBytes), 0);

	if (received < 0) {
		Logger::debug("PlatformSocket: Receive error");
		return std::nullopt;
	}

	if (received == 0) {
		Logger::debug("PlatformSocket: Connection closed by peer");
		return std::nullopt;
	}

	buffer.resize(static_cast<std::size_t>(received));
	return buffer;
}

bool PlatformSocket::send(const std::vector<std::uint8_t>& data) {
	if (!isValid()) {
		Logger::error("PlatformSocket: Attempt to send on invalid socket");
		return false;
	}

	std::size_t sent = 0;
	while (sent < data.size()) {
		int rc = ::send(socket_, reinterpret_cast<const char*>(data.data() + sent),
		                static_cast<int>(data.size() - sent), 0);
		if (rc <= 0) {
			Logger::error("PlatformSocket: Send failed");
			return false;
		}
		sent += static_cast<std::size_t>(rc);
	}

	return true;
}

bool PlatformSocket::send(const std::string& data) {
	if (!isValid()) {
		Logger::error("PlatformSocket: Attempt to send on invalid socket");
		return false;
	}

	std::size_t sent = 0;
	while (sent < data.size()) {
		int rc = ::send(socket_, data.data() + sent, static_cast<int>(data.size() - sent), 0);
		if (rc <= 0) {
			Logger::error("PlatformSocket: Send failed");
			return false;
		}
		sent += static_cast<std::size_t>(rc);
	}

	return true;
}

bool PlatformSocket::setNonBlocking(bool nonBlocking) {
	if (!isValid()) {
		return false;
	}

#ifdef _WIN32
	u_long mode = nonBlocking ? 1 : 0;
	if (ioctlsocket(socket_, FIONBIO, &mode) != 0) {
		Logger::error("PlatformSocket: Failed to set non-blocking mode");
		return false;
	}
#else
	int flags = fcntl(socket_, F_GETFL, 0);
	if (flags < 0) {
		Logger::error("PlatformSocket: Failed to get flags");
		return false;
	}
	if (nonBlocking) {
		flags |= O_NONBLOCK;
	} else {
		flags &= ~O_NONBLOCK;
	}
	if (fcntl(socket_, F_SETFL, flags) < 0) {
		Logger::error("PlatformSocket: Failed to set flags");
		return false;
	}
#endif

	return true;
}

bool PlatformSocket::setReuseAddress(bool reuse) {
	if (!isValid()) {
		return false;
	}

	int opt = reuse ? 1 : 0;
	if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
		Logger::error("PlatformSocket: Failed to set SO_REUSEADDR");
		return false;
	}

	return true;
}

bool PlatformSocket::setKeepAlive(bool keepAlive) {
	if (!isValid()) {
		return false;
	}

	int opt = keepAlive ? 1 : 0;
	if (setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
		Logger::error("PlatformSocket: Failed to set SO_KEEPALIVE");
		return false;
	}

	return true;
}

bool PlatformSocket::setTimeout(int timeoutMs) {
	if (!isValid() || timeoutMs < 0) {
		return false;
	}

#ifdef _WIN32
	DWORD timeout = static_cast<DWORD>(timeoutMs);
	if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) <
	    0) {
		return false;
	}
#else
	struct timeval tv {};
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		return false;
	}
#endif

	return true;
}

std::optional<std::string> PlatformSocket::getLocalAddress() const {
	if (!isValid()) {
		return std::nullopt;
	}

	struct sockaddr_in addr {};
	SocketLen len = sizeof(addr);

	if (getsockname(socket_, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
		return std::nullopt;
	}

	char ipBuffer[INET_ADDRSTRLEN];
	const char* result = inet_ntop(AF_INET, &addr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
	return result != nullptr ? std::optional<std::string>(std::string(ipBuffer)) : std::nullopt;
}

std::optional<std::uint16_t> PlatformSocket::getLocalPort() const {
	if (!isValid()) {
		return std::nullopt;
	}

	struct sockaddr_in addr {};
	SocketLen len = sizeof(addr);

	if (getsockname(socket_, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
		return std::nullopt;
	}

	return ntohs(addr.sin_port);
}

std::optional<std::string> PlatformSocket::getPeerAddress() const {
	if (!isValid()) {
		return std::nullopt;
	}

	struct sockaddr_in addr {};
	SocketLen len = sizeof(addr);

	if (getpeername(socket_, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
		return std::nullopt;
	}

	char ipBuffer[INET_ADDRSTRLEN];
	const char* result = inet_ntop(AF_INET, &addr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
	return result != nullptr ? std::optional<std::string>(std::string(ipBuffer)) : std::nullopt;
}

std::optional<std::uint16_t> PlatformSocket::getPeerPort() const {
	if (!isValid()) {
		return std::nullopt;
	}

	struct sockaddr_in addr {};
	SocketLen len = sizeof(addr);

	if (getpeername(socket_, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
		return std::nullopt;
	}

	return ntohs(addr.sin_port);
}

std::optional<std::string> PlatformSocket::getHostname() {
	char buffer[256];
	if (gethostname(buffer, sizeof(buffer)) != 0) {
		return std::nullopt;
	}
	return std::string(buffer);
}

std::optional<std::vector<std::string>> PlatformSocket::getLocalAddresses() {
	std::vector<std::string> addresses;

	auto sock = createUDP();
	if (!sock) {
		return std::nullopt;
	}

	if (!sock->connect("8.8.8.8", 80)) {
		return addresses;  // Return empty list instead of nullopt
	}

	auto addr = sock->getLocalAddress();
	if (addr) {
		addresses.push_back(*addr);
	}

	return addresses;
}

void PlatformSocket::close() {
	if (isValid()) {
#ifdef _WIN32
		closesocket(socket_);
#else
		::close(socket_);
#endif
		socket_ = INVALID_SOCKET_HANDLE;
	}
}

}  // namespace platform

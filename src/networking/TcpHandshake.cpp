#include "networking/TcpHandshake.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace {
#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLen = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
std::mutex g_winsockMutex;
int g_winsockRefCount = 0;
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
#endif
	return true;
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

bool setNonBlocking(SocketHandle socketFd, bool nonBlocking) {
#ifdef _WIN32
	u_long mode = nonBlocking ? 1 : 0;
	return ioctlsocket(socketFd, FIONBIO, &mode) == 0;
#else
	int flags = fcntl(socketFd, F_GETFL, 0);
	if (flags < 0) {
		return false;
	}
	if (nonBlocking) {
		flags |= O_NONBLOCK;
	} else {
		flags &= ~O_NONBLOCK;
	}
	return fcntl(socketFd, F_SETFL, flags) == 0;
#endif
}

bool setRecvTimeout(SocketHandle socketFd, int timeoutMs) {
#ifdef _WIN32
	const DWORD timeout = timeoutMs < 0 ? 0 : static_cast<DWORD>(timeoutMs);
	return setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
#else
	const timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
	return setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
}

std::string socketIp(const sockaddr_in& addr) {
	char ipBuffer[INET_ADDRSTRLEN] = {0};
	const char* result = inet_ntop(AF_INET, &addr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
	return result != nullptr ? std::string(ipBuffer) : std::string();
}

std::optional<std::string> readLine(SocketHandle socketFd) {
	std::string data;
	data.reserve(256);
	std::array<char, 256> chunk{};

	while (data.size() < 1024) {
		const int received = recv(socketFd, chunk.data(), static_cast<int>(chunk.size()), 0);
		if (received <= 0) {
			return std::nullopt;
		}
		data.append(chunk.data(), static_cast<std::size_t>(received));
		const auto pos = data.find('\n');
		if (pos != std::string::npos) {
			data.resize(pos);
			return data;
		}
	}

	return std::nullopt;
}
}  // namespace

TcpHandshake::TcpHandshake(std::string localDeviceId, std::string localDeviceName, std::uint16_t listenPort)
	: localDeviceId_(std::move(localDeviceId)),
	  localDeviceName_(std::move(localDeviceName)),
	  listenPort_(listenPort),
	  running_(false),
	  listenSocket_(kInvalidSocket) {}

TcpHandshake::~TcpHandshake() {
	stop();
}

bool TcpHandshake::start() {
	if (running_.load()) {
		return true;
	}

	if (!initSockets()) {
		return false;
	}

	const SocketHandle socketFd = socket(AF_INET, SOCK_STREAM, 0);
	if (socketFd == kInvalidSocket) {
		shutdownSockets();
		return false;
	}

	int reuse = 1;
	if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR,
	               reinterpret_cast<const char*>(&reuse),
	               static_cast<int>(sizeof(reuse))) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return false;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(listenPort_);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(socketFd, reinterpret_cast<const sockaddr*>(&addr), static_cast<int>(sizeof(addr))) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return false;
	}

	if (listen(socketFd, 16) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return false;
	}

	running_.store(true);
	listenSocket_ = socketFd;
	return true;
}

void TcpHandshake::stop() {
	if (!running_.exchange(false)) {
		return;
	}

	if (listenSocket_ != kInvalidSocket) {
		closeSocket(static_cast<SocketHandle>(listenSocket_));
		listenSocket_ = kInvalidSocket;
	}

	shutdownSockets();
}

std::optional<TcpHandshake::RemoteDevice> TcpHandshake::pollAccepted(int timeoutMs) {
	if (!running_.load() || listenSocket_ == kInvalidSocket) {
		return std::nullopt;
	}
	const SocketHandle listenFd = static_cast<SocketHandle>(listenSocket_);

	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(listenFd, &readSet);

	timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
	const int selectResult = select(static_cast<int>(listenFd) + 1, &readSet, nullptr, nullptr, &timeout);
	if (selectResult <= 0) {
		return std::nullopt;
	}

	sockaddr_in remoteAddr{};
	SocketLen remoteLen = static_cast<SocketLen>(sizeof(remoteAddr));
	const SocketHandle client = accept(listenFd,
	                                   reinterpret_cast<sockaddr*>(&remoteAddr),
	                                   &remoteLen);
	if (client == kInvalidSocket) {
		return std::nullopt;
	}

	setRecvTimeout(client, timeoutMs);
	const auto incoming = readLine(client);
	if (!incoming.has_value()) {
		closeSocket(client);
		return std::nullopt;
	}

	const auto remote = parseHello(*incoming, socketIp(remoteAddr), ntohs(remoteAddr.sin_port));
	if (!remote.has_value() || remote->deviceId == localDeviceId_) {
		closeSocket(client);
		return std::nullopt;
	}

	const std::string response = buildHello(localDeviceId_, localDeviceName_);
	const int sent = send(client, response.c_str(), static_cast<int>(response.size()), 0);
	closeSocket(client);
	if (sent < 0) {
		return std::nullopt;
	}

	return remote;
}

std::optional<TcpHandshake::RemoteDevice> TcpHandshake::connectAndHandshake(const std::string& ip,
	                                                                         std::uint16_t port,
	                                                                         int timeoutMs) {
	if (!initSockets()) {
		return std::nullopt;
	}

	const SocketHandle socketFd = socket(AF_INET, SOCK_STREAM, 0);
	if (socketFd == kInvalidSocket) {
		shutdownSockets();
		return std::nullopt;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	if (!setNonBlocking(socketFd, true)) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	int connectResult = connect(socketFd, reinterpret_cast<const sockaddr*>(&addr), static_cast<int>(sizeof(addr)));
	if (connectResult < 0) {
#ifdef _WIN32
		const int err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
			closeSocket(socketFd);
			shutdownSockets();
			return std::nullopt;
		}
#else
		if (errno != EINPROGRESS) {
			closeSocket(socketFd);
			shutdownSockets();
			return std::nullopt;
		}
#endif
	}

	fd_set writeSet;
	FD_ZERO(&writeSet);
	FD_SET(socketFd, &writeSet);
	timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
	const int selectResult = select(static_cast<int>(socketFd) + 1, nullptr, &writeSet, nullptr, &timeout);
	if (selectResult <= 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	int soError = 0;
	SocketLen soLen = static_cast<SocketLen>(sizeof(soError));
	if (getsockopt(socketFd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soLen) != 0 || soError != 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	setNonBlocking(socketFd, false);
	setRecvTimeout(socketFd, timeoutMs);

	const std::string hello = buildHello(localDeviceId_, localDeviceName_);
	if (send(socketFd, hello.c_str(), static_cast<int>(hello.size()), 0) < 0) {
		closeSocket(socketFd);
		shutdownSockets();
		return std::nullopt;
	}

	auto response = readLine(socketFd);
	closeSocket(socketFd);
	shutdownSockets();

	if (!response.has_value()) {
		return std::nullopt;
	}

	auto remote = parseHello(*response, ip, port);
	if (!remote.has_value() || remote->deviceId == localDeviceId_) {
		return std::nullopt;
	}

	return remote;
}

bool TcpHandshake::isValidDeviceId(const std::string& value) {
	if (value.size() < 8 || value.size() > 64) {
		return false;
	}
	for (const unsigned char ch : value) {
		if (!(std::isalnum(ch) || ch == '-' || ch == '_')) {
			return false;
		}
	}
	return true;
}

bool TcpHandshake::isValidDeviceName(const std::string& value) {
	if (value.empty() || value.size() > 128) {
		return false;
	}
	return value.find('|') == std::string::npos;
}

std::string TcpHandshake::buildHello(const std::string& id, const std::string& name) {
	return std::string("HELLO|") + id + "|" + name + "\n";
}

std::optional<TcpHandshake::RemoteDevice> TcpHandshake::parseHello(const std::string& text,
	                                                                const std::string& ip,
	                                                                std::uint16_t port) {
	std::stringstream ss(text);
	std::string part;
	std::array<std::string, 3> tokens;
	std::size_t idx = 0;
	while (std::getline(ss, part, '|') && idx < tokens.size()) {
		tokens[idx++] = part;
	}
	if (std::getline(ss, part, '|')) {
		return std::nullopt;
	}
	if (idx != 3 || tokens[0] != "HELLO") {
		return std::nullopt;
	}
	if (!isValidDeviceId(tokens[1]) || !isValidDeviceName(tokens[2])) {
		return std::nullopt;
	}
	return RemoteDevice{tokens[1], tokens[2], ip, port};
}
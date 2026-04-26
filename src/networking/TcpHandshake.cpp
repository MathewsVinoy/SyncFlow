#include "networking/TcpHandshake.h"

#include <array>
#include <cctype>
#include <chrono>
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

constexpr std::chrono::seconds kHeartbeatInterval{2};
constexpr std::chrono::seconds kHeartbeatTimeout{10};
constexpr std::chrono::seconds kRetryDelay{2};
constexpr int kHandshakeTimeoutMs = 1500;

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

bool wouldBlock() {
#ifdef _WIN32
	const int err = WSAGetLastError();
	return err == WSAEWOULDBLOCK;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

bool connectCompleted(SocketHandle socketFd) {
	int soError = 0;
	SocketLen len = static_cast<SocketLen>(sizeof(soError));
	if (getsockopt(socketFd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len) < 0) {
		return false;
	}
	return soError == 0;
}

std::string socketIp(const sockaddr_in& addr) {
	char ipBuffer[INET_ADDRSTRLEN] = {0};
	const char* result = inet_ntop(AF_INET, &addr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
	return result != nullptr ? std::string(ipBuffer) : std::string();
}

bool sendAll(SocketHandle fd, const std::string& data) {
	std::size_t sent = 0;
	while (sent < data.size()) {
		const int rc = send(fd, data.data() + sent, static_cast<int>(data.size() - sent), 0);
		if (rc <= 0) {
			return false;
		}
		sent += static_cast<std::size_t>(rc);
	}
	return true;
}

std::optional<std::string> readLineWithTimeout(SocketHandle fd, int timeoutMs) {
	std::string out;
	out.reserve(256);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	char buf[128];

	for (;;) {
		auto now = std::chrono::steady_clock::now();
		if (now >= deadline) {
			return std::nullopt;
		}
		auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
		timeval tv{static_cast<long>(remaining.count() / 1000), static_cast<long>((remaining.count() % 1000) * 1000)};
		fd_set set;
		FD_ZERO(&set);
		FD_SET(fd, &set);
		const int sel = select(static_cast<int>(fd) + 1, &set, nullptr, nullptr, &tv);
		if (sel <= 0) {
			return std::nullopt;
		}

		const int rc = recv(fd, buf, sizeof(buf), 0);
		if (rc <= 0) {
			return std::nullopt;
		}
		out.append(buf, static_cast<std::size_t>(rc));
		if (out.size() > 1024) {
			return std::nullopt;
		}
		auto pos = out.find('\n');
		if (pos != std::string::npos) {
			out.resize(pos);
			return out;
		}
	}
}
}  // namespace

TcpHandshake::TcpHandshake(std::string localDeviceId, std::string localDeviceName, std::uint16_t listenPort)
	: localDeviceId_(std::move(localDeviceId)),
	  localDeviceName_(std::move(localDeviceName)),
	  listenPort_(listenPort),
	  running_(false),
	  listenSocket_(static_cast<std::intptr_t>(kInvalidSocket)) {}

TcpHandshake::~TcpHandshake() {
	stop();
}

bool TcpHandshake::start() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (running_) {
		return true;
	}
	if (!initSockets()) {
		return false;
	}

	const SocketHandle fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == kInvalidSocket) {
		shutdownSockets();
		return false;
	}

	int reuse = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) < 0) {
		closeSocket(fd);
		shutdownSockets();
		return false;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(listenPort_);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, reinterpret_cast<const sockaddr*>(&addr), static_cast<SocketLen>(sizeof(addr))) < 0 ||
	    listen(fd, 16) < 0 || !setNonBlocking(fd, true)) {
		closeSocket(fd);
		shutdownSockets();
		return false;
	}

	running_ = true;
	listenSocket_ = static_cast<std::intptr_t>(fd);
	pushEventLocked("TCP started on port " + std::to_string(listenPort_));
	return true;
}

void TcpHandshake::stop() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (!running_) {
		return;
	}

	for (auto& [_, st] : states_) {
		if (st.connected && st.socket != static_cast<std::intptr_t>(kInvalidSocket)) {
			closeSocket(static_cast<SocketHandle>(st.socket));
		}
	}
	states_.clear();

	if (listenSocket_ != static_cast<std::intptr_t>(kInvalidSocket)) {
		closeSocket(static_cast<SocketHandle>(listenSocket_));
		listenSocket_ = static_cast<std::intptr_t>(kInvalidSocket);
	}

	running_ = false;
	pushEventLocked("TCP stopped");
	shutdownSockets();
}

void TcpHandshake::observePeer(const RemoteDevice& peer) {
	if (!isValidDeviceId(peer.deviceId) || !isValidDeviceName(peer.deviceName) || peer.port == 0 ||
	    peer.deviceId == localDeviceId_) {
		return;
	}

	std::lock_guard<std::mutex> lock(stateMutex_);
	auto& st = states_[peer.deviceId];
	const bool first = st.remote.deviceId.empty();
	st.remote = peer;
	if (first) {
		st.nextRetry = std::chrono::steady_clock::now();
		pushEventLocked("TCP peer observed: id=" + peer.deviceId + " name=" + peer.deviceName + " ip=" + peer.ip +
		               " port=" + std::to_string(peer.port));
	}
}

void TcpHandshake::removePeer(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(stateMutex_);
	auto it = states_.find(deviceId);
	if (it == states_.end()) {
		return;
	}
	if (it->second.connected && it->second.socket != static_cast<std::intptr_t>(kInvalidSocket)) {
		closeSocket(static_cast<SocketHandle>(it->second.socket));
	}
	states_.erase(it);
	pushEventLocked("TCP peer removed: id=" + deviceId);
}

void TcpHandshake::tick() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (!running_) {
		return;
	}
	acceptIncoming();
	processConnections();
}

std::optional<std::string> TcpHandshake::pollEvent() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (events_.empty()) {
		return std::nullopt;
	}
	std::string e = events_.front();
	events_.pop_front();
	return e;
}

std::vector<TcpHandshake::RemoteDevice> TcpHandshake::getConnectedPeers() const {
	std::lock_guard<std::mutex> lock(stateMutex_);
	std::vector<RemoteDevice> out;
	for (const auto& [_, st] : states_) {
		if (st.connected) {
			out.push_back(st.remote);
		}
	}
	return out;
}

void TcpHandshake::pushEventLocked(const std::string& event) {
	events_.push_back(event);
	if (events_.size() > 300) {
		events_.pop_front();
	}
}

void TcpHandshake::acceptIncoming() {
	SocketHandle listenFd = static_cast<SocketHandle>(listenSocket_);
	for (int i = 0; i < 8; ++i) {
		sockaddr_in remoteAddr{};
		SocketLen remoteLen = static_cast<SocketLen>(sizeof(remoteAddr));
		SocketHandle client = accept(listenFd, reinterpret_cast<sockaddr*>(&remoteAddr), &remoteLen);
		if (client == kInvalidSocket) {
			if (wouldBlock()) {
				break;
			}
			return;
		}

		if (!setNonBlocking(client, false)) {
			closeSocket(client);
			continue;
		}

		auto helloLine = readLineWithTimeout(client, kHandshakeTimeoutMs);
		auto remote = helloLine ? parseHello(*helloLine, socketIp(remoteAddr), ntohs(remoteAddr.sin_port)) : std::nullopt;
		if (!remote.has_value() || remote->deviceId == localDeviceId_) {
			closeSocket(client);
			pushEventLocked("TCP invalid incoming HELLO ignored");
			continue;
		}

		if (!sendAll(client, buildHello(localDeviceId_, localDeviceName_))) {
			closeSocket(client);
			continue;
		}

		auto& st = states_[remote->deviceId];
		if (st.connected) {
			closeSocket(client);
			pushEventLocked("TCP duplicate incoming ignored: id=" + remote->deviceId);
			continue;
		}

		if (!setNonBlocking(client, true)) {
			closeSocket(client);
			continue;
		}

		st.remote = *remote;
		st.connected = true;
		st.outbound = false;
		st.socket = static_cast<std::intptr_t>(client);
		st.readBuffer.clear();
		st.lastPingSent = std::chrono::steady_clock::now();
		st.lastPongSeen = std::chrono::steady_clock::now();

		pushEventLocked("TCP HELLO verified (incoming): id=" + remote->deviceId + " name=" + remote->deviceName);
		pushEventLocked("TCP connected (incoming): id=" + remote->deviceId + " ip=" + remote->ip +
		               " port=" + std::to_string(remote->port));
	}
}

void TcpHandshake::processConnections() {
	const auto now = std::chrono::steady_clock::now();

	for (auto& [_, st] : states_) {
		if (!st.connected) {
			if (now < st.nextRetry) {
				continue;
			}
			if (!connectAndHandshakeLocked(st)) {
				st.nextRetry = now + kRetryDelay;
				pushEventLocked("TCP retry scheduled: id=" + st.remote.deviceId);
			}
			continue;
		}

		SocketHandle fd = static_cast<SocketHandle>(st.socket);
		if (now - st.lastPingSent >= kHeartbeatInterval) {
			if (!sendAll(fd, "PING\n")) {
				disconnectLocked(st, "send-failed");
				continue;
			}
			st.lastPingSent = now;
			pushEventLocked("TCP heartbeat PING sent: id=" + st.remote.deviceId);
		}

		if (!processIncomingLinesLocked(st)) {
			disconnectLocked(st, "read-failed");
			continue;
		}

		if (now - st.lastPongSeen > kHeartbeatTimeout) {
			disconnectLocked(st, "heartbeat-timeout");
		}
	}
}

void TcpHandshake::disconnectLocked(ConnectionState& st, const std::string& reason) {
	if (st.connected && st.socket != static_cast<std::intptr_t>(kInvalidSocket)) {
		closeSocket(static_cast<SocketHandle>(st.socket));
	}
	st.connected = false;
	st.socket = static_cast<std::intptr_t>(kInvalidSocket);
	st.readBuffer.clear();
	st.nextRetry = std::chrono::steady_clock::now() + kRetryDelay;
	pushEventLocked("TCP disconnected: id=" + st.remote.deviceId + " reason=" + reason);
}

bool TcpHandshake::connectAndHandshakeLocked(ConnectionState& st) {
	SocketHandle fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == kInvalidSocket) {
		return false;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(st.remote.port);
	if (inet_pton(AF_INET, st.remote.ip.c_str(), &addr.sin_addr) != 1) {
		closeSocket(fd);
		return false;
	}

	if (!setNonBlocking(fd, true)) {
		closeSocket(fd);
		return false;
	}

	int rc = connect(fd, reinterpret_cast<const sockaddr*>(&addr), static_cast<SocketLen>(sizeof(addr)));
	if (rc < 0 && !wouldBlock()) {
		closeSocket(fd);
		return false;
	}

	fd_set wset;
	FD_ZERO(&wset);
	FD_SET(fd, &wset);
	timeval tv{kHandshakeTimeoutMs / 1000, (kHandshakeTimeoutMs % 1000) * 1000};
	if (select(static_cast<int>(fd) + 1, nullptr, &wset, nullptr, &tv) <= 0) {
		closeSocket(fd);
		return false;
	}

	if (!connectCompleted(fd)) {
		closeSocket(fd);
		return false;
	}

	if (!setNonBlocking(fd, false)) {
		closeSocket(fd);
		return false;
	}

	pushEventLocked("TCP connect attempt: id=" + st.remote.deviceId + " ip=" + st.remote.ip +
	               " port=" + std::to_string(st.remote.port));

	if (!sendAll(fd, buildHello(localDeviceId_, localDeviceName_))) {
		closeSocket(fd);
		return false;
	}

	auto line = readLineWithTimeout(fd, kHandshakeTimeoutMs);
	auto remote = line ? parseHello(*line, st.remote.ip, st.remote.port) : std::nullopt;
	if (!remote.has_value() || remote->deviceId != st.remote.deviceId) {
		closeSocket(fd);
		pushEventLocked("TCP invalid HELLO response ignored: id=" + st.remote.deviceId);
		return false;
	}

	if (!setNonBlocking(fd, true)) {
		closeSocket(fd);
		return false;
	}

	st.connected = true;
	st.outbound = true;
	st.socket = static_cast<std::intptr_t>(fd);
	st.readBuffer.clear();
	st.lastPingSent = std::chrono::steady_clock::now();
	st.lastPongSeen = std::chrono::steady_clock::now();

	pushEventLocked("TCP HELLO verified (outgoing): id=" + remote->deviceId + " name=" + remote->deviceName);
	pushEventLocked("TCP connected (outgoing): id=" + remote->deviceId + " ip=" + remote->ip +
	               " port=" + std::to_string(remote->port));
	return true;
}

bool TcpHandshake::processIncomingLinesLocked(ConnectionState& st) {
	SocketHandle fd = static_cast<SocketHandle>(st.socket);
	char buf[256];

	for (;;) {
		const int rc = recv(fd, buf, sizeof(buf), 0);
		if (rc == 0) {
			return false;
		}
		if (rc < 0) {
			if (wouldBlock()) {
				break;
			}
			return false;
		}

		st.readBuffer.append(buf, static_cast<std::size_t>(rc));
		if (st.readBuffer.size() > 4096) {
			pushEventLocked("TCP invalid oversized message ignored: id=" + st.remote.deviceId);
			return false;
		}

		for (;;) {
			auto nl = st.readBuffer.find('\n');
			if (nl == std::string::npos) {
				break;
			}
			std::string line = st.readBuffer.substr(0, nl);
			st.readBuffer.erase(0, nl + 1);

			if (!isValidControlLine(line)) {
				pushEventLocked("TCP invalid message ignored: id=" + st.remote.deviceId);
				return false;
			}

			if (line == "PING") {
				if (!sendAll(fd, "PONG\n")) {
					return false;
				}
				pushEventLocked("TCP heartbeat PONG sent: id=" + st.remote.deviceId);
			} else {
				st.lastPongSeen = std::chrono::steady_clock::now();
				pushEventLocked("TCP heartbeat PONG received: id=" + st.remote.deviceId);
			}
		}
	}

	return true;
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

bool TcpHandshake::isValidControlLine(const std::string& text) {
	return text == "PING" || text == "PONG";
}

bool TcpHandshake::shouldInitiate(const std::string& localId, const std::string& remoteId) {
	return localId < remoteId;
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
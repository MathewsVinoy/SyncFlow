#if 0
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
	int g_winsockRefCount = 1;
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
	if (g_winsockRefCount == 1) {
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
	if (g_winsockRefCount > 1) {
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
	messages_.clear();

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

bool TcpHandshake::sendMessage(const std::string& deviceId, const std::string& payload) {
	if (!isValidAppPayload(payload)) {
		return false;
	}

	std::lock_guard<std::mutex> lock(stateMutex_);
	auto it = states_.find(deviceId);
	if (it == states_.end() || !it->second.connected ||
	    it->second.socket == static_cast<std::intptr_t>(kInvalidSocket)) {
		return false;
	}

	SocketHandle fd = static_cast<SocketHandle>(it->second.socket);
	return sendAll(fd, std::string("MSG|") + payload + "\n");
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

std::optional<TcpHandshake::InboundMessage> TcpHandshake::pollMessage() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (messages_.empty()) {
		return std::nullopt;
	}
	InboundMessage m = std::move(messages_.front());
	messages_.pop_front();
	return m;
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
		if (st.readBuffer.size() > 131072) {
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
				if (line.rfind("MSG|", 0) == 0) {
					std::string payload = line.substr(4);
					if (!isValidAppPayload(payload)) {
						pushEventLocked("TCP invalid app payload ignored: id=" + st.remote.deviceId);
						return false;
					}
					messages_.push_back(InboundMessage{st.remote.deviceId, std::move(payload)});
					if (messages_.size() > 300) {
						messages_.pop_front();
					}
					continue;
				}

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

bool TcpHandshake::isValidAppPayload(const std::string& payload) {
	if (payload.empty() || payload.size() > 12000) {
		return false;
	}
	return payload.find('\n') == std::string::npos && payload.find('\r') == std::string::npos;
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

#endif

#include "networking/TcpHandshake.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

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
constexpr int kHandshakeTimeoutMs = 3000;
constexpr int kIoTimeoutMs = 2500;

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

std::string opensslErrorString() {
	char buf[256] = {0};
	const unsigned long err = ERR_get_error();
	if (err == 0) {
		return "no-openssl-error";
	}
	ERR_error_string_n(err, buf, sizeof(buf));
	return std::string(buf);
}

int acceptAllCertificates(int /*preverifyOk*/, X509_STORE_CTX* /*ctx*/) {
	return 1;
}

bool waitSocketReady(SocketHandle fd, bool forRead, int timeoutMs) {
	fd_set set;
	FD_ZERO(&set);
	FD_SET(fd, &set);
	timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
	const int rc = select(static_cast<int>(fd) + 1, forRead ? &set : nullptr, forRead ? nullptr : &set, nullptr, &tv);
	return rc > 0;
}

bool performTlsHandshake(SSL* ssl, SocketHandle fd, bool serverMode, int timeoutMs) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	for (;;) {
		const int rc = serverMode ? SSL_accept(ssl) : SSL_connect(ssl);
		if (rc == 1) {
			return true;
		}

		const int err = SSL_get_error(ssl, rc);
		if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
			return false;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline) {
			return false;
		}

		const int remainingMs = static_cast<int>(
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
		if (!waitSocketReady(fd, err == SSL_ERROR_WANT_READ, remainingMs)) {
			return false;
		}
	}
}

bool sslWriteAll(SSL* ssl, SocketHandle fd, const std::string& data, int timeoutMs) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	std::size_t written = 0;
	while (written < data.size()) {
		const int rc = SSL_write(ssl,
		                         data.data() + written,
		                         static_cast<int>(data.size() - written));
		if (rc > 0) {
			written += static_cast<std::size_t>(rc);
			continue;
		}

		const int err = SSL_get_error(ssl, rc);
		if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
			return false;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline) {
			return false;
		}
		const int remainingMs = static_cast<int>(
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
		if (!waitSocketReady(fd, err == SSL_ERROR_WANT_READ, remainingMs)) {
			return false;
		}
	}
	return true;
}

std::optional<std::string> sslReadLineWithTimeout(SSL* ssl, SocketHandle fd, int timeoutMs) {
	std::string out;
	out.reserve(256);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	char buf[128];

	for (;;) {
		const int rc = SSL_read(ssl, buf, static_cast<int>(sizeof(buf)));
		if (rc > 0) {
			out.append(buf, static_cast<std::size_t>(rc));
			if (out.size() > 1024) {
				return std::nullopt;
			}
			auto pos = out.find('\n');
			if (pos != std::string::npos) {
				out.resize(pos);
				return out;
			}
			continue;
		}

		const int err = SSL_get_error(ssl, rc);
		if (err == SSL_ERROR_ZERO_RETURN) {
			return std::nullopt;
		}
		if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
			return std::nullopt;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline) {
			return std::nullopt;
		}
		const int remainingMs = static_cast<int>(
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
		if (!waitSocketReady(fd, err == SSL_ERROR_WANT_READ, remainingMs)) {
			return std::nullopt;
		}
	}
}

std::optional<std::string> extractPeerCommonName(SSL* ssl) {
	X509* cert = SSL_get_peer_certificate(ssl);
	if (cert == nullptr) {
		return std::nullopt;
	}

	char cn[256] = {0};
	X509_NAME* subject = X509_get_subject_name(cert);
	if (subject == nullptr) {
		X509_free(cert);
		return std::nullopt;
	}

	const int len = X509_NAME_get_text_by_NID(subject, NID_commonName, cn, static_cast<int>(sizeof(cn)));
	X509_free(cert);
	if (len <= 0 || static_cast<std::size_t>(len) >= sizeof(cn)) {
		return std::nullopt;
	}
	return std::string(cn, static_cast<std::size_t>(len));
}

bool ensureDeviceCertificate(const std::filesystem::path& keyPath,
                             const std::filesystem::path& certPath,
                             const std::string& deviceId,
                             const std::string& deviceName) {
	if (std::filesystem::exists(keyPath) && std::filesystem::exists(certPath)) {
		return true;
	}

	EVP_PKEY_CTX* keyCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
	if (keyCtx == nullptr) {
		return false;
	}

	if (EVP_PKEY_keygen_init(keyCtx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(keyCtx, 2048) <= 0) {
		EVP_PKEY_CTX_free(keyCtx);
		return false;
	}

	EVP_PKEY* pkey = nullptr;
	if (EVP_PKEY_keygen(keyCtx, &pkey) <= 0 || pkey == nullptr) {
		EVP_PKEY_CTX_free(keyCtx);
		return false;
	}
	EVP_PKEY_CTX_free(keyCtx);

	X509* cert = X509_new();
	if (cert == nullptr) {
		EVP_PKEY_free(pkey);
		return false;
	}

	ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
	X509_gmtime_adj(X509_get_notBefore(cert), -60 * 60);
	X509_gmtime_adj(X509_get_notAfter(cert), 3650L * 24L * 60L * 60L);
	X509_set_pubkey(cert, pkey);

	X509_NAME* name = X509_get_subject_name(cert);
	X509_NAME_add_entry_by_txt(name,
	                           "CN",
	                           MBSTRING_ASC,
	                           reinterpret_cast<const unsigned char*>(deviceId.c_str()),
	                           -1,
	                           -1,
	                           0);
	X509_NAME_add_entry_by_txt(name,
	                           "O",
	                           MBSTRING_ASC,
	                           reinterpret_cast<const unsigned char*>(deviceName.c_str()),
	                           -1,
	                           -1,
	                           0);
	X509_set_issuer_name(cert, name);

	if (X509_sign(cert, pkey, EVP_sha256()) <= 0) {
		X509_free(cert);
		EVP_PKEY_free(pkey);
		return false;
	}

	FILE* keyFile = std::fopen(keyPath.string().c_str(), "wb");
	if (keyFile == nullptr) {
		X509_free(cert);
		EVP_PKEY_free(pkey);
		return false;
	}
	const bool keyOk = PEM_write_PrivateKey(keyFile, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1;
	std::fclose(keyFile);

	FILE* certFile = std::fopen(certPath.string().c_str(), "wb");
	if (certFile == nullptr) {
		X509_free(cert);
		EVP_PKEY_free(pkey);
		return false;
	}
	const bool certOk = PEM_write_X509(certFile, cert) == 1;
	std::fclose(certFile);

	X509_free(cert);
	EVP_PKEY_free(pkey);
	return keyOk && certOk;
}

bool addTrustedCertificate(SSL_CTX* ctx, const std::filesystem::path& certPath) {
	BIO* bio = BIO_new_file(certPath.string().c_str(), "rb");
	if (bio == nullptr) {
		return false;
	}

	X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);
	if (cert == nullptr) {
		return false;
	}

	X509_STORE* store = SSL_CTX_get_cert_store(ctx);
	if (store == nullptr) {
		X509_free(cert);
		return false;
	}

	const int rc = X509_STORE_add_cert(store, cert);
	X509_free(cert);
	if (rc == 1) {
		return true;
	}

	const unsigned long err = ERR_peek_last_error();
	const unsigned long reason = ERR_GET_REASON(err);
	if (reason == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
		ERR_clear_error();
		return true;
	}
	return false;
}
}  // namespace

TcpHandshake::TcpHandshake(std::string localDeviceId, std::string localDeviceName, std::uint16_t listenPort)
	: localDeviceId_(std::move(localDeviceId)),
	  localDeviceName_(std::move(localDeviceName)),
	  listenPort_(listenPort),
	  running_(false),
	  listenSocket_(static_cast<std::intptr_t>(kInvalidSocket)),
	  serverTlsContext_(nullptr),
	  clientTlsContext_(nullptr) {}

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

	if (!ensureTlsInitializedLocked()) {
		shutdownSockets();
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
	pushEventLocked("TLS/TCP started on port " + std::to_string(listenPort_));
	return true;
}

void TcpHandshake::stop() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (!running_) {
		return;
	}

	for (auto& [_, st] : states_) {
		closeConnectionTransportLocked(st);
	}
	states_.clear();
	messages_.clear();

	if (listenSocket_ != static_cast<std::intptr_t>(kInvalidSocket)) {
		closeSocket(static_cast<SocketHandle>(listenSocket_));
		listenSocket_ = static_cast<std::intptr_t>(kInvalidSocket);
	}

	if (serverTlsContext_ != nullptr) {
		SSL_CTX_free(serverTlsContext_);
		serverTlsContext_ = nullptr;
	}
	if (clientTlsContext_ != nullptr) {
		SSL_CTX_free(clientTlsContext_);
		clientTlsContext_ = nullptr;
	}

	running_ = false;
	pushEventLocked("TLS/TCP stopped");
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
		pushEventLocked("TLS peer observed: id=" + peer.deviceId + " name=" + peer.deviceName + " ip=" + peer.ip +
		               " port=" + std::to_string(peer.port));
	}

	if (!loadTrustedDeviceLocked(peer.deviceId)) {
		pushEventLocked("TLS peer certificate not preloaded; using first-run pairing: id=" + peer.deviceId);
	}
}

void TcpHandshake::removePeer(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(stateMutex_);
	auto it = states_.find(deviceId);
	if (it == states_.end()) {
		return;
	}
	closeConnectionTransportLocked(it->second);
	states_.erase(it);
	pushEventLocked("TLS peer removed: id=" + deviceId);
}

bool TcpHandshake::sendMessage(const std::string& deviceId, const std::string& payload) {
	if (!isValidAppPayload(payload)) {
		return false;
	}

	std::lock_guard<std::mutex> lock(stateMutex_);
	auto it = states_.find(deviceId);
	if (it == states_.end() || !it->second.connected || it->second.ssl == nullptr ||
	    it->second.socket == static_cast<std::intptr_t>(kInvalidSocket)) {
		return false;
	}

	SocketHandle fd = static_cast<SocketHandle>(it->second.socket);
	if (!sslWriteAll(it->second.ssl, fd, std::string("MSG|") + payload + "\n", kIoTimeoutMs)) {
		pushEventLocked("TLS write failed: id=" + deviceId + " err=" + opensslErrorString());
		disconnectLocked(it->second, "ssl-write-failed");
		return false;
	}
	return true;
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

std::optional<TcpHandshake::InboundMessage> TcpHandshake::pollMessage() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (messages_.empty()) {
		return std::nullopt;
	}
	InboundMessage m = std::move(messages_.front());
	messages_.pop_front();
	return m;
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

		if (!setNonBlocking(client, true)) {
			closeSocket(client);
			continue;
		}

		SSL* ssl = SSL_new(serverTlsContext_);
		if (ssl == nullptr) {
			closeSocket(client);
			continue;
		}
		SSL_set_fd(ssl, static_cast<int>(client));

		if (!performTlsHandshake(ssl, client, true, kHandshakeTimeoutMs)) {
			pushEventLocked("TLS incoming handshake rejected: err=" + opensslErrorString());
			SSL_free(ssl);
			closeSocket(client);
			continue;
		}

		if (SSL_get_verify_result(ssl) != X509_V_OK) {
			pushEventLocked("TLS incoming certificate is untrusted; continuing with device ID check");
		}

		auto tlsPeerDeviceId = extractPeerCommonName(ssl);
		if (!tlsPeerDeviceId.has_value() || !isValidDeviceId(*tlsPeerDeviceId)) {
			pushEventLocked("TLS incoming certificate missing valid CN");
			SSL_free(ssl);
			closeSocket(client);
			continue;
		}

		auto helloLine = sslReadLineWithTimeout(ssl, client, kHandshakeTimeoutMs);
		auto remote =
			helloLine ? parseHello(*helloLine, socketIp(remoteAddr), ntohs(remoteAddr.sin_port)) : std::nullopt;
		if (!remote.has_value() || remote->deviceId == localDeviceId_ || remote->deviceId != *tlsPeerDeviceId) {
			pushEventLocked("TLS incoming invalid HELLO or identity mismatch");
			SSL_free(ssl);
			closeSocket(client);
			continue;
		}

		if (!sslWriteAll(ssl, client, buildHello(localDeviceId_, localDeviceName_), kHandshakeTimeoutMs)) {
			pushEventLocked("TLS incoming failed sending HELLO response");
			SSL_free(ssl);
			closeSocket(client);
			continue;
		}

		auto& st = states_[remote->deviceId];
		if (st.connected) {
			SSL_free(ssl);
			closeSocket(client);
			pushEventLocked("TLS duplicate incoming ignored: id=" + remote->deviceId);
			continue;
		}

		st.remote = *remote;
		st.connected = true;
		st.outbound = false;
		st.socket = static_cast<std::intptr_t>(client);
		st.ssl = ssl;
		st.tlsPeerDeviceId = *tlsPeerDeviceId;
		st.readBuffer.clear();
		st.lastPingSent = std::chrono::steady_clock::now();
		st.lastPongSeen = std::chrono::steady_clock::now();

		pushEventLocked("TLS connected (incoming): id=" + remote->deviceId + " ip=" + remote->ip +
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
				pushEventLocked("TLS retry scheduled: id=" + st.remote.deviceId);
			}
			continue;
		}

		SocketHandle fd = static_cast<SocketHandle>(st.socket);
		if (now - st.lastPingSent >= kHeartbeatInterval) {
			if (!sslWriteAll(st.ssl, fd, "PING\n", kIoTimeoutMs)) {
				disconnectLocked(st, "ssl-heartbeat-send-failed");
				continue;
			}
			st.lastPingSent = now;
		}

		if (!processIncomingLinesLocked(st)) {
			disconnectLocked(st, "ssl-read-failed");
			continue;
		}

		if (now - st.lastPongSeen > kHeartbeatTimeout) {
			disconnectLocked(st, "heartbeat-timeout");
		}
	}
}

void TcpHandshake::closeConnectionTransportLocked(ConnectionState& st) {
	if (st.ssl != nullptr) {
		SSL_shutdown(st.ssl);
		SSL_free(st.ssl);
		st.ssl = nullptr;
	}
	if (st.socket != static_cast<std::intptr_t>(kInvalidSocket)) {
		closeSocket(static_cast<SocketHandle>(st.socket));
		st.socket = static_cast<std::intptr_t>(kInvalidSocket);
	}
}

void TcpHandshake::disconnectLocked(ConnectionState& st, const std::string& reason) {
	closeConnectionTransportLocked(st);
	st.connected = false;
	st.readBuffer.clear();
	st.tlsPeerDeviceId.clear();
	st.nextRetry = std::chrono::steady_clock::now() + kRetryDelay;
	pushEventLocked("TLS disconnected: id=" + st.remote.deviceId + " reason=" + reason);
}

bool TcpHandshake::ensureTlsInitializedLocked() {
	if (serverTlsContext_ != nullptr && clientTlsContext_ != nullptr) {
		return true;
	}

	if (!OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr)) {
		pushEventLocked("TLS init failed");
		return false;
	}

	const std::filesystem::path root = std::filesystem::current_path() / ".syncflow_tls";
	const std::filesystem::path certDir = root / "certs";
	const std::filesystem::path keyDir = root / "keys";
	const std::filesystem::path trustDir = root / "trusted";
	std::error_code ec;
	std::filesystem::create_directories(certDir, ec);
	std::filesystem::create_directories(keyDir, ec);
	std::filesystem::create_directories(trustDir, ec);
	if (ec) {
		pushEventLocked("TLS directory creation failed: " + ec.message());
		return false;
	}

	tlsRootDir_ = root.string();
	tlsTrustDir_ = trustDir.string();
	tlsCertPath_ = (certDir / (localDeviceId_ + ".crt")).string();
	tlsKeyPath_ = (keyDir / (localDeviceId_ + ".key")).string();

	if (!ensureDeviceCertificate(tlsKeyPath_, tlsCertPath_, localDeviceId_, localDeviceName_)) {
		pushEventLocked("TLS local certificate generation failed: " + opensslErrorString());
		return false;
	}

	serverTlsContext_ = SSL_CTX_new(TLS_server_method());
	clientTlsContext_ = SSL_CTX_new(TLS_client_method());
	if (serverTlsContext_ == nullptr || clientTlsContext_ == nullptr) {
		pushEventLocked("TLS context creation failed: " + opensslErrorString());
		return false;
	}

	SSL_CTX_set_min_proto_version(serverTlsContext_, TLS1_2_VERSION);
	SSL_CTX_set_min_proto_version(clientTlsContext_, TLS1_2_VERSION);
	SSL_CTX_set_verify(serverTlsContext_, SSL_VERIFY_PEER, acceptAllCertificates);
	SSL_CTX_set_verify(clientTlsContext_, SSL_VERIFY_PEER, acceptAllCertificates);

	if (SSL_CTX_use_certificate_file(serverTlsContext_, tlsCertPath_.c_str(), SSL_FILETYPE_PEM) != 1 ||
	    SSL_CTX_use_PrivateKey_file(serverTlsContext_, tlsKeyPath_.c_str(), SSL_FILETYPE_PEM) != 1 ||
	    SSL_CTX_check_private_key(serverTlsContext_) != 1 ||
	    SSL_CTX_use_certificate_file(clientTlsContext_, tlsCertPath_.c_str(), SSL_FILETYPE_PEM) != 1 ||
	    SSL_CTX_use_PrivateKey_file(clientTlsContext_, tlsKeyPath_.c_str(), SSL_FILETYPE_PEM) != 1 ||
	    SSL_CTX_check_private_key(clientTlsContext_) != 1) {
		pushEventLocked("TLS certificate/key load failed: " + opensslErrorString());
		return false;
	}

	for (const auto& entry : std::filesystem::directory_iterator(trustDir, ec)) {
		if (ec || !entry.is_regular_file()) {
			continue;
		}
		if (entry.path().extension() != ".crt") {
			continue;
		}
		const auto deviceId = entry.path().stem().string();
		if (!isValidDeviceId(deviceId)) {
			continue;
		}
		if (addTrustedCertificate(serverTlsContext_, entry.path()) &&
		    addTrustedCertificate(clientTlsContext_, entry.path())) {
			loadedTrustedDeviceIds_.insert(deviceId);
		}
	}

	pushEventLocked("TLS security layer initialized. cert=" + tlsCertPath_ + " trust_dir=" + tlsTrustDir_);
	return true;
}

bool TcpHandshake::loadTrustedDeviceLocked(const std::string& deviceId) {
	if (loadedTrustedDeviceIds_.find(deviceId) != loadedTrustedDeviceIds_.end()) {
		return true;
	}

	if (serverTlsContext_ == nullptr || clientTlsContext_ == nullptr) {
		return false;
	}

	const std::filesystem::path certPath = std::filesystem::path(tlsTrustDir_) / (deviceId + ".crt");
	if (!std::filesystem::exists(certPath)) {
		return false;
	}

	if (!addTrustedCertificate(serverTlsContext_, certPath) || !addTrustedCertificate(clientTlsContext_, certPath)) {
		pushEventLocked("TLS failed loading trusted certificate for device: " + deviceId +
		               " err=" + opensslErrorString());
		return false;
	}

	loadedTrustedDeviceIds_.insert(deviceId);
	pushEventLocked("TLS trusted certificate loaded: id=" + deviceId + " cert=" + certPath.string());
	return true;
}

bool TcpHandshake::isPeerTrustedLocked(const std::string& deviceId) const {
	if (loadedTrustedDeviceIds_.find(deviceId) != loadedTrustedDeviceIds_.end()) {
		return true;
	}
	const std::filesystem::path certPath = std::filesystem::path(tlsTrustDir_) / (deviceId + ".crt");
	return std::filesystem::exists(certPath);
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

	if (!waitSocketReady(fd, false, kHandshakeTimeoutMs) || !connectCompleted(fd)) {
		closeSocket(fd);
		return false;
	}

	SSL* ssl = SSL_new(clientTlsContext_);
	if (ssl == nullptr) {
		closeSocket(fd);
		return false;
	}
	SSL_set_fd(ssl, static_cast<int>(fd));

	if (!performTlsHandshake(ssl, fd, false, kHandshakeTimeoutMs)) {
		pushEventLocked("TLS outbound handshake failed: id=" + st.remote.deviceId + " err=" + opensslErrorString());
		SSL_free(ssl);
		closeSocket(fd);
		return false;
	}

	if (SSL_get_verify_result(ssl) != X509_V_OK) {
		pushEventLocked("TLS outbound certificate validation failed: id=" + st.remote.deviceId);
		SSL_free(ssl);
		closeSocket(fd);
		return false;
	}

	auto tlsPeerDeviceId = extractPeerCommonName(ssl);
	if (!tlsPeerDeviceId.has_value() || *tlsPeerDeviceId != st.remote.deviceId) {
		pushEventLocked("TLS outbound certificate CN mismatch: expected=" + st.remote.deviceId);
		SSL_free(ssl);
		closeSocket(fd);
		return false;
	}

	if (!sslWriteAll(ssl, fd, buildHello(localDeviceId_, localDeviceName_), kHandshakeTimeoutMs)) {
		pushEventLocked("TLS outbound failed sending HELLO: id=" + st.remote.deviceId);
		SSL_free(ssl);
		closeSocket(fd);
		return false;
	}

	auto line = sslReadLineWithTimeout(ssl, fd, kHandshakeTimeoutMs);
	auto remote = line ? parseHello(*line, st.remote.ip, st.remote.port) : std::nullopt;
	if (!remote.has_value() || remote->deviceId != st.remote.deviceId || remote->deviceId != *tlsPeerDeviceId) {
		pushEventLocked("TLS outbound invalid HELLO response: id=" + st.remote.deviceId);
		SSL_free(ssl);
		closeSocket(fd);
		return false;
	}

	st.connected = true;
	st.outbound = true;
	st.socket = static_cast<std::intptr_t>(fd);
	st.ssl = ssl;
	st.tlsPeerDeviceId = *tlsPeerDeviceId;
	st.readBuffer.clear();
	st.lastPingSent = std::chrono::steady_clock::now();
	st.lastPongSeen = std::chrono::steady_clock::now();

	pushEventLocked("TLS connected (outgoing): id=" + remote->deviceId + " ip=" + remote->ip +
	               " port=" + std::to_string(remote->port));
	return true;
}

bool TcpHandshake::processIncomingLinesLocked(ConnectionState& st) {
	if (st.ssl == nullptr || st.socket == static_cast<std::intptr_t>(kInvalidSocket)) {
		return false;
	}

	SocketHandle fd = static_cast<SocketHandle>(st.socket);
	char buf[256];

	for (;;) {
		const int rc = SSL_read(st.ssl, buf, static_cast<int>(sizeof(buf)));
		if (rc == 0) {
			return false;
		}
		if (rc < 0) {
			const int err = SSL_get_error(st.ssl, rc);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
				break;
			}
			return false;
		}

		st.readBuffer.append(buf, static_cast<std::size_t>(rc));
		if (st.readBuffer.size() > 131072) {
			pushEventLocked("TLS invalid oversized message ignored: id=" + st.remote.deviceId);
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
				if (line.rfind("MSG|", 0) == 0) {
					std::string payload = line.substr(4);
					if (!isValidAppPayload(payload)) {
						pushEventLocked("TLS invalid app payload ignored: id=" + st.remote.deviceId);
						return false;
					}
					messages_.push_back(InboundMessage{st.remote.deviceId, std::move(payload)});
					if (messages_.size() > 300) {
						messages_.pop_front();
					}
					continue;
				}

				pushEventLocked("TLS invalid message ignored: id=" + st.remote.deviceId);
				return false;
			}

			if (line == "PING") {
				if (!sslWriteAll(st.ssl, fd, "PONG\n", kIoTimeoutMs)) {
					return false;
				}
			} else {
				st.lastPongSeen = std::chrono::steady_clock::now();
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

bool TcpHandshake::isValidAppPayload(const std::string& payload) {
	if (payload.empty() || payload.size() > 12000) {
		return false;
	}
	return payload.find('\n') == std::string::npos && payload.find('\r') == std::string::npos;
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
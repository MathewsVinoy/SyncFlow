#if 0
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
	#endif

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

	std::string socketIp(const sockaddr_in& addr) {
		char ipBuffer[INET_ADDRSTRLEN] = {0};
		const char* result = inet_ntop(AF_INET, &addr.sin_addr, ipBuffer, INET_ADDRSTRLEN);
		return result != nullptr ? std::string(ipBuffer) : std::string();
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
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (running_) {
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
		if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) < 0) {
			closeSocket(socketFd);
			shutdownSockets();
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(listenPort_);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if (bind(socketFd, reinterpret_cast<const sockaddr*>(&addr), static_cast<SocketLen>(sizeof(addr))) < 0) {
			closeSocket(socketFd);
			shutdownSockets();
			return false;
		}

		if (listen(socketFd, 16) < 0 || !setNonBlocking(socketFd, true)) {
			closeSocket(socketFd);
			shutdownSockets();
			return false;
		}

		listenSocket_ = static_cast<std::intptr_t>(socketFd);
		running_ = true;
		pushEventLocked("TCP started on port " + std::to_string(listenPort_));
		return true;
	}

	void TcpHandshake::stop() {
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (!running_) {
			return;
		}

		for (auto& [_, state] : states_) {
			if (state.connected && state.socket != kInvalidSocket) {
				closeSocket(static_cast<SocketHandle>(state.socket));
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
		if (!isValidDeviceId(peer.deviceId) || !isValidDeviceName(peer.deviceName) || peer.port == 0) {
			return;
		}
		if (peer.deviceId == localDeviceId_) {
			return;
		}

		std::lock_guard<std::mutex> lock(stateMutex_);
		auto& state = states_[peer.deviceId];
		const bool firstSeen = state.remote.deviceId.empty();
		state.remote = peer;
		if (firstSeen) {
			state.nextRetry = std::chrono::steady_clock::now();
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
		if (it->second.connected && it->second.socket != kInvalidSocket) {
			closeSocket(static_cast<SocketHandle>(it->second.socket));
		}
		pushEventLocked("TCP peer removed: id=" + deviceId);
		states_.erase(it);
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
		std::string out = events_.front();
		events_.pop_front();
		return out;
	}

	std::vector<TcpHandshake::RemoteDevice> TcpHandshake::getConnectedPeers() const {
		std::lock_guard<std::mutex> lock(stateMutex_);
		std::vector<RemoteDevice> peers;
		for (const auto& [_, state] : states_) {
			if (state.connected) {
				peers.push_back(state.remote);
			}
		}
		return peers;
	}

	void TcpHandshake::pushEventLocked(const std::string& event) {
		events_.push_back(event);
		if (events_.size() > 200) {
			events_.pop_front();
		}
	}

	void TcpHandshake::acceptIncoming() {
		const SocketHandle listenFd = static_cast<SocketHandle>(listenSocket_);
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

			char buf[512] = {0};
			const int received = recv(client, buf, sizeof(buf) - 1, 0);
			if (received <= 0) {
				closeSocket(client);
				continue;
			}
			buf[received] = '\0';
			std::string hello(buf);
			auto newline = hello.find('\n');
			if (newline != std::string::npos) {
				hello.resize(newline);
			}

			auto remote = parseHello(hello, socketIp(remoteAddr), ntohs(remoteAddr.sin_port));
			if (!remote.has_value() || remote->deviceId == localDeviceId_) {
				closeSocket(client);
				pushEventLocked("TCP invalid incoming HELLO ignored");
				continue;
			}

			const std::string response = buildHello(localDeviceId_, localDeviceName_);
			if (send(client, response.c_str(), static_cast<int>(response.size()), 0) < 0) {
				closeSocket(client);
				continue;
			}

			auto& state = states_[remote->deviceId];
			if (state.connected) {
				closeSocket(client);
				pushEventLocked("TCP duplicate incoming ignored: id=" + remote->deviceId);
				continue;
			}

			state.remote = *remote;
			state.connected = true;
			state.outbound = false;
			state.socket = static_cast<std::intptr_t>(client);
			state.readBuffer.clear();
			state.lastPingSent = std::chrono::steady_clock::now();
			state.lastPongSeen = std::chrono::steady_clock::now();
			pushEventLocked("TCP connected (incoming): id=" + state.remote.deviceId + " name=" + state.remote.deviceName +
			               " ip=" + state.remote.ip + " port=" + std::to_string(state.remote.port));
		}
	}

	void TcpHandshake::processConnections() {
		const auto now = std::chrono::steady_clock::now();

		for (auto& [_, state] : states_) {
			if (!state.connected) {
				if (!shouldInitiate(localDeviceId_, state.remote.deviceId)) {
					continue;
				}
				if (now < state.nextRetry) {
					continue;
				}
				if (!connectAndHandshakeLocked(state)) {
					state.nextRetry = now + kRetryDelay;
					pushEventLocked("TCP retry scheduled: id=" + state.remote.deviceId + " in " +
					               std::to_string(kRetryDelay.count()) + "s");
				}
				continue;
			}

			if (now - state.lastPingSent >= kHeartbeatInterval) {
				const std::string ping = "PING\n";
				SocketHandle fd = static_cast<SocketHandle>(state.socket);
				if (send(fd, ping.c_str(), static_cast<int>(ping.size()), 0) < 0) {
					disconnectLocked(state, "send-failed");
					continue;
				}
				state.lastPingSent = now;
			}

			if (!processIncomingLinesLocked(state)) {
				disconnectLocked(state, "read-failed");
				continue;
			}

			if (now - state.lastPongSeen > kHeartbeatTimeout) {
				disconnectLocked(state, "heartbeat-timeout");
			}
		}
	}

	void TcpHandshake::disconnectLocked(ConnectionState& state, const std::string& reason) {
		if (state.connected && state.socket != kInvalidSocket) {
			closeSocket(static_cast<SocketHandle>(state.socket));
		}
		state.connected = false;
		state.socket = static_cast<std::intptr_t>(kInvalidSocket);
		state.readBuffer.clear();
		state.nextRetry = std::chrono::steady_clock::now() + kRetryDelay;
		pushEventLocked("TCP disconnected: id=" + state.remote.deviceId + " reason=" + reason);
	}

	bool TcpHandshake::connectAndHandshakeLocked(ConnectionState& state) {
		const SocketHandle fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd == kInvalidSocket) {
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(state.remote.port);
		if (inet_pton(AF_INET, state.remote.ip.c_str(), &addr.sin_addr) != 1) {
			closeSocket(fd);
			return false;
		}

		if (connect(fd, reinterpret_cast<const sockaddr*>(&addr), static_cast<SocketLen>(sizeof(addr))) < 0) {
			closeSocket(fd);
			return false;
		}

		const std::string hello = buildHello(localDeviceId_, localDeviceName_);
		if (send(fd, hello.c_str(), static_cast<int>(hello.size()), 0) < 0) {
			closeSocket(fd);
			return false;
		}

		char buf[512] = {0};
		const int received = recv(fd, buf, sizeof(buf) - 1, 0);
		if (received <= 0) {
			closeSocket(fd);
			return false;
		}
		buf[received] = '\0';
		std::string response(buf);
		auto newline = response.find('\n');
		if (newline != std::string::npos) {
			response.resize(newline);
		}

		auto remote = parseHello(response, state.remote.ip, state.remote.port);
		if (!remote.has_value() || remote->deviceId != state.remote.deviceId) {
			closeSocket(fd);
			pushEventLocked("TCP invalid HELLO response ignored for id=" + state.remote.deviceId);
			return false;
		}

		if (!setNonBlocking(fd, true)) {
			closeSocket(fd);
			return false;
		}

		state.connected = true;
		state.outbound = true;
		state.socket = static_cast<std::intptr_t>(fd);
		state.lastPingSent = std::chrono::steady_clock::now();
		state.lastPongSeen = std::chrono::steady_clock::now();
		state.readBuffer.clear();
		pushEventLocked("TCP connected (outgoing): id=" + state.remote.deviceId + " name=" + state.remote.deviceName +
		               " ip=" + state.remote.ip + " port=" + std::to_string(state.remote.port));
		return true;
	}

	bool TcpHandshake::processIncomingLinesLocked(ConnectionState& state) {
		SocketHandle fd = static_cast<SocketHandle>(state.socket);
		char chunk[256];

		for (;;) {
			const int received = recv(fd, chunk, sizeof(chunk), 0);
			if (received == 0) {
				return false;
			}
			if (received < 0) {
				if (wouldBlock()) {
					break;
				}
				return false;
			}

			state.readBuffer.append(chunk, static_cast<std::size_t>(received));
			if (state.readBuffer.size() > 4096) {
				pushEventLocked("TCP invalid oversized message ignored: id=" + state.remote.deviceId);
				return false;
			}

			for (;;) {
				auto pos = state.readBuffer.find('\n');
				if (pos == std::string::npos) {
					break;
				}
				std::string line = state.readBuffer.substr(0, pos);
				state.readBuffer.erase(0, pos + 1);

				if (!isValidControlLine(line)) {
					pushEventLocked("TCP invalid message ignored: id=" + state.remote.deviceId);
					return false;
				}

				if (line == "PING") {
					const std::string pong = "PONG\n";
					if (send(fd, pong.c_str(), static_cast<int>(pong.size()), 0) < 0) {
						return false;
					}
				} else if (line == "PONG") {
					state.lastPongSeen = std::chrono::steady_clock::now();
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
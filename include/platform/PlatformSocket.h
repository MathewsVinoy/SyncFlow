#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
using SocketLen = int;
constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <sys/types.h>
using SocketHandle = int;
using SocketLen = socklen_t;
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif

namespace platform {

/**
 * PlatformSocket: Cross-platform socket abstraction layer
 * Provides unified interface for socket operations on Windows (Winsock) and POSIX systems.
 */
class PlatformSocket {
public:
	struct UdpPacket {
		std::string address;
		std::uint16_t port = 0;
		std::vector<std::uint8_t> data;
	};

	PlatformSocket();
	~PlatformSocket();

	// Non-copyable, movable
	PlatformSocket(const PlatformSocket&) = delete;
	PlatformSocket& operator=(const PlatformSocket&) = delete;
	PlatformSocket(PlatformSocket&& other) noexcept;
	PlatformSocket& operator=(PlatformSocket&& other) noexcept;

	// Static initialization/cleanup
	static bool initializeSocketSystem();
	static void shutdownSocketSystem();

	// Socket creation
	static std::optional<PlatformSocket> createTCP();
	static std::optional<PlatformSocket> createUDP();

	// Socket operations
	bool bind(const std::string& address, std::uint16_t port);
	bool listen(int backlog = 128);
	std::optional<PlatformSocket> accept();

	bool connect(const std::string& address, std::uint16_t port);

	// Data transfer
	std::optional<std::vector<std::uint8_t>> receive(std::size_t maxBytes, int timeoutMs = -1);
	bool send(const std::vector<std::uint8_t>& data);
	bool send(const std::string& data);
	bool sendTo(const std::string& address, std::uint16_t port, const std::vector<std::uint8_t>& data);
	bool sendTo(const std::string& address, std::uint16_t port, const std::string& data);
	std::optional<UdpPacket> receiveFrom(std::size_t maxBytes, int timeoutMs = -1);

	// Configuration
	bool setNonBlocking(bool nonBlocking);
	bool setReuseAddress(bool reuse);
	bool setBroadcast(bool broadcast);
	bool setKeepAlive(bool keepAlive);
	bool setTimeout(int timeoutMs);

	// Socket info
	std::optional<std::string> getLocalAddress() const;
	std::optional<std::uint16_t> getLocalPort() const;
	std::optional<std::string> getPeerAddress() const;
	std::optional<std::uint16_t> getPeerPort() const;

	// Utility
	static std::optional<std::string> getHostname();
	static std::optional<std::vector<std::string>> getLocalAddresses();

	SocketHandle handle() const { return socket_; }
	bool isValid() const { return socket_ != INVALID_SOCKET_HANDLE; }
	void close();

private:
	explicit PlatformSocket(SocketHandle sock);

	SocketHandle socket_ = INVALID_SOCKET_HANDLE;

	static int refCount_;
};

}  // namespace platform

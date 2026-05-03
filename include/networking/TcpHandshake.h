#pragma once

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ssl_ctx_st;
struct ssl_st;

class TcpHandshake {
public:
	enum class ConnectionProtocol {
		TCP_TLS,  // TCP with TLS encryption
		UDP,      // UDP (for faster, best-effort transfers)
		UNKNOWN
	};

	struct RemoteDevice {
		std::string deviceId;
		std::string deviceName;
		std::string ip;
		std::uint16_t port;
		ConnectionProtocol protocol = ConnectionProtocol::TCP_TLS;  // Primary protocol
	};

	struct InboundMessage {
		std::string deviceId;
		std::string payload;
		ConnectionProtocol protocol = ConnectionProtocol::TCP_TLS;
	};

	TcpHandshake(std::string localDeviceId, std::string localDeviceName, std::uint16_t listenPort);
	~TcpHandshake();

	bool start();
	void stop();
	void tick();

	void observePeer(const RemoteDevice& peer);
	void removePeer(const std::string& deviceId);
	bool sendMessage(const std::string& deviceId, const std::string& payload);
	bool sendMessageUdp(const std::string& deviceId, const std::string& payload);  // New UDP method

	std::optional<std::string> pollEvent();
	std::optional<InboundMessage> pollMessage();
	std::vector<RemoteDevice> getConnectedPeers() const;
	ConnectionProtocol getConnectionProtocol(const std::string& deviceId) const;  // Query protocol for device

private:
	struct ConnectionState {
		RemoteDevice remote;
		std::intptr_t socket = -1;
		std::intptr_t udpSocket = -1;  // Separate UDP socket
		ssl_st* ssl = nullptr;
		bool connected = false;
		bool udpConnected = false;  // UDP fallback status
		bool outbound = false;
		std::string tlsPeerDeviceId;
		std::string readBuffer;
		ConnectionProtocol activeProtocol = ConnectionProtocol::TCP_TLS;
		std::chrono::steady_clock::time_point lastPingSent{};
		std::chrono::steady_clock::time_point lastPongSeen{};
		std::chrono::steady_clock::time_point nextRetry{};
	};

	std::string localDeviceId_;
	std::string localDeviceName_;
	std::uint16_t listenPort_;
	std::uint16_t udpPort_;  // Separate UDP port
	bool running_;
	std::intptr_t listenSocket_;
	std::intptr_t udpListenSocket_ = -1;  // UDP listener
	ssl_ctx_st* serverTlsContext_;
	ssl_ctx_st* clientTlsContext_;
	std::string tlsRootDir_;
	std::string tlsKeyPath_;
	std::string tlsCertPath_;
	std::string tlsTrustDir_;
	std::unordered_set<std::string> loadedTrustedDeviceIds_;

	mutable std::mutex stateMutex_;
	std::unordered_map<std::string, ConnectionState> states_;
	std::deque<std::string> events_;
	std::deque<InboundMessage> messages_;

	void pushEventLocked(const std::string& event);
	void acceptIncoming();
	void acceptIncomingUdp();  // UDP acceptance
	void processConnections();
	void closeConnectionTransportLocked(ConnectionState& state);
	void disconnectLocked(ConnectionState& state, const std::string& reason);
	bool ensureTlsInitializedLocked();
	bool loadTrustedDeviceLocked(const std::string& deviceId);
	bool isPeerTrustedLocked(const std::string& deviceId) const;
	bool connectAndHandshakeLocked(ConnectionState& state);
	bool processIncomingLinesLocked(ConnectionState& state);
	bool tryUdpFallbackLocked(ConnectionState& state);  // UDP fallback attempt

	static bool isValidDeviceId(const std::string& value);
	static bool isValidDeviceName(const std::string& value);
	static std::string buildHello(const std::string& id, const std::string& name);
	static bool isValidControlLine(const std::string& text);
	static bool isValidAppPayload(const std::string& payload);
	static bool shouldInitiate(const std::string& localId, const std::string& remoteId);
	static std::optional<RemoteDevice> parseHello(const std::string& text, const std::string& ip, std::uint16_t port);
};
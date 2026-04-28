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
	struct RemoteDevice {
		std::string deviceId;
		std::string deviceName;
		std::string ip;
		std::uint16_t port;
	};

	struct InboundMessage {
		std::string deviceId;
		std::string payload;
	};

	TcpHandshake(std::string localDeviceId, std::string localDeviceName, std::uint16_t listenPort);
	~TcpHandshake();

	bool start();
	void stop();
	void tick();

	void observePeer(const RemoteDevice& peer);
	void removePeer(const std::string& deviceId);
	bool sendMessage(const std::string& deviceId, const std::string& payload);

	std::optional<std::string> pollEvent();
	std::optional<InboundMessage> pollMessage();
	std::vector<RemoteDevice> getConnectedPeers() const;

private:
	struct ConnectionState {
		RemoteDevice remote;
		std::intptr_t socket = -1;
		ssl_st* ssl = nullptr;
		bool connected = false;
		bool outbound = false;
		std::string tlsPeerDeviceId;
		std::string readBuffer;
		std::chrono::steady_clock::time_point lastPingSent{};
		std::chrono::steady_clock::time_point lastPongSeen{};
		std::chrono::steady_clock::time_point nextRetry{};
	};

	std::string localDeviceId_;
	std::string localDeviceName_;
	std::uint16_t listenPort_;
	bool running_;
	std::intptr_t listenSocket_;
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
	void processConnections();
	void closeConnectionTransportLocked(ConnectionState& state);
	void disconnectLocked(ConnectionState& state, const std::string& reason);
	bool ensureTlsInitializedLocked();
	bool loadTrustedDeviceLocked(const std::string& deviceId);
	bool isPeerTrustedLocked(const std::string& deviceId) const;
	bool connectAndHandshakeLocked(ConnectionState& state);
	bool processIncomingLinesLocked(ConnectionState& state);

	static bool isValidDeviceId(const std::string& value);
	static bool isValidDeviceName(const std::string& value);
	static std::string buildHello(const std::string& id, const std::string& name);
	static bool isValidControlLine(const std::string& text);
	static bool isValidAppPayload(const std::string& payload);
	static bool shouldInitiate(const std::string& localId, const std::string& remoteId);
	static std::optional<RemoteDevice> parseHello(const std::string& text, const std::string& ip, std::uint16_t port);
};
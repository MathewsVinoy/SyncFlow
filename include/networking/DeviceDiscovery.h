#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class DeviceDiscovery {
public:
	struct PeerInfo {
		std::string deviceId;
		std::string deviceName;
		std::string ip;
		std::uint16_t port;
		std::chrono::steady_clock::time_point lastSeen;
	};

	DeviceDiscovery(std::string deviceName, std::uint16_t servicePort, std::uint16_t discoveryPort = 45454);

	// Broadcast discovery probe payload: "SYNCFLOW_DISCOVER|device_id|discovery_port"
	bool sender() const;
	// Receive probe/response and process payload: "SYNCFLOW|device_id|device_name|service_port"
	std::optional<PeerInfo> receiver(int timeoutMs = 3000) const;

	std::vector<PeerInfo> getActiveDevices() const;
	void removeInactiveDevices(std::chrono::seconds maxAge = std::chrono::seconds(15)) const;
	const std::string& deviceId() const;

private:
	std::string deviceId_;
	std::string deviceName_;
	std::uint16_t servicePort_;
	std::uint16_t discoveryPort_;

	mutable std::mutex peersMutex_;
	mutable std::unordered_map<std::string, PeerInfo> peersById_;

	std::optional<PeerInfo> upsertPeer(const PeerInfo& peer) const;
	std::optional<PeerInfo> parseResponseMessage(const std::string& payload, const std::string& senderIp) const;
	bool isValidDeviceId(const std::string& value) const;
	bool isValidDeviceName(const std::string& value) const;
};

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

	// Broadcast discovery probe payload: "255.255.255.255:<discovery_port>"
	bool sender() const;
	// Process probe/response packets. Responds to probe and tracks valid responses.
	std::optional<PeerInfo> receiver(int timeoutMs = 3000);

	std::vector<PeerInfo> getActiveDevices(int inactiveTimeoutMs = 15000);
	std::string getDeviceId() const;

private:
	struct TrackedDevice {
		PeerInfo peer;
	};

	std::string deviceName_;
	std::string deviceId_;
	std::uint16_t servicePort_;
	std::uint16_t discoveryPort_;

	mutable std::mutex devicesMutex_;
	std::unordered_map<std::string, TrackedDevice> devices_;

	static std::optional<PeerInfo> parseMessage(const std::string& payload, const std::string& senderIp);
	static bool isValidDeviceId(const std::string& deviceId);
	static bool isValidDeviceName(const std::string& deviceName);
	static std::string createDeviceId();

	bool upsertDevice(const PeerInfo& peer, std::chrono::milliseconds inactiveTimeout);
	void removeInactiveLocked(std::chrono::steady_clock::time_point now,
	                        std::chrono::milliseconds inactiveTimeout);
};

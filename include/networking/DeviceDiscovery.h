#pragma once

#include <cstdint>
#include <optional>
#include <string>

class DeviceDiscovery {
public:
	struct PeerInfo {
		std::string deviceName;
		std::string ip;
		std::uint16_t port;
	};

	DeviceDiscovery(std::string deviceName, std::uint16_t servicePort, std::uint16_t discoveryPort = 45454);

	// Broadcast discovery probe payload: "255.255.255.255:<discovery_port>"
	bool sender() const;
	// Receive probe and reply with payload: "SYNCFLOW|device_name|service_port"
	std::optional<PeerInfo> receiver(int timeoutMs = 3000) const;

private:
	std::string deviceName_;
	std::uint16_t servicePort_;
	std::uint16_t discoveryPort_;

	static std::optional<PeerInfo> parseMessage(const std::string& payload, const std::string& senderIp);
};

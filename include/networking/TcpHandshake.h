#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>

class TcpHandshake {
public:
	struct RemoteDevice {
		std::string deviceId;
		std::string deviceName;
		std::string ip;
		std::uint16_t port;
	};

	TcpHandshake(std::string localDeviceId, std::string localDeviceName, std::uint16_t listenPort);
	~TcpHandshake();

	bool start();
	void stop();

	std::optional<RemoteDevice> pollAccepted(int timeoutMs = 200);
	std::optional<RemoteDevice> connectAndHandshake(const std::string& ip, std::uint16_t port, int timeoutMs = 2000);

private:
	std::string localDeviceId_;
	std::string localDeviceName_;
	std::uint16_t listenPort_;
	std::atomic<bool> running_{false};
	std::intptr_t listenSocket_;

	static bool isValidDeviceId(const std::string& value);
	static bool isValidDeviceName(const std::string& value);
	static std::string buildHello(const std::string& id, const std::string& name);
	static std::optional<RemoteDevice> parseHello(const std::string& text, const std::string& ip, std::uint16_t port);
};
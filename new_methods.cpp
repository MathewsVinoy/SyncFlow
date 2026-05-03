TcpHandshake::ConnectionProtocol TcpHandshake::getConnectionProtocol(const std::string& deviceId) const {
	std::lock_guard<std::mutex> lock(stateMutex_);
	auto it = states_.find(deviceId);
	if (it == states_.end() || !it->second.connected) {
		return ConnectionProtocol::UNKNOWN;
	}
	return it->second.activeProtocol;
}

bool TcpHandshake::sendMessageUdp(const std::string& deviceId, const std::string& payload) {
	if (!isValidAppPayload(payload)) {
		return false;
	}

	std::lock_guard<std::mutex> lock(stateMutex_);
	auto it = states_.find(deviceId);
	if (it == states_.end() || !it->second.udpConnected ||
	    it->second.udpSocket == static_cast<std::intptr_t>(kInvalidSocket)) {
		return false;
	}

	SocketHandle udpFd = static_cast<SocketHandle>(it->second.udpSocket);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(it->second.remote.port + 1);
	if (inet_pton(AF_INET, it->second.remote.ip.c_str(), &addr.sin_addr) != 1) {
		return false;
	}

	std::string msg = "MSG|" + payload + "\n";
	int rc = sendto(udpFd, msg.data(), static_cast<int>(msg.size()), 0,
	               reinterpret_cast<const sockaddr*>(&addr), static_cast<SocketLen>(sizeof(addr)));
	if (rc < 0) {
		pushEventLocked("UDP write failed: id=" + deviceId + " err=" + socketErrorString());
		return false;
	}

	return true;
}

void TcpHandshake::acceptIncomingUdp() {
	SocketHandle udpFd = static_cast<SocketHandle>(udpListenSocket_);
	if (udpFd == kInvalidSocket) {
		return;
	}

	sockaddr_in remoteAddr{};
	SocketLen remoteLen = static_cast<SocketLen>(sizeof(remoteAddr));
	char buf[8192];

	for (int i = 0; i < 4; ++i) {
		const int rc = recvfrom(udpFd, buf, sizeof(buf), 0,
		                        reinterpret_cast<sockaddr*>(&remoteAddr), &remoteLen);
		if (rc <= 0) {
			if (wouldBlock()) {
				break;
			}
			return;
		}

		std::string payload(buf, static_cast<std::size_t>(rc));
		if (payload.find('\n') == std::string::npos) {
			payload += '\n';
		}

		// For simple UDP hello/handshake, extract the device ID
		if (payload.rfind("HELLO|", 0) == 0) {
			auto remote = parseHello(payload.substr(0, payload.find('\n')), 
									 socketIp(remoteAddr), ntohs(remoteAddr.sin_port));
			if (!remote.has_value() || remote->deviceId == localDeviceId_) {
				continue;
			}

			auto& st = states_[remote->deviceId];
			st.remote = *remote;
			st.udpConnected = true;
			st.udpSocket = static_cast<std::intptr_t>(udpFd);
			st.activeProtocol = ConnectionProtocol::UDP;

			pushEventLocked("UDP connected (incoming): id=" + remote->deviceId + " ip=" + remote->ip +
						   " port=" + std::to_string(remote->port));
			Logger::info("UDP fallback connection established: name=" + remote->deviceName + " id=" + 
					   remote->deviceId + " ip=" + remote->ip + " port=" + std::to_string(remote->port) + 
					   " protocol=UDP direction=inbound");
		}
	}
}

bool TcpHandshake::tryUdpFallbackLocked(ConnectionState& state) {
	if (state.udpSocket != static_cast<std::intptr_t>(kInvalidSocket)) {
		return true;  // Already has UDP socket
	}

	SocketHandle udpFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udpFd == kInvalidSocket) {
		return false;
	}

	if (!setNonBlocking(udpFd, true)) {
		closeSocket(udpFd);
		return false;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(state.remote.port + 1);
	if (inet_pton(AF_INET, state.remote.ip.c_str(), &addr.sin_addr) != 1) {
		closeSocket(udpFd);
		return false;
	}

	// Send HELLO via UDP
	std::string hello = buildHello(localDeviceId_, localDeviceName_);
	int rc = sendto(udpFd, hello.data(), static_cast<int>(hello.size()), 0,
	               reinterpret_cast<const sockaddr*>(&addr), static_cast<SocketLen>(sizeof(addr)));
	if (rc < 0) {
		closeSocket(udpFd);
		return false;
	}

	state.udpSocket = static_cast<std::intptr_t>(udpFd);
	state.udpConnected = true;
	state.activeProtocol = ConnectionProtocol::UDP;

	pushEventLocked("UDP fallback attempted: id=" + state.remote.deviceId);
	Logger::info("UDP fallback connection established (outgoing): name=" + state.remote.deviceName + " id=" + 
	           state.remote.deviceId + " ip=" + state.remote.ip + " port=" + std::to_string(state.remote.port) + 
	           " protocol=UDP direction=outbound");
	return true;
}

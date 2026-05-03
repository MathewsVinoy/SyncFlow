#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint16_t kUdpPort = 45454;
constexpr uint16_t kTcpPort = 45455;
constexpr const char* kBroadcastAddress = "255.255.255.255";
constexpr const char* kMagic = "SYNCFLOW_PEER";

std::atomic<bool> g_running{true};

void on_signal(int) {
	g_running = false;
}

void close_socket(int fd) {
	if (fd >= 0) {
		::shutdown(fd, SHUT_RDWR);
		::close(fd);
	}
}

std::string now_string() {
	using clock = std::chrono::system_clock;
	const auto now = clock::now();
	const auto tt = clock::to_time_t(now);
	std::tm tm{};
	localtime_r(&tt, &tm);

	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
	return oss.str();
}

std::string get_hostname() {
	char buffer[256] = {};
	if (::gethostname(buffer, sizeof(buffer)) == 0 && buffer[0] != '\0') {
		return buffer;
	}
	return "unknown-device";
}

std::string get_local_ipv4() {
	struct ifaddrs* ifaddr = nullptr;
	if (::getifaddrs(&ifaddr) != 0) {
		return "0.0.0.0";
	}

	std::string result = "0.0.0.0";
	for (auto* current = ifaddr; current != nullptr; current = current->ifa_next) {
		if (!current->ifa_addr || current->ifa_addr->sa_family != AF_INET) {
			continue;
		}

		if ((current->ifa_flags & IFF_LOOPBACK) != 0) {
			continue;
		}

		char ip[INET_ADDRSTRLEN] = {};
		auto* addr = reinterpret_cast<sockaddr_in*>(current->ifa_addr);
		if (::inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)) != nullptr) {
			result = ip;
			break;
		}
	}

	::freeifaddrs(ifaddr);
	return result;
}

bool send_all(int fd, const std::string& data) {
	const char* ptr = data.data();
	size_t remaining = data.size();

	while (remaining > 0) {
		const ssize_t sent = ::send(fd, ptr, remaining, 0);
		if (sent <= 0) {
			return false;
		}
		ptr += sent;
		remaining -= static_cast<size_t>(sent);
	}

	return true;
}

std::string endpoint_key(const std::string& name, const std::string& ip, uint16_t port) {
	return name + "@" + ip + ":" + std::to_string(port);
}

struct PeerInfo {
	std::string magic;
	std::string name;
	std::string ip;
	uint16_t tcp_port = 0;
};

class PeerNode {
public:
	explicit PeerNode(std::string device_name)
		: device_name_(std::move(device_name)), local_ip_(get_local_ipv4()) {}

	void run() {
		log("starting peer node");
		log("device name: " + device_name_ + ", ip: " + local_ip_ + ", tcp port: " + std::to_string(kTcpPort) + ", udp port: " + std::to_string(kUdpPort));

		tcp_thread_ = std::thread([this] { tcp_server_loop(); });
		udp_thread_ = std::thread([this] { udp_listener_loop(); });
		broadcast_thread_ = std::thread([this] { broadcast_loop(); });

		std::cout << "Press Ctrl+C to stop.\n";

		while (g_running) {
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}

		stop();
	}

private:
	std::string device_name_;
	std::string local_ip_;
	std::atomic<int> tcp_server_fd_{-1};
	std::atomic<int> udp_listener_fd_{-1};
	std::thread tcp_thread_;
	std::thread udp_thread_;
	std::thread broadcast_thread_;
	std::mutex log_mutex_;
	std::mutex active_mutex_;
	std::set<std::string> active_connections_;

	void stop() {
		close_socket(tcp_server_fd_.exchange(-1));
		close_socket(udp_listener_fd_.exchange(-1));

		if (broadcast_thread_.joinable()) {
			broadcast_thread_.join();
		}
		if (udp_thread_.joinable()) {
			udp_thread_.join();
		}
		if (tcp_thread_.joinable()) {
			tcp_thread_.join();
		}

		log("shutdown complete");
	}

	void log(const std::string& message) {
		std::lock_guard<std::mutex> guard(log_mutex_);
		std::cout << "[" << now_string() << "] "
				  << "[device=" << device_name_ << "] "
				  << "[ip=" << local_ip_ << "] "
				  << message << std::endl;
	}

	std::string peer_line(const PeerInfo& peer) const {
		std::ostringstream oss;
		oss << kMagic << '|' << peer.name << '|' << peer.ip << '|' << peer.tcp_port << '\n';
		return oss.str();
	}

	bool parse_peer_line(const std::string& line, PeerInfo& peer) {
		std::vector<std::string> parts;
		std::string current;
		for (char c : line) {
			if (c == '|') {
				parts.push_back(current);
				current.clear();
			} else if (c != '\r' && c != '\n') {
				current.push_back(c);
			}
		}
		if (!current.empty()) {
			parts.push_back(current);
		}

		if (parts.size() != 4 || parts[0] != kMagic) {
			return false;
		}

		peer.magic = parts[0];
		peer.name = parts[1];
		peer.ip = parts[2];
		try {
			peer.tcp_port = static_cast<uint16_t>(std::stoi(parts[3]));
		} catch (...) {
			return false;
		}
		return true;
	}

	bool should_initiate(const PeerInfo& peer) const {
		if (peer.name == device_name_ && peer.ip == local_ip_) {
			return false;
		}

		if (device_name_ != peer.name) {
			return device_name_ < peer.name;
		}

		return local_ip_ < peer.ip;
	}

	void mark_active(const PeerInfo& peer) {
		std::lock_guard<std::mutex> guard(active_mutex_);
		active_connections_.insert(endpoint_key(peer.name, peer.ip, peer.tcp_port));
	}

	void mark_inactive(const PeerInfo& peer) {
		std::lock_guard<std::mutex> guard(active_mutex_);
		active_connections_.erase(endpoint_key(peer.name, peer.ip, peer.tcp_port));
	}

	bool is_active(const PeerInfo& peer) {
		std::lock_guard<std::mutex> guard(active_mutex_);
		return active_connections_.find(endpoint_key(peer.name, peer.ip, peer.tcp_port)) != active_connections_.end();
	}

	void handle_peer_connection(int fd, const PeerInfo& peer, const std::string& direction) {
		mark_active(peer);
		log(direction + " connection established with " + peer.name + " @ " + peer.ip + ":" + std::to_string(peer.tcp_port));

		const std::string hello = "HELLO from " + device_name_ + "@" + local_ip_ + "\n";
		(void)send_all(fd, hello);

		char buffer[1024] = {};
		while (g_running) {
			const ssize_t received = ::recv(fd, buffer, sizeof(buffer) - 1, 0);
			if (received <= 0) {
				break;
			}
			buffer[received] = '\0';
			std::string text = buffer;
			text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
			text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
			if (!text.empty()) {
				log("message from " + peer.name + " @ " + peer.ip + ": " + text);
			}
		}

		log("connection closed with " + peer.name + " @ " + peer.ip);
		mark_inactive(peer);
		close_socket(fd);
	}

	void connect_to_peer(const PeerInfo& peer) {
		if (!g_running || is_active(peer)) {
			return;
		}

		const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			log("failed to create TCP client socket");
			return;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(peer.tcp_port);
		if (::inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr) != 1) {
			log("invalid peer ip address: " + peer.ip);
			close_socket(fd);
			return;
		}

		if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
			close_socket(fd);
			return;
		}

		handle_peer_connection(fd, peer, "outbound");
	}

	void broadcast_loop() {
		const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0) {
			log("failed to create UDP broadcast socket");
			return;
		}

		int yes = 1;
		::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
		::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		sockaddr_in dest{};
		dest.sin_family = AF_INET;
		dest.sin_port = htons(kUdpPort);
		::inet_pton(AF_INET, kBroadcastAddress, &dest.sin_addr);

		PeerInfo self{ kMagic, device_name_, local_ip_, kTcpPort };
		const std::string payload = peer_line(self);

		while (g_running) {
			(void)::sendto(fd, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
			std::this_thread::sleep_for(std::chrono::seconds(2));
		}

		close_socket(fd);
	}

	void udp_listener_loop() {
		const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0) {
			log("failed to create UDP listener socket");
			return;
		}

		udp_listener_fd_ = fd;

		int yes = 1;
		::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(kUdpPort);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
			log("failed to bind UDP listener port " + std::to_string(kUdpPort));
			close_socket(fd);
			return;
		}

		char buffer[1024] = {};
		while (g_running) {
			sockaddr_in from{};
			socklen_t from_len = sizeof(from);
			const ssize_t received = ::recvfrom(fd, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&from), &from_len);
			if (received <= 0) {
				continue;
			}

			buffer[received] = '\0';
			PeerInfo peer;
			if (!parse_peer_line(buffer, peer)) {
				continue;
			}

			if (peer.name == device_name_ && peer.ip == local_ip_) {
				continue;
			}

			log("discovered peer " + peer.name + " @ " + peer.ip + " (tcp port " + std::to_string(peer.tcp_port) + ")");

			if (should_initiate(peer) && !is_active(peer)) {
				std::thread([this, peer] { connect_to_peer(peer); }).detach();
			}
		}

		close_socket(fd);
	}

	void tcp_server_loop() {
		const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			log("failed to create TCP server socket");
			return;
		}

		tcp_server_fd_ = fd;

		int yes = 1;
		::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(kTcpPort);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
			log("failed to bind TCP server port " + std::to_string(kTcpPort));
			close_socket(fd);
			return;
		}

		if (::listen(fd, 8) != 0) {
			log("failed to listen on TCP port " + std::to_string(kTcpPort));
			close_socket(fd);
			return;
		}

		log("TCP server listening on port " + std::to_string(kTcpPort));

		while (g_running) {
			sockaddr_in from{};
			socklen_t from_len = sizeof(from);
			const int client = ::accept(fd, reinterpret_cast<sockaddr*>(&from), &from_len);
			if (client < 0) {
				if (!g_running) {
					break;
				}
				continue;
			}

			char ip[INET_ADDRSTRLEN] = {};
			::inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
			const uint16_t remote_port = ntohs(from.sin_port);

			std::thread([this, client, remote_ip = std::string(ip), remote_port] {
				char buffer[1024] = {};
				const ssize_t received = ::recv(client, buffer, sizeof(buffer) - 1, 0);
				PeerInfo peer;
				peer.name = "unknown";
				peer.ip = remote_ip;
				peer.tcp_port = remote_port;

				if (received > 0) {
					buffer[received] = '\0';
					std::string line = buffer;
					line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
					line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

					PeerInfo parsed;
					if (this->parse_peer_line(line, parsed)) {
						peer = parsed;
					}
				}

				this->handle_peer_connection(client, peer, "inbound");
			}).detach();
		}

		close_socket(fd);
	}
};

}  // namespace

int main(int argc, char** argv) {
	::signal(SIGINT, on_signal);
	::signal(SIGTERM, on_signal);

	std::string device_name = get_hostname();
	if (argc > 1 && argv[1] != nullptr && std::strlen(argv[1]) > 0) {
		device_name = argv[1];
	}

	PeerNode node(device_name);
	node.run();
	return 0;
}

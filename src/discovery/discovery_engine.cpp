// src/discovery/discovery_engine.cpp

#include <syncflow/discovery/discovery.h>
#include <syncflow/platform/platform.h>
#include <syncflow/common/logger.h>
#include <syncflow/common/utils.h>
#include <thread>
#include <chrono>
#include <atomic>

namespace syncflow::discovery {

class DiscoveryEngine::Impl {
public:
    Impl() : running_(false), socket_(nullptr), local_device_id_("") {}
    
    bool start(OnDeviceDiscovered on_discovered, OnDeviceLost on_lost) {
        if (running_) {
            return false;
        }
        
        // Create UDP socket
        auto network = platform::Network::create();
        socket_ = network->create_udp_socket();
        
        if (!socket_) {
            LOG_ERROR("DiscoveryEngine", "Failed to create UDP socket");
            return false;
        }
        
        LOG_INFO("DiscoveryEngine", "UDP socket created successfully");
        
        // Configure socket
        socket_->set_reuse_address(true);
        socket_->set_broadcast(true);
        socket_->set_receive_timeout(1000);  // 1 second timeout
        
        LOG_INFO("DiscoveryEngine", "Socket options configured");
        
        // Bind to discovery port
        platform::SocketAddress local_addr;
        local_addr.port = DISCOVERY_PORT;
        local_addr.ip = "0.0.0.0";
        local_addr.family = platform::AddressFamily::IPv4;
        
        if (!socket_->bind(local_addr)) {
            LOG_ERROR("DiscoveryEngine", "Failed to bind to discovery port " + std::to_string(DISCOVERY_PORT));
            return false;
        }
        
        // LOG_INFO("DiscoveryEngine", "Socket bound to port " + std::to_string(DISCOVERY_PORT));
        
        running_ = true;
        on_discovered_ = on_discovered;
        on_lost_ = on_lost;
        
        // Start receive thread
        receive_thread_ = std::thread([this] { receive_loop(); });
        
        // Start broadcast thread
        broadcast_thread_ = std::thread([this] { broadcast_loop(); });
        
        LOG_INFO("DiscoveryEngine", "Started discovery engine with broadcast and receive threads");
        return true;
    }
    
    bool stop() {
        if (!running_) {
            return false;
        }
        
        running_ = false;
        
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }
        if (broadcast_thread_.joinable()) {
            broadcast_thread_.join();
        }
        
        if (socket_) {
            socket_->close();
            socket_ = nullptr;
        }
        
        LOG_INFO("DiscoveryEngine", "Stopped discovery engine");
        return true;
    }
    
    bool is_running() const {
        return running_;
    }
    
private:
    void receive_loop() {
        const size_t BUFFER_SIZE = 4096;
        std::vector<uint8_t> buffer(BUFFER_SIZE);
        platform::SocketAddress from_addr;
        
        LOG_INFO("DiscoveryEngine", "Receive loop started - listening on port " + std::to_string(DISCOVERY_PORT));
        
        while (running_) {
            size_t received = socket_->receive_from(buffer.data(), BUFFER_SIZE, from_addr);
            
            if (received == 0) {
                continue;
            }
            
            LOG_INFO("DiscoveryEngine", "Received " + std::to_string(received) + " bytes from " + from_addr.ip);
            
            // Parse discovery message
            DeviceInfo device_info = parse_discovery_message(buffer, received, from_addr);
            
            // Skip if it's the local device
            if (!device_info.id.empty() && device_info.id == local_device_id_) {
                LOG_INFO("DiscoveryEngine", "Ignoring discovery from self: " + device_info.name);
                continue;
            }
            
            if (!device_info.id.empty()) {
                if (DeviceManager::instance().add_device(device_info)) {
                    if (on_discovered_) {
                        on_discovered_(device_info);
                    }
                    LOG_INFO("DiscoveryEngine", "Device discovered: " + device_info.name + " from " + device_info.ip_address);
                }
            } else {
                LOG_INFO("DiscoveryEngine", "Failed to parse discovery message from " + from_addr.ip);
            }
        }
    }
    
    void broadcast_loop() {
        auto platform_info = platform::get_platform_info();
        auto network = platform::Network::create();
        
        std::string hostname;
        if (!network->get_hostname(hostname)) {
            LOG_ERROR("DiscoveryEngine", "Failed to get hostname");
            hostname = "unknown";
        }
        
        std::string local_ip;
        if (!network->get_local_ip(platform::AddressFamily::IPv4, local_ip)) {
            LOG_ERROR("DiscoveryEngine", "Failed to get local IP address");
            local_ip = "0.0.0.0";
        }
        
        std::string mac_address;
        if (!network->get_mac_address(mac_address)) {
            LOG_ERROR("DiscoveryEngine", "Failed to get MAC address");
            mac_address = "00:00:00:00:00:00";
        }
        
        LOG_INFO("DiscoveryEngine", "Broadcast info - Hostname: " + hostname + ", IP: " + local_ip + ", MAC: " + mac_address);
        
        // Calculate subnet broadcast address
        // For now, try multiple broadcast addresses
        std::vector<std::string> broadcast_addrs = {
            "255.255.255.255",  // Limited broadcast
            "224.0.0.1",        // Multicast all hosts
        };
        
        // Try to extract subnet broadcast from local IP
        // e.g., 192.168.1.5 -> 192.168.1.255
        size_t last_dot = local_ip.find_last_of('.');
        if (last_dot != std::string::npos) {
            std::string subnet_broadcast = local_ip.substr(0, last_dot + 1) + "255";
            broadcast_addrs.insert(broadcast_addrs.begin(), subnet_broadcast);
            LOG_INFO("DiscoveryEngine", "Calculated subnet broadcast: " + subnet_broadcast);
        }
        
        while (running_) {
            // Create device info
            DeviceInfo local_device;
            local_device.id = mac_address + ":" + hostname;
            local_device.name = hostname;
            local_device.hostname = hostname;
            local_device.ip_address = local_ip;
            local_device.port = TRANSFER_PORT;
            
            // Store local device ID to filter self-discoveries
            if (local_device_id_.empty()) {
                local_device_id_ = local_device.id;
            }
            
#ifdef _WIN32
            local_device.platform = PlatformType::WINDOWS;
#elif __APPLE__
            local_device.platform = PlatformType::MACOS;
#else
            local_device.platform = PlatformType::LINUX;
#endif
            
            local_device.version = "0.1.0";
            local_device.last_seen = std::chrono::system_clock::now();
            
            // Encode discovery message
            std::vector<uint8_t> message = encode_discovery_message(local_device);
            
            // Broadcast to network via multiple addresses
            platform::SocketAddress broadcast_addr;
            broadcast_addr.port = DISCOVERY_PORT;
            broadcast_addr.family = platform::AddressFamily::IPv4;
            
            for (const auto& addr : broadcast_addrs) {
                broadcast_addr.ip = addr;
                size_t sent = socket_->send_to(message.data(), message.size(), broadcast_addr);
                LOG_INFO("DiscoveryEngine", "Broadcasted " + std::to_string(sent) + " bytes to " + addr + ":" + std::to_string(DISCOVERY_PORT));
            }
            
            // Cleanup stale devices
            DeviceManager::instance().cleanup_stale_devices(DISCOVERY_TIMEOUT_MS);
            
            // Wait before next broadcast
            std::this_thread::sleep_for(std::chrono::milliseconds(DISCOVERY_INTERVAL_MS));
        }
    }
    
    std::vector<uint8_t> encode_discovery_message(const DeviceInfo& device) {
        utils::BinaryWriter writer;
        
        writer.write_uint32(HANDSHAKE_MAGIC);
        writer.write_uint32(PROTOCOL_VERSION);
        writer.write_string(device.id);
        writer.write_string(device.name);
        writer.write_string(device.hostname);
        writer.write_uint8((uint8_t)device.platform);
        writer.write_string(device.ip_address);
        writer.write_uint16(device.port);
        writer.write_string(device.version);
        
        return writer.get_buffer();
    }
    
    DeviceInfo parse_discovery_message(const std::vector<uint8_t>& data,
                                       size_t size,
                                       const platform::SocketAddress& from_addr) {
        DeviceInfo device;
        device.ip_address = from_addr.ip;
        
        utils::BinaryReader reader(data.data(), size);
        
        uint32_t magic, version;
        if (!reader.read_uint32(magic) || magic != HANDSHAKE_MAGIC) {
            LOG_INFO("DiscoveryEngine", "Invalid magic number from " + from_addr.ip + ": 0x" + std::to_string(magic));
            return device;  // Invalid magic
        }
        
        if (!reader.read_uint32(version) || version != PROTOCOL_VERSION) {
            LOG_INFO("DiscoveryEngine", "Invalid protocol version from " + from_addr.ip + ": " + std::to_string(version));
            return device;  // Invalid protocol version
        }
        
        if (!reader.read_string(device.id) ||
            !reader.read_string(device.name) ||
            !reader.read_string(device.hostname)) {
            LOG_INFO("DiscoveryEngine", "Failed to parse device info from " + from_addr.ip);
            return device;  // Failed to parse
        }
        
        uint8_t platform_byte;
        if (!reader.read_uint8(platform_byte)) {
            LOG_INFO("DiscoveryEngine", "Failed to parse platform from " + from_addr.ip);
            return device;
        }
        device.platform = (PlatformType)platform_byte;
        
        std::string ip;
        if (!reader.read_string(ip)) {
            LOG_INFO("DiscoveryEngine", "Failed to parse IP address from " + from_addr.ip);
            return device;
        }
        device.ip_address = ip;
        
        if (!reader.read_uint16(device.port) ||
            !reader.read_string(device.version)) {
            LOG_INFO("DiscoveryEngine", "Failed to parse port/version from " + from_addr.ip);
            return device;
        }
        
        device.last_seen = std::chrono::system_clock::now();
        LOG_INFO("DiscoveryEngine", "Successfully parsed device: " + device.name + " from " + from_addr.ip);
        
        return device;
    }
    
    std::atomic<bool> running_;
    std::unique_ptr<platform::Socket> socket_;
    std::thread receive_thread_;
    std::thread broadcast_thread_;
    OnDeviceDiscovered on_discovered_;
    OnDeviceLost on_lost_;
    std::string local_device_id_;  // Store local device ID to filter self-discoveries
};

DiscoveryEngine::DiscoveryEngine() : impl_(std::make_unique<Impl>()) {}

DiscoveryEngine::~DiscoveryEngine() = default;

bool DiscoveryEngine::start(OnDeviceDiscovered on_discovered, OnDeviceLost on_lost) {
    return impl_->start(on_discovered, on_lost);
}

bool DiscoveryEngine::stop() {
    return impl_->stop();
}

bool DiscoveryEngine::is_running() const {
    return impl_->is_running();
}

} // namespace syncflow::discovery

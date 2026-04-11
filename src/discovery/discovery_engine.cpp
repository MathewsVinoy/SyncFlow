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
    Impl() : running_(false), socket_(nullptr) {}
    
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
        
        // Configure socket
        socket_->set_reuse_address(true);
        socket_->set_broadcast(true);
        socket_->set_receive_timeout(1000);  // 1 second timeout
        
        // Bind to discovery port
        platform::SocketAddress local_addr;
        local_addr.port = DISCOVERY_PORT;
        local_addr.ip = "0.0.0.0";
        local_addr.family = platform::AddressFamily::IPv4;
        
        if (!socket_->bind(local_addr)) {
            LOG_ERROR("DiscoveryEngine", "Failed to bind to discovery port");
            return false;
        }
        
        running_ = true;
        on_discovered_ = on_discovered;
        on_lost_ = on_lost;
        
        // Start receive thread
        receive_thread_ = std::thread([this] { receive_loop(); });
        
        // Start broadcast thread
        broadcast_thread_ = std::thread([this] { broadcast_loop(); });
        
        LOG_INFO("DiscoveryEngine", "Started discovery engine");
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
        
        while (running_) {
            size_t received = socket_->receive_from(buffer.data(), BUFFER_SIZE, from_addr);
            
            if (received == 0) {
                continue;
            }
            
            // Parse discovery message
            DeviceInfo device_info = parse_discovery_message(buffer, received, from_addr);
            
            if (!device_info.id.empty()) {
                if (DeviceManager::instance().add_device(device_info)) {
                    if (on_discovered_) {
                        on_discovered_(device_info);
                    }
                    LOG_INFO("DiscoveryEngine", "Device discovered: " + device_info.name);
                }
            }
        }
    }
    
    void broadcast_loop() {
        auto platform_info = platform::get_platform_info();
        auto network = platform::Network::create();
        
        std::string hostname;
        network->get_hostname(hostname);
        
        std::string local_ip;
        network->get_local_ip(platform::AddressFamily::IPv4, local_ip);
        
        std::string mac_address;
        network->get_mac_address(mac_address);
        
        while (running_) {
            // Create device info
            DeviceInfo local_device;
            local_device.id = mac_address + ":" + hostname;
            local_device.name = hostname;
            local_device.hostname = hostname;
            local_device.ip_address = local_ip;
            local_device.port = TRANSFER_PORT;
            
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
            
            // Broadcast to network
            platform::SocketAddress broadcast_addr;
            broadcast_addr.ip = "255.255.255.255";
            broadcast_addr.port = DISCOVERY_PORT;
            broadcast_addr.family = platform::AddressFamily::IPv4;
            
            socket_->send_to(message.data(), message.size(), broadcast_addr);
            
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
            return device;  // Invalid magic
        }
        
        if (!reader.read_uint32(version) || version != PROTOCOL_VERSION) {
            return device;  // Invalid protocol version
        }
        
        if (!reader.read_string(device.id) ||
            !reader.read_string(device.name) ||
            !reader.read_string(device.hostname)) {
            return device;  // Failed to parse
        }
        
        uint8_t platform_byte;
        if (!reader.read_uint8(platform_byte)) {
            return device;
        }
        device.platform = (PlatformType)platform_byte;
        
        std::string ip;
        if (!reader.read_string(ip)) {
            return device;
        }
        device.ip_address = ip;
        
        if (!reader.read_uint16(device.port) ||
            !reader.read_string(device.version)) {
            return device;
        }
        
        device.last_seen = std::chrono::system_clock::now();
        
        return device;
    }
    
    std::atomic<bool> running_;
    std::unique_ptr<platform::Socket> socket_;
    std::thread receive_thread_;
    std::thread broadcast_thread_;
    OnDeviceDiscovered on_discovered_;
    OnDeviceLost on_lost_;
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

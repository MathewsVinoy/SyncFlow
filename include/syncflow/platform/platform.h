// include/syncflow/platform/platform.h
// Cross-platform abstraction layer

#ifndef SYNCFLOW_PLATFORM_H
#define SYNCFLOW_PLATFORM_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace syncflow::platform {

// ============================================================================
// FILE SYSTEM ABSTRACTION
// ============================================================================

enum class FileMode : uint16_t {
    READ = 0x01,
    WRITE = 0x02,
    APPEND = 0x04,
    CREATE = 0x08,
    TRUNCATE = 0x10,
};

struct FileStats {
    uint64_t size;
    bool is_directory;
    bool is_symlink;
    uint64_t modification_time;  // Unix timestamp
    uint16_t permissions;
};

class FileSystem {
public:
    virtual ~FileSystem() = default;
    
    // File operations
    virtual bool file_exists(const std::string& path) = 0;
    virtual bool directory_exists(const std::string& path) = 0;
    virtual bool get_file_stats(const std::string& path, FileStats& stats) = 0;
    virtual bool create_directory(const std::string& path) = 0;
    virtual bool create_directories(const std::string& path) = 0;
    virtual bool delete_file(const std::string& path) = 0;
    virtual bool delete_directory(const std::string& path) = 0;
    virtual bool rename_file(const std::string& old_path, const std::string& new_path) = 0;
    
    // List directory contents
    virtual bool list_directory(const std::string& path, std::vector<std::string>& entries) = 0;
    
    // Path operations
    virtual std::string absolute_path(const std::string& path) = 0;
    virtual std::string canonical_path(const std::string& path) = 0;
    virtual std::string get_filename(const std::string& path) = 0;
    virtual std::string get_directory(const std::string& path) = 0;
    virtual std::string join_paths(const std::vector<std::string>& components) = 0;
    
    // File I/O
    virtual std::vector<uint8_t> read_file(const std::string& path) = 0;
    virtual bool write_file(const std::string& path, const std::vector<uint8_t>& data) = 0;
    virtual bool write_file_chunk(const std::string& path, uint64_t offset, 
                                   const std::vector<uint8_t>& data) = 0;
    
    // Static factory
    static std::unique_ptr<FileSystem> create();
};

// ============================================================================
// NETWORKING ABSTRACTION
// ============================================================================

enum class AddressFamily : uint8_t {
    IPv4 = 0,
    IPv6 = 1,
};

struct SocketAddress {
    std::string ip;
    uint16_t port;
    AddressFamily family;
};

class Socket {
public:
    virtual ~Socket() = default;
    
    virtual bool bind(const SocketAddress& address) = 0;
    virtual bool listen(int backlog) = 0;
    virtual std::unique_ptr<Socket> accept(SocketAddress& peer_addr) = 0;
    virtual bool connect(const SocketAddress& address) = 0;
    virtual bool close() = 0;
    
    // Blocking operations
    virtual size_t send(const uint8_t* data, size_t size) = 0;
    virtual size_t receive(uint8_t* data, size_t max_size) = 0;
    
    // UDP operations
    virtual size_t send_to(const uint8_t* data, size_t size, const SocketAddress& addr) = 0;
    virtual size_t receive_from(uint8_t* data, size_t max_size, SocketAddress& from_addr) = 0;
    
    // Options
    virtual bool set_non_blocking(bool non_blocking) = 0;
    virtual bool set_reuse_address(bool reuse) = 0;
    virtual bool set_broadcast(bool broadcast) = 0;
    virtual bool set_receive_timeout(int milliseconds) = 0;
    virtual bool set_send_timeout(int milliseconds) = 0;
    
    virtual int get_native_handle() const = 0;
    virtual bool is_valid() const = 0;
};

class Network {
public:
    virtual ~Network() = default;
    
    // Socket creation
    virtual std::unique_ptr<Socket> create_tcp_socket() = 0;
    virtual std::unique_ptr<Socket> create_udp_socket() = 0;
    
    // Network info
    virtual bool get_local_ip(AddressFamily family, std::string& ip) = 0;
    virtual bool get_hostname(std::string& hostname) = 0;
    virtual bool get_mac_address(std::string& mac) = 0;
    
    // Utilities
    virtual bool is_network_reachable() = 0;
    
    // Static factory
    static std::unique_ptr<Network> create();
};

// ============================================================================
// THREAD POOL ABSTRACTION
// ============================================================================

using ThreadTask = std::function<void()>;

class ThreadPool {
public:
    virtual ~ThreadPool() = default;
    
    virtual void enqueue(ThreadTask task) = 0;
    virtual void shutdown(bool wait = true) = 0;
    virtual size_t active_count() const = 0;
    virtual size_t pending_count() const = 0;
    
    static std::unique_ptr<ThreadPool> create(size_t num_threads);
};

// ============================================================================
// PLATFORM INFO
// ============================================================================

struct PlatformInfo {
    std::string os_name;
    std::string os_version;
    std::string architecture;
    std::string compiler;
    uint32_t num_cpu_cores;
    uint64_t total_memory;
};

PlatformInfo get_platform_info();
std::string get_config_directory();
std::string get_data_directory();
std::string get_temp_directory();

} // namespace syncflow::platform

#endif // SYNCFLOW_PLATFORM_H

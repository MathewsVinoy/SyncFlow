#include "syncflow/networking/peer_node.h"

#include "syncflow/config.h"
#include "syncflow/platform/system_info.h"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#include <iostream>

namespace syncflow::networking {

namespace {

void close_socket(int fd) {
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

bool send_all(int fd, const void* data, std::size_t size);

bool send_all(int fd, const std::string& data) {
    return send_all(fd, data.data(), data.size());
}

bool send_all(int fd, const void* data, std::size_t size) {
    const auto* ptr = static_cast<const char*>(data);
    std::size_t remaining = size;

    while (remaining > 0) {
        const ssize_t sent = ::send(fd, ptr, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        ptr += sent;
        remaining -= static_cast<std::size_t>(sent);
    }

    return true;
}

bool send_file_stream(int fd, const std::filesystem::path& path, std::uint64_t& bytes_sent) {
    bytes_sent = 0;

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    std::vector<char> buffer(4096);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = input.gcount();
        if (got > 0) {
            if (!send_all(fd, buffer.data(), static_cast<std::size_t>(got))) {
                return false;
            }
            bytes_sent += static_cast<std::uint64_t>(got);
        }
    }

    return true;
}

bool recv_exact(int fd, void* data, std::size_t size) {
    auto* ptr = static_cast<char*>(data);
    std::size_t remaining = size;

    while (remaining > 0) {
        const ssize_t received = ::recv(fd, ptr, remaining, 0);
        if (received <= 0) {
            return false;
        }
        ptr += received;
        remaining -= static_cast<std::size_t>(received);
    }

    return true;
}

bool recv_line(int fd, std::string& line, bool& timed_out) {
    timed_out = false;
    line.clear();
    char ch = '\0';
    while (true) {
        const ssize_t received = ::recv(fd, &ch, 1, 0);
        if (received <= 0) {
            if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                timed_out = true;
            }
            return false;
        }
        if (ch == '\n') {
            return true;
        }
        if (ch != '\r') {
            line.push_back(ch);
        }
    }
}

std::string build_source_signature(const std::filesystem::path& source_path) {
    std::error_code ec;
    if (!std::filesystem::exists(source_path, ec) || ec) {
        return "missing";
    }

    std::ostringstream sig;
    if (std::filesystem::is_regular_file(source_path, ec) && !ec) {
        const auto size = static_cast<std::uint64_t>(std::filesystem::file_size(source_path, ec));
        const auto file_time = std::filesystem::last_write_time(source_path, ec);
        if (ec) {
            return "missing";
        }
        const auto mtime = static_cast<long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(file_time.time_since_epoch()).count());
        sig << "F|" << source_path.filename().string() << '|' << size << '|' << mtime;
        return sig.str();
    }

    if (!std::filesystem::is_directory(source_path, ec) || ec) {
        return "unsupported";
    }

    sig << "D|";
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(source_path, options), end; it != end; ++it) {
        const auto relative = std::filesystem::relative(it->path(), source_path, ec);
        if (ec) {
            ec.clear();
            continue;
        }

        const std::string relative_text = relative.generic_string();
        if (relative_text.rfind(".syncflow", 0) == 0) {
            if (it->is_directory(ec) && !ec) {
                it.disable_recursion_pending();
            }
            ec.clear();
            continue;
        }

        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        const auto size = static_cast<std::uint64_t>(it->file_size(ec));
        if (ec) {
            ec.clear();
            continue;
        }

        const auto file_time = it->last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }

        const auto mtime = static_cast<long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(file_time.time_since_epoch()).count());

        sig << relative_text << '|' << size << '|' << mtime << ';';
    }

    return sig.str();
}



std::filesystem::path hidden_sync_folder(const std::filesystem::path& base_path) {
    std::error_code ec;
    if (std::filesystem::is_directory(base_path, ec) && !ec) {
        return base_path / ".syncflow";
    }

    const auto parent = base_path.has_parent_path() ? base_path.parent_path() : std::filesystem::current_path();
    return parent / ".syncflow";
}

std::filesystem::path hidden_sync_log_path(const std::filesystem::path& base_path) {
    return hidden_sync_folder(base_path) / "transfer.log";
}

std::chrono::system_clock::time_point file_time_to_system_time(const std::filesystem::file_time_type& file_time) {
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
}

std::filesystem::file_time_type system_time_to_file_time(const std::chrono::system_clock::time_point& system_time) {
    return std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(
        system_time - std::chrono::system_clock::now() + std::filesystem::file_time_type::clock::now());
}

std::chrono::system_clock::time_point unix_ms_to_system_time(std::int64_t unix_ms) {
    return std::chrono::system_clock::time_point{std::chrono::milliseconds{unix_ms}};
}

std::int64_t system_time_to_unix_ms(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

std::string format_detailed_time(const std::chrono::system_clock::time_point& tp) {
    const auto ms_tp = std::chrono::time_point_cast<std::chrono::milliseconds>(tp);
    const auto ms = static_cast<int>(ms_tp.time_since_epoch().count() % 1000);
    const std::time_t time = std::chrono::system_clock::to_time_t(tp);

    std::tm tm{};
    if (const std::tm* local = std::localtime(&time)) {
        tm = *local;
    }

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.'
        << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
}

void append_transfer_log(const std::filesystem::path& base_path,
                         const std::string& file_name,
                         std::int64_t modified_time_ms) {
    std::error_code ec;
    const auto log_dir = hidden_sync_folder(base_path);
    std::filesystem::create_directories(log_dir, ec);
    if (ec) {
        return;
    }

    std::ofstream log(hidden_sync_log_path(base_path), std::ios::app);
    if (!log) {
        return;
    }

    const auto event_time = std::chrono::system_clock::now();
    const auto file_time = unix_ms_to_system_time(modified_time_ms);

    log << format_detailed_time(event_time) << '|' << file_name << '|' << format_detailed_time(file_time) << '\n';
}

std::string basename_for_path(const std::filesystem::path& path) {
    return path.filename().string();
}

std::string sanitize_filename(const std::string& filename) {
    return std::filesystem::path(filename).filename().string();
}

std::filesystem::path executable_dir() {
    std::array<char, 4096> buffer{};
    const ssize_t len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (len <= 0) {
        return std::filesystem::current_path();
    }

    buffer[static_cast<std::size_t>(len)] = '\0';
    return std::filesystem::path(buffer.data()).parent_path();
}

std::filesystem::path find_config_path() {
    const std::array<std::filesystem::path, 4> candidates{
        std::filesystem::current_path() / "config.json",
        executable_dir() / "config.json",
        executable_dir().parent_path() / "config.json",
        executable_dir().parent_path().parent_path() / "config.json"
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return candidates.front();
}

bool connect_with_timeout(int fd, const sockaddr_in& addr, std::chrono::seconds timeout, std::string& error_text) {
    const int old_flags = ::fcntl(fd, F_GETFL, 0);
    if (old_flags < 0) {
        error_text = std::strerror(errno);
        return false;
    }

    if (::fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) != 0) {
        error_text = std::strerror(errno);
        return false;
    }

    const int connect_rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (connect_rc == 0) {
        (void)::fcntl(fd, F_SETFL, old_flags);
        return true;
    }

    if (errno != EINPROGRESS) {
        error_text = std::strerror(errno);
        (void)::fcntl(fd, F_SETFL, old_flags);
        return false;
    }

    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(fd, &write_set);

    timeval tv{};
    tv.tv_sec = timeout.count();
    tv.tv_usec = 0;

    const int select_rc = ::select(fd + 1, nullptr, &write_set, nullptr, &tv);
    if (select_rc <= 0) {
        error_text = (select_rc == 0) ? "timeout" : std::strerror(errno);
        (void)::fcntl(fd, F_SETFL, old_flags);
        return false;
    }

    int so_error = 0;
    socklen_t so_error_len = sizeof(so_error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0) {
        error_text = std::strerror(errno);
        (void)::fcntl(fd, F_SETFL, old_flags);
        return false;
    }

    if (so_error != 0) {
        error_text = std::strerror(so_error);
        (void)::fcntl(fd, F_SETFL, old_flags);
        return false;
    }

    (void)::fcntl(fd, F_SETFL, old_flags);
    return true;
}

}  // namespace

PeerNode::PeerNode(std::string device_name)
        : file_sync_config_(syncflow::file_sync::load_config(find_config_path())),
          device_name_([&]() -> std::string {
              if (!device_name.empty()) {
                  return device_name;
              }
              if (!file_sync_config_.device_name.empty()) {
                  return file_sync_config_.device_name;
              }
#ifdef DEVICE_NAME
              return std::string(DEVICE_NAME);
#else
              return platform::get_hostname();
#endif
          }()),
          local_ip_(platform::get_local_ipv4()),
          logger_(device_name_, local_ip_) {}


void PeerNode::run() {
    platform::install_signal_handlers(running_);
    log_startup();

    tcp_thread_ = std::thread([this] { tcp_server_loop(); });
    udp_thread_ = std::thread([this] { udp_listener_loop(); });
    broadcast_thread_ = std::thread([this] { broadcast_loop(); });

    std::cout << "Press Ctrl+C to stop.\n";

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    stop();
}

void PeerNode::stop() {
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

    logger_.info("shutdown complete");
}

void PeerNode::log_startup() {
    logger_.info("starting peer node");
    logger_.info("device name: " + device_name_ + ", ip: " + local_ip_ +
                 ", tcp port: " + std::to_string(config::kTcpPort) +
                 ", udp port: " + std::to_string(config::kUdpDiscoveryPort));

    if (syncflow::file_sync::is_enabled(file_sync_config_)) {
        logger_.info("file sync enabled, source: " + file_sync_config_.source_path.string() +
                     ", receive dir: " + file_sync_config_.receive_dir.string());
    } else {
        logger_.info("file sync disabled or config.json missing source_path");
    }
}

bool PeerNode::should_initiate(const PeerInfo& peer) const {
    return !(peer.name == device_name_ && peer.ip == local_ip_);
}

bool PeerNode::is_active(const PeerInfo& peer) {
    const auto key = endpoint_key(peer);
    std::lock_guard<std::mutex> guard(active_mutex_);
    return active_connections_.find(key) != active_connections_.end();
}

bool PeerNode::should_attempt_connect(const PeerInfo& peer) {
    const auto key = endpoint_key(peer);
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> guard(connect_mutex_);

    if (pending_connections_.find(key) != pending_connections_.end()) {
        return false;
    }

    const auto it = last_connect_attempt_.find(key);
    if (it != last_connect_attempt_.end() && (now - it->second) < config::kConnectRetryInterval) {
        return false;
    }

    pending_connections_.insert(key);
    last_connect_attempt_[key] = now;
    return true;
}

void PeerNode::clear_pending_connect(const PeerInfo& peer) {
    const auto key = endpoint_key(peer);
    std::lock_guard<std::mutex> guard(connect_mutex_);
    pending_connections_.erase(key);
}

void PeerNode::mark_active(const PeerInfo& peer) {
    std::lock_guard<std::mutex> guard(active_mutex_);
    active_connections_.insert(endpoint_key(peer));
}

void PeerNode::mark_inactive(const PeerInfo& peer) {
    std::lock_guard<std::mutex> guard(active_mutex_);
    active_connections_.erase(endpoint_key(peer));
}

bool PeerNode::should_send_file_to_peer(const PeerInfo&) const {
    return syncflow::file_sync::is_enabled(file_sync_config_) && syncflow::file_sync::source_exists(file_sync_config_);
}

bool PeerNode::send_file_payload(int fd, const std::filesystem::path& path, std::uint64_t& bytes_sent) {
    const std::uint64_t total_size = static_cast<std::uint64_t>(std::filesystem::file_size(path));
    const std::string filename = basename_for_path(path);
    std::error_code mtime_ec;
    const auto sender_time = std::filesystem::last_write_time(path, mtime_ec);
    const auto sender_mtime = mtime_ec ? std::chrono::system_clock::now() : file_time_to_system_time(sender_time);
    const std::int64_t sender_mtime_ms = system_time_to_unix_ms(sender_mtime);

    std::ostringstream begin;
    begin << "SYNC_BEGIN|" << filename << "|FILE|" << total_size << '|' << sender_mtime_ms << '\n';
    if (!send_all(fd, begin.str())) {
        return false;
    }

    if (!send_file_stream(fd, path, bytes_sent)) {
        return false;
    }

    std::ostringstream end;
    end << "SYNC_END|" << filename << "|FILE\n";
    append_transfer_log(path.has_parent_path() ? path.parent_path() : std::filesystem::current_path(),
                        filename,
                        sender_mtime_ms);
    return send_all(fd, end.str());
}

bool PeerNode::send_directory_payload(int fd, const std::filesystem::path& root_path, std::uint64_t& bytes_sent) {
    bytes_sent = 0;

    if (!std::filesystem::exists(root_path) || !std::filesystem::is_directory(root_path)) {
        logger_.info("directory sync source is not a directory: " + root_path.string());
        return false;
    }

    const std::string root_name = basename_for_path(root_path);
    const auto scan_options = std::filesystem::directory_options::skip_permission_denied;
    std::uint32_t total_files = 0;
    std::uint64_t total_size = 0;
    for (std::filesystem::recursive_directory_iterator it(root_path, scan_options), end; it != end; ++it) {
        const auto relative = std::filesystem::relative(it->path(), root_path);
        const std::string relative_text = relative.generic_string();

        if (relative_text.rfind(".syncflow", 0) == 0) {
            if (it->is_directory()) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (it->is_regular_file()) {
            ++total_files;
            total_size += static_cast<std::uint64_t>(std::filesystem::file_size(it->path()));
        }
    }

    std::ostringstream begin;
    begin << "SYNC_BEGIN|" << root_name << "|DIR|" << total_files << '|' << total_size << '\n';
    if (!send_all(fd, begin.str())) {
        return false;
    }

    logger_.info("directory sync started: source=" + root_path.string() +
                 " files=" + std::to_string(total_files) +
                 " total_bytes=" + std::to_string(total_size));

    std::uint32_t sent_files = 0;
    for (std::filesystem::recursive_directory_iterator it(root_path, scan_options), end; it != end; ++it) {
        const auto& entry = *it;
        const auto relative = std::filesystem::relative(entry.path(), root_path);
        const std::string relative_text = relative.generic_string();

        if (relative_text.rfind(".syncflow", 0) == 0) {
            if (entry.is_directory()) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!entry.is_regular_file()) {
            continue;
        }

        const std::uint64_t file_size = static_cast<std::uint64_t>(std::filesystem::file_size(entry.path()));
        std::error_code mtime_ec;
        const auto sender_time = std::filesystem::last_write_time(entry.path(), mtime_ec);
        const auto sender_mtime = mtime_ec ? std::chrono::system_clock::now() : file_time_to_system_time(sender_time);
        const std::int64_t sender_mtime_ms = system_time_to_unix_ms(sender_mtime);

        std::ostringstream file_begin;
        file_begin << "FILE_ENTRY|" << relative_text << '|' << file_size << '|' << sender_mtime_ms << '\n';
        if (!send_all(fd, file_begin.str())) {
            return false;
        }

        std::uint64_t file_bytes = 0;
        if (!send_file_stream(fd, entry.path(), file_bytes)) {
            return false;
        }
        bytes_sent += file_bytes;
        ++sent_files;

        logger_.info("sent file " + std::to_string(sent_files) + "/" + std::to_string(total_files) +
                     ": " + relative_text + " bytes=" + std::to_string(file_bytes) +
                     " mtime_ms=" + std::to_string(sender_mtime_ms) +
                     " total_sent=" + std::to_string(bytes_sent));
        append_transfer_log(root_path, relative_text, sender_mtime_ms);

        std::ostringstream file_done;
        file_done << "FILE_DONE|" << relative_text << '|' << file_bytes << '\n';
        if (!send_all(fd, file_done.str())) {
            return false;
        }
    }

    std::ostringstream end;
    end << "SYNC_END|" << root_name << "|DIR\n";
    logger_.info("directory sync complete: source=" + root_path.string() +
                 " files_sent=" + std::to_string(sent_files) +
                 " bytes_sent=" + std::to_string(bytes_sent));
    return send_all(fd, end.str());
}

bool PeerNode::receive_file_payload(int fd, const std::filesystem::path& output_path, std::uint64_t expected_size, std::uint64_t& bytes_received, bool write_output) {
    bytes_received = 0;

    std::ofstream output;
    if (write_output) {
        if (!output_path.has_parent_path()) {
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(output_path.parent_path(), ec);
        if (ec) {
            logger_.info("failed to create parent directories: " + output_path.parent_path().string() + " error=" + ec.message());
            return false;
        }

        output.open(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            logger_.info("failed to create output file: " + output_path.string());
            return false;
        }
    }

    std::vector<char> buffer(4096);
    while (bytes_received < expected_size) {
        const std::uint64_t remaining = expected_size - bytes_received;
        const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        if (!recv_exact(fd, buffer.data(), chunk)) {
            return false;
        }
        if (write_output) {
            output.write(buffer.data(), static_cast<std::streamsize>(chunk));
            if (!output) {
                return false;
            }
        }
        bytes_received += static_cast<std::uint64_t>(chunk);
    }

    return true;
}

void PeerNode::maybe_sync_file(int fd, const PeerInfo& peer) {
    if (!should_send_file_to_peer(peer)) {
        return;
    }

    std::uint64_t bytes_sent = 0;
    const bool source_is_dir = syncflow::file_sync::source_is_directory(file_sync_config_);
    const bool ok = source_is_dir
        ? send_directory_payload(fd, file_sync_config_.source_path, bytes_sent)
        : send_file_payload(fd, file_sync_config_.source_path, bytes_sent);

    if (ok) {
        logger_.info(std::string(source_is_dir ? "directory" : "file") + " sent successfully to " + peer.name + " @ " + peer.ip + " bytes=" + std::to_string(bytes_sent));
    } else {
        logger_.info(std::string(source_is_dir ? "directory" : "file") + " send failed to " + peer.name + " @ " + peer.ip);
    }
}

bool PeerNode::try_acquire_share_slot(const PeerInfo& peer) {
    const auto key = endpoint_key(peer);
    std::lock_guard<std::mutex> guard(share_mutex_);
    if (!share_in_progress_) {
        share_in_progress_ = true;
        share_peer_key_ = key;
        return true;
    }

    return share_peer_key_ == key;
}

void PeerNode::release_share_slot(const PeerInfo& peer) {
    const auto key = endpoint_key(peer);
    std::lock_guard<std::mutex> guard(share_mutex_);
    if (share_in_progress_ && share_peer_key_ == key) {
        share_in_progress_ = false;
        share_peer_key_.clear();
    }
}

void PeerNode::handle_peer_connection(int fd, PeerInfo peer, const std::string& direction) {
    if (!try_acquire_share_slot(peer)) {
        const std::string busy = "SHARE_BUSY|" + device_name_ + "|" + local_ip_ + "\n";
        (void)send_all(fd, busy);
        logger_.info("share already in progress, rejected connection from " + peer.name + " @ " + peer.ip);
        close_socket(fd);
        return;
    }

    mark_active(peer);
    logger_.info(direction + " connection established with " + peer.name + " @ " + peer.ip + ":" + std::to_string(peer.tcp_port));
    logger_.info("connected successfully with " + peer.name + " @ " + peer.ip);

    if (direction == "inbound") {
        const std::string connected_ack = "CONNECTED_SUCCESS|" + device_name_ + "|" + local_ip_ + "\n";
        (void)send_all(fd, connected_ack);
    }

    const PeerInfo self{config::kMagic, device_name_, local_ip_, config::kTcpPort};
    const std::string hello = "HELLO|" + serialize_peer_info(self);
    (void)send_all(fd, hello);

    timeval rcv_timeout{};
    rcv_timeout.tv_sec = 1;
    rcv_timeout.tv_usec = 0;
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout));

    maybe_sync_file(fd, peer);

    std::string last_source_signature;
    if (should_send_file_to_peer(peer)) {
        last_source_signature = build_source_signature(file_sync_config_.source_path);
    }

    std::filesystem::path current_sync_base;
    bool current_sync_is_dir = false;
    std::uint32_t current_sync_total_files = 0;
    std::uint32_t current_sync_received_files = 0;
    std::uint64_t current_sync_total_bytes = 0;
    std::uint64_t current_sync_received_bytes = 0;

    while (running_) {
        std::string line;
        bool recv_timed_out = false;
        if (!recv_line(fd, line, recv_timed_out)) {
            if (recv_timed_out) {
                if (should_send_file_to_peer(peer)) {
                    const std::string current_signature = build_source_signature(file_sync_config_.source_path);
                    if (current_signature != last_source_signature) {
                        logger_.info("realtime change detected; syncing updates to " + peer.name + " @ " + peer.ip);
                        maybe_sync_file(fd, peer);
                        last_source_signature = build_source_signature(file_sync_config_.source_path);
                    }
                }
                continue;
            }
            break;
        }

        if (line.empty()) {
            continue;
        }

        if (line.rfind("SYNC_BEGIN|", 0) == 0) {
            const auto first_sep = line.find('|');
            const auto second_sep = line.find('|', first_sep + 1);
            const auto third_sep = line.find('|', second_sep + 1);
            if (first_sep == std::string::npos || second_sep == std::string::npos || third_sep == std::string::npos) {
                continue;
            }

            const std::string name = sanitize_filename(line.substr(first_sep + 1, second_sep - first_sep - 1));
            const std::string kind = line.substr(second_sep + 1, third_sep - second_sep - 1);
            if (kind == "FILE") {
                std::uint64_t expected_size = 0;
                std::int64_t sender_mtime_ms = 0;
                try {
                    expected_size = static_cast<std::uint64_t>(std::stoull(line.substr(third_sep + 1)));
                    const std::size_t fourth_sep = line.find('|', third_sep + 1);
                    if (fourth_sep != std::string::npos) {
                        sender_mtime_ms = static_cast<std::int64_t>(std::stoll(line.substr(fourth_sep + 1)));
                    }
                } catch (...) {
                    logger_.info("invalid file size received from " + peer.name + " @ " + peer.ip);
                    continue;
                }

                const std::filesystem::path output_path = file_sync_config_.receive_dir / name;
                std::uint64_t received_bytes = 0;
                const auto sender_mtime = unix_ms_to_system_time(sender_mtime_ms);
                bool should_write = true;
                std::int64_t local_mtime_ms = 0;

                std::error_code exists_ec;
                if (std::filesystem::exists(output_path, exists_ec) && !exists_ec) {
                    std::error_code local_mtime_ec;
                    const auto local_file_time = std::filesystem::last_write_time(output_path, local_mtime_ec);
                    if (!local_mtime_ec) {
                        const auto local_mtime = file_time_to_system_time(local_file_time);
                        local_mtime_ms = system_time_to_unix_ms(local_mtime);
                        should_write = local_mtime < sender_mtime;
                    }
                }

                if (receive_file_payload(fd, output_path, expected_size, received_bytes, should_write)) {
                    if (should_write) {
                        const auto sender_file_time = system_time_to_file_time(sender_mtime);
                        std::error_code write_time_ec;
                        std::filesystem::last_write_time(output_path, sender_file_time, write_time_ec);
                        if (write_time_ec) {
                            logger_.info("failed to update mtime for " + output_path.string() + " error=" + write_time_ec.message());
                        }

                        logger_.info("file received successfully from " + peer.name + " @ " + peer.ip +
                                     " saved=" + output_path.string() +
                                     " bytes=" + std::to_string(received_bytes) +
                                     " sender_mtime_ms=" + std::to_string(sender_mtime_ms) +
                                     " local_mtime_ms=" + std::to_string(local_mtime_ms) +
                                     " action=updated");
                        append_transfer_log(file_sync_config_.receive_dir,
                                            name,
                                            sender_mtime_ms);
                    } else {
                        logger_.info("file received but skipped newer local copy from " + peer.name + " @ " + peer.ip +
                                     " path=" + output_path.string() +
                                     " sender_mtime_ms=" + std::to_string(sender_mtime_ms) +
                                     " local_mtime_ms=" + std::to_string(local_mtime_ms) +
                                     " action=skipped_newer_local");
                        append_transfer_log(file_sync_config_.receive_dir,
                                            name,
                                            sender_mtime_ms);
                    }

                    const std::string ack = "FILE_RECEIVED|" + name + "|" + std::to_string(received_bytes) + "\n";
                    (void)send_all(fd, ack);
                } else {
                    logger_.info("file receive failed from " + peer.name + " @ " + peer.ip);
                }
                continue;
            }

            if (kind == "DIR") {
                current_sync_base = file_sync_config_.receive_dir;
                current_sync_is_dir = true;
                current_sync_received_files = 0;
                current_sync_received_bytes = 0;
                current_sync_total_files = 0;
                current_sync_total_bytes = 0;

                try {
                    const std::size_t fourth_sep = line.find('|', third_sep + 1);
                    if (fourth_sep != std::string::npos) {
                        current_sync_total_files = static_cast<std::uint32_t>(std::stoul(line.substr(third_sep + 1, fourth_sep - third_sep - 1)));
                        current_sync_total_bytes = static_cast<std::uint64_t>(std::stoull(line.substr(fourth_sep + 1)));
                    }
                } catch (...) {
                    logger_.info("failed to parse directory sync metadata from " + peer.name + " @ " + peer.ip + " line=" + line);
                }

                std::error_code ec;
                std::filesystem::create_directories(current_sync_base, ec);
                if (ec) {
                    logger_.info("failed to create receive base dir: " + current_sync_base.string() + " error=" + ec.message());
                }

                logger_.info("directory sync started from " + peer.name + " @ " + peer.ip +
                             " base=" + current_sync_base.string() +
                             " source=" + name +
                             " files=" + std::to_string(current_sync_total_files) +
                             " total_bytes=" + std::to_string(current_sync_total_bytes));
                continue;
            }
        }

        if (line.rfind("DIR_ENTRY|", 0) == 0) {
            if (!current_sync_is_dir) {
                continue;
            }

            const std::string relative_path = line.substr(std::string("DIR_ENTRY|").size());
            const std::filesystem::path dir_path = current_sync_base / std::filesystem::path(relative_path);
            std::filesystem::create_directories(dir_path);
            logger_.info("directory created: " + dir_path.string());
            continue;
        }

        if (line.rfind("FILE_ENTRY|", 0) == 0) {
            if (!current_sync_is_dir) {
                continue;
            }

            const std::size_t first_sep = line.find('|');
            const std::size_t second_sep = line.find('|', first_sep + 1);
            if (first_sep == std::string::npos || second_sep == std::string::npos) {
                continue;
            }

            const std::string relative_path = line.substr(first_sep + 1, second_sep - first_sep - 1);
            std::uint64_t expected_size = 0;
            std::int64_t sender_mtime_ms = 0;
            try {
                expected_size = static_cast<std::uint64_t>(std::stoull(line.substr(second_sep + 1)));
                const std::size_t third_sep = line.find('|', second_sep + 1);
                if (third_sep != std::string::npos) {
                    sender_mtime_ms = static_cast<std::int64_t>(std::stoll(line.substr(third_sep + 1)));
                }
            } catch (...) {
                logger_.info("invalid directory file size received from " + peer.name + " @ " + peer.ip);
                continue;
            }

            const std::filesystem::path output_path = current_sync_base / std::filesystem::path(relative_path);
            std::uint64_t received_bytes = 0;
            const auto sender_mtime = unix_ms_to_system_time(sender_mtime_ms);
            bool should_write = true;
            std::int64_t local_mtime_ms = 0;

            std::error_code exists_ec;
            if (std::filesystem::exists(output_path, exists_ec) && !exists_ec) {
                std::error_code local_mtime_ec;
                const auto local_file_time = std::filesystem::last_write_time(output_path, local_mtime_ec);
                if (!local_mtime_ec) {
                    const auto local_mtime = file_time_to_system_time(local_file_time);
                    local_mtime_ms = system_time_to_unix_ms(local_mtime);
                    should_write = local_mtime < sender_mtime;
                }
            }

            if (receive_file_payload(fd, output_path, expected_size, received_bytes, should_write)) {
                ++current_sync_received_files;
                current_sync_received_bytes += received_bytes;
                if (should_write) {
                    const auto sender_file_time = system_time_to_file_time(sender_mtime);
                    std::error_code write_time_ec;
                    std::filesystem::last_write_time(output_path, sender_file_time, write_time_ec);
                    if (write_time_ec) {
                        logger_.info("failed to update mtime for " + output_path.string() + " error=" + write_time_ec.message());
                    }

                    logger_.info("received file " + std::to_string(current_sync_received_files) +
                                 "/" + std::to_string(current_sync_total_files) +
                                 ": " + output_path.string() +
                                 " bytes=" + std::to_string(received_bytes) +
                                 " sender_mtime_ms=" + std::to_string(sender_mtime_ms) +
                                 " local_mtime_ms=" + std::to_string(local_mtime_ms) +
                                 " action=updated");
                    append_transfer_log(file_sync_config_.receive_dir,
                                        relative_path,
                                        sender_mtime_ms);
                } else {
                    logger_.info("received file " + std::to_string(current_sync_received_files) +
                                 "/" + std::to_string(current_sync_total_files) +
                                 ": " + output_path.string() +
                                 " bytes=" + std::to_string(received_bytes) +
                                 " sender_mtime_ms=" + std::to_string(sender_mtime_ms) +
                                 " local_mtime_ms=" + std::to_string(local_mtime_ms) +
                                 " action=skipped_newer_local");
                    append_transfer_log(file_sync_config_.receive_dir,
                                        relative_path,
                                        sender_mtime_ms);
                }
                const std::string ack = "FILE_RECEIVED|" + relative_path + "|" + std::to_string(received_bytes) + "\n";
                (void)send_all(fd, ack);
            } else {
                logger_.info("file receive failed from " + peer.name + " @ " + peer.ip + " path=" + output_path.string());
            }
            continue;
        }

        if (line.rfind("SYNC_END|", 0) == 0) {
            current_sync_is_dir = false;
            logger_.info("sync completed with " + peer.name + " @ " + peer.ip +
                         " received_files=" + std::to_string(current_sync_received_files) +
                         "/" + std::to_string(current_sync_total_files) +
                         " received_bytes=" + std::to_string(current_sync_received_bytes) +
                         "/" + std::to_string(current_sync_total_bytes));
            current_sync_base.clear();
            continue;
        }

        if (line.rfind("FILE_BEGIN|", 0) == 0) {
            const std::size_t first_sep = line.find('|');
            const std::size_t second_sep = line.find('|', first_sep + 1);
            if (first_sep == std::string::npos || second_sep == std::string::npos) {
                continue;
            }

            const std::string filename = sanitize_filename(line.substr(first_sep + 1, second_sep - first_sep - 1));
            std::uint64_t expected_size = 0;
            try {
                expected_size = static_cast<std::uint64_t>(std::stoull(line.substr(second_sep + 1)));
            } catch (...) {
                logger_.info("invalid file size received from " + peer.name + " @ " + peer.ip);
                continue;
            }
            const std::filesystem::path output_path = file_sync_config_.receive_dir / filename;

            std::uint64_t received_bytes = 0;
            if (receive_file_payload(fd, output_path, expected_size, received_bytes)) {
                logger_.info("file received successfully from " + peer.name + " @ " + peer.ip +
                             " saved=" + output_path.string() + " bytes=" + std::to_string(received_bytes));
                const std::string ack = "FILE_RECEIVED|" + filename + "|" + std::to_string(received_bytes) + "\n";
                (void)send_all(fd, ack);
            } else {
                logger_.info("file receive failed from " + peer.name + " @ " + peer.ip);
            }
            continue;
        }

        if (line.rfind("FILE_DONE|", 0) == 0) {
            logger_.info("file transfer completed with " + peer.name + " @ " + peer.ip);
            continue;
        }

        if (line.rfind("CONNECTED_SUCCESS|", 0) == 0) {
            logger_.info("peer confirmed connected successfully: " + line);
            continue;
        }

        if (line.rfind("SHARE_BUSY|", 0) == 0) {
            logger_.info("peer reported share busy: " + line);
            continue;
        }

        if (line.rfind("FILE_RECEIVED|", 0) == 0) {
            logger_.info("peer acknowledged file receipt: " + line);
            continue;
        }

        if (line.rfind("HELLO|", 0) == 0) {
            logger_.info("hello from " + peer.name + " @ " + peer.ip + " payload=" + line);
            continue;
        }

        logger_.info("message from " + peer.name + " @ " + peer.ip + ": " + line);
    }

    logger_.info("connection closed with " + peer.name + " @ " + peer.ip);
    mark_inactive(peer);
    release_share_slot(peer);
    close_socket(fd);
}

void PeerNode::connect_to_peer(PeerInfo peer) {
    if (!running_ || is_active(peer)) {
        clear_pending_connect(peer);
        return;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        clear_pending_connect(peer);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer.tcp_port);
    if (::inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr) != 1) {
        logger_.info("invalid peer ip address: " + peer.ip);
        close_socket(fd);
        clear_pending_connect(peer);
        return;
    }

    logger_.info("trying TCP connect to " + peer.name + " @ " + peer.ip + ":" + std::to_string(peer.tcp_port));
    std::string connect_error;
    if (!connect_with_timeout(fd, addr, config::kConnectTimeout, connect_error)) {
        logger_.info("tcp connect failed to " + peer.name + " @ " + peer.ip + ":" + std::to_string(peer.tcp_port) +
                     " error=" + connect_error);
        close_socket(fd);
        clear_pending_connect(peer);
        return;
    }

    clear_pending_connect(peer);
    handle_peer_connection(fd, std::move(peer), "outbound");
}

void PeerNode::broadcast_loop() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        logger_.info("failed to create UDP broadcast socket");
        return;
    }

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(config::kUdpDiscoveryPort);
    ::inet_pton(AF_INET, config::kBroadcastAddress, &dest.sin_addr);

    sockaddr_in mcast{};
    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(config::kUdpDiscoveryPort);
    ::inet_pton(AF_INET, config::kMulticastAddress, &mcast.sin_addr);

    const PeerInfo self{config::kMagic, device_name_, local_ip_, config::kTcpPort};
    const std::string payload = serialize_peer_info(self);

    while (running_) {
        (void)::sendto(fd, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        (void)::sendto(fd, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&mcast), sizeof(mcast));
        std::this_thread::sleep_for(config::kDiscoveryInterval);
    }

    close_socket(fd);
}

void PeerNode::udp_listener_loop() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        logger_.info("failed to create UDP listener socket");
        return;
    }

    udp_listener_fd_ = fd;

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

#ifdef SO_REUSEPORT
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config::kUdpDiscoveryPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        logger_.info("failed to bind UDP listener port " + std::to_string(config::kUdpDiscoveryPort));
        close_socket(fd);
        return;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = ::inet_addr(config::kMulticastAddress);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    (void)::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    std::array<char, 1024> buffer{};
    while (running_) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        const ssize_t received = ::recvfrom(fd, buffer.data(), buffer.size() - 1, 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (received <= 0) {
            continue;
        }

        buffer[static_cast<std::size_t>(received)] = '\0';
        PeerInfo peer;
        if (!parse_peer_info(buffer.data(), peer)) {
            continue;
        }

        if (peer.name == device_name_ && peer.ip == local_ip_) {
            continue;
        }

        logger_.info("discovered peer " + peer.name + " @ " + peer.ip + " (tcp port " + std::to_string(peer.tcp_port) + ")");

        if (should_initiate(peer) && !is_active(peer) && should_attempt_connect(peer)) {
            logger_.info("scheduling TCP connect to " + peer.name + " @ " + peer.ip + ":" + std::to_string(peer.tcp_port));
            std::thread([this, peer] { connect_to_peer(peer); }).detach();
        }
    }

    close_socket(fd);
}

void PeerNode::tcp_server_loop() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        logger_.info("failed to create TCP server socket");
        return;
    }

    tcp_server_fd_ = fd;

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config::kTcpPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        logger_.info("failed to bind TCP server port " + std::to_string(config::kTcpPort));
        close_socket(fd);
        return;
    }

    if (::listen(fd, 8) != 0) {
        logger_.info("failed to listen on TCP port " + std::to_string(config::kTcpPort));
        close_socket(fd);
        return;
    }

    logger_.info("TCP server listening on port " + std::to_string(config::kTcpPort));

    while (running_) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        const int client = ::accept(fd, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (client < 0) {
            if (!running_) {
                break;
            }
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));

        std::thread([this, client, remote_ip = std::string(ip)] {
            std::array<char, 1024> buffer{};
            const ssize_t received = ::recv(client, buffer.data(), buffer.size() - 1, 0);
            PeerInfo peer{config::kMagic, "unknown", remote_ip, 0};

            if (received > 0) {
                buffer[static_cast<std::size_t>(received)] = '\0';
                std::string line = buffer.data();
                line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
                line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

                if (line.rfind("HELLO|", 0) == 0) {
                    line.erase(0, 6);
                }

                PeerInfo parsed;
                if (parse_peer_info(line, parsed)) {
                    peer = parsed;
                }
            }

            handle_peer_connection(client, peer, "inbound");
        }).detach();
    }

    close_socket(fd);
}

}  // namespace syncflow::networking

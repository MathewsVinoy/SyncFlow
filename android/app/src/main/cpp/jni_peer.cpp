#include <jni.h>

#include <android/log.h>
#include <arpa/inet.h>
#include <filesystem>
#include <ifaddrs.h>
#include <memory>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace {

constexpr uint16_t kUdpPort = 45454;
constexpr uint16_t kTcpPort = 45455;
constexpr const char* kMagic = "SYNCFLOW";
constexpr const char* kTag = "syncflow";

std::mutex g_mutex;
std::string g_device_name = "android-phone";
std::string g_local_ip = "0.0.0.0";
std::set<std::string> g_connected_peers;
std::thread g_udp_thread;
std::thread g_tcp_server_thread;
std::thread g_broadcast_thread;
std::atomic<bool> g_running{false};

int g_udp_listener_fd = -1;
int g_tcp_server_fd = -1;

std::string jstring_to_utf8(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }
    const char* raw = env->GetStringUTFChars(value, nullptr);
    std::string result = raw ? raw : "";
    if (raw != nullptr) {
        env->ReleaseStringUTFChars(value, raw);
    }
    return result;
}

std::string get_local_ipv4() {
    ifaddrs* ifaddr = nullptr;
    if (::getifaddrs(&ifaddr) != 0) {
        return "0.0.0.0";
    }

    std::string result = "0.0.0.0";
    for (auto* current = ifaddr; current != nullptr; current = current->ifa_next) {
        if (current->ifa_addr == nullptr || current->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((current->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        auto* addr = reinterpret_cast<sockaddr_in*>(current->ifa_addr);
        char ip[INET_ADDRSTRLEN] = {};
        if (::inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)) != nullptr) {
            result = ip;
            break;
        }
    }
    ::freeifaddrs(ifaddr);
    return result;
}

void close_socket(int& fd) {
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        fd = -1;
    }
}

void broadcast_loop() {
    __android_log_print(ANDROID_LOG_INFO, kTag, "Starting UDP broadcast");

    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Failed to create UDP socket");
        return;
    }

    int opt = 1;
    if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close_socket(sock);
        return;
    }

    if (::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        close_socket(sock);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    addr.sin_port = htons(kUdpPort);

    std::string message = std::string(kMagic) + "|" + g_device_name + "|" + g_local_ip + "|" +
                          std::to_string(kTcpPort) + "\n";

    while (g_running.load()) {
        ::sendto(sock, message.c_str(), message.size(), 0, (sockaddr*)&addr, sizeof(addr));
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    close_socket(sock);
    __android_log_print(ANDROID_LOG_INFO, kTag, "Broadcast stopped");
}

void udp_listener_loop() {
    __android_log_print(ANDROID_LOG_INFO, kTag, "Starting UDP listener");

    g_udp_listener_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_listener_fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Failed to create UDP listener socket");
        return;
    }

    int opt = 1;
    ::setsockopt(g_udp_listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kUdpPort);

    if (::bind(g_udp_listener_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Failed to bind UDP socket");
        close_socket(g_udp_listener_fd);
        return;
    }

    char buffer[512];
    while (g_running.load()) {
        sockaddr_in src_addr{};
        socklen_t src_len = sizeof(src_addr);

        int n = ::recvfrom(g_udp_listener_fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT,
                           (sockaddr*)&src_addr, &src_len);
        if (n > 0) {
            buffer[n] = '\0';
            std::string msg(buffer);

            // Parse: SYNCFLOW|device_name|ip|port
            if (msg.find(kMagic) == 0) {
                std::lock_guard<std::mutex> guard(g_mutex);
                g_connected_peers.insert(msg.substr(0, msg.find('\n')));
                __android_log_print(ANDROID_LOG_INFO, kTag, "Discovered peer: %s", buffer);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    close_socket(g_udp_listener_fd);
    __android_log_print(ANDROID_LOG_INFO, kTag, "UDP listener stopped");
}

void tcp_server_loop() {
    __android_log_print(ANDROID_LOG_INFO, kTag, "Starting TCP server");

    g_tcp_server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (g_tcp_server_fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Failed to create TCP socket");
        return;
    }

    int opt = 1;
    ::setsockopt(g_tcp_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kTcpPort);

    if (::bind(g_tcp_server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Failed to bind TCP socket to port %d", kTcpPort);
        close_socket(g_tcp_server_fd);
        return;
    }

    if (::listen(g_tcp_server_fd, 5) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Failed to listen on TCP socket");
        close_socket(g_tcp_server_fd);
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, kTag, "TCP server listening on port %d", kTcpPort);

    while (g_running.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = ::accept(g_tcp_server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            char client_ip[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            __android_log_print(ANDROID_LOG_INFO, kTag, "Accepted connection from %s", client_ip);

            // Send a simple response
            const char* response = "SYNCFLOW_ACCEPT|android-phone\n";
            ::send(client_fd, response, strlen(response), 0);
            ::close(client_fd);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    close_socket(g_tcp_server_fd);
    __android_log_print(ANDROID_LOG_INFO, kTag, "TCP server stopped");
}

std::string get_status_json() {
    std::lock_guard<std::mutex> guard(g_mutex);

    std::ostringstream oss;
    oss << "{\n  \"running\": " << (g_running.load() ? "true" : "false") << ",\n"
        << "  \"device_name\": \"" << g_device_name << "\",\n"
        << "  \"local_ip\": \"" << g_local_ip << "\",\n"
        << "  \"connections\": [";

    int count = 0;
    for (const auto& peer : g_connected_peers) {
        if (count > 0) oss << ", ";
        oss << "\"" << peer << "\"";
        count++;
        if (count >= 10) break;  // Limit to 10 peers in status
    }

    oss << "]\n}";
    return oss.str();
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_NativeBridge_startPeer(JNIEnv* env, jobject /* this */, jstring configPath) {
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_running.load()) {
        return;
    }

    const std::string cfg = jstring_to_utf8(env, configPath);
    g_local_ip = get_local_ipv4();

    __android_log_print(ANDROID_LOG_INFO, kTag, "Starting peer: %s at %s", g_device_name.c_str(),
                        g_local_ip.c_str());

    g_running.store(true);
    g_connected_peers.clear();

    g_udp_thread = std::thread(udp_listener_loop);
    g_tcp_server_thread = std::thread(tcp_server_loop);
    g_broadcast_thread = std::thread(broadcast_loop);
}

extern "C" JNIEXPORT void JNICALL
Java_com_syncflow_NativeBridge_stopPeer(JNIEnv* /* env */, jobject /* this */) {
    std::lock_guard<std::mutex> guard(g_mutex);

    __android_log_print(ANDROID_LOG_INFO, kTag, "Stopping peer");

    g_running.store(false);

    if (g_udp_listener_fd >= 0) {
        close_socket(g_udp_listener_fd);
    }
    if (g_tcp_server_fd >= 0) {
        close_socket(g_tcp_server_fd);
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_NativeBridge_getStatus(JNIEnv* env, jobject /* this */) {
    const std::string json = get_status_json();
    return env->NewStringUTF(json.c_str());
}

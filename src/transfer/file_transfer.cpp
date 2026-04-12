#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

constexpr int DEFAULT_TRANSFER_PORT = 37030;
constexpr uint32_t PROTOCOL_MAGIC = 0x53465431;  // SFT1
constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr uint32_t CHUNK_SIZE = 256 * 1024;

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void close_socket(SocketHandle fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

bool init_socket_runtime() {
#ifdef _WIN32
    WSADATA wsa_data{};
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#else
    return true;
#endif
}

void shutdown_socket_runtime() {
#ifdef _WIN32
    WSACleanup();
#endif
}

uint64_t bswap64(uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8) |
           ((x & 0x000000FF00000000ULL) >> 8) |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
}

uint64_t host_to_net64(uint64_t x) {
#ifdef _WIN32
    return bswap64(x);
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return bswap64(x);
#else
    return x;
#endif
}

uint64_t net_to_host64(uint64_t x) {
#ifdef _WIN32
    return bswap64(x);
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return bswap64(x);
#else
    return x;
#endif
}

bool parse_port(const std::string& raw, int& out) {
    try {
        int p = std::stoi(raw);
        if (p < 1024 || p > 65535) {
            return false;
        }
        out = p;
        return true;
    } catch (...) {
        return false;
    }
}

bool send_all(SocketHandle sock, const uint8_t* data, size_t size) {
    size_t sent_total = 0;
    while (sent_total < size) {
        const int sent = send(sock,
                              reinterpret_cast<const char*>(data + sent_total),
                              static_cast<int>(size - sent_total),
                              0);
        if (sent <= 0) {
            return false;
        }
        sent_total += static_cast<size_t>(sent);
    }
    return true;
}

bool recv_all(SocketHandle sock, uint8_t* data, size_t size) {
    size_t recv_total = 0;
    while (recv_total < size) {
        const int got = recv(sock,
                             reinterpret_cast<char*>(data + recv_total),
                             static_cast<int>(size - recv_total),
                             0);
        if (got <= 0) {
            return false;
        }
        recv_total += static_cast<size_t>(got);
    }
    return true;
}

std::array<uint32_t, 256> build_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
        }
        table[i] = c;
    }
    return table;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    static const std::array<uint32_t, 256> table = build_crc32_table();
    uint32_t c = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8U);
    }
    return c ^ 0xFFFFFFFFU;
}

#pragma pack(push, 1)
struct FileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t name_len;
    uint64_t file_size;
    uint32_t chunk_size;
};

struct ChunkHeader {
    uint32_t seq;
    uint32_t size;
    uint32_t crc;
};

struct AckPacket {
    uint32_t seq;
    uint8_t status;
};
#pragma pack(pop)

std::string file_name_only(const std::filesystem::path& p) {
    return p.filename().string();
}

bool send_file(SocketHandle sock, const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        std::cerr << "Invalid file path: " << path << "\n";
        return false;
    }

    const uint64_t file_size = std::filesystem::file_size(path);
    const std::string name = file_name_only(path);

    if (name.empty() || name.size() > 1024) {
        std::cerr << "Invalid file name length\n";
        return false;
    }

    FileHeader header{};
    header.magic = htonl(PROTOCOL_MAGIC);
    header.version = htonl(PROTOCOL_VERSION);
    header.name_len = htonl(static_cast<uint32_t>(name.size()));
    header.file_size = host_to_net64(file_size);
    header.chunk_size = htonl(CHUNK_SIZE);

    if (!send_all(sock, reinterpret_cast<const uint8_t*>(&header), sizeof(header))) {
        std::cerr << "Failed to send file header\n";
        return false;
    }

    if (!send_all(sock, reinterpret_cast<const uint8_t*>(name.data()), name.size())) {
        std::cerr << "Failed to send file name\n";
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open file for reading: " << path << "\n";
        return false;
    }

    std::vector<uint8_t> chunk(CHUNK_SIZE);
    uint64_t sent_bytes = 0;
    uint32_t seq = 0;

    while (sent_bytes < file_size) {
        const uint64_t remaining = file_size - sent_bytes;
        const uint32_t this_size = static_cast<uint32_t>(remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining);

        in.read(reinterpret_cast<char*>(chunk.data()), this_size);
        if (in.gcount() != static_cast<std::streamsize>(this_size)) {
            std::cerr << "Failed to read chunk from file\n";
            return false;
        }

        ChunkHeader ch{};
        ch.seq = htonl(seq);
        ch.size = htonl(this_size);
        ch.crc = htonl(crc32(chunk.data(), this_size));

        if (!send_all(sock, reinterpret_cast<const uint8_t*>(&ch), sizeof(ch)) ||
            !send_all(sock, chunk.data(), this_size)) {
            std::cerr << "Failed to send chunk seq=" << seq << "\n";
            return false;
        }

        AckPacket ack{};
        if (!recv_all(sock, reinterpret_cast<uint8_t*>(&ack), sizeof(ack))) {
            std::cerr << "Failed to receive ack for seq=" << seq << "\n";
            return false;
        }

        const uint32_t ack_seq = ntohl(ack.seq);
        if (ack.status != 1 || ack_seq != seq) {
            std::cerr << "Chunk ack failed at seq=" << seq << "\n";
            return false;
        }

        sent_bytes += this_size;
        ++seq;
        std::cout << "Sent " << sent_bytes << "/" << file_size << " bytes\r" << std::flush;
    }

    std::cout << "\nFile transfer completed successfully.\n";
    return true;
}

bool receive_file(SocketHandle sock, const std::filesystem::path& output_dir) {
    FileHeader header{};
    if (!recv_all(sock, reinterpret_cast<uint8_t*>(&header), sizeof(header))) {
        std::cerr << "Failed to receive file header\n";
        return false;
    }

    if (ntohl(header.magic) != PROTOCOL_MAGIC || ntohl(header.version) != PROTOCOL_VERSION) {
        std::cerr << "Unsupported protocol\n";
        return false;
    }

    const uint32_t name_len = ntohl(header.name_len);
    const uint64_t file_size = net_to_host64(header.file_size);
    const uint32_t chunk_size = ntohl(header.chunk_size);

    if (name_len == 0 || name_len > 1024 || chunk_size == 0 || chunk_size > (4U * 1024U * 1024U)) {
        std::cerr << "Invalid header values\n";
        return false;
    }

    std::string file_name(name_len, '\0');
    if (!recv_all(sock, reinterpret_cast<uint8_t*>(file_name.data()), file_name.size())) {
        std::cerr << "Failed to receive file name\n";
        return false;
    }

    std::filesystem::create_directories(output_dir);
    const auto output_file = output_dir / std::filesystem::path(file_name).filename();

    std::ofstream out(output_file, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << output_file << "\n";
        return false;
    }

    std::vector<uint8_t> chunk(chunk_size);
    uint64_t received_bytes = 0;
    uint32_t expected_seq = 0;

    while (received_bytes < file_size) {
        ChunkHeader ch{};
        if (!recv_all(sock, reinterpret_cast<uint8_t*>(&ch), sizeof(ch))) {
            std::cerr << "Failed to receive chunk header\n";
            return false;
        }

        const uint32_t seq = ntohl(ch.seq);
        const uint32_t size = ntohl(ch.size);
        const uint32_t expected_crc = ntohl(ch.crc);

        if (seq != expected_seq || size == 0 || size > chunk_size || received_bytes + size > file_size) {
            std::cerr << "Invalid chunk header values\n";
            return false;
        }

        if (!recv_all(sock, chunk.data(), size)) {
            std::cerr << "Failed to receive chunk data\n";
            return false;
        }

        AckPacket ack{};
        ack.seq = htonl(seq);
        ack.status = (crc32(chunk.data(), size) == expected_crc) ? 1 : 0;

        if (ack.status == 0) {
            send_all(sock, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
            std::cerr << "CRC mismatch on seq=" << seq << "\n";
            return false;
        }

        out.write(reinterpret_cast<const char*>(chunk.data()), size);
        if (!out.good()) {
            std::cerr << "Failed writing output file\n";
            return false;
        }

        if (!send_all(sock, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack))) {
            std::cerr << "Failed to send ack\n";
            return false;
        }

        received_bytes += size;
        ++expected_seq;
        std::cout << "Received " << received_bytes << "/" << file_size << " bytes\r" << std::flush;
    }

    std::cout << "\nSaved file to: " << output_file << "\n";
    return true;
}

int run_send(const std::string& ip, int port, const std::filesystem::path& file_path) {
    SocketHandle sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        std::cerr << "Failed to create TCP socket\n";
        return 1;
    }

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &target.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address: " << ip << "\n";
        close_socket(sock);
        return 1;
    }

    if (connect(sock, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) < 0) {
        std::cerr << "Failed to connect to receiver\n";
        close_socket(sock);
        return 1;
    }

    const bool ok = send_file(sock, file_path);
    close_socket(sock);
    return ok ? 0 : 1;
}

int run_recv(int port, const std::filesystem::path& output_dir) {
    SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == kInvalidSocket) {
        std::cerr << "Failed to create TCP listener\n";
        return 1;
    }

    int reuse = 1;
#ifdef _WIN32
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) != 0) {
#else
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
#endif
        std::cerr << "Failed to set SO_REUSEADDR\n";
        close_socket(listener);
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listener, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind receiver on port " << port << "\n";
        close_socket(listener);
        return 1;
    }

    if (listen(listener, 8) < 0) {
        std::cerr << "Failed to listen\n";
        close_socket(listener);
        return 1;
    }

    std::cout << "Receiver listening on TCP " << port << "...\n";

    sockaddr_in peer{};
#ifdef _WIN32
    int peer_len = sizeof(peer);
#else
    socklen_t peer_len = sizeof(peer);
#endif

    SocketHandle conn = accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (conn == kInvalidSocket) {
        std::cerr << "Accept failed\n";
        close_socket(listener);
        return 1;
    }

    close_socket(listener);

    bool ok = receive_file(conn, output_dir);
    close_socket(conn);
    return ok ? 0 : 1;
}

void print_usage() {
    std::cout << "Usage:\n"
              << "  syncflow_transfer recv [port] [output_dir]\n"
              << "  syncflow_transfer send <ip> [port] <file_path>\n"
              << "Defaults: port=" << DEFAULT_TRANSFER_PORT << ", output_dir=./received\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (!init_socket_runtime()) {
        std::cerr << "Socket runtime init failed\n";
        return 1;
    }

    int rc = 1;

    if (argc < 2) {
        print_usage();
        shutdown_socket_runtime();
        return 1;
    }

    const std::string mode = argv[1];

    if (mode == "recv") {
        int port = DEFAULT_TRANSFER_PORT;
        if (argc >= 3 && !parse_port(argv[2], port)) {
            std::cerr << "Invalid port: " << argv[2] << "\n";
            shutdown_socket_runtime();
            return 1;
        }
        const std::filesystem::path out_dir = (argc >= 4) ? argv[3] : std::filesystem::path("received");
        rc = run_recv(port, out_dir);
    } else if (mode == "send") {
        if (argc < 4) {
            print_usage();
            shutdown_socket_runtime();
            return 1;
        }

        std::string ip = argv[2];
        int port = DEFAULT_TRANSFER_PORT;
        std::filesystem::path file_path;

        if (argc == 4) {
            file_path = argv[3];
        } else {
            if (!parse_port(argv[3], port)) {
                std::cerr << "Invalid port: " << argv[3] << "\n";
                shutdown_socket_runtime();
                return 1;
            }
            file_path = argv[4];
        }

        rc = run_send(ip, port, file_path);
    } else {
        print_usage();
    }

    shutdown_socket_runtime();
    return rc;
}

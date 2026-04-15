#include <array>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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
constexpr std::uint32_t PROTOCOL_MAGIC = 0x53465431U;  // SFT1
constexpr std::uint32_t PROTOCOL_VERSION = 3U;
constexpr std::uint32_t TCP_CHUNK_SIZE = 128U * 1024U;
constexpr std::uint32_t TCP_PIPELINE_QUEUE_DEPTH = 8U;
constexpr std::uint32_t UDP_PAYLOAD_SIZE = 1024U;
constexpr std::uint32_t UDP_WINDOW_SIZE = 32U;
constexpr int SOCKET_TIMEOUT_MS = 100;
constexpr int UDP_RETRANSMIT_TIMEOUT_MS = 50;

enum class TransportMode { Tcp, Udp };
enum class UdpPacketType : std::uint32_t {
	Start = 1,
	StartAck = 2,
	Data = 3,
	DataAck = 4,
	Complete = 5,
	CompleteAck = 6,
	Error = 7,
};

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

#pragma pack(push, 1)
struct FileHeader {
	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t name_len;
	std::uint64_t file_size;
	std::uint32_t chunk_size;
};

struct ChunkHeader {
	std::uint32_t seq;
	std::uint32_t size;
	std::uint32_t crc;
};

struct TcpChunkPacketHeader {
	std::uint32_t seq;
	std::uint32_t orig_size;
	std::uint32_t wire_size;
	std::uint32_t flags;
	std::uint32_t crc;
};

struct AckPacket {
	std::uint32_t seq;
	std::uint8_t status;
};

struct UdpFrameHeader {
	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t type;
	std::uint64_t session;
	std::uint32_t seq;
	std::uint32_t total_chunks;
	std::uint32_t payload_size;
	std::uint32_t crc;
};
#pragma pack(pop)

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

bool set_socket_option_int(SocketHandle socket_fd, int level, int name, int value) {
#ifdef _WIN32
	return setsockopt(socket_fd, level, name, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
#else
	return setsockopt(socket_fd, level, name, &value, sizeof(value)) == 0;
#endif
}

bool set_recv_timeout(SocketHandle socket_fd, int timeout_ms) {
#ifdef _WIN32
	const DWORD to = static_cast<DWORD>(timeout_ms);
	return setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to)) == 0;
#else
	timeval timeout{};
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = (timeout_ms % 1000) * 1000;
	return setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
}

bool set_send_timeout(SocketHandle socket_fd, int timeout_ms) {
#ifdef _WIN32
	const DWORD to = static_cast<DWORD>(timeout_ms);
	return setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&to), sizeof(to)) == 0;
#else
	timeval timeout{};
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = (timeout_ms % 1000) * 1000;
	return setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
}

std::uint64_t bswap64(std::uint64_t x) {
	return ((x & 0x00000000000000FFULL) << 56) |
		   ((x & 0x000000000000FF00ULL) << 40) |
		   ((x & 0x0000000000FF0000ULL) << 24) |
		   ((x & 0x00000000FF000000ULL) << 8) |
		   ((x & 0x000000FF00000000ULL) >> 8) |
		   ((x & 0x0000FF0000000000ULL) >> 24) |
		   ((x & 0x00FF000000000000ULL) >> 40) |
		   ((x & 0xFF00000000000000ULL) >> 56);
}

std::uint64_t host_to_net64(std::uint64_t x) {
#ifdef _WIN32
	return bswap64(x);
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return bswap64(x);
#else
	return x;
#endif
}

std::uint64_t net_to_host64(std::uint64_t x) {
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
		const int p = std::stoi(raw);
		if (p < 1024 || p > 65535) {
			return false;
		}
		out = p;
		return true;
	} catch (...) {
		return false;
	}
}

bool is_numeric(const std::string& value) {
	if (value.empty()) {
		return false;
	}
	for (char c : value) {
		if (c < '0' || c > '9') {
			return false;
		}
	}
	return true;
}

bool send_all(SocketHandle sock, const std::uint8_t* data, size_t size) {
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

bool recv_all(SocketHandle sock, std::uint8_t* data, size_t size) {
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

std::array<std::uint32_t, 256> build_crc32_table() {
	std::array<std::uint32_t, 256> table{};
	for (std::uint32_t i = 0; i < 256; ++i) {
		std::uint32_t c = i;
		for (int j = 0; j < 8; ++j) {
			c = (c & 1U) ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
		}
		table[i] = c;
	}
	return table;
}

std::uint32_t crc32(const std::uint8_t* data, size_t len) {
	static const std::array<std::uint32_t, 256> table = build_crc32_table();
	std::uint32_t c = 0xFFFFFFFFU;
	for (size_t i = 0; i < len; ++i) {
		c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8U);
	}
	return c ^ 0xFFFFFFFFU;
}

std::string file_name_only(const std::filesystem::path& p) {
	return p.filename().string();
}

bool parse_ipv4(const std::string& ip, sockaddr_in& out, int port) {
	out = {};
	out.sin_family = AF_INET;
	out.sin_port = htons(port);
	return inet_pton(AF_INET, ip.c_str(), &out.sin_addr) == 1;
}

bool read_file_chunk_at(std::ifstream& in,
						std::uint64_t offset,
						std::uint32_t size,
						std::vector<std::uint8_t>& out) {
	out.resize(size);
	in.clear();
	in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	in.read(reinterpret_cast<char*>(out.data()), size);
	return in.gcount() == static_cast<std::streamsize>(size);
}

std::vector<std::uint8_t> make_udp_frame(UdpPacketType type,
										 std::uint64_t session,
										 std::uint32_t seq,
										 std::uint32_t total_chunks,
										 const std::uint8_t* payload,
										 std::uint32_t payload_size,
										 std::uint32_t crc = 0) {
	UdpFrameHeader hdr{};
	hdr.magic = htonl(PROTOCOL_MAGIC);
	hdr.version = htonl(PROTOCOL_VERSION);
	hdr.type = htonl(static_cast<std::uint32_t>(type));
	hdr.session = host_to_net64(session);
	hdr.seq = htonl(seq);
	hdr.total_chunks = htonl(total_chunks);
	hdr.payload_size = htonl(payload_size);
	hdr.crc = htonl(crc);

	std::vector<std::uint8_t> packet(sizeof(hdr) + payload_size);
	std::memcpy(packet.data(), &hdr, sizeof(hdr));
	if (payload_size > 0 && payload != nullptr) {
		std::memcpy(packet.data() + sizeof(hdr), payload, payload_size);
	}
	return packet;
}

bool send_udp_frame(SocketHandle sock,
					const sockaddr_in& target,
					const std::vector<std::uint8_t>& packet) {
	const int sent = sendto(sock,
							reinterpret_cast<const char*>(packet.data()),
							static_cast<int>(packet.size()),
							0,
							reinterpret_cast<const sockaddr*>(&target),
							sizeof(target));
	return sent == static_cast<int>(packet.size());
}

bool receive_udp_frame(SocketHandle sock,
					   std::vector<std::uint8_t>& buffer,
					   sockaddr_in& from,
					   size_t& received) {
#ifdef _WIN32
	int len = sizeof(from);
#else
	socklen_t len = sizeof(from);
#endif
	received = recvfrom(sock,
						reinterpret_cast<char*>(buffer.data()),
						static_cast<int>(buffer.size()),
						0,
						reinterpret_cast<sockaddr*>(&from),
						&len);
	return received > 0;
}

bool parse_udp_frame(const std::uint8_t* data,
					 size_t size,
					 UdpFrameHeader& header,
					 const std::uint8_t*& payload) {
	if (size < sizeof(UdpFrameHeader)) {
		return false;
	}

	std::memcpy(&header, data, sizeof(UdpFrameHeader));
	header.magic = ntohl(header.magic);
	header.version = ntohl(header.version);
	header.type = ntohl(header.type);
	header.session = net_to_host64(header.session);
	header.seq = ntohl(header.seq);
	header.total_chunks = ntohl(header.total_chunks);
	header.payload_size = ntohl(header.payload_size);
	header.crc = ntohl(header.crc);

	if (header.magic != PROTOCOL_MAGIC || header.version != PROTOCOL_VERSION) {
		return false;
	}

	if (size != sizeof(UdpFrameHeader) + header.payload_size) {
		return false;
	}

	payload = data + sizeof(UdpFrameHeader);
	return true;
}

std::uint32_t chunks_for_size(std::uint64_t file_size, std::uint32_t chunk_size) {
	if (chunk_size == 0) {
		return 0;
	}
	return static_cast<std::uint32_t>((file_size + chunk_size - 1U) / chunk_size);
}

std::uint32_t chunk_bytes_for_seq(std::uint64_t file_size, std::uint32_t chunk_size, std::uint32_t seq) {
	const std::uint64_t offset = static_cast<std::uint64_t>(seq) * chunk_size;
	if (offset >= file_size) {
		return 0;
	}
	return static_cast<std::uint32_t>(std::min<std::uint64_t>(chunk_size, file_size - offset));
}

bool compute_chunk_crc_list(const std::filesystem::path& path,
							std::uint64_t file_size,
							std::uint32_t chunk_size,
							std::vector<std::uint32_t>& out_crc) {
	out_crc.clear();
	const std::uint32_t chunk_count = chunks_for_size(file_size, chunk_size);
	out_crc.reserve(chunk_count);

	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return false;
	}

	std::vector<std::uint8_t> buf(chunk_size);
	for (std::uint32_t seq = 0; seq < chunk_count; ++seq) {
		const std::uint32_t need = chunk_bytes_for_seq(file_size, chunk_size, seq);
		in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(need));
		if (in.gcount() != static_cast<std::streamsize>(need)) {
			return false;
		}
		out_crc.push_back(crc32(buf.data(), need));
	}
	return true;
}

bool rle_compress(const std::vector<std::uint8_t>& input, std::vector<std::uint8_t>& output) {
	output.clear();
	if (input.empty()) {
		return false;
	}
	output.reserve(input.size());
	size_t i = 0;
	while (i < input.size()) {
		const std::uint8_t value = input[i];
		std::uint8_t run = 1;
		while (i + run < input.size() && input[i + run] == value && run < 255) {
			++run;
		}
		output.push_back(run);
		output.push_back(value);
		i += run;
	}
	return output.size() + 16 < input.size();
}

bool rle_decompress(const std::uint8_t* input,
					 size_t input_size,
					 std::uint32_t expected_output_size,
					 std::vector<std::uint8_t>& output) {
	output.clear();
	if (input_size % 2 != 0) {
		return false;
	}
	output.reserve(expected_output_size);
	for (size_t i = 0; i < input_size; i += 2) {
		const std::uint8_t run = input[i];
		const std::uint8_t value = input[i + 1];
		if (run == 0) {
			return false;
		}
		for (std::uint32_t n = 0; n < run; ++n) {
			output.push_back(value);
			if (output.size() > expected_output_size) {
				return false;
			}
		}
	}
	return output.size() == expected_output_size;
}

inline bool bit_is_set(const std::vector<std::uint8_t>& bitmap, std::uint32_t idx) {
	return (bitmap[idx / 8U] & static_cast<std::uint8_t>(1U << (idx % 8U))) != 0;
}

inline void bit_set(std::vector<std::uint8_t>& bitmap, std::uint32_t idx) {
	bitmap[idx / 8U] = static_cast<std::uint8_t>(bitmap[idx / 8U] | static_cast<std::uint8_t>(1U << (idx % 8U)));
}

bool send_file_tcp(SocketHandle sock, const std::filesystem::path& path) {
	if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
		std::cerr << "Invalid file path: " << path << "\n";
		return false;
	}

	const std::uint64_t file_size = std::filesystem::file_size(path);
	const std::string name = file_name_only(path);

	if (name.empty() || name.size() > 1024) {
		std::cerr << "Invalid file name length\n";
		return false;
	}

	FileHeader header{};
	header.magic = htonl(PROTOCOL_MAGIC);
	header.version = htonl(PROTOCOL_VERSION);
	header.name_len = htonl(static_cast<std::uint32_t>(name.size()));
	header.file_size = host_to_net64(file_size);
	header.chunk_size = htonl(TCP_CHUNK_SIZE);

	if (!send_all(sock, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header))) {
		std::cerr << "Failed to send file header\n";
		return false;
	}

	if (!send_all(sock, reinterpret_cast<const std::uint8_t*>(name.data()), name.size())) {
		std::cerr << "Failed to send file name\n";
		return false;
	}

	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		std::cerr << "Failed to open file for reading: " << path << "\n";
		return false;
	}

	const std::uint32_t chunk_count = chunks_for_size(file_size, TCP_CHUNK_SIZE);
	std::vector<std::uint32_t> local_crc;
	if (!compute_chunk_crc_list(path, file_size, TCP_CHUNK_SIZE, local_crc)) {
		std::cerr << "Failed computing sender chunk fingerprints\n";
		return false;
	}

	const std::uint32_t chunk_count_be = htonl(chunk_count);
	if (!send_all(sock, reinterpret_cast<const std::uint8_t*>(&chunk_count_be), sizeof(chunk_count_be))) {
		std::cerr << "Failed to send chunk count\n";
		return false;
	}

	for (std::uint32_t c : local_crc) {
		const std::uint32_t be = htonl(c);
		if (!send_all(sock, reinterpret_cast<const std::uint8_t*>(&be), sizeof(be))) {
			std::cerr << "Failed to send fingerprint list\n";
			return false;
		}
	}

	std::uint32_t bitmap_size_be = 0;
	if (!recv_all(sock, reinterpret_cast<std::uint8_t*>(&bitmap_size_be), sizeof(bitmap_size_be))) {
		std::cerr << "Failed to receive delta bitmap size\n";
		return false;
	}
	const std::uint32_t bitmap_size = ntohl(bitmap_size_be);
	if (bitmap_size != (chunk_count + 7U) / 8U) {
		std::cerr << "Invalid delta bitmap size\n";
		return false;
	}

	std::vector<std::uint8_t> bitmap(bitmap_size, 0);
	if (bitmap_size > 0 && !recv_all(sock, bitmap.data(), bitmap_size)) {
		std::cerr << "Failed to receive delta bitmap\n";
		return false;
	}

	std::vector<std::uint32_t> needed_seq;
	needed_seq.reserve(chunk_count);
	for (std::uint32_t seq = 0; seq < chunk_count; ++seq) {
		if (bit_is_set(bitmap, seq)) {
			needed_seq.push_back(seq);
		}
	}

	const std::uint32_t needed_count_be = htonl(static_cast<std::uint32_t>(needed_seq.size()));
	if (!send_all(sock, reinterpret_cast<const std::uint8_t*>(&needed_count_be), sizeof(needed_count_be))) {
		std::cerr << "Failed to send needed chunk count\n";
		return false;
	}

	if (needed_seq.empty()) {
		std::cout << "File already up-to-date. Nothing to transfer.\n";
		return true;
	}

	struct PreparedChunk {
		TcpChunkPacketHeader header{};
		std::vector<std::uint8_t> payload;
	};

	std::deque<PreparedChunk> queue;
	std::mutex queue_mu;
	std::condition_variable cv_not_empty;
	std::condition_variable cv_not_full;
	bool producer_done = false;
	bool producer_error = false;

	std::thread producer([&]() {
		std::ifstream fin(path, std::ios::binary);
		if (!fin.is_open()) {
			std::lock_guard<std::mutex> lock(queue_mu);
			producer_error = true;
			producer_done = true;
			cv_not_empty.notify_all();
			return;
		}

		for (std::uint32_t seq : needed_seq) {
			const std::uint32_t raw_size = chunk_bytes_for_seq(file_size, TCP_CHUNK_SIZE, seq);
			if (raw_size == 0) {
				std::lock_guard<std::mutex> lock(queue_mu);
				producer_error = true;
				producer_done = true;
				cv_not_empty.notify_all();
				return;
			}

			std::vector<std::uint8_t> raw;
			if (!read_file_chunk_at(fin, static_cast<std::uint64_t>(seq) * TCP_CHUNK_SIZE, raw_size, raw)) {
				std::lock_guard<std::mutex> lock(queue_mu);
				producer_error = true;
				producer_done = true;
				cv_not_empty.notify_all();
				return;
			}

			std::vector<std::uint8_t> compressed;
			bool use_compressed = rle_compress(raw, compressed);
			PreparedChunk item{};
			item.header.seq = htonl(seq);
			item.header.orig_size = htonl(raw_size);
			item.header.flags = htonl(use_compressed ? 1U : 0U);
			item.header.crc = htonl(crc32(raw.data(), raw.size()));
			item.payload = use_compressed ? std::move(compressed) : std::move(raw);
			item.header.wire_size = htonl(static_cast<std::uint32_t>(item.payload.size()));

			std::unique_lock<std::mutex> lock(queue_mu);
			cv_not_full.wait(lock, [&]() {
				return queue.size() < TCP_PIPELINE_QUEUE_DEPTH;
			});
			queue.push_back(std::move(item));
			lock.unlock();
			cv_not_empty.notify_one();
		}

		std::lock_guard<std::mutex> lock(queue_mu);
		producer_done = true;
		cv_not_empty.notify_all();
	});

	std::uint64_t raw_sent = 0;
	std::uint64_t wire_sent = 0;
	std::uint32_t sent_chunks = 0;

	while (true) {
		PreparedChunk item;
		{
			std::unique_lock<std::mutex> lock(queue_mu);
			cv_not_empty.wait(lock, [&]() {
				return !queue.empty() || producer_done;
			});
			if (queue.empty()) {
				break;
			}
			item = std::move(queue.front());
			queue.pop_front();
			cv_not_full.notify_one();
		}

		if (!send_all(sock, reinterpret_cast<const std::uint8_t*>(&item.header), sizeof(item.header)) ||
			!send_all(sock, item.payload.data(), item.payload.size())) {
			producer.join();
			std::cerr << "Failed sending optimized chunk stream\n";
			return false;
		}

		raw_sent += ntohl(item.header.orig_size);
		wire_sent += ntohl(item.header.wire_size);
		++sent_chunks;
		std::cout << "Optimized send chunks " << sent_chunks << "/" << needed_seq.size() << "\r" << std::flush;
	}

	producer.join();
	if (producer_error) {
		std::cerr << "Failed while preparing chunks\n";
		return false;
	}

	const double ratio = raw_sent == 0 ? 1.0 : static_cast<double>(wire_sent) / static_cast<double>(raw_sent);
	std::cout << "\nOptimized TCP transfer completed. raw=" << raw_sent
			  << " wire=" << wire_sent
			  << " ratio=" << ratio << "\n";
	return true;
}

bool receive_file_tcp(SocketHandle sock, const std::filesystem::path& output_dir) {
	FileHeader header{};
	if (!recv_all(sock, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
		std::cerr << "Failed to receive file header\n";
		return false;
	}

	if (ntohl(header.magic) != PROTOCOL_MAGIC || ntohl(header.version) != PROTOCOL_VERSION) {
		std::cerr << "Unsupported protocol\n";
		return false;
	}

	const std::uint32_t name_len = ntohl(header.name_len);
	const std::uint64_t file_size = net_to_host64(header.file_size);
	const std::uint32_t chunk_size = ntohl(header.chunk_size);

	if (name_len == 0 || name_len > 1024 || chunk_size == 0 || chunk_size > (4U * 1024U * 1024U)) {
		std::cerr << "Invalid header values\n";
		return false;
	}
	if (chunk_size != TCP_CHUNK_SIZE) {
		std::cerr << "Unsupported chunk size\n";
		return false;
	}

	std::string file_name(name_len, '\0');
	if (!recv_all(sock, reinterpret_cast<std::uint8_t*>(file_name.data()), file_name.size())) {
		std::cerr << "Failed to receive file name\n";
		return false;
	}

	std::filesystem::create_directories(output_dir);
	const auto output_file = output_dir / std::filesystem::path(file_name).filename();

	std::uint32_t chunk_count_be = 0;
	if (!recv_all(sock, reinterpret_cast<std::uint8_t*>(&chunk_count_be), sizeof(chunk_count_be))) {
		std::cerr << "Failed to receive chunk count\n";
		return false;
	}
	const std::uint32_t chunk_count = ntohl(chunk_count_be);
	if (chunk_count != chunks_for_size(file_size, chunk_size)) {
		std::cerr << "Invalid chunk count\n";
		return false;
	}

	std::vector<std::uint32_t> sender_crc(chunk_count, 0);
	for (std::uint32_t i = 0; i < chunk_count; ++i) {
		std::uint32_t be = 0;
		if (!recv_all(sock, reinterpret_cast<std::uint8_t*>(&be), sizeof(be))) {
			std::cerr << "Failed to receive sender fingerprint list\n";
			return false;
		}
		sender_crc[i] = ntohl(be);
	}

	std::vector<std::uint32_t> local_crc(chunk_count, 0);
	if (std::filesystem::exists(output_file) && std::filesystem::is_regular_file(output_file)) {
		const std::uint64_t existing_size = std::filesystem::file_size(output_file);
		const std::uint32_t existing_chunks = chunks_for_size(existing_size, chunk_size);
		std::ifstream existing(output_file, std::ios::binary);
		if (existing.is_open()) {
			std::vector<std::uint8_t> buf(chunk_size);
			for (std::uint32_t seq = 0; seq < std::min<std::uint32_t>(chunk_count, existing_chunks); ++seq) {
				const std::uint32_t expect = chunk_bytes_for_seq(file_size, chunk_size, seq);
				existing.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(expect));
				if (existing.gcount() != static_cast<std::streamsize>(expect)) {
					break;
				}
				local_crc[seq] = crc32(buf.data(), expect);
			}
		}
	}

	const std::uint32_t bitmap_size = (chunk_count + 7U) / 8U;
	std::vector<std::uint8_t> bitmap(bitmap_size, 0);
	std::uint32_t needed_count = 0;
	for (std::uint32_t seq = 0; seq < chunk_count; ++seq) {
		if (local_crc[seq] != sender_crc[seq]) {
			bit_set(bitmap, seq);
			++needed_count;
		}
	}

	const std::uint32_t bitmap_size_send_be = htonl(bitmap_size);
	if (!send_all(sock, reinterpret_cast<const std::uint8_t*>(&bitmap_size_send_be), sizeof(bitmap_size_send_be))) {
		std::cerr << "Failed to send bitmap size\n";
		return false;
	}
	if (bitmap_size > 0 && !send_all(sock, bitmap.data(), bitmap.size())) {
		std::cerr << "Failed to send bitmap\n";
		return false;
	}

	std::uint32_t sender_needed_be = 0;
	if (!recv_all(sock, reinterpret_cast<std::uint8_t*>(&sender_needed_be), sizeof(sender_needed_be))) {
		std::cerr << "Failed to receive sender needed chunk count\n";
		return false;
	}
	const std::uint32_t sender_needed = ntohl(sender_needed_be);
	if (sender_needed != needed_count) {
		std::cerr << "Sender/receiver delta mismatch\n";
		return false;
	}

	std::fstream out(output_file, std::ios::binary | std::ios::in | std::ios::out);
	if (!out.is_open()) {
		std::ofstream create(output_file, std::ios::binary | std::ios::trunc);
		create.close();
		out.open(output_file, std::ios::binary | std::ios::in | std::ios::out);
	}
	if (!out.is_open()) {
		std::cerr << "Failed to open output file: " << output_file << "\n";
		return false;
	}

	std::vector<std::uint8_t> wire_buf;
	std::vector<std::uint8_t> raw_buf;
	for (std::uint32_t i = 0; i < sender_needed; ++i) {
		TcpChunkPacketHeader packet{};
		if (!recv_all(sock, reinterpret_cast<std::uint8_t*>(&packet), sizeof(packet))) {
			std::cerr << "Failed to receive optimized chunk header\n";
			return false;
		}

		const std::uint32_t seq = ntohl(packet.seq);
		const std::uint32_t orig_size = ntohl(packet.orig_size);
		const std::uint32_t wire_size = ntohl(packet.wire_size);
		const std::uint32_t flags = ntohl(packet.flags);
		const std::uint32_t expected_crc = ntohl(packet.crc);

		if (seq >= chunk_count || orig_size != chunk_bytes_for_seq(file_size, chunk_size, seq) || wire_size == 0) {
			std::cerr << "Invalid optimized chunk metadata\n";
			return false;
		}

		wire_buf.resize(wire_size);
		if (!recv_all(sock, wire_buf.data(), wire_buf.size())) {
			std::cerr << "Failed to receive optimized chunk payload\n";
			return false;
		}

		if ((flags & 1U) != 0U) {
			if (!rle_decompress(wire_buf.data(), wire_buf.size(), orig_size, raw_buf)) {
				std::cerr << "Failed to decompress chunk seq=" << seq << "\n";
				return false;
			}
		} else {
			raw_buf.assign(wire_buf.begin(), wire_buf.end());
			if (raw_buf.size() != orig_size) {
				std::cerr << "Invalid raw chunk size\n";
				return false;
			}
		}

		if (crc32(raw_buf.data(), raw_buf.size()) != expected_crc) {
			std::cerr << "CRC mismatch on optimized chunk seq=" << seq << "\n";
			return false;
		}

		out.seekp(static_cast<std::streamoff>(static_cast<std::uint64_t>(seq) * chunk_size), std::ios::beg);
		out.write(reinterpret_cast<const char*>(raw_buf.data()), static_cast<std::streamsize>(raw_buf.size()));
		if (!out.good()) {
			std::cerr << "Failed writing optimized chunk\n";
			return false;
		}

		std::cout << "Optimized recv chunks " << (i + 1) << "/" << sender_needed << "\r" << std::flush;
	}

	out.flush();
	std::error_code ec;
	std::filesystem::resize_file(output_file, file_size, ec);
	if (ec) {
		std::cerr << "Failed to resize output file\n";
		return false;
	}

	std::cout << "\nSaved file to: " << output_file << "\n";
	return true;
}

std::vector<std::uint8_t> build_udp_start_payload(const std::filesystem::path& path,
												  std::uint64_t file_size,
												  std::uint32_t chunk_size,
												  std::uint32_t total_chunks) {
	const std::string name = file_name_only(path);
	if (name.empty() || name.size() > 1024) {
		return {};
	}

	std::vector<std::uint8_t> payload(sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t) + name.size());
	std::uint8_t* ptr = payload.data();

	const std::uint32_t name_len_be = htonl(static_cast<std::uint32_t>(name.size()));
	const std::uint64_t file_size_be = host_to_net64(file_size);
	const std::uint32_t chunk_size_be = htonl(chunk_size);
	const std::uint32_t total_chunks_be = htonl(total_chunks);

	std::memcpy(ptr, &name_len_be, sizeof(name_len_be));
	ptr += sizeof(name_len_be);
	std::memcpy(ptr, &file_size_be, sizeof(file_size_be));
	ptr += sizeof(file_size_be);
	std::memcpy(ptr, &chunk_size_be, sizeof(chunk_size_be));
	ptr += sizeof(chunk_size_be);
	std::memcpy(ptr, &total_chunks_be, sizeof(total_chunks_be));
	ptr += sizeof(total_chunks_be);
	std::memcpy(ptr, name.data(), name.size());
	return payload;
}

bool parse_udp_start_payload(const std::uint8_t* payload,
							 size_t size,
							 std::string& name,
							 std::uint64_t& file_size,
							 std::uint32_t& chunk_size,
							 std::uint32_t& total_chunks) {
	if (size < sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t)) {
		return false;
	}

	std::uint32_t name_len_be = 0;
	std::uint64_t file_size_be = 0;
	std::uint32_t chunk_size_be = 0;
	std::uint32_t total_chunks_be = 0;
	std::memcpy(&name_len_be, payload, sizeof(name_len_be));
	std::memcpy(&file_size_be, payload + sizeof(name_len_be), sizeof(file_size_be));
	std::memcpy(&chunk_size_be, payload + sizeof(name_len_be) + sizeof(file_size_be), sizeof(chunk_size_be));
	std::memcpy(&total_chunks_be, payload + sizeof(name_len_be) + sizeof(file_size_be) + sizeof(chunk_size_be), sizeof(total_chunks_be));

	const std::uint32_t name_len = ntohl(name_len_be);
	file_size = net_to_host64(file_size_be);
	chunk_size = ntohl(chunk_size_be);
	total_chunks = ntohl(total_chunks_be);

	if (name_len == 0 || size != sizeof(name_len_be) + sizeof(file_size_be) + sizeof(chunk_size_be) + sizeof(total_chunks_be) + name_len) {
		return false;
	}

	name.assign(reinterpret_cast<const char*>(payload + sizeof(name_len_be) + sizeof(file_size_be) + sizeof(chunk_size_be) + sizeof(total_chunks_be)), name_len);
	return true;
}

std::vector<std::uint8_t> read_udp_chunk(std::ifstream& in,
										 std::uint64_t offset,
										 std::uint32_t size,
										 std::uint32_t seq,
										 std::uint32_t total_chunks,
										 std::uint64_t session) {
	std::vector<std::uint8_t> chunk;
	if (!read_file_chunk_at(in, offset, size, chunk)) {
		return {};
	}

	UdpFrameHeader hdr{};
	hdr.magic = htonl(PROTOCOL_MAGIC);
	hdr.version = htonl(PROTOCOL_VERSION);
	hdr.type = htonl(static_cast<std::uint32_t>(UdpPacketType::Data));
	hdr.session = host_to_net64(session);
	hdr.seq = htonl(seq);
	hdr.total_chunks = htonl(total_chunks);
	hdr.payload_size = htonl(size);
	hdr.crc = htonl(crc32(chunk.data(), size));

	std::vector<std::uint8_t> packet(sizeof(hdr) + chunk.size());
	std::memcpy(packet.data(), &hdr, sizeof(hdr));
	std::memcpy(packet.data() + sizeof(hdr), chunk.data(), chunk.size());
	return packet;
}

int run_send_udp(const std::string& ip, int port, const std::filesystem::path& path) {
	if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
		std::cerr << "Invalid file path: " << path << "\n";
		return 1;
	}

	SocketHandle sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == kInvalidSocket) {
		std::cerr << "Failed to create UDP socket\n";
		return 1;
	}

	int reuse = 1;
	if (!set_socket_option_int(sock, SOL_SOCKET, SO_REUSEADDR, reuse) ||
		!set_socket_option_int(sock, SOL_SOCKET, SO_BROADCAST, 1) ||
		!set_recv_timeout(sock, SOCKET_TIMEOUT_MS) ||
		!set_send_timeout(sock, SOCKET_TIMEOUT_MS)) {
		std::cerr << "Failed to configure UDP socket\n";
		close_socket(sock);
		return 1;
	}

	sockaddr_in target{};
	if (!parse_ipv4(ip, target, port)) {
		std::cerr << "Invalid IPv4 address: " << ip << "\n";
		close_socket(sock);
		return 1;
	}

	const std::uint64_t file_size = std::filesystem::file_size(path);
	const std::uint32_t total_chunks = static_cast<std::uint32_t>((file_size + UDP_PAYLOAD_SIZE - 1U) / UDP_PAYLOAD_SIZE);
	const std::uint64_t session = (static_cast<std::uint64_t>(std::random_device{}()) << 32U) ^ static_cast<std::uint64_t>(std::random_device{}());

	const std::vector<std::uint8_t> start_payload = build_udp_start_payload(path, file_size, UDP_PAYLOAD_SIZE, total_chunks);
	if (start_payload.empty()) {
		std::cerr << "Invalid file name for UDP transfer\n";
		close_socket(sock);
		return 1;
	}

	const std::vector<std::uint8_t> start_frame = make_udp_frame(
		UdpPacketType::Start, session, 0, total_chunks,
		start_payload.data(), static_cast<std::uint32_t>(start_payload.size()));

	std::vector<std::uint8_t> recv_buffer(1500);
	sockaddr_in from{};
	size_t received = 0;
	bool started = false;

	for (int attempt = 0; attempt < 50 && !started; ++attempt) {
		if (!send_udp_frame(sock, target, start_frame)) {
			std::cerr << "Failed to send UDP start frame\n";
			close_socket(sock);
			return 1;
		}

		if (receive_udp_frame(sock, recv_buffer, from, received)) {
			UdpFrameHeader header{};
			const std::uint8_t* payload = nullptr;
			if (parse_udp_frame(recv_buffer.data(), received, header, payload) &&
				header.type == static_cast<std::uint32_t>(UdpPacketType::StartAck) &&
				header.session == session) {
				started = true;
				break;
			}
		}
	}

	if (!started) {
		std::cerr << "UDP receiver did not acknowledge start\n";
		close_socket(sock);
		return 1;
	}

	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		std::cerr << "Failed to open file for reading: " << path << "\n";
		close_socket(sock);
		return 1;
	}

	struct PendingPacket {
		std::vector<std::uint8_t> bytes;
		bool acked = false;
		std::chrono::steady_clock::time_point last_sent{};
	};

	std::map<std::uint32_t, PendingPacket> pending;
	std::uint32_t next_seq = 0;
	std::uint32_t acked_total = 0;
	std::uint32_t lowest_unacked = 0;

	auto send_chunk = [&](std::uint32_t seq) -> bool {
		const std::uint64_t offset = static_cast<std::uint64_t>(seq) * UDP_PAYLOAD_SIZE;
		const std::uint32_t size = static_cast<std::uint32_t>(std::min<std::uint64_t>(UDP_PAYLOAD_SIZE, file_size - offset));
		const std::vector<std::uint8_t> packet = read_udp_chunk(in, offset, size, seq, total_chunks, session);
		if (packet.empty()) {
			return false;
		}

		auto& slot = pending[seq];
		slot.bytes = packet;
		slot.acked = false;
		slot.last_sent = std::chrono::steady_clock::now();
		return send_udp_frame(sock, target, slot.bytes);
	};

	while (acked_total < total_chunks) {
		while (next_seq < total_chunks && pending.size() < UDP_WINDOW_SIZE) {
			if (!send_chunk(next_seq)) {
				std::cerr << "Failed to prepare UDP chunk " << next_seq << "\n";
				close_socket(sock);
				return 1;
			}
			++next_seq;
		}

		if (receive_udp_frame(sock, recv_buffer, from, received)) {
			UdpFrameHeader header{};
			const std::uint8_t* payload = nullptr;
			if (parse_udp_frame(recv_buffer.data(), received, header, payload) &&
				header.session == session &&
				header.type == static_cast<std::uint32_t>(UdpPacketType::DataAck) &&
				header.payload_size >= 1 &&
				payload[0] == 1) {
				auto it = pending.find(header.seq);
				if (it != pending.end() && !it->second.acked) {
					it->second.acked = true;
					++acked_total;
				}
			}
		}

		const auto now = std::chrono::steady_clock::now();
		for (auto& [seq, pkt] : pending) {
			if (!pkt.acked) {
				const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pkt.last_sent).count();
				if (elapsed >= UDP_RETRANSMIT_TIMEOUT_MS) {
					if (!send_udp_frame(sock, target, pkt.bytes)) {
						std::cerr << "Failed to retransmit UDP chunk " << seq << "\n";
						close_socket(sock);
						return 1;
					}
					pkt.last_sent = now;
				}
			}
		}

		while (!pending.empty()) {
			auto it = pending.find(lowest_unacked);
			if (it == pending.end() || !it->second.acked) {
				break;
			}
			pending.erase(it);
			++lowest_unacked;
		}
	}

	const std::vector<std::uint8_t> complete_frame = make_udp_frame(
		UdpPacketType::Complete, session, total_chunks, total_chunks, nullptr, 0);
	send_udp_frame(sock, target, complete_frame);

	std::cout << "UDP file transfer completed successfully.\n";
	close_socket(sock);
	return 0;
}

int run_recv_udp(int port, const std::filesystem::path& output_dir) {
	SocketHandle sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == kInvalidSocket) {
		std::cerr << "Failed to create UDP socket\n";
		return 1;
	}

	int reuse = 1;
	if (!set_socket_option_int(sock, SOL_SOCKET, SO_REUSEADDR, reuse) ||
		!set_socket_option_int(sock, SOL_SOCKET, SO_BROADCAST, 1) ||
		!set_recv_timeout(sock, SOCKET_TIMEOUT_MS)) {
		std::cerr << "Failed to configure UDP receiver\n";
		close_socket(sock);
		return 1;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
		std::cerr << "Failed to bind receiver on port " << port << "\n";
		close_socket(sock);
		return 1;
	}

	std::cout << "UDP receiver listening on " << port << "...\n";

	std::vector<std::uint8_t> buffer(1500);
	bool have_session = false;
	std::uint64_t session = 0;
	std::uint32_t total_chunks = 0;
	std::uint32_t chunk_size = UDP_PAYLOAD_SIZE;
	std::uint64_t file_size = 0;
	std::string file_name;
	std::ofstream out;
	std::map<std::uint32_t, std::vector<std::uint8_t>> buffered_chunks;
	std::uint32_t expected_seq = 0;

	while (true) {
		size_t received = 0;
		sockaddr_in from{};
		if (!receive_udp_frame(sock, buffer, from, received)) {
			continue;
		}

		UdpFrameHeader header{};
		const std::uint8_t* payload = nullptr;
		if (!parse_udp_frame(buffer.data(), received, header, payload)) {
			continue;
		}

		if (!have_session) {
			if (header.type != static_cast<std::uint32_t>(UdpPacketType::Start)) {
				continue;
			}

			if (!parse_udp_start_payload(payload, header.payload_size, file_name, file_size, chunk_size, total_chunks)) {
				continue;
			}

			session = header.session;
			have_session = true;
			expected_seq = 0;

			std::filesystem::create_directories(output_dir);
			const auto output_file = output_dir / std::filesystem::path(file_name).filename();
			out.open(output_file, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) {
				std::cerr << "Failed to open output file: " << output_file << "\n";
				close_socket(sock);
				return 1;
			}

			const std::uint8_t ok = 1;
			const auto ack = make_udp_frame(UdpPacketType::StartAck, session, 0, total_chunks, &ok, 1);
			send_udp_frame(sock, from, ack);
			std::cout << "Receiving " << file_name << " (" << file_size << " bytes)\n";
			continue;
		}

		if (header.session != session) {
			continue;
		}

		if (header.type == static_cast<std::uint32_t>(UdpPacketType::Data)) {
			const std::uint32_t seq = header.seq;
			const std::uint32_t size = header.payload_size;
			const std::uint32_t expected_crc = header.crc;

			const bool valid = size > 0 && size <= chunk_size && crc32(payload, size) == expected_crc;
			const std::uint8_t status = valid ? 1 : 0;
			const auto ack = make_udp_frame(UdpPacketType::DataAck, session, seq, total_chunks, &status, 1);
			send_udp_frame(sock, from, ack);

			if (!valid || seq < expected_seq || buffered_chunks.find(seq) != buffered_chunks.end()) {
				continue;
			}

			buffered_chunks.emplace(seq, std::vector<std::uint8_t>(payload, payload + size));

			while (true) {
				auto it = buffered_chunks.find(expected_seq);
				if (it == buffered_chunks.end()) {
					break;
				}
				out.write(reinterpret_cast<const char*>(it->second.data()), static_cast<std::streamsize>(it->second.size()));
				if (!out.good()) {
					std::cerr << "Failed writing UDP output file\n";
					close_socket(sock);
					return 1;
				}
				buffered_chunks.erase(it);
				++expected_seq;
			}

			if (expected_seq >= total_chunks) {
				const std::uint8_t done = 1;
				const auto complete_ack = make_udp_frame(UdpPacketType::CompleteAck, session, expected_seq, total_chunks, &done, 1);
				send_udp_frame(sock, from, complete_ack);
				std::cout << "Saved file to: " << (output_dir / std::filesystem::path(file_name).filename()) << "\n";
				close_socket(sock);
				return 0;
			}
		} else if (header.type == static_cast<std::uint32_t>(UdpPacketType::Complete)) {
			const std::uint8_t done = 1;
			const auto complete_ack = make_udp_frame(UdpPacketType::CompleteAck, session, header.seq, total_chunks, &done, 1);
			send_udp_frame(sock, from, complete_ack);
			if (expected_seq >= total_chunks) {
				std::cout << "Saved file to: " << (output_dir / std::filesystem::path(file_name).filename()) << "\n";
				close_socket(sock);
				return 0;
			}
		}
	}
}

TransportMode parse_transport_mode(int argc, char* argv[], int& index) {
	if (index < argc) {
		const std::string arg = argv[index];
		if (arg == "--udp") {
			++index;
			return TransportMode::Udp;
		}
		if (arg == "--tcp") {
			++index;
			return TransportMode::Tcp;
		}
	}
	return TransportMode::Tcp;
}

void print_usage() {
	std::cout << "Usage:\n"
			  << "  syncflow_transfer send [--tcp|--udp] <ip> [port] <file_path>\n"
			  << "  syncflow_transfer recv [--tcp|--udp] [port] [output_dir]\n"
			  << "Defaults: tcp mode, port=" << DEFAULT_TRANSFER_PORT << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
	if (!init_socket_runtime()) {
		std::cerr << "Socket runtime init failed\n";
		return 1;
	}

	if (argc < 2) {
		print_usage();
		shutdown_socket_runtime();
		return 1;
	}

	const std::string mode = argv[1];
	int index = 2;
	const TransportMode transport = parse_transport_mode(argc, argv, index);
	int rc = 1;

	if (mode == "recv") {
		int port = DEFAULT_TRANSFER_PORT;
		std::filesystem::path out_dir = "received";

		if (index < argc) {
			if (is_numeric(argv[index])) {
				if (!parse_port(argv[index], port)) {
					std::cerr << "Invalid port: " << argv[index] << "\n";
					shutdown_socket_runtime();
					return 1;
				}
				++index;
				if (index < argc) {
					out_dir = argv[index++];
				}
			} else {
				out_dir = argv[index++];
			}
		}

		if (transport == TransportMode::Udp) {
			rc = run_recv_udp(port, out_dir);
		} else {
			SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
			if (listener == kInvalidSocket) {
				std::cerr << "Failed to create TCP listener\n";
				shutdown_socket_runtime();
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
				shutdown_socket_runtime();
				return 1;
			}

			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = htonl(INADDR_ANY);
			addr.sin_port = htons(port);

			if (bind(listener, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
				std::cerr << "Failed to bind receiver on port " << port << "\n";
				close_socket(listener);
				shutdown_socket_runtime();
				return 1;
			}

			if (listen(listener, 8) < 0) {
				std::cerr << "Failed to listen\n";
				close_socket(listener);
				shutdown_socket_runtime();
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
				shutdown_socket_runtime();
				return 1;
			}

			close_socket(listener);
			const bool ok = receive_file_tcp(conn, out_dir);
			close_socket(conn);
			rc = ok ? 0 : 1;
		}
	} else if (mode == "send") {
		if (index >= argc) {
			print_usage();
			shutdown_socket_runtime();
			return 1;
		}

		const std::string ip = argv[index++];
		int port = DEFAULT_TRANSFER_PORT;
		std::filesystem::path file_path;

		if (index < argc && index + 1 < argc && is_numeric(argv[index])) {
			if (!parse_port(argv[index], port)) {
				std::cerr << "Invalid port: " << argv[index] << "\n";
				shutdown_socket_runtime();
				return 1;
			}
			++index;
		}

		if (index < argc) {
			file_path = argv[index++];
		} else {
			print_usage();
			shutdown_socket_runtime();
			return 1;
		}

		if (transport == TransportMode::Udp) {
			rc = run_send_udp(ip, port, file_path);
		} else {
			SocketHandle sock = socket(AF_INET, SOCK_STREAM, 0);
			if (sock == kInvalidSocket) {
				std::cerr << "Failed to create TCP socket\n";
				shutdown_socket_runtime();
				return 1;
			}

			sockaddr_in target{};
			if (!parse_ipv4(ip, target, port)) {
				std::cerr << "Invalid IPv4 address: " << ip << "\n";
				close_socket(sock);
				shutdown_socket_runtime();
				return 1;
			}

			if (connect(sock, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) < 0) {
				std::cerr << "Failed to connect to receiver\n";
				close_socket(sock);
				shutdown_socket_runtime();
				return 1;
			}

			const bool ok = send_file_tcp(sock, file_path);
			close_socket(sock);
			rc = ok ? 0 : 1;
		}
	} else {
		print_usage();
		rc = 1;
	}

	shutdown_socket_runtime();
	return rc;
}

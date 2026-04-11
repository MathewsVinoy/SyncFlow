// src/common/utils.cpp

#include <syncflow/common/utils.h>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <random>
#include <cstring>
#include <iomanip>
#include <ctime>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

namespace syncflow::utils {

// ============================================================================
// STRING UTILITIES
// ============================================================================

std::vector<std::string> split_string(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string trim(const std::string& str) {
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start)) {
        start++;
    }
    auto end = str.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    return std::string(start, end + 1);
}

std::string to_lowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string to_uppercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

bool starts_with(const std::string& str, const std::string& prefix) {
    if (prefix.size() > str.size()) return false;
    return str.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string replace_all(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

// ============================================================================
// CRC32 IMPLEMENTATION
// ============================================================================

static constexpr uint32_t CRC32_TABLE[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71642, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa44e5d6, 0x8d079fd5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcf30da99, 0xb8a9470f,
    0x2c855cb2, 0x5bdeae24, 0xc2d7d09e, 0xb5d0cf31,
    0x2b6e2032, 0x5c4494a4, 0xcf3d6f1e, 0xb8a7a988,
};

uint32_t crc32(const std::vector<uint8_t>& data) {
    return crc32(data.data(), data.size());
}

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        crc = CRC32_TABLE[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }
    return crc ^ 0xffffffff;
}

// ============================================================================
// HASH IMPLEMENTATIONS (Stubs - real implementations use OpenSSL or similar)
// ============================================================================

std::string sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

std::string sha256(const uint8_t* data, size_t size) {
    // Note: This is a stub. In production, use OpenSSL or similar library
    // For now, return hex representation of first 32 bytes of data
    std::stringstream ss;
    for (size_t i = 0; i < std::min(size, size_t(32)); ++i) {
        ss << std::hex << (int)data[i];
    }
    return ss.str();
}

std::string sha256_file(const std::string& file_path) {
    // Stub implementation
    return "sha256_stub_" + file_path;
}

std::string md5(const std::vector<uint8_t>& data) {
    // Stub implementation
    return "md5_stub";
}

// ============================================================================
// BINARY SERIALIZATION
// ============================================================================

void BinaryWriter::write_uint8(uint8_t value) {
    buffer_.push_back(value);
}

void BinaryWriter::write_uint16(uint16_t value) {
    uint16_t net_value = htons(value);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(&net_value);
    buffer_.insert(buffer_.end(), ptr, ptr + 2);
}

void BinaryWriter::write_uint32(uint32_t value) {
    uint32_t net_value = htonl(value);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(&net_value);
    buffer_.insert(buffer_.end(), ptr, ptr + 4);
}

void BinaryWriter::write_uint64(uint64_t value) {
    // Network byte order for 64-bit
    uint64_t net_value = ((uint64_t)htonl(value & 0xffffffff) << 32) |
                         (uint64_t)htonl(value >> 32);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(&net_value);
    buffer_.insert(buffer_.end(), ptr, ptr + 8);
}

void BinaryWriter::write_string(const std::string& value) {
    write_uint32(static_cast<uint32_t>(value.size()));
    write_bytes(reinterpret_cast<const uint8_t*>(value.c_str()), value.size());
}

void BinaryWriter::write_bytes(const std::vector<uint8_t>& data) {
    write_bytes(data.data(), data.size());
}

void BinaryWriter::write_bytes(const uint8_t* data, size_t size) {
    buffer_.insert(buffer_.end(), data, data + size);
}

std::vector<uint8_t> BinaryWriter::get_buffer() const {
    return buffer_;
}

void BinaryWriter::clear() {
    buffer_.clear();
}

// BinaryReader implementation

BinaryReader::BinaryReader(const std::vector<uint8_t>& data)
    : data_(data.data()), size_(data.size()), position_(0), external_data_(false) {}

BinaryReader::BinaryReader(const uint8_t* data, size_t size)
    : data_(data), size_(size), position_(0), external_data_(true) {}

bool BinaryReader::read_uint8(uint8_t& value) {
    if (position_ >= size_) return false;
    value = data_[position_++];
    return true;
}

bool BinaryReader::read_uint16(uint16_t& value) {
    if (position_ + 2 > size_) return false;
    uint16_t net_value;
    std::memcpy(&net_value, &data_[position_], 2);
    value = ntohs(net_value);
    position_ += 2;
    return true;
}

bool BinaryReader::read_uint32(uint32_t& value) {
    if (position_ + 4 > size_) return false;
    uint32_t net_value;
    std::memcpy(&net_value, &data_[position_], 4);
    value = ntohl(net_value);
    position_ += 4;
    return true;
}

bool BinaryReader::read_uint64(uint64_t& value) {
    if (position_ + 8 > size_) return false;
    uint64_t net_value;
    std::memcpy(&net_value, &data_[position_], 8);
    value = ((uint64_t)ntohl(net_value & 0xffffffff) << 32) |
            (uint64_t)ntohl(net_value >> 32);
    position_ += 8;
    return true;
}

bool BinaryReader::read_string(std::string& value) {
    uint32_t length;
    if (!read_uint32(length)) return false;
    if (position_ + length > size_) return false;
    value = std::string(reinterpret_cast<const char*>(&data_[position_]), length);
    position_ += length;
    return true;
}

bool BinaryReader::read_bytes(std::vector<uint8_t>& data, size_t size) {
    if (position_ + size > size_) return false;
    data.insert(data.end(), &data_[position_], &data_[position_ + size]);
    position_ += size;
    return true;
}

size_t BinaryReader::remaining() const {
    return size_ - position_;
}

bool BinaryReader::is_valid() const {
    return data_ != nullptr && size_ > 0;
}

void BinaryReader::reset() {
    position_ = 0;
}

// ============================================================================
// TIME UTILITIES
// ============================================================================

uint64_t get_current_timestamp_ms() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

uint64_t get_current_timestamp_us() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

std::string format_timestamp(uint64_t timestamp_ms) {
    auto duration = std::chrono::milliseconds(timestamp_ms);
    auto tp = std::chrono::system_clock::time_point(duration);
    auto time = std::chrono::system_clock::to_time_t(tp);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ============================================================================
// FILE UTILITIES
// ============================================================================

std::string file_hash(const std::string& file_path) {
    // Stub implementation
    return "file_hash_stub";
}

uint64_t get_file_size(const std::string& file_path) {
    // Implementation depends on platform
    return 0; // Stub
}

bool is_absolute_path(const std::string& path) {
    #ifdef _WIN32
        return path.size() >= 3 && path[1] == ':' && path[2] == '\\';
    #else
        return !path.empty() && path[0] == '/';
    #endif
}

std::string normalize_path(const std::string& path) {
    std::string result = path;
    #ifdef _WIN32
        std::replace(result.begin(), result.end(), '/', '\\');
    #else
        std::replace(result.begin(), result.end(), '\\', '/');
    #endif
    return result;
}

// ============================================================================
// COMPRESSION (Stubs)
// ============================================================================

bool compress_data(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    // Stub: In production, use zlib or zstd
    output = input;
    return true;
}

bool decompress_data(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    // Stub: In production, use zlib or zstd
    output = input;
    return true;
}

// ============================================================================
// UUID/ID GENERATION
// ============================================================================

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    const char hex_chars[] = "0123456789abcdef";
    std::string uuid;
    
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            uuid += '-';
        } else {
            uuid += hex_chars[dis(gen)];
        }
    }
    
    return uuid;
}

std::string generate_session_id() {
    return generate_uuid();
}

} // namespace syncflow::utils

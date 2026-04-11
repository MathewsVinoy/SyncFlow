// include/syncflow/common/utils.h
// Utility functions and helpers

#ifndef SYNCFLOW_UTILS_H
#define SYNCFLOW_UTILS_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace syncflow::utils {

// ============================================================================
// STRING UTILITIES
// ============================================================================

std::vector<std::string> split_string(const std::string& str, char delimiter);
std::string trim(const std::string& str);
std::string to_lowercase(const std::string& str);
std::string to_uppercase(const std::string& str);
bool starts_with(const std::string& str, const std::string& prefix);
bool ends_with(const std::string& str, const std::string& suffix);
std::string replace_all(const std::string& str, const std::string& from, const std::string& to);

// ============================================================================
// HASH AND CHECKSUM
// ============================================================================

// CRC32 checksum
uint32_t crc32(const std::vector<uint8_t>& data);
uint32_t crc32(const uint8_t* data, size_t size);

// SHA256 hash
std::string sha256(const std::vector<uint8_t>& data);
std::string sha256(const uint8_t* data, size_t size);
std::string sha256_file(const std::string& file_path);

// MD5 hash for compatibility
std::string md5(const std::vector<uint8_t>& data);

// ============================================================================
// BINARY SERIALIZATION
// ============================================================================

class BinaryWriter {
public:
    void write_uint8(uint8_t value);
    void write_uint16(uint16_t value);
    void write_uint32(uint32_t value);
    void write_uint64(uint64_t value);
    void write_string(const std::string& value);
    void write_bytes(const std::vector<uint8_t>& data);
    void write_bytes(const uint8_t* data, size_t size);
    
    std::vector<uint8_t> get_buffer() const;
    void clear();
    
private:
    std::vector<uint8_t> buffer_;
};

class BinaryReader {
public:
    explicit BinaryReader(const std::vector<uint8_t>& data);
    explicit BinaryReader(const uint8_t* data, size_t size);
    
    bool read_uint8(uint8_t& value);
    bool read_uint16(uint16_t& value);
    bool read_uint32(uint32_t& value);
    bool read_uint64(uint64_t& value);
    bool read_string(std::string& value);
    bool read_bytes(std::vector<uint8_t>& data, size_t size);
    
    size_t remaining() const;
    bool is_valid() const;
    void reset();
    
private:
    const uint8_t* data_;
    size_t size_;
    size_t position_;
    bool external_data_;
};

// ============================================================================
// TIME UTILITIES
// ============================================================================

uint64_t get_current_timestamp_ms();
uint64_t get_current_timestamp_us();
std::string format_timestamp(uint64_t timestamp_ms);

// ============================================================================
// FILE UTILITIES
// ============================================================================

// Calculate file hash
std::string file_hash(const std::string& file_path);

// Get file size
uint64_t get_file_size(const std::string& file_path);

// Check if path is absolute
bool is_absolute_path(const std::string& path);

// Normalize path separators
std::string normalize_path(const std::string& path);

// ============================================================================
// COMPRESSION
// ============================================================================

// Simple compression (can use zlib or zstd)
bool compress_data(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
bool decompress_data(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);

// ============================================================================
// UUID/ID GENERATION
// ============================================================================

std::string generate_uuid();
std::string generate_session_id();

} // namespace syncflow::utils

#endif // SYNCFLOW_UTILS_H

#ifndef SYNCFLOW_COMMON_UTILS_H
#define SYNCFLOW_COMMON_UTILS_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace syncflow::utils {

inline std::uint16_t host_to_be16(std::uint16_t value) {
#if defined(_WIN32)
    return static_cast<std::uint16_t>((value >> 8U) | (value << 8U));
#else
    return htons(value);
#endif
}

inline std::uint32_t host_to_be32(std::uint32_t value) {
#if defined(_WIN32)
    return ((value & 0x000000FFU) << 24U) |
           ((value & 0x0000FF00U) << 8U) |
           ((value & 0x00FF0000U) >> 8U) |
           ((value & 0xFF000000U) >> 24U);
#else
    return htonl(value);
#endif
}

inline std::uint16_t be_to_host16(std::uint16_t value) {
#if defined(_WIN32)
    return static_cast<std::uint16_t>((value >> 8U) | (value << 8U));
#else
    return ntohs(value);
#endif
}

inline std::uint32_t be_to_host32(std::uint32_t value) {
#if defined(_WIN32)
    return ((value & 0x000000FFU) << 24U) |
           ((value & 0x0000FF00U) << 8U) |
           ((value & 0x00FF0000U) >> 8U) |
           ((value & 0xFF000000U) >> 24U);
#else
    return ntohl(value);
#endif
}

class BinaryWriter {
public:
    void write_uint8(std::uint8_t value) {
        buffer_.push_back(value);
    }

    void write_uint16(std::uint16_t value) {
        const std::uint16_t v = host_to_be16(value);
        append_bytes(&v, sizeof(v));
    }

    void write_uint32(std::uint32_t value) {
        const std::uint32_t v = host_to_be32(value);
        append_bytes(&v, sizeof(v));
    }

    void write_string(const std::string& value) {
        write_uint32(static_cast<std::uint32_t>(value.size()));
        append_bytes(value.data(), value.size());
    }

    const std::vector<std::uint8_t>& get_buffer() const {
        return buffer_;
    }

private:
    void append_bytes(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + size);
    }

    std::vector<std::uint8_t> buffer_;
};

class BinaryReader {
public:
    BinaryReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size), offset_(0) {}

    bool read_uint8(std::uint8_t& value) {
        if (offset_ + 1 > size_) {
            return false;
        }
        value = data_[offset_++];
        return true;
    }

    bool read_uint16(std::uint16_t& value) {
        std::uint16_t temp = 0;
        if (!read_raw(&temp, sizeof(temp))) {
            return false;
        }
        value = be_to_host16(temp);
        return true;
    }

    bool read_uint32(std::uint32_t& value) {
        std::uint32_t temp = 0;
        if (!read_raw(&temp, sizeof(temp))) {
            return false;
        }
        value = be_to_host32(temp);
        return true;
    }

    bool read_string(std::string& value) {
        std::uint32_t len = 0;
        if (!read_uint32(len) || offset_ + len > size_) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(data_ + offset_), len);
        offset_ += len;
        return true;
    }

private:
    bool read_raw(void* out, std::size_t size) {
        if (offset_ + size > size_) {
            return false;
        }
        std::memcpy(out, data_ + offset_, size);
        offset_ += size;
        return true;
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_;
};

} // namespace syncflow::utils

#endif // SYNCFLOW_COMMON_UTILS_H

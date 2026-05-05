#include "syncflow/networking/file_transfer.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace syncflow::file_transfer {

namespace {

// Helper to send all data reliably
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

// Helper to receive exact number of bytes
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

// Message format: [magic:16][version:2][type:1][payload_size:4][payload:N]
bool send_message_impl(int fd, MessageType type, const void* payload, std::size_t payload_size) {
    // Send header
    if (!send_all(fd, PROTOCOL_MAGIC, 16)) {
        return false;
    }

    std::uint16_t version = PROTOCOL_VERSION;
    if (!send_all(fd, &version, sizeof(version))) {
        return false;
    }

    std::uint8_t type_byte = static_cast<std::uint8_t>(type);
    if (!send_all(fd, &type_byte, sizeof(type_byte))) {
        return false;
    }

    std::uint32_t size = static_cast<std::uint32_t>(payload_size);
    if (!send_all(fd, &size, sizeof(size))) {
        return false;
    }

    // Send payload
    if (payload_size > 0) {
        if (!send_all(fd, payload, payload_size)) {
            return false;
        }
    }

    return true;
}

bool receive_message_impl(int fd, MessageType& type, std::vector<char>& payload) {
    // Receive header
    char magic[16];
    if (!recv_exact(fd, magic, 16)) {
        return false;
    }

    if (std::memcmp(magic, PROTOCOL_MAGIC, 16) != 0) {
        return false;
    }

    std::uint16_t version;
    if (!recv_exact(fd, &version, sizeof(version))) {
        return false;
    }

    if (version != PROTOCOL_VERSION) {
        return false;
    }

    std::uint8_t type_byte;
    if (!recv_exact(fd, &type_byte, sizeof(type_byte))) {
        return false;
    }
    type = static_cast<MessageType>(type_byte);

    std::uint32_t payload_size;
    if (!recv_exact(fd, &payload_size, sizeof(payload_size))) {
        return false;
    }

    // Receive payload
    payload.resize(payload_size);
    if (payload_size > 0) {
        if (!recv_exact(fd, payload.data(), payload_size)) {
            return false;
        }
    }

    return true;
}

}  // namespace

// FileMetadata serialization
std::string FileMetadata::serialize() const {
    std::ostringstream oss;
    oss << relative_path.size() << "|" << relative_path << "|" << file_size << "|" << permissions;
    return oss.str();
}

FileMetadata FileMetadata::deserialize(const std::string& data) {
    FileMetadata result;
    std::istringstream iss(data);
    std::string size_str;
    std::getline(iss, size_str, '|');

    const std::size_t path_len = std::stoul(size_str);
    result.relative_path.resize(path_len);
    iss.read(result.relative_path.data(), static_cast<std::streamsize>(path_len));
    iss.ignore(1, '|');  // Skip pipe

    std::string size_val;
    std::getline(iss, size_val, '|');
    result.file_size = std::stoull(size_val);

    std::string perms;
    std::getline(iss, perms, '|');
    result.permissions = std::stoul(perms);

    return result;
}

// TransferMetadata serialization
std::string TransferMetadata::serialize() const {
    std::ostringstream oss;
    oss << magic << "|" << protocol_version << "|" << source_base_path.size() << "|"
        << source_base_path << "|" << file_count << "|" << total_size;
    return oss.str();
}

TransferMetadata TransferMetadata::deserialize(const std::string& data) {
    TransferMetadata result;
    std::istringstream iss(data);

    std::getline(iss, result.magic, '|');

    std::string version_str;
    std::getline(iss, version_str, '|');
    result.protocol_version = std::stoul(version_str);

    std::string path_len_str;
    std::getline(iss, path_len_str, '|');
    const std::size_t path_len = std::stoul(path_len_str);

    result.source_base_path.resize(path_len);
    iss.read(result.source_base_path.data(), static_cast<std::streamsize>(path_len));
    iss.ignore(1, '|');  // Skip pipe

    std::string file_count_str;
    std::getline(iss, file_count_str, '|');
    result.file_count = std::stoul(file_count_str);

    std::string total_size_str;
    std::getline(iss, total_size_str);
    result.total_size = std::stoull(total_size_str);

    return result;
}

// FolderScanner implementation
FolderScanner::ScanResult FolderScanner::scan_folder(const std::filesystem::path& folder_path) {
    ScanResult result;

    if (!std::filesystem::exists(folder_path) || !std::filesystem::is_directory(folder_path)) {
        return result;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder_path)) {
        if (std::filesystem::is_regular_file(entry)) {
            const auto relative_path = std::filesystem::relative(entry.path(), folder_path);
            const auto file_size = std::filesystem::file_size(entry);

            FileMetadata metadata;
            metadata.relative_path = relative_path.string();
            metadata.file_size = file_size;

            // Get file permissions
            const auto status = std::filesystem::status(entry);
            metadata.permissions = static_cast<std::uint32_t>(status.permissions());

            result.files.push_back(metadata);
            result.total_size += file_size;
        }
    }

    result.total_files = static_cast<std::uint32_t>(result.files.size());
    return result;
}

bool FolderScanner::validate_safe_path(const std::filesystem::path& base_path,
                                       const std::filesystem::path& relative_path) {
    if (relative_path.is_absolute()) {
        return false;
    }

    // Check for directory traversal attempts
    if (relative_path.string().find("..") != std::string::npos) {
        return false;
    }

    // Ensure the resolved path is within base_path
    const auto resolved = std::filesystem::canonical(base_path / relative_path);
    const auto base_canonical = std::filesystem::canonical(base_path);

    return resolved.string().find(base_canonical.string()) == 0;
}

// FileTransferHelper implementation
bool FileTransferHelper::send_message(int fd, MessageType type, const std::string& payload) {
    return send_message_impl(fd, type, payload.data(), payload.size());
}

bool FileTransferHelper::send_message_binary(int fd, MessageType type, const std::vector<char>& payload) {
    return send_message_impl(fd, type, payload.data(), payload.size());
}

bool FileTransferHelper::receive_message(int fd, MessageType& type, std::string& payload) {
    std::vector<char> buffer;
    if (!receive_message_impl(fd, type, buffer)) {
        return false;
    }
    payload.assign(buffer.begin(), buffer.end());
    return true;
}

bool FileTransferHelper::receive_message_binary(int fd, MessageType& type, std::vector<char>& payload) {
    return receive_message_impl(fd, type, payload);
}

bool FileTransferHelper::send_folder(int fd, const std::filesystem::path& source_folder,
                                     std::uint64_t& bytes_sent) {
    bytes_sent = 0;

    // Scan folder
    const auto scan_result = FolderScanner::scan_folder(source_folder);

    // Send metadata
    TransferMetadata metadata;
    metadata.source_base_path = source_folder.filename().string();
    metadata.file_count = scan_result.total_files;
    metadata.total_size = scan_result.total_size;

    const auto metadata_str = metadata.serialize();
    if (!send_message(fd, MessageType::METADATA_START, metadata_str)) {
        return false;
    }
    bytes_sent += 16 + 2 + 1 + 4 + metadata_str.size();

    // Send file metadata list
    for (const auto& file_meta : scan_result.files) {
        const auto file_meta_str = file_meta.serialize();
        if (!send_message(fd, MessageType::METADATA_START, file_meta_str)) {
            return false;
        }
        bytes_sent += 16 + 2 + 1 + 4 + file_meta_str.size();
    }

    // Send each file
    for (const auto& file_meta : scan_result.files) {
        const auto file_path = source_folder / file_meta.relative_path;
        std::uint64_t file_bytes_sent = 0;

        if (!send_file(fd, file_path, file_meta.relative_path, file_bytes_sent)) {
            return false;
        }
        bytes_sent += file_bytes_sent;
    }

    // Send transfer complete message
    if (!send_message(fd, MessageType::TRANSFER_COMPLETE, "")) {
        return false;
    }
    bytes_sent += 16 + 2 + 1 + 4;

    return true;
}

bool FileTransferHelper::send_file(int fd, const std::filesystem::path& file_path,
                                   const std::filesystem::path& relative_path,
                                   std::uint64_t& bytes_sent) {
    bytes_sent = 0;

    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        return false;
    }

    // Send file header with relative path
    std::string file_header = relative_path.string();
    if (!send_message(fd, MessageType::FILE_CHUNK, file_header)) {
        return false;
    }
    bytes_sent += 16 + 2 + 1 + 4 + file_header.size();

    // Send file in chunks
    std::vector<char> chunk(CHUNK_SIZE);
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(CHUNK_SIZE));
        const std::streamsize got = input.gcount();

        if (got > 0) {
            std::vector<char> chunk_data(chunk.begin(), chunk.begin() + got);
            if (!send_message_binary(fd, MessageType::FILE_CHUNK, chunk_data)) {
                return false;
            }
            bytes_sent += 16 + 2 + 1 + 4 + got;
        }
    }

    // Send file complete marker
    if (!send_message(fd, MessageType::FILE_COMPLETE, relative_path.string())) {
        return false;
    }
    bytes_sent += 16 + 2 + 1 + 4 + relative_path.string().size();

    return true;
}

bool FileTransferHelper::receive_folder(int fd, const std::filesystem::path& output_folder,
                                        std::uint64_t& bytes_received) {
    bytes_received = 0;

    // Create output folder
    std::error_code ec;
    std::filesystem::create_directories(output_folder, ec);
    if (ec) {
        return false;
    }

    // Receive metadata
    MessageType type;
    std::string payload;

    if (!receive_message(fd, type, payload)) {
        return false;
    }
    bytes_received += 16 + 2 + 1 + 4 + payload.size();

    if (type != MessageType::METADATA_START) {
        return false;
    }

    const auto transfer_metadata = TransferMetadata::deserialize(payload);

    std::string current_file_path;
    std::ofstream current_file;

    while (true) {
        if (!receive_message(fd, type, payload)) {
            return false;
        }
        bytes_received += 16 + 2 + 1 + 4 + payload.size();

        switch (type) {
        case MessageType::METADATA_START: {
            // File metadata
            auto file_meta = FileMetadata::deserialize(payload);
            if (!FolderScanner::validate_safe_path(output_folder, file_meta.relative_path)) {
                return false;
            }
            current_file_path = (output_folder / file_meta.relative_path).string();
            break;
        }
        case MessageType::FILE_CHUNK: {
            if (current_file_path.empty()) {
                // First chunk contains the file path
                current_file_path = (output_folder / payload).string();
                std::error_code dir_ec;
                std::filesystem::create_directories(
                    std::filesystem::path(current_file_path).parent_path(), dir_ec);
                if (dir_ec) {
                    return false;
                }
                current_file.open(current_file_path, std::ios::binary);
                if (!current_file) {
                    return false;
                }
            } else {
                // Actual file data
                current_file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            }
            break;
        }
        case MessageType::FILE_COMPLETE: {
            if (current_file.is_open()) {
                current_file.close();
            }
            current_file_path.clear();
            break;
        }
        case MessageType::TRANSFER_COMPLETE: {
            if (current_file.is_open()) {
                current_file.close();
            }
            return true;
        }
        case MessageType::ERROR:
            return false;
        }
    }
}

bool FileTransferHelper::receive_file(int fd, const std::filesystem::path& output_path,
                                      std::uint64_t expected_size, std::uint64_t& bytes_received) {
    bytes_received = 0;

    // Create parent directories
    std::error_code ec;
    std::filesystem::create_directories(output_path.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        return false;
    }

    std::uint64_t total_received = 0;

    while (total_received < expected_size) {
        MessageType type;
        std::vector<char> payload;
        if (!receive_message_binary(fd, type, payload)) {
            return false;
        }
        bytes_received += 16 + 2 + 1 + 4 + payload.size();

        if (type != MessageType::FILE_CHUNK) {
            return false;
        }

        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        total_received += payload.size();
    }

    output.close();
    return true;
}

}  // namespace syncflow::file_transfer

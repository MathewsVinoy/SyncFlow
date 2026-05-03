#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace syncflow::file_transfer {

// Constants
constexpr std::uint16_t PROTOCOL_VERSION = 1;
constexpr std::size_t CHUNK_SIZE = 8192;  // 8 KB chunks for reliable transfer
constexpr char PROTOCOL_MAGIC[] = "SYNCFLOW_TRANSFER";

// Message types
enum class MessageType : std::uint8_t {
    METADATA_START = 0x01,      // Initiates transfer with file list and sizes
    FILE_CHUNK = 0x02,          // Transmits a chunk of file data
    FILE_COMPLETE = 0x03,       // Indicates completion of a file
    TRANSFER_COMPLETE = 0x04,   // Indicates all files transferred
    ERROR = 0x05                // Error occurred during transfer
};

// File metadata
struct FileMetadata {
    std::string relative_path;   // e.g., "subdir/file.txt"
    std::uint64_t file_size;     // Total size in bytes
    std::uint32_t permissions;   // Unix-style permissions

    std::string serialize() const;
    static FileMetadata deserialize(const std::string& data);
};

// Transfer session metadata
struct TransferMetadata {
    std::string magic = PROTOCOL_MAGIC;
    std::uint16_t protocol_version = PROTOCOL_VERSION;
    std::string source_base_path;  // Base folder being sent
    std::uint32_t file_count = 0;
    std::uint64_t total_size = 0;

    std::string serialize() const;
    static TransferMetadata deserialize(const std::string& data);
};

// Utilities for folder scanning
class FolderScanner {
public:
    struct ScanResult {
        std::vector<FileMetadata> files;
        std::uint32_t total_files = 0;
        std::uint64_t total_size = 0;
    };

    /**
     * Recursively scans a folder and returns metadata for all files
     * @param folder_path Root folder to scan
     * @return ScanResult with all files and their metadata
     */
    static ScanResult scan_folder(const std::filesystem::path& folder_path);

    /**
     * Validates that a path is safe to extract (prevents directory traversal)
     * @param base_path Root directory for extraction
     * @param relative_path Relative path to validate
     * @return true if path is safe, false otherwise
     */
    static bool validate_safe_path(const std::filesystem::path& base_path,
                                   const std::filesystem::path& relative_path);
};

// Utilities for file transfer
class FileTransferHelper {
public:
    /**
     * Sends transfer metadata followed by all files in chunks
     * @param fd Socket file descriptor
     * @param source_folder Folder to transfer
     * @param bytes_sent Output parameter for total bytes sent
     * @return true if successful, false otherwise
     */
    static bool send_folder(int fd, const std::filesystem::path& source_folder,
                           std::uint64_t& bytes_sent);

    /**
     * Sends a single file in chunks
     * @param fd Socket file descriptor
     * @param file_path Path to file to send
     * @param relative_path Relative path to use in protocol
     * @param bytes_sent Output parameter for bytes sent
     * @return true if successful, false otherwise
     */
    static bool send_file(int fd, const std::filesystem::path& file_path,
                         const std::filesystem::path& relative_path,
                         std::uint64_t& bytes_sent);

    /**
     * Receives all files from a transfer session
     * @param fd Socket file descriptor
     * @param output_folder Folder to extract files to
     * @param bytes_received Output parameter for total bytes received
     * @return true if successful, false otherwise
     */
    static bool receive_folder(int fd, const std::filesystem::path& output_folder,
                              std::uint64_t& bytes_received);

    /**
     * Receives a single file in chunks
     * @param fd Socket file descriptor
     * @param output_path Path where file will be written
     * @param expected_size Expected file size
     * @param bytes_received Output parameter for bytes received
     * @return true if successful, false otherwise
     */
    static bool receive_file(int fd, const std::filesystem::path& output_path,
                            std::uint64_t expected_size, std::uint64_t& bytes_received);

private:
    // Helper functions for message framing
    static bool send_message(int fd, MessageType type, const std::string& payload);
    static bool send_message_binary(int fd, MessageType type, const std::vector<char>& payload);
    static bool receive_message(int fd, MessageType& type, std::string& payload);
    static bool receive_message_binary(int fd, MessageType& type, std::vector<char>& payload);
};

}  // namespace syncflow::file_transfer

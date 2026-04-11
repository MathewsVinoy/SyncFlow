// include/syncflow/transfer/transfer.h
// File transfer engine

#ifndef SYNCFLOW_TRANSFER_H
#define SYNCFLOW_TRANSFER_H

#include <syncflow/types.h>
#include <vector>
#include <memory>
#include <mutex>
#include <map>

namespace syncflow::transfer {

/**
 * Handles individual file transfer with resume capability
 */
class FileTransfer {
public:
    FileTransfer(const SessionID& session_id, const std::string& file_path,
                 uint64_t total_size, const DeviceID& remote_device);
    
    const SessionID& get_session_id() const;
    const std::string& get_file_path() const;
    uint64_t get_total_size() const;
    uint64_t get_transferred_bytes() const;
    TransferState get_state() const;
    
    bool add_chunk(const ChunkInfo& chunk_info, const std::vector<uint8_t>& data);
    bool pause();
    bool resume();
    bool cancel();
    bool is_complete() const;
    
private:
    SessionID session_id_;
    std::string file_path_;
    uint64_t total_size_;
    uint64_t transferred_bytes_;
    DeviceID remote_device_;
    TransferState state_;
    std::vector<bool> received_chunks_;  // Track which chunks received
    mutable std::mutex state_mutex_;
};

/**
 * Transfer protocol handler (binary protocol over TCP)
 */
class TransferProtocol {
public:
    // Protocol message types
    enum MessageType : uint8_t {
        HANDSHAKE_REQ = 0x01,
        HANDSHAKE_RESP = 0x02,
        FILE_OFFER = 0x03,
        FILE_ACCEPT = 0x04,
        FILE_REJECT = 0x05,
        CHUNK_REQUEST = 0x06,
        CHUNK_DATA = 0x07,
        CHUNK_ACK = 0x08,
        TRANSFER_COMPLETE = 0x09,
        ERROR = 0x0A,
    };
    
    // Serialization helpers
    static std::vector<uint8_t> encode_handshake(const DeviceInfo& local_info);
    static std::vector<uint8_t> encode_file_offer(const FileMetadata& file, const std::string& dest_path);
    static std::vector<uint8_t> encode_chunk_data(const ChunkInfo& chunk, const std::vector<uint8_t>& data);
    static std::vector<uint8_t> encode_transfer_complete(const SessionID& session_id);
    
    // Deserialization helpers
    static bool decode_handshake(const std::vector<uint8_t>& data, DeviceInfo& info);
    static bool decode_file_offer(const std::vector<uint8_t>& data, FileMetadata& file, std::string& dest_path);
    static bool decode_chunk_data(const std::vector<uint8_t>& data, ChunkInfo& chunk, std::vector<uint8_t>& chunk_data);
};

/**
 * Manages multiple concurrent transfers
 */
class TransferManager {
public:
    static TransferManager& instance();
    
    // Transfer control
    bool start_send(const std::string& file_path, const DeviceID& device_id,
                    const std::string& remote_path, OnTransferProgress progress_cb,
                    OnTransferComplete complete_cb);
    
    bool start_receive(const std::string& file_path, uint64_t file_size,
                       const DeviceID& device_id, OnTransferProgress progress_cb,
                       OnTransferComplete complete_cb);
    
    bool pause_transfer(const SessionID& session_id);
    bool resume_transfer(const SessionID& session_id);
    bool cancel_transfer(const SessionID& session_id);
    
    // Query transfers
    std::shared_ptr<FileTransfer> get_transfer(const SessionID& session_id);
    std::vector<std::shared_ptr<FileTransfer>> get_active_transfers();
    NetworkStats get_stats() const;
    
private:
    TransferManager() = default;
    TransferManager(const TransferManager&) = delete;
    TransferManager& operator=(const TransferManager&) = delete;
    
    std::map<SessionID, std::shared_ptr<FileTransfer>> transfers_;
    mutable std::mutex transfers_mutex_;
};

} // namespace syncflow::transfer

#endif // SYNCFLOW_TRANSFER_H

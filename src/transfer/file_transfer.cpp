// src/transfer/file_transfer.cpp

#include <syncflow/transfer/transfer.h>
#include <syncflow/common/logger.h>

namespace syncflow::transfer {

FileTransfer::FileTransfer(const SessionID& session_id,
                          const std::string& file_path,
                          uint64_t total_size,
                          const DeviceID& remote_device)
    : session_id_(session_id),
      file_path_(file_path),
      total_size_(total_size),
      transferred_bytes_(0),
      remote_device_(remote_device),
      state_(TransferState::IDLE) {
    
    // Calculate number of chunks
    size_t num_chunks = (total_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    received_chunks_.resize(num_chunks, false);
    
    LOG_INFO("FileTransfer", "Created transfer session: " + session_id);
}

const SessionID& FileTransfer::get_session_id() const {
    return session_id_;
}

const std::string& FileTransfer::get_file_path() const {
    return file_path_;
}

uint64_t FileTransfer::get_total_size() const {
    return total_size_;
}

uint64_t FileTransfer::get_transferred_bytes() const {
    std::unique_lock<std::mutex> lock(state_mutex_);
    return transferred_bytes_;
}

TransferState FileTransfer::get_state() const {
    std::unique_lock<std::mutex> lock(state_mutex_);
    return state_;
}

bool FileTransfer::add_chunk(const ChunkInfo& chunk_info, const std::vector<uint8_t>& data) {
    std::unique_lock<std::mutex> lock(state_mutex_);
    
    if (state_ != TransferState::TRANSFERRING) {
        return false;
    }
    
    if (chunk_info.id >= received_chunks_.size()) {
        LOG_WARN("FileTransfer", "Chunk ID out of range: " + std::to_string(chunk_info.id));
        return false;
    }
    
    if (received_chunks_[chunk_info.id]) {
        return false;  // Already received
    }
    
    // TODO: Write chunk to disk
    // platform::FileSystem::write_file_chunk(file_path_, chunk_info.offset, data);
    
    received_chunks_[chunk_info.id] = true;
    transferred_bytes_ += data.size();
    
    // Check if all chunks received
    bool all_received = true;
    for (bool received : received_chunks_) {
        if (!received) {
            all_received = false;
            break;
        }
    }
    
    if (all_received) {
        state_ = TransferState::COMPLETED;
        LOG_INFO("FileTransfer", "Transfer completed: " + session_id_);
    }
    
    return true;
}

bool FileTransfer::pause() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (state_ == TransferState::TRANSFERRING) {
        state_ = TransferState::PAUSED;
        return true;
    }
    return false;
}

bool FileTransfer::resume() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (state_ == TransferState::PAUSED) {
        state_ = TransferState::TRANSFERRING;
        return true;
    }
    return false;
}

bool FileTransfer::cancel() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    state_ = TransferState::CANCELLED;
    return true;
}

bool FileTransfer::is_complete() const {
    std::unique_lock<std::mutex> lock(state_mutex_);
    return state_ == TransferState::COMPLETED;
}

} // namespace syncflow::transfer

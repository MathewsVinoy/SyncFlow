// src/transfer/transfer_manager.cpp

#include <syncflow/transfer/transfer.h>
#include <syncflow/common/logger.h>
#include <syncflow/common/utils.h>

namespace syncflow::transfer {

TransferManager& TransferManager::instance() {
    static TransferManager manager;
    return manager;
}

bool TransferManager::start_send(const std::string& file_path,
                                 const DeviceID& device_id,
                                 const std::string& remote_path,
                                 OnTransferProgress progress_cb,
                                 OnTransferComplete complete_cb) {
    // TODO: Implement file sending logic
    LOG_INFO("TransferManager", "Starting file send: " + file_path + " to device " + device_id);
    return true;
}

bool TransferManager::start_receive(const std::string& file_path,
                                    uint64_t file_size,
                                    const DeviceID& device_id,
                                    OnTransferProgress progress_cb,
                                    OnTransferComplete complete_cb) {
    SessionID session_id = syncflow::utils::generate_session_id();
    
    auto transfer = std::make_shared<FileTransfer>(session_id, file_path, file_size, device_id);
    
    {
        std::unique_lock<std::mutex> lock(transfers_mutex_);
        transfers_[session_id] = transfer;
    }
    
    LOG_INFO("TransferManager", "Started file receive: " + file_path);
    return true;
}

bool TransferManager::pause_transfer(const SessionID& session_id) {
    auto transfer = get_transfer(session_id);
    if (transfer) {
        return transfer->pause();
    }
    return false;
}

bool TransferManager::resume_transfer(const SessionID& session_id) {
    auto transfer = get_transfer(session_id);
    if (transfer) {
        return transfer->resume();
    }
    return false;
}

bool TransferManager::cancel_transfer(const SessionID& session_id) {
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    auto it = transfers_.find(session_id);
    if (it != transfers_.end()) {
        it->second->cancel();
        transfers_.erase(it);
        return true;
    }
    return false;
}

std::shared_ptr<FileTransfer> TransferManager::get_transfer(const SessionID& session_id) {
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    auto it = transfers_.find(session_id);
    return it != transfers_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<FileTransfer>> TransferManager::get_active_transfers() {
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    std::vector<std::shared_ptr<FileTransfer>> result;
    for (const auto& pair : transfers_) {
        result.push_back(pair.second);
    }
    return result;
}

NetworkStats TransferManager::get_stats() const {
    NetworkStats stats = {};
    stats.bytes_sent = 0;
    stats.bytes_received = 0;
    stats.total_files_transferred = 0;
    return stats;
}

} // namespace syncflow::transfer

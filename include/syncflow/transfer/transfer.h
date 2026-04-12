#ifndef SYNCFLOW_TRANSFER_TRANSFER_H
#define SYNCFLOW_TRANSFER_TRANSFER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace syncflow::transfer {

class Transfer {
public:
    Transfer(std::filesystem::path file_path = {}, std::uint64_t total_size = 0)
        : file_path_(std::move(file_path)), total_size_(total_size) {}

    const std::filesystem::path& get_file_path() const {
        return file_path_;
    }

    std::uint64_t get_total_size() const {
        return total_size_;
    }

    std::uint64_t get_transferred_bytes() const {
        return transferred_bytes_;
    }

    void set_transferred_bytes(std::uint64_t transferred) {
        transferred_bytes_ = transferred;
    }

private:
    std::filesystem::path file_path_;
    std::uint64_t total_size_ = 0;
    std::uint64_t transferred_bytes_ = 0;
};

class TransferManager {
public:
    static TransferManager& instance() {
        static TransferManager manager;
        return manager;
    }

    std::vector<std::shared_ptr<Transfer>> get_active_transfers() {
        return active_transfers_;
    }

    void add_transfer(const std::shared_ptr<Transfer>& transfer) {
        active_transfers_.push_back(transfer);
    }

    void clear() {
        active_transfers_.clear();
    }

private:
    TransferManager() = default;
    std::vector<std::shared_ptr<Transfer>> active_transfers_;
};

} // namespace syncflow::transfer

#endif // SYNCFLOW_TRANSFER_TRANSFER_H

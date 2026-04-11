// tests/test_transfer.cpp

#include <syncflow/transfer/transfer.h>
#include <syncflow/common/utils.h>
#include <iostream>

using namespace syncflow;

int main() {
    std::cout << "Testing Transfer Module\n";
    
    // Test transfer protocol encoding/decoding
    DeviceInfo local_info;
    local_info.id = "test:device:001";
    local_info.name = "TestDevice";
    local_info.hostname = "testhost";
    local_info.platform = PlatformType::LINUX;
    local_info.version = "0.1.0";
    
    auto handshake_data = transfer::TransferProtocol::encode_handshake(local_info);
    std::cout << "Handshake encoded, size: " << handshake_data.size() << "\n";
    
    // Test file metadata encoding
    FileMetadata file_meta;
    file_meta.path = "test/file.txt";
    file_meta.id = "abc123";
    file_meta.size = 1024;
    file_meta.is_directory = false;
    file_meta.crc32 = 12345;
    
    auto file_offer = transfer::TransferProtocol::encode_file_offer(file_meta, "/tmp/file.txt");
    std::cout << "File offer encoded, size: " << file_offer.size() << "\n";
    
    // Test transfer manager
    auto& transfer_mgr = transfer::TransferManager::instance();
    
    SessionID session_id = utils::generate_session_id();
    std::cout << "Generated session ID: " << session_id << "\n";
    
    bool started = transfer_mgr.start_receive("test_file.bin", 1024000, "device:001", nullptr, nullptr);
    std::cout << "Transfer started: " << (started ? "yes" : "no") << "\n";
    
    return 0;
}

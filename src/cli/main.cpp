// src/cli/main.cpp

#include <syncflow/discovery/discovery.h>
#include <syncflow/transfer/transfer.h>
#include <syncflow/sync/sync.h>
#include <syncflow/common/logger.h>
#include <iostream>
#include <vector>
#include <string>

using namespace syncflow;

// Forward declarations
class CLIHandler;

/**
 * Main CLI entry point for syncflow
 */
int main(int argc, char* argv[]) {
    // Initialize logger
    Logger::instance().set_level(LogLevel::INFO);
    
    LOG_INFO("main", "Syncflow starting...");
    
    if (argc < 2) {
        std::cout << "Syncflow - File Synchronization System\n\n";
        std::cout << "Usage: syncflow <command> [options]\n\n";
        std::cout << "Commands:\n";
        std::cout << "  start              Start sync daemon\n";
        std::cout << "  stop               Stop sync daemon\n";
        std::cout << "  list-devices       List discovered devices\n";
        std::cout << "  add-folder         Add folder to sync\n";
        std::cout << "  list-folders       List synced folders\n";
        std::cout << "  send <file> <dev>  Send file to device\n";
        std::cout << "  receive            Start receiving file\n";
        std::cout << "  status             Show current status\n";
        std::cout << "  --help             Show this help\n";
        return 1;
    }
    
    std::string command = argv[1];
    
    try {
        if (command == "list-devices") {
            // List discovered devices
            auto& device_mgr = discovery::DeviceManager::instance();
            auto devices = device_mgr.get_all_devices();
            
            if (devices.empty()) {
                std::cout << "No devices discovered.\n";
            } else {
                std::cout << "Discovered Devices:\n";
                std::cout << "--------------------\n";
                for (const auto& device : devices) {
                    const auto& info = device->get_info();
                    std::cout << "Name: " << info.name << "\n";
                    std::cout << "ID: " << info.id << "\n";
                    std::cout << "IP: " << info.ip_address << ":" << info.port << "\n";
                    std::cout << "Platform: " << (int)info.platform << "\n";
                    std::cout << "---\n";
                }
            }
            
        } else if (command == "status") {
            // Show status
            auto& device_mgr = discovery::DeviceManager::instance();
            std::cout << "Connected devices: " << device_mgr.device_count() << "\n";
            
            auto& transfer_mgr = transfer::TransferManager::instance();
            auto active = transfer_mgr.get_active_transfers();
            std::cout << "Active transfers: " << active.size() << "\n";
            
            for (const auto& transfer : active) {
                uint64_t total = transfer->get_total_size();
                uint64_t transferred = transfer->get_transferred_bytes();
                int percent = total > 0 ? (transferred * 100) / total : 0;
                std::cout << "  - " << transfer->get_file_path() << ": " << percent << "%\n";
            }
            
        } else if (command == "start") {
            std::cout << "Starting sync daemon...\n";
            // TODO: Implement daemon start
            
        } else if (command == "stop") {
            std::cout << "Stopping sync daemon...\n";
            // TODO: Implement daemon stop
            
        } else if (command == "--help" || command == "-h" || command == "help") {
            std::cout << "Syncflow Help\n";
            std::cout << "=============\n";
            // Show help
            
        } else {
            std::cerr << "Unknown command: " << command << "\n";
            std::cerr << "Use 'syncflow --help' for usage information\n";
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "syncflow/sync_engine.hpp"
#include "syncflow/types.hpp"

void print_usage(const std::string& prog) {
  std::cout << "Usage: " << prog << " <command> [args]\n\n"
            << "Commands:\n"
            << "  start              Start sync daemon\n"
            << "  stop               Stop sync daemon\n"
            << "  add-folder PATH    Add folder to sync\n"
            << "  add-peer ID NAME   Add trusted peer\n"
            << "  status             Show sync status\n"
            << "  stats              Show sync statistics\n"
            << "  help               Show this help\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  std::string command = argv[1];

  // Create default configuration
  syncflow::SyncConfig config;
  config.device_name = "DefaultDevice";
  config.listening_port = 22000;
  config.chunk_size = 16384;

  // Create sync engine
  auto engine = syncflow::create_sync_engine(config);

  if (command == "start") {
    std::cout << "Starting SyncFlow daemon...\n";
    auto err = engine->start();
    if (!err.is_success()) {
      std::cerr << "Error: " << err.to_string() << "\n";
      return 1;
    }
    std::cout << "SyncFlow daemon started.\n";
    std::cout << "Local Device ID: " << engine->get_local_device_id() << "\n";
    
    // Keep running
    std::cout << "Press Ctrl+C to stop.\n";
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

  } else if (command == "add-folder") {
    if (argc < 3) {
      std::cerr << "Usage: syncflowctl add-folder PATH\n";
      return 1;
    }
    engine->start();
    auto err = engine->add_sync_folder(argv[2]);
    if (!err.is_success()) {
      std::cerr << "Error: " << err.to_string() << "\n";
      return 1;
    }
    std::cout << "Added folder: " << argv[2] << "\n";

  } else if (command == "status") {
    engine->start();
    std::cout << "Engine State: " << engine->get_state() << "\n";
    std::cout << "Local Device ID: " << engine->get_local_device_id() << "\n";
    std::cout << "Sync Queue Size: " << engine->get_sync_queue_size() << "\n";

  } else if (command == "stats") {
    engine->start();
    auto stats = engine->get_statistics();
    std::cout << "Sync Statistics:\n"
              << "  Total Files: " << stats.total_files << "\n"
              << "  Synced Files: " << stats.synced_files << "\n"
              << "  Bytes Transferred: " << stats.bytes_transferred << "\n"
              << "  Conflicted Files: " << stats.conflicted_files << "\n"
              << "  Active Peers: " << stats.active_peers << "\n";

  } else if (command == "help") {
    print_usage(argv[0]);

  } else {
    std::cerr << "Unknown command: " << command << "\n";
    print_usage(argv[0]);
    return 1;
  }

  return 0;
}

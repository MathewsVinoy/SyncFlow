#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path pid_file_path() {
    return std::filesystem::temp_directory_path() / "syncflow_discovery.pid";
}

std::filesystem::path discovery_binary_path(const char* argv0) {
    std::filesystem::path cli_path = std::filesystem::absolute(argv0);
    const auto dir = cli_path.parent_path();
#ifdef _WIN32
    return dir / "syncflow_discovery.exe";
#else
    return dir / "syncflow_discovery";
#endif
}

std::filesystem::path transfer_binary_path(const char* argv0) {
    std::filesystem::path cli_path = std::filesystem::absolute(argv0);
    const auto dir = cli_path.parent_path();
#ifdef _WIN32
    return dir / "syncflow_transfer.exe";
#else
    return dir / "syncflow_transfer";
#endif
}

std::filesystem::path sync_binary_path(const char* argv0) {
    std::filesystem::path cli_path = std::filesystem::absolute(argv0);
    const auto dir = cli_path.parent_path();
#ifdef _WIN32
    return dir / "syncflow_sync.exe";
#else
    return dir / "syncflow_sync";
#endif
}

bool write_pid_file(unsigned long long pid) {
    std::ofstream out(pid_file_path(), std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << pid;
    return true;
}

bool read_pid_file(unsigned long long& pid) {
    std::ifstream in(pid_file_path());
    if (!in.is_open()) {
        return false;
    }
    in >> pid;
    return in.good() || in.eof();
}

void remove_pid_file() {
    std::error_code ec;
    std::filesystem::remove(pid_file_path(), ec);
}

#ifdef _WIN32
bool is_process_running(unsigned long long pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return false;
    }

    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return ok && code == STILL_ACTIVE;
}

int spawn_server(const std::filesystem::path& server_bin) {
    std::string cmd = "\"" + server_bin.string() + "\" server";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessA(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!ok) {
        return 1;
    }

    const unsigned long long pid = static_cast<unsigned long long>(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!write_pid_file(pid)) {
        return 1;
    }

    std::cout << "syncflow discovery started (pid=" << pid << ")\n";
    return 0;
}

int stop_server() {
    unsigned long long pid = 0;
    if (!read_pid_file(pid)) {
        std::cerr << "No running server found (pid file missing).\n";
        return 1;
    }

    HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        std::cerr << "Server process not found. Cleaning stale pid file.\n";
        remove_pid_file();
        return 1;
    }

    if (!TerminateProcess(process, 0)) {
        CloseHandle(process);
        std::cerr << "Failed to stop process pid=" << pid << "\n";
        return 1;
    }

    CloseHandle(process);
    remove_pid_file();
    std::cout << "syncflow discovery stopped (pid=" << pid << ")\n";
    return 0;
}
#else
bool is_process_running(unsigned long long pid) {
    if (pid == 0) {
        return false;
    }
    return kill(static_cast<pid_t>(pid), 0) == 0;
}

int spawn_server(const std::filesystem::path& server_bin) {
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork failed\n";
        return 1;
    }

    if (pid == 0) {
        if (setsid() < 0) {
            std::exit(1);
        }

        execl(server_bin.c_str(), server_bin.c_str(), "server", nullptr);
        std::exit(1);
    }

    if (!write_pid_file(static_cast<unsigned long long>(pid))) {
        std::cerr << "Failed to write pid file\n";
        return 1;
    }

    std::cout << "syncflow discovery started (pid=" << pid << ")\n";
    return 0;
}

int stop_server() {
    unsigned long long pid = 0;
    if (!read_pid_file(pid)) {
        std::cerr << "No running server found (pid file missing).\n";
        return 1;
    }

    if (!is_process_running(pid)) {
        std::cerr << "Server process not found. Cleaning stale pid file.\n";
        remove_pid_file();
        return 1;
    }

    if (kill(static_cast<pid_t>(pid), SIGTERM) != 0) {
        std::cerr << "Failed to stop process pid=" << pid << "\n";
        return 1;
    }

    remove_pid_file();
    std::cout << "syncflow discovery stopped (pid=" << pid << ")\n";
    return 0;
}
#endif

int list_devices(const std::filesystem::path& server_bin) {
    std::string cmd = "\"" + server_bin.string() + "\" client";
    return std::system(cmd.c_str());
}

void print_usage() {
    std::cout << "Usage: syncflow <start|stop|list-devices|status|recv-file|send-file|sync-dir|sync-recv|sync-auto>\n"
              << "  recv-file [port] [output_dir]\n"
              << "  send-file <ip> [port] <file_path>\n"
              << "  sync-dir [--tcp|--udp] <ip> [port] <source_dir> [interval_ms]\n"
              << "  sync-recv [--tcp|--udp] [port] [output_dir]\n"
              << "  sync-auto [--tcp|--udp] <peer_ip> [port] <sync_dir> [interval_ms]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::string command = argv[1];
    const auto server_bin = discovery_binary_path(argv[0]);
    const auto transfer_bin = transfer_binary_path(argv[0]);
    const auto sync_bin = sync_binary_path(argv[0]);

    if (command == "start") {
        if (!std::filesystem::exists(server_bin)) {
            std::cerr << "Discovery binary not found: " << server_bin << "\n";
            std::cerr << "Build target syncflow_discovery first.\n";
            return 1;
        }
        unsigned long long pid = 0;
        if (read_pid_file(pid) && is_process_running(pid)) {
            std::cout << "syncflow discovery already running (pid=" << pid << ")\n";
            return 0;
        }
        remove_pid_file();
        return spawn_server(server_bin);
    }

    if (command == "stop") {
        return stop_server();
    }

    if (command == "list-devices") {
        if (!std::filesystem::exists(server_bin)) {
            std::cerr << "Discovery binary not found: " << server_bin << "\n";
            std::cerr << "Build target syncflow_discovery first.\n";
            return 1;
        }
        return list_devices(server_bin);
    }

    if (command == "recv-file") {
        if (!std::filesystem::exists(transfer_bin)) {
            std::cerr << "Transfer binary not found: " << transfer_bin << "\n";
            return 1;
        }

        std::string cmd = "\"" + transfer_bin.string() + "\" recv";
        if (argc >= 3) {
            cmd += " \"" + std::string(argv[2]) + "\"";
        }
        if (argc >= 4) {
            cmd += " \"" + std::string(argv[3]) + "\"";
        }
        return std::system(cmd.c_str());
    }

    if (command == "send-file") {
        if (!std::filesystem::exists(transfer_bin)) {
            std::cerr << "Transfer binary not found: " << transfer_bin << "\n";
            return 1;
        }

        if (argc < 4) {
            print_usage();
            return 1;
        }

        std::string cmd = "\"" + transfer_bin.string() + "\" send \"" + std::string(argv[2]) + "\"";
        if (argc == 4) {
            cmd += " \"" + std::string(argv[3]) + "\"";
        } else {
            cmd += " \"" + std::string(argv[3]) + "\" \"" + std::string(argv[4]) + "\"";
        }
        return std::system(cmd.c_str());
    }

    if (command == "status") {
        if (!std::filesystem::exists(server_bin)) {
            std::cerr << "Discovery binary not found: " << server_bin << "\n";
            std::cerr << "Build target syncflow_discovery first.\n";
            return 1;
        }
        unsigned long long pid = 0;
        if (read_pid_file(pid) && is_process_running(pid)) {
            std::cout << "syncflow discovery is running (pid=" << pid << ")\n";
            return 0;
        }
        std::cout << "syncflow discovery is not running\n";
        return 1;
    }

    if (command == "sync-dir") {
        if (!std::filesystem::exists(sync_bin)) {
            std::cerr << "Sync binary not found: " << sync_bin << "\n";
            std::cerr << "Build target syncflow_sync first.\n";
            return 1;
        }

        if (argc < 4) {
            print_usage();
            return 1;
        }

        std::string cmd = "\"" + sync_bin.string() + "\" send";
        for (int i = 2; i < argc; ++i) {
            cmd += " \"" + std::string(argv[i]) + "\"";
        }
        return std::system(cmd.c_str());
    }

    if (command == "sync-recv") {
        if (!std::filesystem::exists(sync_bin)) {
            std::cerr << "Sync binary not found: " << sync_bin << "\n";
            std::cerr << "Build target syncflow_sync first.\n";
            return 1;
        }

        std::string cmd = "\"" + sync_bin.string() + "\" recv";
        for (int i = 2; i < argc; ++i) {
            cmd += " \"" + std::string(argv[i]) + "\"";
        }
        return std::system(cmd.c_str());
    }

    if (command == "sync-auto") {
        if (!std::filesystem::exists(sync_bin)) {
            std::cerr << "Sync binary not found: " << sync_bin << "\n";
            std::cerr << "Build target syncflow_sync first.\n";
            return 1;
        }

        if (argc < 4) {
            print_usage();
            return 1;
        }

        std::string cmd = "\"" + sync_bin.string() + "\" auto";
        for (int i = 2; i < argc; ++i) {
            cmd += " \"" + std::string(argv[i]) + "\"";
        }
        return std::system(cmd.c_str());
    }

    print_usage();
    return 1;
}

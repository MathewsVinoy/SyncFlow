#include "syncflow/networking/peer_node.h"
#include "syncflow/platform/system_info.h"
#include "syncflow/file_sync/file_sync.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <sstream>

#if !defined(_WIN32) && !defined(_WIN64)
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
#endif

static std::filesystem::path find_config_path() {
    const std::array<std::filesystem::path, 4> candidates{
        std::filesystem::current_path() / "config.json",
        std::filesystem::path("./") / "config.json",
        std::filesystem::current_path().parent_path() / "config.json",
        std::filesystem::current_path().parent_path().parent_path() / "config.json"
    };

    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) return c;
    }
    return candidates.front();
}

static void print_help() {
    std::cout << "Usage: syncflow_peer [options] [command]\n"
              << "Options:\n"
              << "  -d, --device <name>    Specify device name (overrides config)\n"
              << "  -c, --config <path>    Use specified config.json\n"
              << "  --detach               Run in background as daemon\n"
              << "Commands:\n"
              << "  start                  Start the peer (default)\n"
              << "  show-config            Print resolved configuration\n"
              << "  set-device <name>      Set device_name in config.json\n"
              << "  --help                 Show this help\n";
}

static bool write_device_to_config(const std::filesystem::path& config_path, const std::string& device_name) {
    std::string content;
    if (std::filesystem::exists(config_path)) {
        std::ifstream in(config_path);
        if (!in) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        content = ss.str();
        const std::string key = "\"device_name\"";
        const auto pos = content.find(key);
        if (pos != std::string::npos) {
            const auto first_quote = content.find('"', pos + key.size());
            const auto second_quote = content.find('"', first_quote + 1);
            if (first_quote != std::string::npos && second_quote != std::string::npos) {
                // replace value
                // fallback: perform a simple replace of the value between the two quotes after key
                const auto val_begin = content.find('"', pos + key.size());
                const auto val_open = content.find('"', val_begin + 1);
                const auto val_close = content.find('"', val_open + 1);
                if (val_open != std::string::npos && val_close != std::string::npos) {
                    content.replace(val_open + 1, val_close - val_open - 1, device_name);
                }
            }
        } else {
            // insert device_name before final closing brace
            const auto insert_pos = content.rfind('}');
            if (insert_pos == std::string::npos) return false;
            // ensure trailing comma if needed
            const auto before = content.substr(0, insert_pos);
            const bool needs_comma = before.find_last_not_of(" \t\r\n") != std::string::npos && before.back() != ',';
            const std::string entry = std::string(needs_comma ? ",\n    \"device_name\": \"" : "\n    \"device_name\": \"") + device_name + "\"\n";
            content.insert(insert_pos, entry);
        }
    } else {
        // create a minimal config
        content = "{\n  \"file_sync\": {\n    \"enabled\": true,\n    \"source_path\": \"sync/\",\n    \"receive_dir\": \"received\",\n    \"device_name\": \"" + device_name + "\"\n  }\n}\n";
    }

    std::ofstream out(config_path);
    if (!out) return false;
    out << content;
    return true;
}

static bool daemonize(const std::filesystem::path& log_dir) {
#ifdef _WIN32
    // Windows: run as background process (not traditional daemon)
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);

    const auto log_file = log_dir / "syncflow.log";
    std::cout << "Running in background mode.\n"
              << "Logs: " << log_file << "\n"
              << "To view logs: type 'type " << log_file.string() << "'\n"
              << "To stop: press Ctrl+C or close the window\n";

    // Redirect output to log file
    freopen(log_file.c_str(), "ab", stdout);
    freopen(log_file.c_str(), "ab", stderr);
    return true;
#else
    // Unix/Linux/macOS: fork daemon
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);

    const pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "fork failed\n";
        return false;
    }
    if (pid > 0) {
        std::cout << "Started syncflow_peer in background (PID " << pid << ")\n";
        std::cout << "View logs in: " << log_dir / "syncflow.log" << "\n";
        std::exit(0);
    }

    // Child process continues
    if (::setsid() < 0) {
        std::cerr << "setsid failed\n";
        return false;
    }

    if (::chdir("/") < 0) {
        std::cerr << "chdir failed\n";
        return false;
    }

    const auto log_file = log_dir / "syncflow.log";
    const int fd = ::open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        std::cerr << "failed to open log file\n";
        return false;
    }

    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    ::close(fd);

    const int null_fd = ::open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
        ::dup2(null_fd, STDIN_FILENO);
        ::close(null_fd);
    }

    return true;
#endif
}

int main(int argc, char** argv) {
    std::string cli_device;
    std::filesystem::path config_override;
    bool detach = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "-d" || a == "--device") {
            if (i + 1 < args.size()) { cli_device = args[++i]; }
        } else if (a == "-c" || a == "--config") {
            if (i + 1 < args.size()) { config_override = args[++i]; }
        } else if (a == "--detach") {
            detach = true;
        } else if (a == "--help" || a == "-h") {
            print_help();
            return 0;
        }
    }

    std::string command;
    // last non-option argument is the command if present
    for (int i = argc - 1; i > 0; --i) {
        std::string s = argv[i];
        if (!s.empty() && s[0] != '-') { command = s; break; }
    }

    const auto config_path = config_override.empty() ? find_config_path() : config_override;

    if (command == "show-config") {
        const auto cfg = syncflow::file_sync::load_config(config_path);
        std::cout << "Config: " << config_path << "\n"
                  << " device_name: " << cfg.device_name << "\n"
                  << " source_path: " << cfg.source_path.string() << "\n"
                  << " receive_dir: " << cfg.receive_dir.string() << "\n";
#ifdef DEVICE_NAME
        std::cout << " build DEVICE_NAME: " << DEVICE_NAME << "\n";
#endif
        return 0;
    }

    if (command == "set-device") {
        // next argument should be name
        std::string newname;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "set-device" && i + 1 < argc) { newname = argv[i + 1]; break; }
        }
        if (newname.empty()) {
            std::cerr << "set-device requires a name\n";
            return 1;
        }
        if (!write_device_to_config(config_path, newname)) {
            std::cerr << "failed to write config\n";
            return 1;
        }
        std::cout << "wrote device_name to " << config_path << "\n";
        return 0;
    }

    // Default action: start the peer node
    std::string device_name = cli_device;
    if (device_name.empty()) {
        // if CLI not provided, allow PeerNode to select from config or build macro
        device_name = ""; // let PeerNode resolve from config or macro or hostname
    }

    // Daemonize if requested
    if (detach) {
        const auto log_dir = config_override.empty() 
            ? std::filesystem::current_path() 
            : config_override.parent_path();
        if (!daemonize(log_dir)) {
            std::cerr << "failed to daemonize\n";
            return 1;
        }
    }

    syncflow::networking::PeerNode node(device_name);
    node.run();
    return 0;
}

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path pid_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("syncflow_agent_" + name + ".pid");
}

std::filesystem::path bin_path(const char* argv0, const char* name) {
    const auto dir = std::filesystem::absolute(argv0).parent_path();
#ifdef _WIN32
    return dir / (std::string(name) + ".exe");
#else
    return dir / name;
#endif
}

bool write_pid_file(const std::filesystem::path& p, unsigned long long pid) {
    std::ofstream out(p, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << pid;
    return true;
}

bool read_pid_file(const std::filesystem::path& p, unsigned long long& pid) {
    std::ifstream in(p);
    if (!in.is_open()) {
        return false;
    }
    in >> pid;
    return in.good() || in.eof();
}

void remove_pid_file(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

#ifdef _WIN32
bool is_running(unsigned long long pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return false;
    }
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return ok && code == STILL_ACTIVE;
}

std::string quote_arg(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
    return out;
}

bool spawn_detached(const std::filesystem::path& exe,
                    const std::vector<std::string>& args,
                    const std::filesystem::path& pid_out) {
    std::string cmd = quote_arg(exe.string());
    for (const auto& a : args) {
        cmd += " " + quote_arg(a);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string mutable_cmd = cmd;

    BOOL ok = CreateProcessA(
        nullptr,
        mutable_cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        nullptr,
        &si,
        &pi);
    if (!ok) {
        return false;
    }

    const unsigned long long pid = static_cast<unsigned long long>(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return write_pid_file(pid_out, pid);
}

bool stop_by_pid_file(const std::filesystem::path& pid) {
    unsigned long long p = 0;
    if (!read_pid_file(pid, p)) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(p));
    if (!process) {
        remove_pid_file(pid);
        return false;
    }
    const BOOL ok = TerminateProcess(process, 0);
    CloseHandle(process);
    remove_pid_file(pid);
    return ok;
}
#else
bool is_running(unsigned long long pid) {
    if (pid == 0) {
        return false;
    }
    return kill(static_cast<pid_t>(pid), 0) == 0;
}

bool spawn_detached(const std::filesystem::path& exe,
                    const std::vector<std::string>& args,
                    const std::filesystem::path& pid_out) {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        if (setsid() < 0) {
            std::exit(1);
        }

        std::vector<char*> argvv;
        argvv.push_back(const_cast<char*>(exe.c_str()));
        for (const auto& a : args) {
            argvv.push_back(const_cast<char*>(a.c_str()));
        }
        argvv.push_back(nullptr);
        execv(exe.c_str(), argvv.data());
        std::exit(1);
    }

    return write_pid_file(pid_out, static_cast<unsigned long long>(pid));
}

bool stop_by_pid_file(const std::filesystem::path& pid) {
    unsigned long long p = 0;
    if (!read_pid_file(pid, p)) {
        return false;
    }
    const bool ok = kill(static_cast<pid_t>(p), SIGTERM) == 0;
    remove_pid_file(pid);
    return ok;
}
#endif

bool ensure_running(const std::filesystem::path& exe,
                    const std::vector<std::string>& args,
                    const std::filesystem::path& pid) {
    unsigned long long p = 0;
    if (read_pid_file(pid, p) && is_running(p)) {
        return true;
    }
    remove_pid_file(pid);
    return spawn_detached(exe, args, pid);
}

int start_services(const std::filesystem::path& discovery, const std::filesystem::path& ui) {
    const bool d = ensure_running(discovery, {"server"}, pid_file("discovery"));
    const bool u = ensure_running(ui, {}, pid_file("ui"));
    std::cout << "discovery: " << (d ? "running" : "failed") << "\n";
    std::cout << "ui: " << (u ? "running" : "failed") << "\n";
    return (d && u) ? 0 : 1;
}

int stop_services() {
    const bool d = stop_by_pid_file(pid_file("discovery"));
    const bool u = stop_by_pid_file(pid_file("ui"));
    std::cout << "discovery stop: " << (d ? "ok" : "not running") << "\n";
    std::cout << "ui stop: " << (u ? "ok" : "not running") << "\n";
    return 0;
}

int status_services() {
    unsigned long long p = 0;
    bool d = read_pid_file(pid_file("discovery"), p) && is_running(p);
    p = 0;
    bool u = read_pid_file(pid_file("ui"), p) && is_running(p);
    std::cout << "discovery: " << (d ? "running" : "stopped") << "\n";
    std::cout << "ui: " << (u ? "running" : "stopped") << "\n";
    return (d && u) ? 0 : 1;
}

int monitor_services(const std::filesystem::path& discovery, const std::filesystem::path& ui) {
    std::cout << "syncflow agent monitor mode started\n";
    while (true) {
        ensure_running(discovery, {"server"}, pid_file("discovery"));
        ensure_running(ui, {}, pid_file("ui"));
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

int install_autostart(const std::filesystem::path& self) {
#ifdef _WIN32
    const std::string reg_cmd =
        "reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run /v SyncflowAgent /t REG_SZ /d \"" +
        self.string() + " monitor\" /f";
    const int rc = std::system(reg_cmd.c_str());
    std::cout << (rc == 0 ? "autostart installed\n" : "failed to install autostart\n");
    return rc == 0 ? 0 : 1;
#elif defined(__APPLE__)
    const auto launch_agents = std::filesystem::path(std::getenv("HOME")) / "Library/LaunchAgents";
    std::filesystem::create_directories(launch_agents);
    const auto plist = launch_agents / "com.syncflow.agent.plist";
    std::ofstream out(plist, std::ios::trunc);
    if (!out.is_open()) {
        return 1;
    }
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\"><dict>\n"
           "<key>Label</key><string>com.syncflow.agent</string>\n"
           "<key>ProgramArguments</key><array><string>" << self.string() << "</string><string>monitor</string></array>\n"
           "<key>RunAtLoad</key><true/>\n"
           "<key>KeepAlive</key><true/>\n"
           "</dict></plist>\n";
    out.close();
    const std::string cmd = "launchctl load -w \"" + plist.string() + "\"";
    const int rc = std::system(cmd.c_str());
    std::cout << (rc == 0 ? "autostart installed\n" : "failed to install autostart\n");
    return rc == 0 ? 0 : 1;
#else
    const auto home = std::filesystem::path(std::getenv("HOME"));
    const auto user_dir = home / ".config/systemd/user";
    std::filesystem::create_directories(user_dir);
    const auto service = user_dir / "syncflow-agent.service";
    std::ofstream out(service, std::ios::trunc);
    if (!out.is_open()) {
        return 1;
    }
    out << "[Unit]\nDescription=Syncflow Background Agent\nAfter=network-online.target\n\n"
           "[Service]\nType=simple\nExecStart=" << self.string() << " monitor\nRestart=always\nRestartSec=3\n\n"
           "[Install]\nWantedBy=default.target\n";
    out.close();
    const int rc1 = std::system("systemctl --user daemon-reload");
    const int rc2 = std::system("systemctl --user enable --now syncflow-agent.service");
    const bool ok = (rc1 == 0 && rc2 == 0);
    std::cout << (ok ? "autostart installed\n" : "failed to install autostart\n");
    return ok ? 0 : 1;
#endif
}

int uninstall_autostart() {
#ifdef _WIN32
    const int rc = std::system("reg delete HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run /v SyncflowAgent /f");
    std::cout << (rc == 0 ? "autostart removed\n" : "failed to remove autostart\n");
    return rc == 0 ? 0 : 1;
#elif defined(__APPLE__)
    const auto plist = std::filesystem::path(std::getenv("HOME")) / "Library/LaunchAgents/com.syncflow.agent.plist";
    const std::string unload_cmd = "launchctl unload -w \"" + plist.string() + "\"";
    std::system(unload_cmd.c_str());
    std::error_code ec;
    std::filesystem::remove(plist, ec);
    std::cout << "autostart removed\n";
    return 0;
#else
    std::system("systemctl --user disable --now syncflow-agent.service");
    const auto service = std::filesystem::path(std::getenv("HOME")) / ".config/systemd/user/syncflow-agent.service";
    std::error_code ec;
    std::filesystem::remove(service, ec);
    std::system("systemctl --user daemon-reload");
    std::cout << "autostart removed\n";
    return 0;
#endif
}

void print_usage() {
    std::cout << "Usage:\n"
              << "  syncflow_agent start\n"
              << "  syncflow_agent stop\n"
              << "  syncflow_agent status\n"
              << "  syncflow_agent monitor\n"
              << "  syncflow_agent install-autostart\n"
              << "  syncflow_agent uninstall-autostart\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const auto discovery = bin_path(argv[0], "syncflow_discovery");
    const auto ui = bin_path(argv[0], "syncflow_ui");
    const auto self = std::filesystem::absolute(argv[0]);

    if (!std::filesystem::exists(discovery) || !std::filesystem::exists(ui)) {
        std::cerr << "required binaries missing (need syncflow_discovery and syncflow_ui in build folder)\n";
        return 1;
    }

    const std::string cmd = argv[1];
    if (cmd == "start") return start_services(discovery, ui);
    if (cmd == "stop") return stop_services();
    if (cmd == "status") return status_services();
    if (cmd == "monitor") return monitor_services(discovery, ui);
    if (cmd == "install-autostart") return install_autostart(self);
    if (cmd == "uninstall-autostart") return uninstall_autostart();

    print_usage();
    return 1;
}

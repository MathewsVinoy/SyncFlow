#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_Window.H>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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

struct AppState {
    std::filesystem::path syncflow_bin;
    std::filesystem::path transfer_bin;
    std::filesystem::path sync_bin;

    Fl_Multiline_Output* log_output = nullptr;

    Fl_Choice* transfer_transport = nullptr;
    Fl_Input* transfer_ip = nullptr;
    Fl_Input* transfer_port = nullptr;
    Fl_Input* transfer_file = nullptr;
    Fl_Input* recv_port = nullptr;
    Fl_Input* recv_dir = nullptr;

    Fl_Choice* sync_transport = nullptr;
    Fl_Input* sync_peer = nullptr;
    Fl_Input* sync_port = nullptr;
    Fl_Input* sync_dir = nullptr;
    Fl_Input* sync_interval = nullptr;

    std::string logs;
};

std::filesystem::path pid_file_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("syncflow_fltk_" + name + ".pid");
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

bool spawn_detached(const std::vector<std::string>& args, const std::filesystem::path& pid_file) {
    if (args.empty()) {
        return false;
    }

    std::string cmd = quote_arg(args[0]);
    for (size_t i = 1; i < args.size(); ++i) {
        cmd += " " + quote_arg(args[i]);
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

    return write_pid_file(pid_file, pid);
}

bool stop_pid(const std::filesystem::path& pid_file) {
    unsigned long long pid = 0;
    if (!read_pid_file(pid_file, pid)) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        remove_pid_file(pid_file);
        return false;
    }
    const BOOL ok = TerminateProcess(process, 0);
    CloseHandle(process);
    remove_pid_file(pid_file);
    return ok;
}
#else
bool is_process_running(unsigned long long pid) {
    if (pid == 0) {
        return false;
    }
    return kill(static_cast<pid_t>(pid), 0) == 0;
}

bool spawn_detached(const std::vector<std::string>& args, const std::filesystem::path& pid_file) {
    if (args.empty()) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        setsid();
        std::vector<char*> argvv;
        argvv.reserve(args.size() + 1);
        for (const auto& s : args) {
            argvv.push_back(const_cast<char*>(s.c_str()));
        }
        argvv.push_back(nullptr);
        execv(args[0].c_str(), argvv.data());
        std::exit(1);
    }

    return write_pid_file(pid_file, static_cast<unsigned long long>(pid));
}

bool stop_pid(const std::filesystem::path& pid_file) {
    unsigned long long pid = 0;
    if (!read_pid_file(pid_file, pid)) {
        return false;
    }
    const bool ok = kill(static_cast<pid_t>(pid), SIGTERM) == 0;
    remove_pid_file(pid_file);
    return ok;
}
#endif

std::string capture_command(const std::vector<std::string>& args) {
    if (args.empty()) {
        return "";
    }

    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            cmd += ' ';
        }
        cmd += '"';
        cmd += args[i];
        cmd += '"';
    }

#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        return "Failed to execute command\n";
    }

    std::string output;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return output;
}

void append_log(AppState* st, const std::string& line) {
    st->logs += line;
    if (!st->logs.empty() && st->logs.back() != '\n') {
        st->logs += '\n';
    }
    st->log_output->value(st->logs.c_str());
    Fl::check();
}

const char* selected_transport(Fl_Choice* c) {
    if (!c || !c->text()) {
        return "--tcp";
    }
    const std::string t = c->text();
    return (t == "UDP") ? "--udp" : "--tcp";
}

void on_discovery_start(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    const auto out = capture_command({st->syncflow_bin.string(), "start"});
    append_log(st, out.empty() ? "Discovery start sent." : out);
}

void on_discovery_stop(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    const auto out = capture_command({st->syncflow_bin.string(), "stop"});
    append_log(st, out.empty() ? "Discovery stop sent." : out);
}

void on_discovery_list(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    const auto out = capture_command({st->syncflow_bin.string(), "list-devices"});
    append_log(st, out.empty() ? "No output." : out);
}

void on_send_file(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    const std::string ip = st->transfer_ip->value();
    const std::string port = st->transfer_port->value();
    const std::string file = st->transfer_file->value();
    if (ip.empty() || port.empty() || file.empty()) {
        append_log(st, "Fill IP, port, and file path.");
        return;
    }
    const auto out = capture_command({st->transfer_bin.string(), "send", selected_transport(st->transfer_transport), ip, port, file});
    append_log(st, out.empty() ? "Send done." : out);
}

void on_recv_start(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    const std::string port = st->recv_port->value();
    const std::string dir = st->recv_dir->value();
    if (port.empty() || dir.empty()) {
        append_log(st, "Fill receiver port and output dir.");
        return;
    }
    const bool ok = spawn_detached(
        {st->transfer_bin.string(), "recv", selected_transport(st->transfer_transport), port, dir},
        pid_file_path("transfer_recv"));
    append_log(st, ok ? "Receiver started." : "Failed to start receiver.");
}

void on_recv_stop(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    const bool ok = stop_pid(pid_file_path("transfer_recv"));
    append_log(st, ok ? "Receiver stopped." : "Receiver is not running.");
}

void on_sync_start(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    const std::string peer = st->sync_peer->value();
    const std::string port = st->sync_port->value();
    const std::string dir = st->sync_dir->value();
    const std::string interval = st->sync_interval->value();
    if (peer.empty() || port.empty() || dir.empty() || interval.empty()) {
        append_log(st, "Fill peer, port, dir, interval.");
        return;
    }

    const bool ok = spawn_detached(
        {st->sync_bin.string(), "auto", selected_transport(st->sync_transport), peer, port, dir, interval},
        pid_file_path("sync_auto"));
    append_log(st, ok ? "Auto-sync started." : "Failed to start auto-sync.");
}

void on_sync_stop(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    const bool ok = stop_pid(pid_file_path("sync_auto"));
    append_log(st, ok ? "Auto-sync stopped." : "Auto-sync is not running.");
}

void on_status(Fl_Widget*, void* data) {
    auto* st = static_cast<AppState*>(data);
    std::ostringstream ss;
    ss << capture_command({st->syncflow_bin.string(), "status"});

    unsigned long long pid = 0;
    const bool recv_running = read_pid_file(pid_file_path("transfer_recv"), pid) && is_process_running(pid);
    ss << "transfer receiver: " << (recv_running ? "running" : "stopped") << "\n";

    pid = 0;
    const bool sync_running = read_pid_file(pid_file_path("sync_auto"), pid) && is_process_running(pid);
    ss << "auto-sync: " << (sync_running ? "running" : "stopped") << "\n";
    append_log(st, ss.str());
}

} // namespace

int main(int argc, char* argv[]) {
    AppState st;

    const auto bin_dir = std::filesystem::absolute(argv[0]).parent_path();
#ifdef _WIN32
    st.syncflow_bin = bin_dir / "syncflow.exe";
    st.transfer_bin = bin_dir / "syncflow_transfer.exe";
    st.sync_bin = bin_dir / "syncflow_sync.exe";
#else
    st.syncflow_bin = bin_dir / "syncflow";
    st.transfer_bin = bin_dir / "syncflow_transfer";
    st.sync_bin = bin_dir / "syncflow_sync";
#endif

    Fl_Window* win = new Fl_Window(920, 640, "Syncflow Desktop UI (FLTK)");

    auto* title = new Fl_Box(20, 16, 880, 28, "Syncflow Desktop UI");
    title->labelsize(22);

    auto* b1 = new Fl_Button(20, 60, 140, 34, "Discovery Start");
    auto* b2 = new Fl_Button(170, 60, 140, 34, "Discovery Stop");
    auto* b3 = new Fl_Button(320, 60, 140, 34, "List Devices");
    auto* b4 = new Fl_Button(470, 60, 140, 34, "Status");

    auto* transferLbl = new Fl_Box(20, 112, 260, 24, "File Transfer");
    transferLbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    st.transfer_transport = new Fl_Choice(20, 140, 110, 30, "Transport");
    st.transfer_transport->add("TCP");
    st.transfer_transport->add("UDP");
    st.transfer_transport->value(0);

    st.transfer_ip = new Fl_Input(140, 140, 180, 30, "Peer IP");
    st.transfer_port = new Fl_Input(330, 140, 90, 30, "Port");
    st.transfer_port->value("37030");
    st.transfer_file = new Fl_Input(430, 140, 300, 30, "File");
    auto* sendBtn = new Fl_Button(740, 140, 160, 30, "Send File");

    st.recv_port = new Fl_Input(140, 180, 90, 30, "Recv Port");
    st.recv_port->value("37030");
    st.recv_dir = new Fl_Input(240, 180, 280, 30, "Output Dir");
    st.recv_dir->value("received");
    auto* recvStartBtn = new Fl_Button(530, 180, 180, 30, "Start Receiver");
    auto* recvStopBtn = new Fl_Button(720, 180, 180, 30, "Stop Receiver");

    auto* syncLbl = new Fl_Box(20, 232, 260, 24, "Auto Sync");
    syncLbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    st.sync_transport = new Fl_Choice(20, 260, 110, 30, "Transport");
    st.sync_transport->add("TCP");
    st.sync_transport->add("UDP");
    st.sync_transport->value(0);

    st.sync_peer = new Fl_Input(140, 260, 180, 30, "Peer IP");
    st.sync_port = new Fl_Input(330, 260, 90, 30, "Port");
    st.sync_port->value("37030");
    st.sync_dir = new Fl_Input(430, 260, 220, 30, "Sync Dir");
    st.sync_dir->value("project_dir");
    st.sync_interval = new Fl_Input(660, 260, 80, 30, "Interval");
    st.sync_interval->value("2000");
    auto* syncStartBtn = new Fl_Button(750, 260, 150, 30, "Start Auto Sync");
    auto* syncStopBtn = new Fl_Button(750, 300, 150, 30, "Stop Auto Sync");

    st.log_output = new Fl_Multiline_Output(20, 350, 880, 270, "Logs");

    b1->callback(on_discovery_start, &st);
    b2->callback(on_discovery_stop, &st);
    b3->callback(on_discovery_list, &st);
    b4->callback(on_status, &st);
    sendBtn->callback(on_send_file, &st);
    recvStartBtn->callback(on_recv_start, &st);
    recvStopBtn->callback(on_recv_stop, &st);
    syncStartBtn->callback(on_sync_start, &st);
    syncStopBtn->callback(on_sync_stop, &st);

    win->end();
    win->resizable(st.log_output);
    win->show(argc, argv);

    append_log(&st, "FLTK UI ready.");
    append_log(&st, "Make sure binaries exist in same build folder.");

    return Fl::run();
}

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

namespace {

constexpr int DEFAULT_UI_PORT = 8080;
constexpr const char* DEFAULT_UI_BIND_ADDR = "127.0.0.1";
constexpr int DEFAULT_DISCOVERY_UDP_PORT = 37020;
constexpr int DEFAULT_TRANSFER_PORT = 37030;
constexpr int DEFAULT_SYNC_INTERVAL_MS = 2000;

struct UiRuntimeConfig {
    int ui_port = DEFAULT_UI_PORT;
    std::string bind_addr = DEFAULT_UI_BIND_ADDR;
    std::string api_token;
};

using SocketHandle =
#ifdef _WIN32
    SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
    int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void close_socket(SocketHandle sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

struct ServiceSpec {
    std::filesystem::path pid_file;
    std::filesystem::path executable;
    std::vector<std::string> args;
};

std::filesystem::path binary_path(const char* argv0, const char* name) {
    const auto dir = std::filesystem::absolute(argv0).parent_path();
#ifdef _WIN32
    return dir / (std::string(name) + ".exe");
#else
    return dir / name;
#endif
}

std::filesystem::path pid_file_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("syncflow_ui_" + name + ".pid");
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

bool spawn_detached(const ServiceSpec& spec) {
    std::string cmd = quote_arg(spec.executable.string());
    for (const auto& arg : spec.args) {
        cmd += " " + quote_arg(arg);
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
    return write_pid_file(spec.pid_file, pid);
}

bool stop_service(const std::filesystem::path& pid_file) {
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

bool spawn_detached(const ServiceSpec& spec) {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        if (setsid() < 0) {
            std::exit(1);
        }

        std::vector<char*> argvv;
        argvv.push_back(const_cast<char*>(spec.executable.c_str()));
        for (const auto& arg : spec.args) {
            argvv.push_back(const_cast<char*>(arg.c_str()));
        }
        argvv.push_back(nullptr);

        execv(spec.executable.c_str(), argvv.data());
        std::exit(1);
    }

    return write_pid_file(spec.pid_file, static_cast<unsigned long long>(pid));
}

bool stop_service(const std::filesystem::path& pid_file) {
    unsigned long long pid = 0;
    if (!read_pid_file(pid_file, pid)) {
        return false;
    }

    if (!is_process_running(pid)) {
        remove_pid_file(pid_file);
        return false;
    }

    const bool ok = kill(static_cast<pid_t>(pid), SIGTERM) == 0;
    remove_pid_file(pid_file);
    return ok;
}
#endif

std::string read_file_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const std::string hex = s.substr(i + 1, 2);
            char* end = nullptr;
            const long value = std::strtol(hex.c_str(), &end, 16);
            if (end != hex.c_str() + 2) {
                out += s[i];
            } else {
                out += static_cast<char>(value);
                i += 2;
            }
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& q) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start < q.size()) {
        const size_t end = q.find('&', start);
        const std::string pair = q.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t eq = pair.find('=');
        const std::string key = url_decode(pair.substr(0, eq));
        const std::string value = (eq == std::string::npos) ? std::string{} : url_decode(pair.substr(eq + 1));
        if (!key.empty()) {
            out[key] = value;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

bool parse_port(const std::string& raw, int& out) {
    try {
        const int p = std::stoi(raw);
        if (p < 1024 || p > 65535) {
            return false;
        }
        out = p;
        return true;
    } catch (...) {
        return false;
    }
}

std::string get_env_or_default(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (!v || v[0] == '\0') {
        return fallback;
    }
    return std::string(v);
}

bool parse_int_range(const std::string& raw, int min_v, int max_v, int& out) {
    try {
        const int v = std::stoi(raw);
        if (v < min_v || v > max_v) {
            return false;
        }
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool normalize_transport(const std::string& raw, std::string& out) {
    std::string t;
    t.reserve(raw.size());
    for (char c : raw) {
        t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (t.empty() || t == "tcp") {
        out = "--tcp";
        return true;
    }
    if (t == "udp") {
        out = "--udp";
        return true;
    }
    return false;
}

bool is_local_bind_address(const std::string& ip) {
    return ip == "127.0.0.1" || ip == "localhost";
}

UiRuntimeConfig load_ui_config(int argc, char* argv[]) {
    UiRuntimeConfig cfg;
    cfg.bind_addr = get_env_or_default("SYNCFLOW_UI_BIND_ADDR", DEFAULT_UI_BIND_ADDR);
    cfg.api_token = get_env_or_default("SYNCFLOW_UI_TOKEN", "");

    const std::string env_port = get_env_or_default("SYNCFLOW_UI_PORT", std::to_string(DEFAULT_UI_PORT));
    int parsed_port = DEFAULT_UI_PORT;
    if (parse_int_range(env_port, 1024, 65535, parsed_port)) {
        cfg.ui_port = parsed_port;
    }

    if (argc >= 2) {
        int cli_port = DEFAULT_UI_PORT;
        if (!parse_int_range(argv[1], 1024, 65535, cli_port)) {
            std::cerr << "invalid port\n";
            std::exit(1);
        }
        cfg.ui_port = cli_port;
    }

    return cfg;
}

std::string capture_command(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {};
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
        return "failed to execute command\n";
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

std::string root_html(int port, bool token_required) {
    std::ostringstream html;
    html << R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Syncflow UI</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; font-family: Inter, system-ui, Arial, sans-serif; background: #0b1020; color: #e5e7eb; }
  .wrap { max-width: 1200px; margin: 0 auto; padding: 24px; }
  .hero { background: linear-gradient(135deg, #111827, #1f2937); border: 1px solid #263244; border-radius: 20px; padding: 24px; box-shadow: 0 20px 60px rgba(0,0,0,.25); }
  h1 { margin: 0 0 8px; font-size: 32px; }
  p { line-height: 1.5; color: #cbd5e1; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 16px; margin-top: 20px; }
  .card { background: rgba(17,24,39,.9); border: 1px solid #263244; border-radius: 18px; padding: 18px; }
  .card h2 { margin: 0 0 12px; font-size: 18px; }
  .row { display: grid; gap: 10px; margin-bottom: 12px; }
  label { font-size: 12px; color: #94a3b8; text-transform: uppercase; letter-spacing: .06em; }
  input, select, button {
    width: 100%; box-sizing: border-box; border-radius: 12px; border: 1px solid #334155; background: #0f172a; color: #e5e7eb;
    padding: 12px 14px; font-size: 14px;
  }
  button { cursor: pointer; background: linear-gradient(135deg, #2563eb, #7c3aed); border: 0; font-weight: 700; }
  button.secondary { background: #1f2937; border: 1px solid #334155; }
  button.danger { background: linear-gradient(135deg, #dc2626, #f97316); }
  .actions { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
  pre { white-space: pre-wrap; word-break: break-word; background: #020617; border: 1px solid #334155; border-radius: 16px; padding: 14px; min-height: 180px; overflow: auto; }
  .small { font-size: 12px; color: #94a3b8; }
  .status { display: inline-flex; gap: 8px; align-items: center; padding: 8px 12px; border-radius: 999px; background: rgba(59,130,246,.15); border: 1px solid rgba(59,130,246,.35); }
</style>
</head>
<body>
<div class="wrap">
  <div class="hero">
    <div class="status">Syncflow Web UI · port )HTML" << port << R"HTML(</div>
    <h1>Cross-platform file sync control center</h1>
    <p>Use this browser-based interface on Linux, Windows, macOS, or Android. It controls discovery, transfer, and auto-sync with one page.</p>
  </div>

  <div class="grid">
    <div class="card">
      <h2>Discovery</h2>
      <div class="actions">
        <button onclick="call('/api/discovery/start')">Start</button>
        <button class="danger" onclick="call('/api/discovery/stop')">Stop</button>
      </div>
      <div class="actions" style="margin-top:10px;">
        <button class="secondary" onclick="call('/api/discovery/status')">Status</button>
        <button class="secondary" onclick="call('/api/discovery/list')">List devices</button>
      </div>
    </div>

    <div class="card">
      <h2>File transfer</h2>
      <div class="row"><label>Transport</label><select id="txTransport"><option value="tcp">TCP</option><option value="udp">UDP</option></select></div>
      <div class="row"><label>Receiver port</label><input id="txPort" value="37030"></div>
      <div class="row"><label>Output directory</label><input id="txOut" value="received"></div>
      <div class="actions">
        <button onclick="call('/api/transfer/start?transport=' + val('txTransport') + '&port=' + enc(val('txPort')) + '&dir=' + enc(val('txOut')))">Start receiver</button>
        <button class="danger" onclick="call('/api/transfer/stop')">Stop receiver</button>
      </div>
      <div class="row" style="margin-top:10px;"><label>Send file</label></div>
      <div class="row"><input id="sendIp" placeholder="Peer IP"></div>
      <div class="row"><input id="sendPath" placeholder="/path/to/file"></div>
      <div class="actions"><button onclick="call('/api/transfer/send?transport=' + val('txTransport') + '&ip=' + enc(val('sendIp')) + '&port=' + enc(val('txPort')) + '&path=' + enc(val('sendPath')))">Send file</button><button class="secondary" onclick="call('/api/transfer/status')">Receiver status</button></div>
    </div>

    <div class="card">
      <h2>Auto sync</h2>
      <div class="row"><label>Transport</label><select id="syncTransport"><option value="tcp">TCP</option><option value="udp">UDP</option></select></div>
      <div class="row"><label>Peer IP</label><input id="syncPeer" placeholder="Peer IP"></div>
      <div class="row"><label>Port</label><input id="syncPort" value="37030"></div>
      <div class="row"><label>Sync directory</label><input id="syncDir" value="project_dir"></div>
      <div class="row"><label>Interval ms</label><input id="syncInterval" value="2000"></div>
      <div class="actions">
        <button onclick="call('/api/sync/start?mode=auto&transport=' + val('syncTransport') + '&peer=' + enc(val('syncPeer')) + '&port=' + enc(val('syncPort')) + '&dir=' + enc(val('syncDir')) + '&interval=' + enc(val('syncInterval')))">Start auto-sync</button>
        <button class="danger" onclick="call('/api/sync/stop')">Stop auto-sync</button>
      </div>
      <div class="actions" style="margin-top:10px;">
        <button class="secondary" onclick="call('/api/sync/status')">Sync status</button>
      </div>
    </div>
  </div>

    <div class="card" style="margin-top:16px;">
        <h2>Security</h2>
        <div class="row"><label>API Token (optional unless required)</label><input id="apiToken" placeholder="token"></div>
        <p class="small">Token required: )HTML" << (token_required ? "yes" : "no") << R"HTML(</p>
    </div>

    <div class="card" style="margin-top:16px;">
    <h2>Output</h2>
    <pre id="out">Ready.</pre>
  </div>
</div>
<script>
const out = document.getElementById('out');
function enc(v){ return encodeURIComponent(v || ''); }
function val(id){ return document.getElementById(id).value; }
function tokenSuffix(){
    const t = val('apiToken');
    if(!t) return '';
    return (t ? ( (arguments[0] ? '&' : '?') + 'token=' + enc(t) ) : '');
}
async function call(url){
    const hasQ = url.indexOf('?') >= 0;
    url = url + tokenSuffix(hasQ);
  out.textContent = 'Working... ' + url;
  try {
    const r = await fetch(url, { method: 'GET' });
    const t = await r.text();
    out.textContent = t;
  } catch (e) {
    out.textContent = String(e);
  }
}
</script>
</body>
</html>)HTML";
    return html.str();
}

std::string status_reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default: return "Error";
    }
}

std::string http_response(const std::string& body, const std::string& content_type = "text/plain; charset=utf-8", int status = 200) {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << status << " " << status_reason(status) << "\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "X-Content-Type-Options: nosniff\r\n"
       << "Cache-Control: no-store\r\n"
       << "Connection: close\r\n\r\n"
       << body;
    return ss.str();
}

std::string pid_status_line(const std::string& label, const std::filesystem::path& pid_file) {
    unsigned long long pid = 0;
    if (read_pid_file(pid_file, pid) && is_process_running(pid)) {
        return label + ": running (pid=" + std::to_string(pid) + ")\n";
    }
    return label + ": stopped\n";
}

std::string run_discovery_action(const std::string& action, const std::filesystem::path& discovery_bin) {
    if (action == "start") {
        const ServiceSpec spec{pid_file_path("discovery"), discovery_bin, {"server"}};
        return spawn_detached(spec) ? "discovery started\n" : "failed to start discovery\n";
    }
    if (action == "stop") {
        return stop_service(pid_file_path("discovery")) ? "discovery stopped\n" : "failed to stop discovery\n";
    }
    if (action == "status") {
        return pid_status_line("discovery", pid_file_path("discovery"));
    }
    if (action == "list") {
        return capture_command({discovery_bin.string(), "client"});
    }
    return "unknown discovery action\n";
}

std::string run_transfer_action(const std::string& action,
                                const std::filesystem::path& transfer_bin,
                                const std::map<std::string, std::string>& q) {
    if (action == "start") {
        auto it_transport = q.find("transport");
        auto it_port = q.find("port");
        auto it_dir = q.find("dir");
        const std::string transport = (it_transport != q.end() ? it_transport->second : "tcp");
        const std::string port = (it_port != q.end() ? it_port->second : std::to_string(DEFAULT_TRANSFER_PORT));
        const std::string dir = (it_dir != q.end() ? it_dir->second : "received");

        std::string mode;
        int p = DEFAULT_TRANSFER_PORT;
        if (!normalize_transport(transport, mode)) {
            return "invalid transport\n";
        }
        if (!parse_int_range(port, 1024, 65535, p)) {
            return "invalid port\n";
        }

        ServiceSpec spec{pid_file_path("transfer_recv"), transfer_bin, {"recv", mode, std::to_string(p), dir}};
        return spawn_detached(spec) ? "transfer receiver started\n" : "failed to start transfer receiver\n";
    }
    if (action == "stop") {
        return stop_service(pid_file_path("transfer_recv")) ? "transfer receiver stopped\n" : "failed to stop transfer receiver\n";
    }
    if (action == "status") {
        return pid_status_line("transfer receiver", pid_file_path("transfer_recv"));
    }
    if (action == "send") {
        const std::string transport = q.count("transport") ? q.at("transport") : "tcp";
        const std::string ip = q.count("ip") ? q.at("ip") : "";
        const std::string port = q.count("port") ? q.at("port") : std::to_string(DEFAULT_TRANSFER_PORT);
        const std::string path = q.count("path") ? q.at("path") : "";
        if (ip.empty() || path.empty()) {
            return "missing ip or path\n";
        }
        std::string mode;
        int p = DEFAULT_TRANSFER_PORT;
        if (!normalize_transport(transport, mode)) {
            return "invalid transport\n";
        }
        if (!parse_int_range(port, 1024, 65535, p)) {
            return "invalid port\n";
        }
        if (!std::filesystem::exists(path)) {
            return "file path not found\n";
        }
        return capture_command({transfer_bin.string(), "send", mode, ip, std::to_string(p), path});
    }
    return "unknown transfer action\n";
}

std::string run_sync_action(const std::string& action,
                            const std::filesystem::path& sync_bin,
                            const std::map<std::string, std::string>& q) {
    if (action == "start") {
        const std::string transport = q.count("transport") ? q.at("transport") : "tcp";
        const std::string peer = q.count("peer") ? q.at("peer") : "";
        const std::string port = q.count("port") ? q.at("port") : std::to_string(DEFAULT_TRANSFER_PORT);
        const std::string dir = q.count("dir") ? q.at("dir") : "project_dir";
        const std::string interval = q.count("interval") ? q.at("interval") : std::to_string(DEFAULT_SYNC_INTERVAL_MS);
        if (peer.empty()) {
            return "missing peer IP\n";
        }

        std::string mode;
        int p = DEFAULT_TRANSFER_PORT;
        int interval_ms = DEFAULT_SYNC_INTERVAL_MS;
        if (!normalize_transport(transport, mode)) {
            return "invalid transport\n";
        }
        if (!parse_int_range(port, 1024, 65535, p)) {
            return "invalid port\n";
        }
        if (!parse_int_range(interval, 200, 60000, interval_ms)) {
            return "invalid interval\n";
        }

        ServiceSpec spec{pid_file_path("sync"), sync_bin, {"auto", mode, peer, std::to_string(p), dir, std::to_string(interval_ms)}};
        return spawn_detached(spec) ? "auto-sync started\n" : "failed to start auto-sync\n";
    }
    if (action == "stop") {
        return stop_service(pid_file_path("sync")) ? "auto-sync stopped\n" : "failed to stop auto-sync\n";
    }
    if (action == "status") {
        return pid_status_line("auto-sync", pid_file_path("sync"));
    }
    return "unknown sync action\n";
}

std::string handle_request(const std::string& request_line,
                           const UiRuntimeConfig& cfg,
                           const std::filesystem::path& discovery_bin,
                           const std::filesystem::path& transfer_bin,
                           const std::filesystem::path& sync_bin) {
    std::istringstream iss(request_line);
    std::string method, target, version;
    iss >> method >> target >> version;
    if (method.empty() || target.empty()) {
        return http_response("bad request\n", "text/plain; charset=utf-8", 400);
    }

    const auto qpos = target.find('?');
    const std::string path = target.substr(0, qpos);
    const std::map<std::string, std::string> q = (qpos == std::string::npos) ? std::map<std::string, std::string>{} : parse_query(target.substr(qpos + 1));

    if (method != "GET") {
        return http_response("method not allowed\n", "text/plain; charset=utf-8", 405);
    }

    if (path == "/healthz") {
        return http_response("ok\n", "text/plain; charset=utf-8", 200);
    }

    if (path.rfind("/api/", 0) == 0 && !cfg.api_token.empty()) {
        auto it = q.find("token");
        if (it == q.end() || it->second != cfg.api_token) {
            return http_response("unauthorized\n", "text/plain; charset=utf-8", 401);
        }
    }

    if (path == "/") {
        return http_response(root_html(cfg.ui_port, !cfg.api_token.empty()), "text/html; charset=utf-8", 200);
    }
    if (path == "/api/discovery/start") return http_response(run_discovery_action("start", discovery_bin));
    if (path == "/api/discovery/stop") return http_response(run_discovery_action("stop", discovery_bin));
    if (path == "/api/discovery/status") return http_response(run_discovery_action("status", discovery_bin));
    if (path == "/api/discovery/list") return http_response(run_discovery_action("list", discovery_bin));

    if (path == "/api/transfer/start") return http_response(run_transfer_action("start", transfer_bin, q));
    if (path == "/api/transfer/stop") return http_response(run_transfer_action("stop", transfer_bin, q));
    if (path == "/api/transfer/status") return http_response(run_transfer_action("status", transfer_bin, q));
    if (path == "/api/transfer/send") return http_response(run_transfer_action("send", transfer_bin, q));

    if (path == "/api/sync/start") return http_response(run_sync_action("start", sync_bin, q));
    if (path == "/api/sync/stop") return http_response(run_sync_action("stop", sync_bin, q));
    if (path == "/api/sync/status") return http_response(run_sync_action("status", sync_bin, q));

    return http_response("not found\n", "text/plain; charset=utf-8", 404);
}

bool send_all(SocketHandle sock, const char* data, size_t len) {
    size_t total = 0;
    while (total < len) {
#ifdef _WIN32
        const int sent = send(sock, data + total, static_cast<int>(len - total), 0);
#else
        const int sent = ::send(sock, data + total, static_cast<int>(len - total), 0);
#endif
        if (sent <= 0) {
            return false;
        }
        total += static_cast<size_t>(sent);
    }
    return true;
}

std::string read_http_request(SocketHandle sock) {
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
        const int got = recv(sock, buffer, sizeof(buffer), 0);
#else
        const int got = ::recv(sock, buffer, sizeof(buffer), 0);
#endif
        if (got <= 0) {
            break;
        }
        request.append(buffer, buffer + got);
        if (request.size() > 64 * 1024) {
            break;
        }
    }
    return request;
}

void run_server(const UiRuntimeConfig& cfg,
                const std::filesystem::path& discovery_bin,
                const std::filesystem::path& transfer_bin,
                const std::filesystem::path& sync_bin) {
    SocketHandle server =
#ifdef _WIN32
        socket(AF_INET, SOCK_STREAM, 0);
#else
        ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (server == kInvalidSocket) {
        std::cerr << "failed to create UI socket\n";
        return;
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    if (cfg.bind_addr == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, cfg.bind_addr.c_str(), &addr.sin_addr) != 1) {
            std::cerr << "invalid SYNCFLOW_UI_BIND_ADDR: " << cfg.bind_addr << "\n";
            return;
        }
    }
    addr.sin_port = htons(static_cast<std::uint16_t>(cfg.ui_port));
    if (bind(server, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "failed to bind UI " << cfg.bind_addr << ":" << cfg.ui_port << "\n";
        close_socket(server);
        return;
    }

    if (listen(server, 16) < 0) {
        std::cerr << "failed to listen on UI port\n";
        close_socket(server);
        return;
    }

    std::cout << "Syncflow UI running on http://" << cfg.bind_addr << ":" << cfg.ui_port << "\n";
    if (cfg.api_token.empty()) {
        std::cout << "Warning: API token not set (set SYNCFLOW_UI_TOKEN for production)\n";
    }
    if (!is_local_bind_address(cfg.bind_addr) && cfg.api_token.empty()) {
        std::cout << "Warning: non-local bind without token is insecure\n";
    }
    while (true) {
        sockaddr_in client{};
#ifdef _WIN32
        int len = sizeof(client);
#else
        socklen_t len = sizeof(client);
#endif
        SocketHandle conn = accept(server, reinterpret_cast<sockaddr*>(&client), &len);
        if (conn == kInvalidSocket) {
            continue;
        }

        const std::string request = read_http_request(conn);
        const std::string first_line = request.substr(0, request.find("\r\n"));
        const std::string response = handle_request(first_line, cfg, discovery_bin, transfer_bin, sync_bin);
        send_all(conn, response.c_str(), response.size());

#ifdef _WIN32
        closesocket(conn);
#else
        close(conn);
#endif
    }

    close_socket(server);
}

} // namespace

int main(int argc, char* argv[]) {
    const UiRuntimeConfig cfg = load_ui_config(argc, argv);

#ifdef _WIN32
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    const auto discovery_bin = binary_path(argv[0], "syncflow_discovery");
    const auto transfer_bin = binary_path(argv[0], "syncflow_transfer");
    const auto sync_bin = binary_path(argv[0], "syncflow_sync");

    if (!std::filesystem::exists(discovery_bin) || !std::filesystem::exists(transfer_bin) || !std::filesystem::exists(sync_bin)) {
        std::cerr << "missing backend binaries in build directory\n";
        return 1;
    }

    run_server(cfg, discovery_bin, transfer_bin, sync_bin);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

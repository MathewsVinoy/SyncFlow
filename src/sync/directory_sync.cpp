#include <algorithm>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t PACKAGE_MAGIC_SINGLE = 0x53465031U; // SFP1
constexpr std::uint32_t PACKAGE_MAGIC_BATCH = 0x53465032U;  // SFP2
constexpr int DEFAULT_TRANSFER_PORT = 37030;
constexpr int DEFAULT_SYNC_INTERVAL_MS = 2000;

std::uint32_t bswap32(std::uint32_t x) {
    return ((x & 0x000000FFU) << 24U) |
           ((x & 0x0000FF00U) << 8U) |
           ((x & 0x00FF0000U) >> 8U) |
           ((x & 0xFF000000U) >> 24U);
}

std::uint64_t bswap64(std::uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8) |
           ((x & 0x000000FF00000000ULL) >> 8) |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
}

std::uint32_t host_to_be32(std::uint32_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return bswap32(x);
#else
    return x;
#endif
}

std::uint64_t host_to_be64(std::uint64_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return bswap64(x);
#else
    return x;
#endif
}

std::uint32_t be32_to_host(std::uint32_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return bswap32(x);
#else
    return x;
#endif
}

std::uint64_t be64_to_host(std::uint64_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return bswap64(x);
#else
    return x;
#endif
}

bool parse_int(const std::string& raw, int& out, int min_v, int max_v) {
    try {
        int v = std::stoi(raw);
        if (v < min_v || v > max_v) {
            return false;
        }
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

std::filesystem::path binary_path(const char* argv0, const char* name) {
    std::filesystem::path self = std::filesystem::absolute(argv0);
#ifdef _WIN32
    return self.parent_path() / (std::string(name) + ".exe");
#else
    return self.parent_path() / name;
#endif
}

bool run_command(const std::string& cmd) {
    return std::system(cmd.c_str()) == 0;
}

struct FileSnapshot {
    std::uint64_t mtime = 0;
    std::uint64_t size = 0;
};

enum class EntryKind : std::uint8_t {
    Upsert = 1,
    Delete = 2,
};

struct DeltaEntry {
    EntryKind kind = EntryKind::Upsert;
    std::filesystem::path rel;
    FileSnapshot snap{};
};

struct SyncState {
    std::mutex mu;
    std::map<std::filesystem::path, FileSnapshot> recently_received;
    std::set<std::filesystem::path> recently_deleted;
};

bool should_ignore_relative_path(const std::filesystem::path& rel) {
    const auto s = rel.generic_string();
    return s == ".syncflow_inbox" ||
           s.rfind(".syncflow_inbox/", 0) == 0 ||
           s == ".syncflow_spool" ||
           s.rfind(".syncflow_spool/", 0) == 0;
}

std::map<std::filesystem::path, FileSnapshot> scan_files(const std::filesystem::path& root) {
    std::map<std::filesystem::path, FileSnapshot> out;
    if (!std::filesystem::exists(root)) {
        return out;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto abs = entry.path();
        const auto rel = std::filesystem::relative(abs, root).lexically_normal();
        if (should_ignore_relative_path(rel)) {
            continue;
        }
        FileSnapshot snap{};
        snap.size = static_cast<std::uint64_t>(entry.file_size());
        snap.mtime = static_cast<std::uint64_t>(entry.last_write_time().time_since_epoch().count());
        out[rel] = snap;
    }
    return out;
}

bool copy_stream(std::ifstream& in, std::ofstream& out, std::uint64_t bytes) {
    std::vector<char> buf(64 * 1024);
    std::uint64_t remaining = bytes;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buf.size()));
        in.read(buf.data(), chunk);
        if (in.gcount() != chunk) {
            return false;
        }
        out.write(buf.data(), chunk);
        if (!out.good()) {
            return false;
        }
        remaining -= static_cast<std::uint64_t>(chunk);
    }
    return true;
}

bool create_delta_package(const std::filesystem::path& source_root,
                          const std::vector<DeltaEntry>& entries,
                          const std::filesystem::path& package_path) {
    if (entries.empty()) {
        return false;
    }

    std::ofstream out(package_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    const std::uint32_t magic = host_to_be32(PACKAGE_MAGIC_BATCH);
    const std::uint32_t count_be = host_to_be32(static_cast<std::uint32_t>(entries.size()));
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&count_be), sizeof(count_be));
    if (!out.good()) {
        return false;
    }

    for (const auto& e : entries) {
        const auto rel_str = e.rel.generic_string();
        if (rel_str.empty() || rel_str.size() > 4096) {
            return false;
        }

        const std::uint8_t kind = static_cast<std::uint8_t>(e.kind);
        const std::uint32_t rel_len_be = host_to_be32(static_cast<std::uint32_t>(rel_str.size()));
        const std::uint64_t size_be = host_to_be64(e.snap.size);
        const std::uint64_t mtime_be = host_to_be64(e.snap.mtime);

        out.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
        out.write(reinterpret_cast<const char*>(&rel_len_be), sizeof(rel_len_be));
        out.write(reinterpret_cast<const char*>(&size_be), sizeof(size_be));
        out.write(reinterpret_cast<const char*>(&mtime_be), sizeof(mtime_be));
        out.write(rel_str.data(), static_cast<std::streamsize>(rel_str.size()));
        if (!out.good()) {
            return false;
        }

        if (e.kind == EntryKind::Delete) {
            continue;
        }

        const auto abs = source_root / e.rel;
        if (!std::filesystem::exists(abs) || !std::filesystem::is_regular_file(abs)) {
            return false;
        }

        std::ifstream in(abs, std::ios::binary);
        if (!in.is_open()) {
            return false;
        }
        if (!copy_stream(in, out, e.snap.size)) {
            return false;
        }
    }

    return out.good();
}

bool path_has_parent_ref(const std::filesystem::path& p) {
    for (const auto& part : p) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

void cleanup_empty_parent_dirs(const std::filesystem::path& root, const std::filesystem::path& from_path) {
    std::error_code ec;
    auto cur = from_path;
    while (!cur.empty() && cur != root) {
        if (!std::filesystem::exists(cur, ec) || !std::filesystem::is_directory(cur, ec)) {
            break;
        }
        if (!std::filesystem::is_empty(cur, ec)) {
            break;
        }
        std::filesystem::remove(cur, ec);
        if (ec) {
            break;
        }
        cur = cur.parent_path();
    }
}

bool unpack_package_file(const std::filesystem::path& package_file,
                         const std::filesystem::path& output_root,
                         std::vector<DeltaEntry>* out_entries = nullptr) {
    std::ifstream in(package_file, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    std::uint32_t magic_be = 0;
    in.read(reinterpret_cast<char*>(&magic_be), sizeof(magic_be));
    if (!in.good()) {
        return false;
    }

    const std::uint32_t magic = be32_to_host(magic_be);

    if (magic == PACKAGE_MAGIC_SINGLE) {
        std::uint32_t rel_len_be = 0;
        std::uint64_t size_be = 0;

        in.read(reinterpret_cast<char*>(&rel_len_be), sizeof(rel_len_be));
        in.read(reinterpret_cast<char*>(&size_be), sizeof(size_be));
        if (!in.good()) {
            return false;
        }

        const std::uint32_t rel_len = be32_to_host(rel_len_be);
        const std::uint64_t file_size = be64_to_host(size_be);
        if (rel_len == 0 || rel_len > 4096) {
            return false;
        }

        std::string rel(rel_len, '\0');
        in.read(rel.data(), static_cast<std::streamsize>(rel_len));
        if (!in.good()) {
            return false;
        }

        const std::filesystem::path rel_path = std::filesystem::path(rel).lexically_normal();
        if (rel_path.is_absolute() || path_has_parent_ref(rel_path)) {
            return false;
        }

        const auto target = (output_root / rel_path).lexically_normal();
        std::filesystem::create_directories(target.parent_path());

        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }

        if (!copy_stream(in, out, file_size)) {
            return false;
        }

        std::cout << "Synced: " << rel_path.generic_string() << "\n";
        if (out_entries) {
            FileSnapshot snap{};
            snap.size = file_size;
            std::error_code ec;
            snap.mtime = static_cast<std::uint64_t>(std::filesystem::last_write_time(target, ec).time_since_epoch().count());
            out_entries->push_back(DeltaEntry{EntryKind::Upsert, rel_path, snap});
        }
        return true;
    }

    if (magic != PACKAGE_MAGIC_BATCH) {
        return false;
    }

    std::uint32_t count_be = 0;
    in.read(reinterpret_cast<char*>(&count_be), sizeof(count_be));
    if (!in.good()) {
        return false;
    }

    const std::uint32_t entry_count = be32_to_host(count_be);
    if (entry_count == 0 || entry_count > 100000) {
        return false;
    }

    for (std::uint32_t i = 0; i < entry_count; ++i) {
        std::uint8_t kind_raw = 0;
        std::uint32_t rel_len_be = 0;
        std::uint64_t size_be = 0;
        std::uint64_t mtime_be = 0;

        in.read(reinterpret_cast<char*>(&kind_raw), sizeof(kind_raw));
        in.read(reinterpret_cast<char*>(&rel_len_be), sizeof(rel_len_be));
        in.read(reinterpret_cast<char*>(&size_be), sizeof(size_be));
        in.read(reinterpret_cast<char*>(&mtime_be), sizeof(mtime_be));
        if (!in.good()) {
            return false;
        }

        const std::uint32_t rel_len = be32_to_host(rel_len_be);
        const std::uint64_t file_size = be64_to_host(size_be);
        const std::uint64_t mtime = be64_to_host(mtime_be);

        if (rel_len == 0 || rel_len > 4096) {
            return false;
        }

        std::string rel(rel_len, '\0');
        in.read(rel.data(), static_cast<std::streamsize>(rel_len));
        if (!in.good()) {
            return false;
        }

        const std::filesystem::path rel_path = std::filesystem::path(rel).lexically_normal();
        if (rel_path.is_absolute() || path_has_parent_ref(rel_path)) {
            return false;
        }

        const auto target = (output_root / rel_path).lexically_normal();

        if (kind_raw == static_cast<std::uint8_t>(EntryKind::Delete)) {
            std::error_code ec;
            std::filesystem::remove(target, ec);
            cleanup_empty_parent_dirs(output_root, target.parent_path());
            std::cout << "Deleted: " << rel_path.generic_string() << "\n";
            if (out_entries) {
                out_entries->push_back(DeltaEntry{EntryKind::Delete, rel_path, FileSnapshot{}});
            }
            continue;
        }

        if (kind_raw != static_cast<std::uint8_t>(EntryKind::Upsert)) {
            return false;
        }

        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        if (!copy_stream(in, out, file_size)) {
            return false;
        }

        std::cout << "Synced: " << rel_path.generic_string() << "\n";
        if (out_entries) {
            out_entries->push_back(DeltaEntry{EntryKind::Upsert, rel_path, FileSnapshot{mtime, file_size}});
        }
    }

    return true;
}

std::set<std::filesystem::path> list_regular_files(const std::filesystem::path& dir) {
    std::set<std::filesystem::path> out;
    if (!std::filesystem::exists(dir)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            out.insert(entry.path());
        }
    }
    return out;
}

std::vector<std::filesystem::path> diff_new_files(const std::set<std::filesystem::path>& before,
                                                  const std::set<std::filesystem::path>& after) {
    std::vector<std::filesystem::path> out;
    for (const auto& p : after) {
        if (before.find(p) == before.end()) {
            out.push_back(p);
        }
    }
    return out;
}

int send_one_file(const std::filesystem::path& transfer_bin,
                  const std::string& transport_flag,
                  const std::string& ip,
                  int port,
                  const std::filesystem::path& package_file) {
    const std::string cmd =
        "\"" + transfer_bin.string() + "\" send " + transport_flag +
        " \"" + ip + "\" \"" + std::to_string(port) + "\" \"" + package_file.string() + "\"";
    return run_command(cmd) ? 0 : 1;
}

int run_send_loop(const std::filesystem::path& transfer_bin,
                  const std::string& transport_flag,
                  const std::string& ip,
                  int port,
                  const std::filesystem::path& source_dir,
                  int interval_ms,
                  SyncState* state = nullptr) {
    if (!std::filesystem::exists(source_dir) || !std::filesystem::is_directory(source_dir)) {
        std::cerr << "Source directory does not exist: " << source_dir << "\n";
        return 1;
    }

    const auto spool_dir = std::filesystem::temp_directory_path() / "syncflow_spool";
    std::filesystem::create_directories(spool_dir);

    std::map<std::filesystem::path, FileSnapshot> prev;

    while (true) {
        const auto now = scan_files(source_dir);

        std::vector<DeltaEntry> delta;
        delta.reserve(now.size());

        for (const auto& [rel, snap] : now) {
            auto it = prev.find(rel);
            const bool changed = (it == prev.end()) || it->second.mtime != snap.mtime || it->second.size != snap.size;
            if (!changed) {
                continue;
            }

            if (state) {
                std::lock_guard<std::mutex> lock(state->mu);
                auto rr = state->recently_received.find(rel);
                if (rr != state->recently_received.end() && rr->second.mtime == snap.mtime && rr->second.size == snap.size) {
                    prev[rel] = snap;
                    state->recently_received.erase(rr);
                    continue;
                }
            }

            delta.push_back(DeltaEntry{EntryKind::Upsert, rel, snap});
        }

        for (const auto& [rel, old_snap] : prev) {
            (void)old_snap;
            if (now.find(rel) != now.end()) {
                continue;
            }

            if (state) {
                std::lock_guard<std::mutex> lock(state->mu);
                auto it_del = state->recently_deleted.find(rel);
                if (it_del != state->recently_deleted.end()) {
                    state->recently_deleted.erase(it_del);
                    continue;
                }
            }

            delta.push_back(DeltaEntry{EntryKind::Delete, rel, FileSnapshot{}});
        }

        if (delta.empty()) {
            prev = now;
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            continue;
        }

        const auto package_name = "pkg_" + std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())) + ".bin";
        const auto pkg = spool_dir / package_name;

        if (!create_delta_package(source_dir, delta, pkg)) {
            std::cerr << "Failed creating delta package\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            continue;
        }

        if (send_one_file(transfer_bin, transport_flag, ip, port, pkg) == 0) {
            std::size_t puts = 0;
            std::size_t dels = 0;
            for (const auto& e : delta) {
                if (e.kind == EntryKind::Upsert) {
                    ++puts;
                } else {
                    ++dels;
                }
            }
            std::cout << "Pushed batch: upsert=" << puts << " delete=" << dels << "\n";
        } else {
            std::cerr << "Send failed for delta package\n";
        }

        std::error_code ec;
        std::filesystem::remove(pkg, ec);

        prev = now;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

int run_recv_loop(const std::filesystem::path& transfer_bin,
                  const std::string& transport_flag,
                  int port,
                  const std::filesystem::path& output_dir,
                  SyncState* state = nullptr) {
    const auto inbox = output_dir / ".syncflow_inbox";
    std::filesystem::create_directories(inbox);
    std::filesystem::create_directories(output_dir);

    while (true) {
        const auto before = list_regular_files(inbox);

        const std::string recv_cmd =
            "\"" + transfer_bin.string() + "\" recv " + transport_flag +
            " \"" + std::to_string(port) + "\" \"" + inbox.string() + "\"";

        if (!run_command(recv_cmd)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        const auto after = list_regular_files(inbox);
        const auto newcomers = diff_new_files(before, after);

        for (const auto& pkg : newcomers) {
            std::vector<DeltaEntry> applied;
            if (!unpack_package_file(pkg, output_dir, &applied)) {
                std::cerr << "Invalid sync package: " << pkg << "\n";
            } else if (state) {
                std::lock_guard<std::mutex> lock(state->mu);
                for (const auto& e : applied) {
                    if (e.kind == EntryKind::Upsert) {
                        state->recently_received[e.rel] = e.snap;
                        state->recently_deleted.erase(e.rel);
                    } else {
                        state->recently_received.erase(e.rel);
                        state->recently_deleted.insert(e.rel);
                    }
                }
            }
            std::error_code ec;
            std::filesystem::remove(pkg, ec);
        }
    }
}

int run_auto_loop(const std::filesystem::path& transfer_bin,
                  const std::string& transport_flag,
                  const std::string& peer_ip,
                  int port,
                  const std::filesystem::path& sync_dir,
                  int interval_ms) {
    if (!std::filesystem::exists(sync_dir)) {
        std::filesystem::create_directories(sync_dir);
    }
    if (!std::filesystem::is_directory(sync_dir)) {
        std::cerr << "Sync path is not a directory: " << sync_dir << "\n";
        return 1;
    }

    SyncState state;
    std::thread recv_thread([&]() {
        (void)run_recv_loop(transfer_bin, transport_flag, port, sync_dir, &state);
    });

    const int send_code = run_send_loop(transfer_bin, transport_flag, peer_ip, port, sync_dir, interval_ms, &state);
    recv_thread.join();
    return send_code;
}

void print_usage() {
    std::cout << "Usage:\n"
              << "  syncflow_sync send [--tcp|--udp] <ip> [port] <source_dir> [interval_ms]\n"
              << "  syncflow_sync recv [--tcp|--udp] [port] [output_dir]\n"
              << "  syncflow_sync auto [--tcp|--udp] <peer_ip> [port] <sync_dir> [interval_ms]\n"
              << "Defaults: tcp, port=37030, interval_ms=2000, output_dir=./synced\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::filesystem::path transfer_bin = binary_path(argv[0], "syncflow_transfer");
    if (!std::filesystem::exists(transfer_bin)) {
        std::cerr << "syncflow_transfer binary not found: " << transfer_bin << "\n";
        return 1;
    }

    const std::string mode = argv[1];
    int idx = 2;
    std::string transport_flag = "--tcp";

    if (idx < argc) {
        const std::string t = argv[idx];
        if (t == "--tcp" || t == "--udp") {
            transport_flag = t;
            ++idx;
        }
    }

    if (mode == "send") {
        if (idx >= argc) {
            print_usage();
            return 1;
        }

        const std::string ip = argv[idx++];
        int port = DEFAULT_TRANSFER_PORT;

        if (idx < argc && idx + 1 < argc) {
            int parsed = 0;
            if (parse_int(argv[idx], parsed, 1024, 65535)) {
                port = parsed;
                ++idx;
            }
        }

        if (idx >= argc) {
            print_usage();
            return 1;
        }

        const std::filesystem::path source_dir = argv[idx++];
        int interval_ms = DEFAULT_SYNC_INTERVAL_MS;
        if (idx < argc) {
            int parsed = 0;
            if (!parse_int(argv[idx], parsed, 200, 60000)) {
                std::cerr << "Invalid interval_ms: " << argv[idx] << "\n";
                return 1;
            }
            interval_ms = parsed;
        }

        return run_send_loop(transfer_bin, transport_flag, ip, port, source_dir, interval_ms);
    }

    if (mode == "recv") {
        int port = DEFAULT_TRANSFER_PORT;
        std::filesystem::path output_dir = "synced";

        if (idx < argc) {
            int parsed = 0;
            if (parse_int(argv[idx], parsed, 1024, 65535)) {
                port = parsed;
                ++idx;
            }
        }

        if (idx < argc) {
            output_dir = argv[idx++];
        }

        return run_recv_loop(transfer_bin, transport_flag, port, output_dir, nullptr);
    }

    if (mode == "auto") {
        if (idx >= argc) {
            print_usage();
            return 1;
        }

        const std::string peer_ip = argv[idx++];
        int port = DEFAULT_TRANSFER_PORT;

        if (idx < argc && idx + 1 < argc) {
            int parsed = 0;
            if (parse_int(argv[idx], parsed, 1024, 65535)) {
                port = parsed;
                ++idx;
            }
        }

        if (idx >= argc) {
            print_usage();
            return 1;
        }

        const std::filesystem::path sync_dir = argv[idx++];
        int interval_ms = DEFAULT_SYNC_INTERVAL_MS;
        if (idx < argc) {
            int parsed = 0;
            if (!parse_int(argv[idx], parsed, 200, 60000)) {
                std::cerr << "Invalid interval_ms: " << argv[idx] << "\n";
                return 1;
            }
            interval_ms = parsed;
        }

        return run_auto_loop(transfer_bin, transport_flag, peer_ip, port, sync_dir, interval_ms);
    }

    print_usage();
    return 1;
}

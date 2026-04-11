// src/platform/file_system.cpp

#include <syncflow/platform/platform.h>
#include <syncflow/common/logger.h>
#include <fstream>
#include <memory>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define MKDIR(p) _mkdir(p)
#else
    #include <sys/stat.h>
    #include <dirent.h>
    #include <unistd.h>
    #define MKDIR(p) mkdir(p, 0755)
#endif

namespace syncflow::platform {

class FileSystemImpl : public FileSystem {
public:
    bool file_exists(const std::string& path) override {
        std::ifstream file(path);
        return file.good();
    }
    
    bool directory_exists(const std::string& path) override {
#ifdef _WIN32
        DWORD attribs = GetFileAttributesA(path.c_str());
        return (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
    }
    
    bool get_file_stats(const std::string& path, FileStats& stats) override {
#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA file_data;
        if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &file_data)) {
            return false;
        }
        
        stats.is_directory = (file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        stats.is_symlink = false;
        
        ULARGE_INTEGER size;
        size.LowPart = file_data.nFileSizeLow;
        size.HighPart = file_data.nFileSizeHigh;
        stats.size = size.QuadPart;
        
        // Convert Windows FILETIME to Unix timestamp
        ULARGE_INTEGER time;
        time.LowPart = file_data.ftLastWriteTime.dwLowDateTime;
        time.HighPart = file_data.ftLastWriteTime.dwHighDateTime;
        stats.modification_time = (time.QuadPart - 116444736000000000LL) / 10000000;
        
        return true;
#else
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            return false;
        }
        
        stats.size = st.st_size;
        stats.is_directory = S_ISDIR(st.st_mode);
        stats.is_symlink = S_ISLNK(st.st_mode);
        stats.modification_time = st.st_mtime;
        stats.permissions = st.st_mode;
        
        return true;
#endif
    }
    
    bool create_directory(const std::string& path) override {
        return MKDIR(path.c_str()) == 0 || directory_exists(path);
    }
    
    bool create_directories(const std::string& path) override {
        std::string current_path;
        
#ifdef _WIN32
        const char sep = '\\';
#else
        const char sep = '/';
#endif
        
        for (size_t i = 0; i < path.length(); ++i) {
            if (path[i] == sep || i == path.length() - 1) {
                if (i == path.length() - 1 && path[i] != sep) {
                    current_path += path[i];
                }
                if (!current_path.empty() && !directory_exists(current_path)) {
                    if (!create_directory(current_path)) {
                        return false;
                    }
                }
                current_path += sep;
            } else {
                current_path += path[i];
            }
        }
        return true;
    }
    
    bool delete_file(const std::string& path) override {
#ifdef _WIN32
        return DeleteFileA(path.c_str()) != 0;
#else
        return unlink(path.c_str()) == 0;
#endif
    }
    
    bool delete_directory(const std::string& path) override {
#ifdef _WIN32
        return RemoveDirectoryA(path.c_str()) != 0;
#else
        return rmdir(path.c_str()) == 0;
#endif
    }
    
    bool rename_file(const std::string& old_path, const std::string& new_path) override {
        return std::rename(old_path.c_str(), new_path.c_str()) == 0;
    }
    
    bool list_directory(const std::string& path, std::vector<std::string>& entries) override {
#ifdef _WIN32
        WIN32_FIND_DATAA find_data;
        HANDLE find_handle = FindFirstFileA((path + "\\*").c_str(), &find_data);
        
        if (find_handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        do {
            std::string name = find_data.cFileName;
            if (name != "." && name != "..") {
                entries.push_back(name);
            }
        } while (FindNextFileA(find_handle, &find_data));
        
        FindClose(find_handle);
        return true;
#else
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            return false;
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name != "." && name != "..") {
                entries.push_back(name);
            }
        }
        
        closedir(dir);
        return true;
#endif
    }
    
    std::string absolute_path(const std::string& path) override {
#ifdef _WIN32
        char abs_path[MAX_PATH];
        if (GetFullPathNameA(path.c_str(), MAX_PATH, abs_path, nullptr)) {
            return std::string(abs_path);
        }
        return path;
#else
        char abs_path[PATH_MAX];
        if (realpath(path.c_str(), abs_path)) {
            return std::string(abs_path);
        }
        return path;
#endif
    }
    
    std::string canonical_path(const std::string& path) override {
        return absolute_path(path);
    }
    
    std::string get_filename(const std::string& path) override {
        size_t sep_pos = path.find_last_of("/\\");
        if (sep_pos == std::string::npos) {
            return path;
        }
        return path.substr(sep_pos + 1);
    }
    
    std::string get_directory(const std::string& path) override {
        size_t sep_pos = path.find_last_of("/\\");
        if (sep_pos == std::string::npos) {
            return ".";
        }
        return path.substr(0, sep_pos);
    }
    
    std::string join_paths(const std::vector<std::string>& components) override {
#ifdef _WIN32
        const char sep = '\\';
#else
        const char sep = '/';
#endif
        
        std::string result;
        for (size_t i = 0; i < components.size(); ++i) {
            if (i > 0) result += sep;
            result += components[i];
        }
        return result;
    }
    
    std::vector<uint8_t> read_file(const std::string& path) override {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("FileSystem", "Failed to open file: " + path);
            return {};
        }
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        
        return data;
    }
    
    bool write_file(const std::string& path, const std::vector<uint8_t>& data) override {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("FileSystem", "Failed to open file for writing: " + path);
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        return file.good();
    }
    
    bool write_file_chunk(const std::string& path, uint64_t offset, 
                          const std::vector<uint8_t>& data) override {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            LOG_ERROR("FileSystem", "Failed to open file for chunk write: " + path);
            return false;
        }
        
        file.seekp(offset);
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        
        return file.good();
    }
};

std::unique_ptr<FileSystem> FileSystem::create() {
    return std::make_unique<FileSystemImpl>();
}

} // namespace syncflow::platform

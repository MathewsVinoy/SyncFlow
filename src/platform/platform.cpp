// src/platform/platform.cpp

#include <syncflow/platform/platform.h>
#include <syncflow/common/logger.h>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#else
    #include <unistd.h>
    #include <sys/utsname.h>
    #include <thread>
#endif

namespace syncflow::platform {

PlatformInfo get_platform_info() {
    PlatformInfo info;
    
#ifdef _WIN32
    info.os_name = "Windows";
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    info.num_cpu_cores = si.dwNumberOfProcessors;
    
    // Get Windows version
    OSVERSIONINFO osvi = {sizeof(OSVERSIONINFO)};
    GetVersionEx(&osvi);
    info.os_version = std::to_string(osvi.dwMajorVersion) + "." + 
                      std::to_string(osvi.dwMinorVersion);
    
    #ifdef _M_X64
        info.architecture = "x86_64";
    #elif _M_IX86
        info.architecture = "x86";
    #endif
    
    info.compiler = "MSVC";
    
#elif defined(__APPLE__)
    info.os_name = "macOS";
    info.architecture = "x86_64";
    info.compiler = "Clang";
    
    // Get CPU count
    info.num_cpu_cores = std::thread::hardware_concurrency();
    
#else
    info.os_name = "Linux";
    struct utsname uts;
    uname(&uts);
    info.os_version = std::string(uts.release);
    info.architecture = std::string(uts.machine);
    info.compiler = "GCC";
    info.num_cpu_cores = std::thread::hardware_concurrency();
#endif
    
    return info;
}

std::string get_config_directory() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return std::string(path) + "\\syncflow";
    }
    return "";
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/syncflow";
    }
    return "";
#endif
}

std::string get_data_directory() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return std::string(path) + "\\syncflow\\data";
    }
    return "";
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.local/share/syncflow";
    }
    return "";
#endif
}

std::string get_temp_directory() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (GetTempPathA(MAX_PATH, path)) {
        return std::string(path);
    }
    return "C:\\Temp";
#else
    return "/tmp";
#endif
}

} // namespace syncflow::platform

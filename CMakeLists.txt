cmake_minimum_required(VERSION 3.20)
project(syncflow VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SYNCFLOW_WINDOWS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SYNCFLOW_LINUX TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(SYNCFLOW_MACOS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(SYNCFLOW_ANDROID TRUE)
endif()

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags for optimization
if(MSVC)
    # MSVC-specific compiler flags
    add_compile_options(/W4 /O2 /Oi /Ot)
    # Add NOMINMAX to prevent Windows.h min/max macros
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
    # MSVC runtime library
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
    if(SYNCFLOW_LINUX OR SYNCFLOW_ANDROID)
        add_compile_options(-pthread)
    endif()
endif()

# Android/Termux-specific configuration
if(SYNCFLOW_ANDROID)
    # For Termux, we use standard POSIX APIs
    # No special Android NDK features required
    add_compile_definitions(__ANDROID__)
    # Termux provides glibc-compatible environment
    message(STATUS "Building for Android/Termux")
endif()

# Windows-specific configuration
if(SYNCFLOW_WINDOWS)
    # Minimum Windows version: Windows 7
    add_compile_definitions(_WIN32_WINNT=0x0601)
    # Enable advanced networking features
    add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Find required packages
find_package(Threads REQUIRED)

# Core libraries
add_library(syncflow_common
    src/common/types.cpp
    src/common/logger.cpp
    src/common/utils.cpp
    src/common/config.cpp
)
target_include_directories(syncflow_common PUBLIC include)
target_link_libraries(syncflow_common PUBLIC Threads::Threads)

# Platform abstraction layer
add_library(syncflow_platform
    src/platform/platform.cpp
    src/platform/file_system.cpp
    src/platform/networking.cpp
    src/platform/thread_pool.cpp
)
target_include_directories(syncflow_platform PUBLIC include)
target_link_libraries(syncflow_platform PUBLIC syncflow_common)

if(SYNCFLOW_LINUX)
    target_sources(syncflow_platform PRIVATE src/platform/linux/file_watcher.cpp)
endif()

if(SYNCFLOW_WINDOWS)
    target_sources(syncflow_platform PRIVATE 
        src/platform/windows/file_watcher.cpp
        src/platform/windows/network_config.cpp
    )
    target_link_libraries(syncflow_platform PUBLIC ws2_32 Shlwapi iphlpapi)
endif()

if(SYNCFLOW_MACOS)
    target_sources(syncflow_platform PRIVATE src/platform/macos/file_watcher.cpp)
    target_link_libraries(syncflow_platform PUBLIC "-framework CoreServices")
endif()

if(SYNCFLOW_ANDROID)
    target_sources(syncflow_platform PRIVATE 
        src/platform/android/file_watcher.cpp
        src/platform/android/network_config.cpp
    )
    # Termux provides standard POSIX socket libraries
    # log and android libraries only if building with NDK
    if(DEFINED ANDROID_ABI)
        target_link_libraries(syncflow_platform PUBLIC log android)
    endif()
endif()

# Discovery module
add_library(syncflow_discovery
    src/discovery/device_manager.cpp
    src/discovery/discovery_engine.cpp
    src/discovery/device.cpp
)
target_include_directories(syncflow_discovery PUBLIC include)
target_link_libraries(syncflow_discovery PUBLIC syncflow_platform)

# Transfer module
add_library(syncflow_transfer
    src/transfer/transfer_manager.cpp
    src/transfer/file_transfer.cpp
    src/transfer/transfer_protocol.cpp
    src/transfer/chunk_manager.cpp
)
target_include_directories(syncflow_transfer PUBLIC include)
target_link_libraries(syncflow_transfer PUBLIC syncflow_platform)

# Watcher module
add_library(syncflow_watcher
    src/watcher/file_watcher.cpp
    src/watcher/fs_monitor.cpp
)
target_include_directories(syncflow_watcher PUBLIC include)
target_link_libraries(syncflow_watcher PUBLIC syncflow_platform)

# Sync module
add_library(syncflow_sync
    src/sync/sync_engine.cpp
    src/sync/conflict_resolver.cpp
    src/sync/file_manifest.cpp
)
target_include_directories(syncflow_sync PUBLIC include)
target_link_libraries(syncflow_sync PUBLIC syncflow_watcher syncflow_transfer)

# CLI application
add_executable(syncflow
    src/cli/main.cpp
    src/cli/cli_handler.cpp
    src/cli/commands.cpp
)
target_include_directories(syncflow PRIVATE include)
target_link_libraries(syncflow PRIVATE syncflow_sync syncflow_discovery)

# Testing
enable_testing()
add_subdirectory(tests)

# Installation
install(TARGETS syncflow DESTINATION bin)
install(TARGETS syncflow_common syncflow_platform syncflow_discovery 
        syncflow_transfer syncflow_watcher syncflow_sync
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

# Install man page
install(FILES docs/syncflow.1 DESTINATION share/man/man1)

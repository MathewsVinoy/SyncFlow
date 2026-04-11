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
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
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
    target_sources(syncflow_platform PRIVATE src/platform/windows/file_watcher.cpp)
    target_link_libraries(syncflow_platform PUBLIC ws2_32 Shlwapi)
endif()

if(SYNCFLOW_MACOS)
    target_sources(syncflow_platform PRIVATE src/platform/macos/file_watcher.cpp)
    target_link_libraries(syncflow_platform PUBLIC "-framework CoreServices")
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

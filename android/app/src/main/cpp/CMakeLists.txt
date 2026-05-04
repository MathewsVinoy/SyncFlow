cmake_minimum_required(VERSION 3.22.1)

project("syncflow")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(syncflow_core_android STATIC
        ../../../../../src/core/logger.cpp
        ../../../../../src/file_sync/file_sync.cpp
        ../../../../../src/platform/system_info.cpp
        ../../../../../src/networking/peer_protocol.cpp
        ../../../../../src/networking/file_transfer.cpp
        ../../../../../src/networking/peer_node.cpp)

target_include_directories(syncflow_core_android PUBLIC
        ../../../../../include
        ../../../../../src)

target_compile_options(syncflow_core_android PRIVATE -Wall -Wextra -Wpedantic)

# Create the native bridge shared library
add_library(native-lib SHARED native_bridge.cpp)

# Link necessary libraries
find_library(log-lib log)

target_link_libraries(native-lib
        syncflow_core_android
        ${log-lib}
        android)
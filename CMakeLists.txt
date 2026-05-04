cmake_minimum_required(VERSION 3.16)

project(syncflow_peer_demo LANGUAGES CXX)

# Allow embedding a device name at build time. If not provided, defaults to project name.
if(NOT DEFINED DEVICE_NAME)
    set(DEVICE_NAME "${PROJECT_NAME}" CACHE STRING "Device name embedded into the build")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(syncflow_peer
    src/main.cpp
)

target_compile_definitions(syncflow_peer PRIVATE DEVICE_NAME="${DEVICE_NAME}")

add_library(syncflow_core
    src/core/logger.cpp
    src/file_sync/file_sync.cpp
    src/platform/system_info.cpp
    src/networking/peer_protocol.cpp
    src/networking/peer_node.cpp
    src/networking/file_transfer.cpp
)

target_include_directories(syncflow_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(syncflow_core PRIVATE -Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
target_link_libraries(syncflow_core PUBLIC Threads::Threads)

target_link_libraries(syncflow_peer PRIVATE syncflow_core)

target_include_directories(syncflow_peer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(syncflow_peer PRIVATE -Wall -Wextra -Wpedantic)

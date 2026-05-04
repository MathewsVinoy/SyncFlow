cmake_minimum_required(VERSION 3.16)

project(syncflow_peer_demo LANGUAGES CXX)

# Allow embedding a device name at build time. If not provided, defaults to project name.
if(NOT DEFINED DEVICE_NAME)
    set(DEVICE_NAME "${PROJECT_NAME}" CACHE STRING "Device name embedded into the build")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Detect platform
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    message(STATUS "Building for Windows")
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
    message(STATUS "Building for macOS")
else()
    set(PLATFORM_LINUX TRUE)
    message(STATUS "Building for Linux")
endif()

# Platform-specific settings
if(PLATFORM_WINDOWS)
    # Windows specific flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7 and later
    set(EXTRA_LIBS ws2_32 iphlpapi)
elseif(PLATFORM_MACOS)
    # macOS specific settings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(EXTRA_LIBS)
else()
    # Linux specific settings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(EXTRA_LIBS)
endif()

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
    src/security/certificate_manager.cpp
    src/security/trusted_device_list.cpp
    src/security/device_authenticator.cpp
    src/security/integrity_checker.cpp
)

target_include_directories(syncflow_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(syncflow_core PRIVATE -Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

if(PLATFORM_WINDOWS)
    target_link_libraries(syncflow_core PUBLIC Threads::Threads OpenSSL::Crypto OpenSSL::SSL ${EXTRA_LIBS})
    target_link_libraries(syncflow_peer PRIVATE ws2_32 iphlpapi)
else()
    target_link_libraries(syncflow_core PUBLIC Threads::Threads OpenSSL::Crypto OpenSSL::SSL)
endif()

target_link_libraries(syncflow_peer PRIVATE syncflow_core)

target_include_directories(syncflow_peer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(syncflow_peer PRIVATE -Wall -Wextra -Wpedantic)

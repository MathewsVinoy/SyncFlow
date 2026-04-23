cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(
  SyncFlow
  VERSION 0.1.0
  DESCRIPTION "Production-grade P2P file synchronization system"
  LANGUAGES CXX C
)

# ============================================================================
# Global Settings
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# Options
# ============================================================================

option(SYNCFLOW_BUILD_TESTS "Build test suite" ON)
option(SYNCFLOW_BUILD_DESKTOP_UI "Build Qt desktop UI" OFF)
option(SYNCFLOW_BUILD_ANDROID "Build Android NDK components" OFF)
option(SYNCFLOW_ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(SYNCFLOW_ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(SYNCFLOW_ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

# ============================================================================
# Find Dependencies
# ============================================================================

find_package(OpenSSL 3.0 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(spdlog REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

# Asio: header-only library
if(NOT TARGET asio)
  add_subdirectory(third_party/asio)
  message(STATUS "Using bundled Asio header-only library")
endif()

# blake3: Find library or use vendored wrapper
find_package(blake3 QUIET)
if(NOT blake3_FOUND)
  message(STATUS "blake3 not found, creating wrapper for system blake3")
  add_subdirectory(third_party/blake3)
endif()

# ============================================================================
# Compiler Flags
# ============================================================================

if(MSVC)
  add_compile_options(/W4)
  add_compile_options(/utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  # C++ specific flags
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
endif()

if(SYNCFLOW_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

if(SYNCFLOW_ENABLE_TSAN)
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)
endif()

if(SYNCFLOW_ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endif()

# ============================================================================
# Version Header Generation
# ============================================================================

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/syncflow/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/syncflow/version.hpp"
)

# ============================================================================
# Core Library
# ============================================================================

add_library(syncflow_core STATIC)

# Source files
target_sources(syncflow_core PRIVATE
  # Core
  src/core/types.cpp
  src/core/sync_engine.cpp
  src/core/peer_manager.cpp
  src/core/event_bus.cpp
  src/core/config_manager.cpp
  
  # Networking
  src/networking/peer_connection.cpp
  src/networking/stun_client.cpp
  src/networking/local_discovery.cpp
  src/networking/relay_manager.cpp
  src/networking/connection_pool.cpp
  
  # Sync Engine
  src/sync_engine/file_watcher.cpp
  src/sync_engine/chunk_hasher.cpp
  src/sync_engine/sync_diff_algorithm.cpp
  src/sync_engine/version_vector.cpp
  src/sync_engine/conflict_resolver.cpp
  
  # Storage
  src/storage/database_manager.cpp
  src/storage/file_index.cpp
  src/storage/sync_journal.cpp
  
  # Security
  src/security/crypto_manager.cpp
  src/security/key_store.cpp
  src/security/mutual_auth.cpp
  src/security/tls_context.cpp
  
  # Crypto
  src/crypto/blake3.c
)

# Headers
target_include_directories(syncflow_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(syncflow_core
  PRIVATE
    OpenSSL::SSL OpenSSL::Crypto
    SQLite3::SQLite3
    spdlog::spdlog
    protobuf::libprotobuf
    Threads::Threads
  PUBLIC
    asio
    blake3::blake3
)

# Platform-specific
if(WIN32)
  target_link_libraries(syncflow_core PRIVATE ws2_32 mswsock)
elseif(APPLE)
  target_link_libraries(syncflow_core PRIVATE "-framework CoreServices")
elseif(LINUX)
  target_link_libraries(syncflow_core PRIVATE dl)
endif()

# ============================================================================
# Protocol Buffers
# ============================================================================

protobuf_generate(
  LANGUAGE cpp
  OUT_VAR PROTO_SRCS
  PROTOS
    src/sync_engine/messages.proto
)

target_sources(syncflow_core PRIVATE ${PROTO_SRCS})
target_include_directories(syncflow_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================================
# Executable: CLI Tool
# ============================================================================

add_executable(syncflowctl src/main.cpp)
target_link_libraries(syncflowctl PRIVATE syncflow_core)
target_include_directories(syncflowctl PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${ASIO_INCLUDE_DIR}
)

# ============================================================================
# Tests
# ============================================================================

if(SYNCFLOW_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  
  add_subdirectory(tests)
endif()

# ============================================================================
# Desktop UI (Qt)
# ============================================================================

if(SYNCFLOW_BUILD_DESKTOP_UI)
  add_subdirectory(desktop)
endif()

# ============================================================================
# Android (NDK)
# ============================================================================

if(SYNCFLOW_BUILD_ANDROID)
  add_subdirectory(android)
endif()

# ============================================================================
# Installation
# ============================================================================

install(
  TARGETS syncflow_core syncflowctl
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/include/syncflow/version.hpp
  DESTINATION include/syncflow
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "=====================================================")
message(STATUS "SyncFlow Build Configuration")
message(STATUS "=====================================================")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Tests: ${SYNCFLOW_BUILD_TESTS}")
message(STATUS "Desktop UI: ${SYNCFLOW_BUILD_DESKTOP_UI}")
message(STATUS "Android: ${SYNCFLOW_BUILD_ANDROID}")
message(STATUS "ASAN: ${SYNCFLOW_ENABLE_ASAN}")
message(STATUS "TSAN: ${SYNCFLOW_ENABLE_TSAN}")
message(STATUS "UBSAN: ${SYNCFLOW_ENABLE_UBSAN}")
message(STATUS "=====================================================")

cmake_minimum_required(VERSION 3.10)
project(SyncFlowCore)

set(CMAKE_CXX_STANDARD 17)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Find Protobuf
find_package(Protobuf REQUIRED)

# Protobuf generation
set(PROTO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../proto/sync_messages.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_SRC})

include_directories(
    include
    ${OPENSSL_INCLUDE_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
)

add_library(syncflow_core STATIC
    src/CryptoUtils.cpp
    src/NetworkManager.cpp
    src/SyncEngine.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_link_libraries(syncflow_core
    OpenSSL::SSL
    OpenSSL::Crypto
    ${PROTOBUF_LIBRARIES}
    pthread
)

# Test executable
enable_testing()
add_executable(core_tests
    tests/test_main.cpp
    tests/test_crypto.cpp
)
target_link_libraries(core_tests syncflow_core)
add_test(NAME core_tests COMMAND core_tests)

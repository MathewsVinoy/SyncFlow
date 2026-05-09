cmake_minimum_required(VERSION 3.10)

project(SyncFlowAndroid)

# The core library CMake
set(CORE_DIR "../../../../../core")

# Find OpenSSL and Protobuf (In real Android build, these would be provided by prebuilts or vcpkg)
# For this skeleton, we'll point to the core sources

include_directories(
    "${CORE_DIR}/include"
    "${CORE_DIR}/../proto"
    # Placeholder for system includes if needed
)

# In a real Android build, we'd use find_package or add_subdirectory for the core
# Here we add the sources directly for the JNI library
add_library(syncflow SHARED
    syncflow-jni.cpp
    "${CORE_DIR}/src/CryptoUtils.cpp"
    "${CORE_DIR}/src/NetworkManager.cpp"
    "${CORE_DIR}/src/SyncEngine.cpp"
)

# Real build would link to libssl, libcrypto, and libprotobuf
target_link_libraries(syncflow
    log
    android
    # ssl
    # crypto
    # protobuf
)

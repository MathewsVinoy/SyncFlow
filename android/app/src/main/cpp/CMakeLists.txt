cmake_minimum_required(VERSION 3.22)
project(syncflow_jni)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add native library
add_library(syncflow_jni SHARED
    jni_bridge.cpp
)

# Link Android NDK log library
target_link_libraries(syncflow_jni
    PRIVATE log
)

# Include directories
target_include_directories(syncflow_jni
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue
)

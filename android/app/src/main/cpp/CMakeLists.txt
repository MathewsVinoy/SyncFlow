# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html.
# For more examples on how to use CMake, see https://github.com/android/ndk-samples.

# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Declares the project name. The project name can be accessed via ${ PROJECT_NAME},
# Since this is the top level CMakeLists.txt, the project name is also accessible
# with ${CMAKE_PROJECT_NAME} (both CMake variables are in-sync within the top level
# build script scope).
project("syncflow")

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.
#
# In this top level CMakeLists.txt, ${CMAKE_PROJECT_NAME} is used to define
# the target library name; in the sub-module's CMakeLists.txt, ${PROJECT_NAME}
# is preferred for the same purpose.
#
# In order to load a library into your app from Java/Kotlin, you must call
# System.loadLibrary() and pass the name of the library defined here;
# for GameActivity/NativeActivity derived applications, the same library name must be
# used in the AndroidManifest.xml file.
file(GLOB_RECURSE SYNCFLOW_DESKTOP_SOURCES_CORE
        "${CMAKE_SOURCE_DIR}/../../../../../src/core/*.cpp"
        "${CMAKE_SOURCE_DIR}/../../../../../src/core/*.c"
)
file(GLOB_RECURSE SYNCFLOW_DESKTOP_SOURCES_FILE_SYNC
        "${CMAKE_SOURCE_DIR}/../../../../../src/file_sync/*.cpp"
)
file(GLOB_RECURSE SYNCFLOW_DESKTOP_SOURCES_NETWORKING
        "${CMAKE_SOURCE_DIR}/../../../../../src/networking/*.cpp"
)
file(GLOB_RECURSE SYNCFLOW_DESKTOP_SOURCES_PLATFORM
        "${CMAKE_SOURCE_DIR}/../../../../../src/platform/*.cpp"
)
file(GLOB_RECURSE SYNCFLOW_DESKTOP_SOURCES_SECURITY
        "${CMAKE_SOURCE_DIR}/../../../../../src/security/*.cpp"
)

set(SYNCFLOW_DESKTOP_SOURCES ${SYNCFLOW_DESKTOP_SOURCES_CORE}
    ${SYNCFLOW_DESKTOP_SOURCES_FILE_SYNC}
    ${SYNCFLOW_DESKTOP_SOURCES_NETWORKING}
        ${SYNCFLOW_DESKTOP_SOURCES_PLATFORM}
        ${SYNCFLOW_DESKTOP_SOURCES_SECURITY}
)

set(SYNCFLOW_STUB_SOURCES
        native-lib.cpp
        native-peer.cpp
)

list(APPEND SYNCFLOW_DESKTOP_SOURCES ${SYNCFLOW_STUB_SOURCES})

add_library(${CMAKE_PROJECT_NAME} SHARED ${SYNCFLOW_DESKTOP_SOURCES})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/../../../../../include"
)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        Threads::Threads
        OpenSSL::Crypto
        OpenSSL::SSL)
cmake_minimum_required(VERSION 3.16)

project(syncflow_peer_demo LANGUAGES CXX)

# Enable Qt meta-object compiler (MOC)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Allow embedding a device name at build time. If not provided, defaults to project name.
if(NOT DEFINED DEVICE_NAME)
    set(DEVICE_NAME "${PROJECT_NAME}" CACHE STRING "Device name embedded into the build")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to control building the Qt GUI. Turned off automatically for Termux/Android.
option(BUILD_GUI "Build Qt GUI (syncflow_gui)" ON)

# If building on Termux/Android, disable GUI by default
if(DEFINED ENV{TERMUX_VERSION} OR DEFINED ENV{TERMUX_PREFIX} OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    message(STATUS "Termux/Android detected - disabling GUI build by default")
    set(BUILD_GUI OFF CACHE BOOL "Build Qt GUI (syncflow_gui)" FORCE)
endif()

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

# Only try to find Qt when GUI is requested
if(BUILD_GUI)
    find_package(Qt5 COMPONENTS Core Gui Widgets Network QUIET)

    # Fallback to Qt6 if Qt5 not found
    if(NOT Qt5_FOUND)
        find_package(Qt6 COMPONENTS Core Gui Widgets Network REQUIRED)
        set(QT_LIBRARIES Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
    else()
        set(QT_LIBRARIES Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
    endif()
else()
    message(STATUS "Skipping Qt discovery because BUILD_GUI is OFF")
endif()

if(PLATFORM_WINDOWS)
    target_link_libraries(syncflow_core PUBLIC Threads::Threads OpenSSL::Crypto OpenSSL::SSL ${EXTRA_LIBS})
    target_link_libraries(syncflow_peer PRIVATE ws2_32 iphlpapi)
else()
    target_link_libraries(syncflow_core PUBLIC Threads::Threads OpenSSL::Crypto OpenSSL::SSL)
endif()

target_link_libraries(syncflow_peer PRIVATE syncflow_core)

target_include_directories(syncflow_peer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(syncflow_peer PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_GUI)
    add_executable(syncflow_gui
        src/gui/gui_main.cpp
        src/gui/main_window.cpp
        src/gui/settings_dialog.cpp
        src/gui/device_approval_dialog.cpp
        src/gui/sync_worker.cpp
        resources/syncflow.qrc
    )

    # Ensure headers with Q_OBJECT are processed by automoc
    target_sources(syncflow_gui PRIVATE
        include/syncflow/gui/main_window.h
        include/syncflow/gui/settings_dialog.h
        include/syncflow/gui/device_approval_dialog.h
        include/syncflow/gui/sync_worker.h
    )

    target_include_directories(syncflow_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(syncflow_gui PRIVATE
        syncflow_core
        ${QT_LIBRARIES}
    )

    target_compile_options(syncflow_gui PRIVATE -Wall -Wextra -Wpedantic)

    if(PLATFORM_WINDOWS)
        # Set GUI subsystem for Windows to hide console window
        set_target_properties(syncflow_gui PROPERTIES WIN32_EXECUTABLE ON)
    endif()
endif()

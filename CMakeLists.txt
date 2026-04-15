cmake_minimum_required(VERSION 3.16)

project(syncflow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ANDROID)
    add_compile_definitions(SYNCFLOW_ANDROID=1)
endif()

add_executable(syncflow_discovery
    src/discovery/device.cpp
)

add_executable(syncflow
    src/cli/syncflow_cli.cpp
)

add_executable(syncflow_transfer
    src/transfer/file_transfer.cpp
)

add_executable(syncflow_sync
    src/sync/directory_sync.cpp
)

add_executable(syncflow_ui
    src/ui/syncflow_ui.cpp
)

find_package(FLTK QUIET)
if(FLTK_FOUND)
    add_executable(syncflow_desktop
        src/ui/syncflow_desktop_fltk.cpp
    )
    target_include_directories(syncflow_desktop
        PRIVATE
            include
            ${FLTK_INCLUDE_DIR}
    )
    target_link_libraries(syncflow_desktop PRIVATE ${FLTK_LIBRARIES})
else()
    message(STATUS "FLTK not found: syncflow_desktop target will be skipped")
endif()

target_include_directories(syncflow_discovery
    PRIVATE
        include
)

target_include_directories(syncflow
    PRIVATE
        include
)

target_include_directories(syncflow_transfer
    PRIVATE
        include
)

target_include_directories(syncflow_sync
    PRIVATE
        include
)

target_include_directories(syncflow_ui
    PRIVATE
        include
)

if(WIN32)
    target_link_libraries(syncflow_discovery PRIVATE ws2_32)
    target_link_libraries(syncflow PRIVATE ws2_32)
    target_link_libraries(syncflow_transfer PRIVATE ws2_32)
    target_link_libraries(syncflow_sync PRIVATE ws2_32)
    target_link_libraries(syncflow_ui PRIVATE ws2_32)
    if(FLTK_FOUND)
        target_link_libraries(syncflow_desktop PRIVATE ws2_32)
    endif()
endif()

# Convenience run targets:
#   cmake --build build --target run_server
#   cmake --build build --target run_client
add_custom_target(run_server
    COMMAND syncflow_discovery server
    DEPENDS syncflow_discovery
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    USES_TERMINAL
)

add_custom_target(run_client
    COMMAND syncflow_discovery client
    DEPENDS syncflow_discovery
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    USES_TERMINAL
)

add_custom_target(run_cli_start
    COMMAND syncflow start
    DEPENDS syncflow syncflow_discovery
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    USES_TERMINAL
)

add_custom_target(run_cli_stop
    COMMAND syncflow stop
    DEPENDS syncflow
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    USES_TERMINAL
)

add_custom_target(run_cli_list_devices
    COMMAND syncflow list-devices
    DEPENDS syncflow syncflow_discovery
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    USES_TERMINAL
)

add_custom_target(run_transfer_recv
    COMMAND syncflow_transfer recv
    DEPENDS syncflow_transfer
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    USES_TERMINAL
)

add_custom_target(run_ui
    COMMAND syncflow_ui
    DEPENDS syncflow_ui syncflow_discovery syncflow_transfer syncflow_sync
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    USES_TERMINAL
)

if(FLTK_FOUND)
    add_custom_target(run_desktop_ui
        COMMAND syncflow_desktop
        DEPENDS syncflow_desktop syncflow_discovery syncflow_transfer syncflow_sync syncflow
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL
    )
endif()

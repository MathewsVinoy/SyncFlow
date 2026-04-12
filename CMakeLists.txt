cmake_minimum_required(VERSION 3.16)

project(syncflow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(syncflow_discovery
    src/discovery/device.cpp
)

add_executable(syncflow
    src/cli/syncflow_cli.cpp
)

add_executable(syncflow_transfer
    src/transfer/file_transfer.cpp
)

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

if(WIN32)
    target_link_libraries(syncflow_discovery PRIVATE ws2_32)
    target_link_libraries(syncflow PRIVATE ws2_32)
    target_link_libraries(syncflow_transfer PRIVATE ws2_32)
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

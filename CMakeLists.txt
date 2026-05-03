cmake_minimum_required(VERSION 3.16)

project(syncflow_peer_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(syncflow_peer
    src/main.cpp
)

target_compile_options(syncflow_peer PRIVATE -Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
target_link_libraries(syncflow_peer PRIVATE Threads::Threads)

cmake_minimum_required(VERSION 3.20)

# Simple test CMakeLists.txt
add_executable(test_discovery test_discovery.cpp)
target_link_libraries(test_discovery PRIVATE syncflow_discovery)

add_executable(test_transfer test_transfer.cpp)
target_link_libraries(test_transfer PRIVATE syncflow_transfer)

add_test(NAME TestDiscovery COMMAND test_discovery)
add_test(NAME TestTransfer COMMAND test_transfer)

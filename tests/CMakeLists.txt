cmake_minimum_required(VERSION 3.20)
project(syncflow_tests)

find_package(GTest REQUIRED)

# ============================================================================
# Unit Tests
# ============================================================================

add_executable(test_chunk_hasher
  unit/test_chunk_hasher.cpp
)
target_link_libraries(test_chunk_hasher
  PRIVATE
    syncflow_core
    GTest::gtest_main
)
target_include_directories(test_chunk_hasher
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_version_vector
  unit/test_version_vector.cpp
)
target_link_libraries(test_version_vector
  PRIVATE
    syncflow_core
    GTest::gtest_main
)

add_executable(test_sync_diff
  unit/test_sync_diff.cpp
)
target_link_libraries(test_sync_diff
  PRIVATE
    syncflow_core
    GTest::gtest_main
)

add_executable(test_crypto
  unit/test_crypto.cpp
)
target_link_libraries(test_crypto
  PRIVATE
    syncflow_core
    GTest::gtest_main
)

# ============================================================================
# Integration Tests
# ============================================================================

add_executable(test_peer_sync
  integration/test_peer_sync.cpp
)
target_link_libraries(test_peer_sync
  PRIVATE
    syncflow_core
    GTest::gtest_main
)

add_executable(test_multi_device
  integration/test_multi_device.cpp
)
target_link_libraries(test_multi_device
  PRIVATE
    syncflow_core
    GTest::gtest_main
)

# ============================================================================
# Register Tests with CTest
# ============================================================================

gtest_discover_tests(test_chunk_hasher)
gtest_discover_tests(test_version_vector)
gtest_discover_tests(test_sync_diff)
gtest_discover_tests(test_crypto)
gtest_discover_tests(test_peer_sync)
gtest_discover_tests(test_multi_device)

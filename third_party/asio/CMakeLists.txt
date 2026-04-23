# Asio Header-Only Library Configuration
# This is a header-only library, no compilation needed

add_library(asio INTERFACE)

target_include_directories(asio INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# Asio requires these definitions
target_compile_definitions(asio INTERFACE
  ASIO_STANDALONE
  ASIO_NO_DEPRECATED
)

# Enable threads (Asio uses std::thread)
find_package(Threads REQUIRED)
target_link_libraries(asio INTERFACE Threads::Threads)

# On Unix systems, Asio may need socket libraries
if(UNIX)
  target_link_libraries(asio INTERFACE pthread)
endif()

set_target_properties(asio PROPERTIES
  INTERFACE_LIBRARY_TYPE "INTERFACE"
)

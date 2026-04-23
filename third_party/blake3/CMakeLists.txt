# BLAKE3 Vendored Build Configuration
# Uses stub implementation provided in src/crypto/blake3.c

if(APPLE)
  set(BLAKE3_PLATFORM "macos")
elseif(WIN32)
  set(BLAKE3_PLATFORM "windows")
else()
  set(BLAKE3_PLATFORM "linux")
endif()

# Create interface library for BLAKE3
add_library(blake3 INTERFACE)

# Add include path for blake3.h header (in project root include/)
target_include_directories(blake3 INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

# Create namespace alias
add_library(blake3::blake3 ALIAS blake3)

cmake_minimum_required(VERSION 3.22.1)

project("syncflow")

# Create the native-lib shared library
add_library(native-lib SHARED native-lib.cpp)

# Link necessary libraries
find_library(log-lib log)

target_link_libraries(native-lib
        ${log-lib}
        android)
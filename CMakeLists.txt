cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()

project(syncflow LANGUAGES CXX)

# C++ standard setup (C++17 or C++20 requested, defaulting to C++20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT SYNCFLOW_IPO_SUPPORTED OUTPUT SYNCFLOW_IPO_ERROR)
find_package(Threads REQUIRED)
include(CTest)

# Third-party dependencies
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_FUZZ OFF CACHE BOOL "" FORCE)
set(FMT_CUDA_TEST OFF CACHE BOOL "" FORCE)
set(FMT_WERROR OFF CACHE BOOL "" FORCE)

set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_WARNINGS OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL_HO OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/fmt EXCLUDE_FROM_ALL)
add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
	include(FetchContent)
	FetchContent_Declare(
		nlohmann_json
		URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(syncflow_warnings INTERFACE)
add_library(syncflow_release_opts INTERFACE)

if(MSVC)
	target_compile_options(syncflow_warnings INTERFACE /W4 /WX)
	target_compile_options(syncflow_release_opts INTERFACE $<$<CONFIG:Release>:/O2>)
else()
	target_compile_options(syncflow_warnings INTERFACE -Wall -Wextra -Werror)
	target_compile_options(syncflow_release_opts INTERFACE $<$<CONFIG:Release>:-O3>)
endif()

function(syncflow_add_module target_name)
	file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS
		"${PROJECT_SOURCE_DIR}/src/${target_name}/*.cpp"
		"${PROJECT_SOURCE_DIR}/src/${target_name}/*.cc"
		"${PROJECT_SOURCE_DIR}/src/${target_name}/*.cxx"
		"${PROJECT_SOURCE_DIR}/src/${target_name}/*.c"
	)

	if(module_sources)
		add_library(${target_name} STATIC ${module_sources})

		target_include_directories(${target_name}
			PUBLIC
				"${PROJECT_SOURCE_DIR}/include"
		)

		target_link_libraries(${target_name}
			PRIVATE
				syncflow_warnings
				syncflow_release_opts
		)

		if(target_name STREQUAL "core")
			target_link_libraries(${target_name}
				PRIVATE
					spdlog::spdlog
					nlohmann_json::nlohmann_json
			)
		endif()

		if(target_name STREQUAL "sync_engine")
			target_link_libraries(${target_name}
				PRIVATE
					core
			)
		endif()

		if(target_name STREQUAL "security")
			target_link_libraries(${target_name}
				PRIVATE
					crypto
			)
		endif()

		if(target_name STREQUAL "networking" AND WIN32)
			target_link_libraries(${target_name}
				PRIVATE
					ws2_32
			)
		endif()

		if(SYNCFLOW_IPO_SUPPORTED)
			set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
		endif()
	else()
		# Keep target available even if module has no implementation files yet.
		add_library(${target_name} INTERFACE)

		target_include_directories(${target_name}
			INTERFACE
				"${PROJECT_SOURCE_DIR}/include"
		)

		target_link_libraries(${target_name}
			INTERFACE
				syncflow_warnings
				syncflow_release_opts
		)
	endif()
endfunction()

# Module libraries
syncflow_add_module(core)
syncflow_add_module(networking)
syncflow_add_module(sync_engine)
syncflow_add_module(crypto)
syncflow_add_module(security)

# Application executable
add_executable(app
	src/main.cpp
)

target_include_directories(app
	PRIVATE
		"${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(app
	PRIVATE
		core
		networking
		sync_engine
		crypto
		security
		nlohmann_json::nlohmann_json
		Threads::Threads
		syncflow_warnings
		syncflow_release_opts
)

if(SYNCFLOW_IPO_SUPPORTED)
	set_property(TARGET app PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if(BUILD_TESTING)
	add_executable(syncflow_tests
		tests/test_sync_core.cpp
	)

	target_include_directories(syncflow_tests
		PRIVATE
			"${PROJECT_SOURCE_DIR}/include"
	)

	target_link_libraries(syncflow_tests
		PRIVATE
			core
			networking
			sync_engine
			crypto
			security
			Threads::Threads
			syncflow_warnings
			syncflow_release_opts
	)

	add_test(NAME syncflow_tests COMMAND syncflow_tests)
endif()

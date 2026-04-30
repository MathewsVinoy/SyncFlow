cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()

project(syncflow
	VERSION 1.0.0
	DESCRIPTION "Cross-platform file synchronization application"
	HOMEPAGE_URL "https://github.com/yourusername/syncflow"
	LANGUAGES CXX
)

include(GNUInstallDirs)

# Platform detection
if(WIN32)
	set(SYNCFLOW_PLATFORM "Windows")
elseif(APPLE)
	set(SYNCFLOW_PLATFORM "macOS")
elseif(UNIX AND NOT APPLE)
	set(SYNCFLOW_PLATFORM "Linux")
else()
	set(SYNCFLOW_PLATFORM "Unknown")
endif()

message(STATUS "Syncflow: Detected platform: ${SYNCFLOW_PLATFORM}")

# C++ standard setup (C++17 or C++20 requested, defaulting to C++20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Binary output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

include(CheckIPOSupported)
check_ipo_supported(RESULT SYNCFLOW_IPO_SUPPORTED OUTPUT SYNCFLOW_IPO_ERROR)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
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
					OpenSSL::Crypto
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
					OpenSSL::SSL
					OpenSSL::Crypto
			)
		elseif(target_name STREQUAL "networking")
			target_link_libraries(${target_name}
				PRIVATE
					OpenSSL::SSL
					OpenSSL::Crypto
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
syncflow_add_module(platform)
syncflow_add_module(networking)
syncflow_add_module(sync_engine)
syncflow_add_module(crypto)
syncflow_add_module(security)

# Inter-module dependencies
target_link_libraries(core
	PRIVATE
		platform
)

target_link_libraries(networking
	PRIVATE
		platform
)

# Ensure platform module links appropriate system libraries
if(WIN32)
	target_link_libraries(platform
		PRIVATE
			ws2_32
			iphlpapi
			shlwapi
	)
elseif(UNIX)
	# POSIX systems (Linux, macOS, BSD, etc.)
	target_link_libraries(platform
		PRIVATE
			Threads::Threads
	)
endif()

# Application executable
add_executable(app
	src/main.cpp
)

set_target_properties(app PROPERTIES
	OUTPUT_NAME syncflow
)

target_include_directories(app
	PRIVATE
		"${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(app
	PRIVATE
		core
		platform
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
			platform
			networking
			sync_engine
			crypto
			security
			Threads::Threads
			syncflow_warnings
			syncflow_release_opts
	)

	add_test(NAME syncflow_tests COMMAND syncflow_tests)

	# Platform tests
	add_executable(platform_tests
		tests/test_platform.cpp
	)

	target_include_directories(platform_tests
		PRIVATE
			"${PROJECT_SOURCE_DIR}/include"
	)

	target_link_libraries(platform_tests
		PRIVATE
			platform
			core
			Threads::Threads
			syncflow_warnings
			syncflow_release_opts
	)

	add_test(NAME platform_tests COMMAND platform_tests)
endif()

# ========== CPack Configuration ==========
# Determine package generator based on platform
if(WIN32)
	set(CPACK_GENERATOR "NSIS;ZIP")
	if(EXISTS "${CMAKE_SOURCE_DIR}/assets/icon.ico")
		set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/icon.ico")
	endif()
	set(CPACK_NSIS_INSTALL_ROOT "C:\\Program Files")
	set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
	set(CPACK_NSIS_DISPLAY_NAME "Syncflow")
elseif(APPLE)
	set(CPACK_GENERATOR "DragNDrop;TGZ")
	set(CPACK_DMG_VOLUME_NAME "Syncflow")
	set(CPACK_BUNDLE_NAME "Syncflow")
	if(EXISTS "${CMAKE_SOURCE_DIR}/assets/Info.plist")
		set(CPACK_BUNDLE_PLIST "${CMAKE_SOURCE_DIR}/assets/Info.plist")
	endif()
elseif(UNIX AND NOT APPLE)
	# Linux
	set(CPACK_GENERATOR "DEB;RPM;TGZ")
	set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
	set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
	set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
	set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
endif()

# Common CPack settings
set(CPACK_PACKAGE_VENDOR "SyncFlow Project")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_CONTACT "syncflow@users.noreply.github.com")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Syncflow")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${SYNCFLOW_PLATFORM}")
set(CPACK_PACKAGE_EXECUTABLES "syncflow" "Syncflow")
set(CPACK_STRIP_FILES ON)
set(CPACK_VERBATIM_VARIABLES ON)

# License
if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
	set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()

# Install targets
install(TARGETS app
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	BUNDLE DESTINATION Applications
)

# Install config files
if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
	if(WIN32)
		install(FILES "${CMAKE_SOURCE_DIR}/config.json"
			DESTINATION ${CMAKE_INSTALL_DATADIR}/syncflow
		)
	elseif(APPLE)
		install(FILES "${CMAKE_SOURCE_DIR}/config.json"
			DESTINATION ${CMAKE_INSTALL_DATADIR}/syncflow
		)
	else()
		install(FILES "${CMAKE_SOURCE_DIR}/config.json"
			DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/syncflow
		)
	endif()
endif()

# Install documentation
install(FILES README.md
	DESTINATION ${CMAKE_INSTALL_DOCDIR}
	OPTIONAL
)

include(CPack)

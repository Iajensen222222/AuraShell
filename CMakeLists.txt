cmake_minimum_required(VERSION 3.22)

# Use ../ to go up from AuraShell to the Desktop Icon Changer folder, then into vcpkg
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# Project definition
project(AuraShell
    VERSION 0.1.0
    DESCRIPTION "High-performance Windows 11 customization utility"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# MSVC-specific settings
if(MSVC)
    # Compiler flags
    add_compile_options(
        /W4                    # Warning level 4
        /WX                    # Treat warnings as errors (disable with /WX-)
        /permissive-           # Standards conformance
        /MP                    # Multi-processor compilation
        /fp:fast               # Fast floating point (for performance)
        /Zf                    # Faster PDB generation
    )

    # Release optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /Oi /Ot")

    # Debug symbols in release
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")

    # Disable exceptions for performance-critical code (optional)
    # add_compile_options(/EHsc)  # Enable C++ exceptions
endif()

# Find required packages via vcpkg
find_package(cppwinrt REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)

# Optional: Testing framework
find_package(Catch2 QUIET)

# Windows SDK minimum version
if(MSVC)
    # Minimum Windows 11 SDK (22621)
    set(CMAKE_SYSTEM_VERSION 10.0.22621.0)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00  # Windows 10 and above
        NTDDI_VERSION=0x0A00000C  # Windows 11
    )
endif()

# DirectX headers (included with Windows SDK)
# No separate find_package needed; included automatically with Windows SDK

# Include CMake helper modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Option for building tests
option(BUILD_TESTS "Build test suite" ON)

# Option for building samples
option(BUILD_SAMPLES "Build sample applications" OFF)

# Option for building service
option(BUILD_SERVICE "Build optional service executable" ON)

# Main project subdirectories
add_subdirectory(src)

if(BUILD_TESTS AND Catch2_FOUND)
    enable_testing()
    add_subdirectory(tests)
else()
    message(STATUS "Tests disabled (Catch2 not found or BUILD_TESTS=OFF)")
endif()

if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

# Installation rules
install(DIRECTORY data/themes DESTINATION bin/data)
install(DIRECTORY data/icons DESTINATION bin/data)
install(DIRECTORY data/shaders DESTINATION bin/data)
install(DIRECTORY docs DESTINATION . PATTERN "*.md")

# Version header generation
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/include/aurashell/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/aurashell/version.h"
)

# Print configuration summary
message(STATUS "===========================================")
message(STATUS "AuraShell Configuration Summary")
message(STATUS "===========================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build samples: ${BUILD_SAMPLES}")
message(STATUS "Build service: ${BUILD_SERVICE}")
message(STATUS "Generator: ${CMAKE_GENERATOR}")
if(MSVC)
    message(STATUS "MSVC Version: ${MSVC_VERSION}")
endif()
message(STATUS "===========================================")

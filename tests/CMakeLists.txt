cmake_minimum_required(VERSION 3.22)

# Unit Tests for AuraShell
project(AuraShellTests)

# Clean up find_package (v3 is standard now)
find_package(Catch2 3 CONFIG REQUIRED)

# This is the more robust way to include the test discovery helper
list(APPEND CMAKE_MODULE_PATH ${Catch2_DIR})
include(Catch)

# Add unit tests
add_executable(test_runner
    unit/test_logging.cpp
    unit/test_common_types.cpp
    unit/test_window_manager.cpp
    unit/test_dpi_awareness.cpp
    unit/test_system_metrics.cpp
    unit/test_ipc_message.cpp
    unit/test_ipc_handshake.cpp
    unit/test_taskbar_controller.cpp
)

# Link against Catch2 and core library modules
target_link_libraries(test_runner PRIVATE
    Catch2::Catch2WithMain
    aurashell_logging
    aurashell_platform
    aurashell_ipc
    aurashell_taskbar_engine
)

# Add include directories
target_include_directories(test_runner PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/core/platform/ipc"
    "${CMAKE_SOURCE_DIR}/src/taskbar_engine"
)

# Register tests with CTest
catch_discover_tests(test_runner)

# Set output directory
set_target_properties(test_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
)

message(STATUS "Unit tests configured successfully")

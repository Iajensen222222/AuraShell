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
    unit/test_hover_detection.cpp
    unit/test_icon_overlay.cpp
    unit/test_animations.cpp
    unit/test_multi_monitor.cpp
    unit/test_service_ipc.cpp
    unit/test_config_logic.cpp
    unit/test_ui_logic.cpp          # Phase 10.1: StyleManager, color math, spring
    unit/test_navigation.cpp        # Phase 10.2: NavigationManager, hit-test, spring
    unit/test_ui_interactions.cpp   # Phase 10.5: Card hit-test, CardHover anim, easing
    unit/test_personalization.cpp   # Phase 10.6: ColorGrid, Slider, IPC boundary checks
    unit/test_behavior_logic.cpp    # Phase 10.7: BehaviorPage hit-tests, config defaults
    unit/test_shell_customization.cpp # Phase 10.9: ShellIconModifier path utils, result codes
    unit/test_spectrum_analyzer.cpp   # Sprint 2: FFT math, frequency band mapping
    unit/test_virtual_desktop.cpp     # Sprint 4: VirtualDesktopDetector smoke tests
)

# Link against Catch2 and core library modules
target_link_libraries(test_runner PRIVATE
    Catch2::Catch2WithMain
    aurashell_logging
    aurashell_platform
    aurashell_audio
    aurashell_ipc
    aurashell_taskbar_engine
    aurashell_service
    aurashell_config_app
)

# Add include directories
target_include_directories(test_runner PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/audio"
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/core/platform/ipc"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
    "${CMAKE_SOURCE_DIR}/src/taskbar_engine"
    "${CMAKE_SOURCE_DIR}/src/app"
)

# Register tests with CTest
catch_discover_tests(test_runner)

# Set output directory
set_target_properties(test_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
)

message(STATUS "Unit tests configured successfully")

# ============================================================================
# Integration Tests (Phase 6+)
# Requires: aurashell_service, aurashell_config_app, named pipe IPC
# Run with: ctest -R "[integration]"
# ============================================================================
add_subdirectory(integration)

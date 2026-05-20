cmake_minimum_required(VERSION 3.22)
project(AuraShellIntegrationTests)

find_package(Catch2 3 CONFIG REQUIRED)
list(APPEND CMAKE_MODULE_PATH ${Catch2_DIR})
include(Catch)

add_executable(integration_test_runner
    test_full_stack.cpp
    test_theme_apply.cpp
    test_audio_workspace.cpp
    test_ipc_live.cpp
    test_shell_integration.cpp
)

target_link_libraries(integration_test_runner PRIVATE
    Catch2::Catch2WithMain
    aurashell_service
    aurashell_ipc
    aurashell_logging
    aurashell_audio
    aurashell_context_tools
    aurashell_taskbar_engine
    aurashell_system_integration
    aurashell_config_app
)

target_include_directories(integration_test_runner PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/system_integration"
    "${CMAKE_SOURCE_DIR}/src/core/platform/ipc"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
    "${CMAKE_SOURCE_DIR}/src/core/audio"
    "${CMAKE_SOURCE_DIR}/src/service"
    "${CMAKE_SOURCE_DIR}/src/taskbar_engine"
    "${CMAKE_SOURCE_DIR}/src/app"
    "${CMAKE_SOURCE_DIR}/src/context_tools"
)

catch_discover_tests(integration_test_runner
    TEST_SPEC "[integration]"
)

set_target_properties(integration_test_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
)

message(STATUS "Integration tests configured successfully")

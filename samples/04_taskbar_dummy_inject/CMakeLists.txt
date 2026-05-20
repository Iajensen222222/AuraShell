cmake_minimum_required(VERSION 3.22)
project(DummyInjectSample)

# ============================================================================
# test_hook_dll — injected into notepad.exe to verify injection works.
# Writes a log entry on attach and detach. No hooks, no system changes.
# ============================================================================

add_library(test_hook_dll SHARED test_hook_dll.cpp)
target_compile_features(test_hook_dll PRIVATE cxx_std_20)
set_target_properties(test_hook_dll PROPERTIES
    OUTPUT_NAME "test_hook_dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/samples"
)

if(MSVC)
    target_compile_options(test_hook_dll PRIVATE /W4 /permissive-)
    target_compile_definitions(test_hook_dll PRIVATE
        NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    )
endif()

# ============================================================================
# dummy_inject — the injector executable. Finds notepad.exe by name and
# injects test_hook_dll.dll using the standard LoadLibrary remote thread method.
# MUST be run with a notepad.exe process open.
# ============================================================================

add_executable(dummy_inject dummy_inject.cpp)
target_compile_features(dummy_inject PRIVATE cxx_std_20)
set_target_properties(dummy_inject PROPERTIES
    OUTPUT_NAME "dummy_inject"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/samples"
)

if(MSVC)
    target_compile_options(dummy_inject PRIVATE /W4 /permissive-)
    target_compile_definitions(dummy_inject PRIVATE
        NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    )
    # Console subsystem so printf output is visible in an elevated terminal.
    target_link_options(dummy_inject PRIVATE /SUBSYSTEM:CONSOLE)
endif()

message(STATUS "Sample 04: DLL injection dummy target configured")

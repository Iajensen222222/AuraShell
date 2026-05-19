cmake_minimum_required(VERSION 3.22)

# ============================================================================
# aurashell_service — static library with the lean service logic.
# Linked by both AuraShellService.exe and the unit test runner.
# Zero UI components.  advapi32 only.
# ============================================================================

add_library(aurashell_service STATIC
    pipe_security.cpp
    service_core.cpp
)

target_link_libraries(aurashell_service PUBLIC
    aurashell_logging
    aurashell_ipc
    aurashell_audio
    advapi32.lib
)

target_include_directories(aurashell_service PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/core/platform/ipc"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
    "${CMAKE_SOURCE_DIR}/src/core/audio"
)

target_compile_features(aurashell_service PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(aurashell_service PRIVATE
        /W4 /WX /permissive-
        /wd4251 /wd4275
    )
    target_compile_definitions(aurashell_service PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
endif()

# ============================================================================
# AuraShellService.exe — SCM entry point + self-registration CLI
#
# Subsystem: CONSOLE so that --install / --uninstall printf output is visible
# in an elevated terminal.  Windows Services launched by the SCM do not have
# a console, so the subsystem choice does not affect normal service operation.
# ============================================================================

add_executable(AuraShellService main_service.cpp)
target_link_libraries(AuraShellService PRIVATE aurashell_service)
target_compile_features(AuraShellService PRIVATE cxx_std_20)

if(MSVC)
    # /SUBSYSTEM:CONSOLE allows printf output for --install / --uninstall;
    # the SCM is unaffected (it never creates a console window for services).
    target_link_options(AuraShellService PRIVATE /SUBSYSTEM:CONSOLE)
    target_compile_options(AuraShellService PRIVATE /W4 /WX /permissive-)
    target_compile_definitions(AuraShellService PRIVATE
        NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE
    )
endif()

set_target_properties(AuraShellService PROPERTIES
    OUTPUT_NAME "AuraShellService"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "Service module configured (aurashell_service + AuraShellService.exe)")

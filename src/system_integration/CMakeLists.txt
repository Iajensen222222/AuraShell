add_library(aurashell_system_integration STATIC
    shell_integration.cpp
    shell_integration.h
    hotkey_manager.cpp
    hotkey_manager.h
)

target_include_directories(aurashell_system_integration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(aurashell_system_integration PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
    "${CMAKE_SOURCE_DIR}/src/taskbar_engine"
    "${CMAKE_SOURCE_DIR}/src/visual_enhancements"
    "${CMAKE_SOURCE_DIR}/src/context_tools"
    "${CMAKE_SOURCE_DIR}/src/app"
)

target_link_libraries(aurashell_system_integration PRIVATE
    aurashell_taskbar_engine
    aurashell_visual_enhancements
    aurashell_context_tools
    aurashell_logging
    Shell32
)

target_compile_features(aurashell_system_integration PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(aurashell_system_integration PRIVATE /W4 /permissive-)
    target_compile_definitions(aurashell_system_integration PRIVATE
        NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    )
endif()

message(STATUS "system_integration configured")

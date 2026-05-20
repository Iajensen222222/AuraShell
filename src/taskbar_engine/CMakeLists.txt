cmake_minimum_required(VERSION 3.22)

# TaskbarEngine Module
project(aurashell_taskbar_engine)

# Add taskbar engine library
add_library(aurashell_taskbar_engine
    animation_controller.cpp
    taskbar_controller.cpp
    hover_detector.cpp
    icon_overlay_manager.cpp
    acrylic_backdrop.cpp
    icon_replacer.cpp
)

# Link dependencies
target_link_libraries(aurashell_taskbar_engine PRIVATE
    aurashell_platform
    aurashell_logging
    comctl32.lib
    d2d1.lib
    dwrite.lib
    dwmapi.lib   # DwmFlush — vsync-aligned animation ticks
    nlohmann_json::nlohmann_json  # icon_replacer.cpp JSON serialization
)

# Include directories
target_include_directories(aurashell_taskbar_engine PUBLIC
    "${CMAKE_SOURCE_DIR}/src/core/include"
)

target_include_directories(aurashell_taskbar_engine PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
    "${CMAKE_SOURCE_DIR}/src/taskbar_engine"
    "${CMAKE_SOURCE_DIR}/src/core/logging"  # performance_logger.h
)

# C++20 standard
target_compile_features(aurashell_taskbar_engine PRIVATE cxx_std_20)

# MSVC specific settings
if(MSVC)
    target_compile_options(aurashell_taskbar_engine PRIVATE /W4 /permissive-)
    # Suppress C4996 warnings for unsafe functions (CRT_SECURE_NO_WARNINGS, SCL_SECURE_NO_WARNINGS)
    target_compile_definitions(aurashell_taskbar_engine PRIVATE 
        _CRT_SECURE_NO_WARNINGS 
        _SCL_SECURE_NO_WARNINGS
    )
endif()

message(STATUS "TaskbarEngine configured successfully")


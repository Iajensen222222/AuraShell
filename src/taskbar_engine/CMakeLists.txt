cmake_minimum_required(VERSION 3.22)

# TaskbarEngine Module
project(aurashell_taskbar_engine)

# Add taskbar engine library
add_library(aurashell_taskbar_engine
    taskbar_controller.cpp
)

# Link dependencies
target_link_libraries(aurashell_taskbar_engine PRIVATE
    aurashell_platform
    aurashell_logging
)

# Include directories
target_include_directories(aurashell_taskbar_engine PUBLIC
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/taskbar_engine"
)

target_include_directories(aurashell_taskbar_engine PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
)

# C++20 standard
target_compile_features(aurashell_taskbar_engine PRIVATE cxx_std_20)

# MSVC specific settings
if(MSVC)
    target_compile_options(aurashell_taskbar_engine PRIVATE /W4 /permissive-)
endif()

message(STATUS "TaskbarEngine configured successfully")

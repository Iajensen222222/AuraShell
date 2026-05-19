# src/core/platform/CMakeLists.txt - Platform abstraction layer

# Core platform utilities (DPI, Metrics, Window Manager)
add_library(aurashell_platform OBJECT
    dpi_awareness.cpp
    system_metrics.cpp
    window_manager.cpp
    virtual_desktop.cpp
)

target_include_directories(aurashell_platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Link dependencies
target_link_libraries(aurashell_platform
    PUBLIC
        spdlog::spdlog
        shcore.lib
        user32.lib
        Ole32       # CoCreateInstance for IVirtualDesktopManager
)

# Set C++ standard
set_target_properties(aurashell_platform PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# MSVC-specific compiler options
if(MSVC)
    target_compile_options(aurashell_platform PRIVATE /W4 /WX /permissive-)
endif()

# IPC submodule (depends on platform)
add_subdirectory(ipc)

message(STATUS "Platform layer modules configured successfully")

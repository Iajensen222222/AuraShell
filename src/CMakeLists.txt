# src/CMakeLists.txt - Build configuration for all AuraShell modules

# Core shared library (libaurashell)
add_subdirectory(core)

# Feature modules (depend on core)
add_subdirectory(taskbar_engine)
add_subdirectory(visual_enhancements)
add_subdirectory(context_tools)
add_subdirectory(system_integration)

# Main application (GUI + system tray)
add_subdirectory(app)

# Optional service (requires elevation)
if(BUILD_SERVICE)
    add_subdirectory(service)
endif()

message(STATUS "AuraShell modules configured successfully")

# src/core/platform/ipc/CMakeLists.txt - IPC named pipe implementation

# IPC library - compiles into core library
add_library(aurashell_ipc OBJECT
    named_pipe_server.cpp
    named_pipe_client.cpp
)

# Link dependencies
target_link_libraries(aurashell_ipc PUBLIC
    spdlog::spdlog
)

# Include directories
target_include_directories(aurashell_ipc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Windows-specific settings
if(MSVC)
    target_compile_options(aurashell_ipc PRIVATE /W4 /WX)
endif()

# Expose headers for linking targets
set(AURASHELL_IPC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/named_pipe_server.h
    ${CMAKE_CURRENT_SOURCE_DIR}/named_pipe_client.h
    CACHE INTERNAL "AuraShell IPC headers"
)

message(STATUS "IPC named pipe module configured successfully")

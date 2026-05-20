# src/core/logging/CMakeLists.txt - Logging module (spdlog integration)

add_library(aurashell_logging STATIC
    logger.cpp
    logger.h
    performance_logger.cpp
    performance_logger.h
)

target_include_directories(aurashell_logging
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Link dependencies
target_link_libraries(aurashell_logging
    PUBLIC
        spdlog::spdlog
)

# Set C++ standard
set_target_properties(aurashell_logging PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# MSVC-specific compiler options
if(MSVC)
    target_compile_options(aurashell_logging PRIVATE /W4 /WX /permissive-)
    target_compile_definitions(aurashell_logging PRIVATE NOMINMAX UNICODE _UNICODE)
endif()

message(STATUS "Logging module configured")

add_library(aurashell_context_tools STATIC
    workspace_manager.cpp
    theme_applier.cpp
    theme_preset_loader.cpp
)

target_include_directories(aurashell_context_tools PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(aurashell_context_tools PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
    "${CMAKE_SOURCE_DIR}/src/app"
)

target_link_libraries(aurashell_context_tools PRIVATE
    aurashell_platform
    aurashell_logging
    nlohmann_json::nlohmann_json
)

target_compile_features(aurashell_context_tools PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(aurashell_context_tools PRIVATE /W4 /permissive-)
    target_compile_definitions(aurashell_context_tools PRIVATE NOMINMAX UNICODE _UNICODE)
endif()

message(STATUS "context_tools configured")

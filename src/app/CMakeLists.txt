cmake_minimum_required(VERSION 3.22)

find_package(nlohmann_json CONFIG REQUIRED)

# ---- aurashell_config_app — testable logic library (no WinMain) ----
add_library(aurashell_config_app STATIC
    settings_manager.cpp
    app_client.cpp
    config_window.cpp
)

target_include_directories(aurashell_config_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/core/platform/ipc"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
    "${CMAKE_SOURCE_DIR}/src/core/include"
)

target_link_libraries(aurashell_config_app PUBLIC
    aurashell_ipc
    aurashell_logging
    nlohmann_json::nlohmann_json
)

target_compile_definitions(aurashell_config_app PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

# ---- AuraConfig.exe — GUI entry point ----
add_executable(AuraConfig WIN32
    main_app.cpp
)

target_link_libraries(AuraConfig PRIVATE
    aurashell_config_app
)

target_compile_definitions(AuraConfig PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

set_target_properties(AuraConfig PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "AuraConfig configured")

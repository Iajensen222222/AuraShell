cmake_minimum_required(VERSION 3.22)

find_package(nlohmann_json CONFIG REQUIRED)

# ---- aurashell_config_app — testable logic library (no WinMain) ----
add_library(aurashell_config_app STATIC
    settings_manager.cpp
    app_client.cpp
    config_window.cpp
    ui_styles.cpp           # Phase 10.1: StyleManager, spring integrator, HSL math
    navigation_manager.cpp  # Phase 10.2: Sidebar, kinetic accent bar, page routing
    page_dashboard.cpp      # Phase 10.3/10.4: Immersive dashboard, hero preview, grid layout
    card_renderer.cpp       # Phase 10.4: D2D gradient cards, shadow rings, acrylic border
    page_visuals.cpp        # Phase 10.6: Color picker, speed slider, options toggles
    page_behavior.cpp       # Phase 10.7: Shell monitoring, auto-start, SCM integration
    page_about.cpp          # Phase 10.8: Version info, license, live log viewer
    shell_icon_modifier.cpp # Phase 10.9: IShellLinkW COM utility for icon customization
    page_desktop_items.cpp  # Phase 10.9: Drag-drop shortcut/folder icon customizer page
)

target_include_directories(aurashell_config_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/core/platform/ipc"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/audio"
    "${CMAKE_SOURCE_DIR}/src/visual_enhancements"
    "${CMAKE_SOURCE_DIR}/src/system_integration"
)

target_link_libraries(aurashell_config_app PUBLIC
    aurashell_ipc
    aurashell_logging
    aurashell_audio
    aurashell_visual_enhancements
    aurashell_system_integration
    nlohmann_json::nlohmann_json
    dwmapi.lib              # DwmGetColorizationColor (ui_styles.cpp)
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

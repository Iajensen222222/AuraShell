add_library(aurashell_visual_enhancements STATIC
    audio_visualizer.cpp
)

target_include_directories(aurashell_visual_enhancements PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(aurashell_visual_enhancements PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/audio"
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
)

target_link_libraries(aurashell_visual_enhancements PRIVATE
    aurashell_audio
    aurashell_platform
    aurashell_logging
    D2d1
)

target_compile_features(aurashell_visual_enhancements PUBLIC cxx_std_20)

message(STATUS "visual_enhancements configured")

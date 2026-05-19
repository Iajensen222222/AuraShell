add_library(aurashell_audio STATIC
    audio_capture.cpp
    spectrum_analyzer.cpp
    audio_engine.cpp
)

target_include_directories(aurashell_audio PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
)

target_link_libraries(aurashell_audio PRIVATE
    aurashell_logging
    Mmdevapi
    Ole32
)

target_compile_features(aurashell_audio PUBLIC cxx_std_20)

add_executable(sample_02_dpi_overlay WIN32 main.cpp)
target_compile_features(sample_02_dpi_overlay PRIVATE cxx_std_20)
target_link_libraries(sample_02_dpi_overlay PRIVATE d2d1 dwmapi)
set_target_properties(sample_02_dpi_overlay PROPERTIES
    OUTPUT_NAME "Sample02_DpiOverlay"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/samples")
if(MSVC)
    target_compile_definitions(sample_02_dpi_overlay PRIVATE NOMINMAX UNICODE _UNICODE)
    target_link_options(sample_02_dpi_overlay PRIVATE /SUBSYSTEM:WINDOWS)
endif()

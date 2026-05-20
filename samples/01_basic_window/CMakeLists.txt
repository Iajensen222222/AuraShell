add_executable(sample_01_basic_window WIN32 main.cpp)
target_compile_features(sample_01_basic_window PRIVATE cxx_std_20)
target_link_libraries(sample_01_basic_window PRIVATE dwmapi)
set_target_properties(sample_01_basic_window PROPERTIES
    OUTPUT_NAME "Sample01_BasicWindow"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/samples")
if(MSVC)
    target_compile_definitions(sample_01_basic_window PRIVATE NOMINMAX UNICODE _UNICODE)
    target_link_options(sample_01_basic_window PRIVATE /SUBSYSTEM:WINDOWS)
endif()

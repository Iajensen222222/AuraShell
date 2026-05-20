add_executable(sample_03_ipc_client main.cpp)
target_compile_features(sample_03_ipc_client PRIVATE cxx_std_20)
set_target_properties(sample_03_ipc_client PROPERTIES
    OUTPUT_NAME "Sample03_IpcClient"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/samples")
if(MSVC)
    target_compile_definitions(sample_03_ipc_client PRIVATE NOMINMAX UNICODE _UNICODE)
    target_link_options(sample_03_ipc_client PRIVATE /SUBSYSTEM:CONSOLE)
endif()

add_library(aurashell_config STATIC
    change_observer.cpp
)

target_include_directories(aurashell_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(aurashell_config PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/include"
    "${CMAKE_SOURCE_DIR}/src/core/logging"
)

target_link_libraries(aurashell_config PRIVATE
    aurashell_logging
)

target_compile_features(aurashell_config PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(aurashell_config PRIVATE /W4 /permissive-)
    target_compile_definitions(aurashell_config PRIVATE NOMINMAX UNICODE _UNICODE)
endif()

message(STATUS "config module configured")

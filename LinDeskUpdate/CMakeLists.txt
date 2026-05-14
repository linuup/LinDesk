cmake_minimum_required(VERSION 3.31)

project(LinDeskUpdate LANGUAGES CXX)

file(GLOB_RECURSE LINDESK_UPDATE_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

add_executable(LinDeskUpdate ${LINDESK_UPDATE_SOURCES})

set_target_properties(LinDeskUpdate PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE TRUE
)

target_compile_definitions(LinDeskUpdate PRIVATE UNICODE _UNICODE)

target_link_libraries(LinDeskUpdate PRIVATE
    Shell32
)

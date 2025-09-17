# CMakeLists.txt for a Qt project that creates a window and draws text with QPainter
cmake_minimum_required(VERSION 3.14)
#C++17 标准
set(CMAKE_CXX_STANDARD 17)
add_definitions(-DUNICODE -D_UNICODE)

# 必须先设置 vcpkg toolchain
# set(CMAKE_TOOLCHAIN_FILE "C:/software1/vcpkg/scripts/buildsystems/vcpkg.cmake")

# set(VCPKG_MANIFEST_MODE ON)

project("LinDesk")

add_compile_options("/utf-8")

# 添加子目录，先编译库，再编译主程序lib
set(CMAKE_PREFIX_PATH "C:/Qt/5.15.2/msvc2019_64/lib/cmake")


# 查找 Qt5 库
find_package(Qt5 COMPONENTS Widgets WinExtras REQUIRED)
# 找库
# 设置输出目录（可选，保持构建目录整洁）
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib) # 动态库输出到 build/lib
#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib) # 静态库统一输出到 build/lib

add_subdirectory("OS")

message(STATUS "Vcpkg include dir: ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/include")

#include_directories(
#    ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/include
#    ${CMAKE_CURRENT_SOURCE_DIR}/src
#    ${CMAKE_CURRENT_SOURCE_DIR}/core    
#)
# 自动收集 src 下所有 .cpp 和 .h 文件
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
    "src/core/*.h"
    "src/core/Data/*.h"
)

# 添加可执行文件
add_executable(LinDesk ${SOURCES})

target_link_libraries(LinDesk 
    PRIVATE
        Qt5::Widgets
        Qt5::WinExtras
        "OS"
)
target_include_directories(LinDesk
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        
)


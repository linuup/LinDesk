# CMakeLists.txt for a Qt project that creates a window and draws text with QPainter
cmake_minimum_required(VERSION 3.31)

#C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 使用 Unicode
add_definitions(-DUNICODE -D_UNICODE)

project("LinDesk")

add_compile_options("/utf-8")

# 查找 Qt5 库
find_package(Qt5 COMPONENTS Widgets WinExtras Core REQUIRED)
# 包含 Qt 的 MOC 生成
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 使用预设中的 CMAKE_TOOLCHAIN_FILE（如果没有预设，则回退到默认值）
# if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
#   message(WARNING "未指定 vcpkg 工具链文件，请通过预设配置！")
# endif()

add_subdirectory("OS")

# 自动收集 src 下所有 .cpp 和 .h 文件
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
    "src/*.h"
    "src/Data/*.h"
    "src/Monitor/*.h"
    "src/Monitor/*.cpp"
)

# 添加可执行文件
add_executable(LinDesk ${SOURCES})


target_link_libraries(LinDesk 
    PRIVATE
        Shell32
        Shlwapi
        Comctl32
        Qt5::Widgets
        Qt5::Core
        Qt5::WinExtras
        "OS"
)
target_include_directories(LinDesk
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Monitor
)
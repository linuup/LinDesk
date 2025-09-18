# CMakeLists.txt for a Qt project that creates a window and draws text with QPainter
cmake_minimum_required(VERSION 3.14)
#C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 使用 Unicode
add_definitions(-DUNICODE -D_UNICODE)

project("LinDesk")

add_compile_options("/utf-8")

# 添加子目录，先编译库，再编译主程序lib
set(CMAKE_PREFIX_PATH "C:/Qt/5.15.2/msvc2019_64/lib/cmake")

# 查找 Qt5 库
find_package(Qt5 COMPONENTS Widgets WinExtras REQUIRED)
# 包含 Qt 的 MOC 生成
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)



add_subdirectory("OS")


# 你的 vcpkg 安装路径
set(CMAKE_TOOLCHAIN_FILE "C:/software1/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

message(STATUS "Vcpkg include dir: ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/include")
# set(VCPKG_MANIFEST_MODE ON)

# 找到 WIL
# find_path(WIL_INCLUDE_DIRS "wil")
# if (NOT WIL_INCLUDE_DIRS)
#    message(FATAL_ERROR "WIL not found! Please install it with vcpkg: vcpkg install wil:x64-windows")
# endif()
# message(STATUS "WIL include dir: ${WIL_INCLUDE_DIRS}")

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
        Shell32
        Shlwapi
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


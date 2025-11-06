#添加SystemApi整个文件夹
# 自动收集 src 下所有 .cpp 和 .h 文件
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "OS.cpp"
    "OS.h"
    "*/*.cpp"
    "*/*.h"
)
# 添加静态库
add_library(OS STATIC ${SOURCES})

# 让主程序能找到 OS.h
target_include_directories(OS PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg_installed/x64-windows/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/SystemApi
)
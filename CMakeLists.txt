# CMakeLists.txt for a Qt project that creates a window and draws text with QPainter
cmake_minimum_required(VERSION 3.14)
project(LinDesk)

set(CMAKE_PREFIX_PATH "C:/Qt/5.15.2/mingw81_64/lib/cmake")

# 添加到配置里

find_package(Qt5 COMPONENTS Widgets REQUIRED)

# 其他文件要在main.cpp里引用,添加目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(LinDesk main.cpp DeskFileUi.cpp DeskFileModel.cpp)

target_link_libraries(LinDesk Qt5::Widgets)

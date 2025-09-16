add_library(OS STATIC "OS.cpp" "OS.h")

# 让主程序能找到 OS.h
target_include_directories(OS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

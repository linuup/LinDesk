cmake_minimum_required(VERSION 3.16)
project(OCR_Plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Widgets Network REQUIRED)

add_library(OCR_Plugin MODULE 
    OCR_Baidu.h 
    OCR_Baidu.cpp
    ScreenShot.h
    OcrResultDialog.h
    OcrSettingsDialog.h
    ../../src/Plugins/ILinPlugin.h
    ../../src/Fluent/FluentButton.h
    ../../src/Fluent/FluentResultView.h
)

target_link_libraries(OCR_Plugin PRIVATE 
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
)

# Optional: Automatic metadata JSON inclusion
target_sources(OCR_Plugin PRIVATE mock_ocr.json)

# Post-build: Copy to LinDesk runtime plugins directory
add_custom_command(TARGET OCR_Plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:LinDesk>/plugins
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OCR_Plugin> $<TARGET_FILE_DIR:LinDesk>/plugins/
    COMMENT "Copying OCR_Plugin to plugins folder..."
)

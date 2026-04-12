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

# Plugin metadata JSON
target_sources(OCR_Plugin PRIVATE ocr_plugin.json)

# Post-build: when built from the LinDesk root project, copy the plugin into
# the app's runtime plugins directory. Standalone builds should not depend on
# the LinDesk target existing.
if(TARGET LinDesk)
    add_custom_command(TARGET OCR_Plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:LinDesk>/plugins
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OCR_Plugin> $<TARGET_FILE_DIR:LinDesk>/plugins/
        COMMENT "Copying OCR_Plugin to plugins folder..."
    )
endif()

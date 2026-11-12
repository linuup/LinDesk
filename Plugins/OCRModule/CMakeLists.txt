cmake_minimum_required(VERSION 3.16)
project(OCR_Plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Widgets Network Concurrent REQUIRED)

add_library(OCR_Plugin MODULE 
    OCR_Baidu.h 
    OCR_Baidu.cpp
    ScreenShot.h
    OcrResultDialog.h
    OcrSettingsDialog.h
    OcrLocalEngine.h
    ../../src/plugins/ILinPlugin.h
    ../../src/ui/Fluent/FluentButton.h
    ../../src/ui/Fluent/FluentDialog.h
    ../../src/ui/Fluent/FluentDialog.cpp
    ../../src/ui/Fluent/FluentInput.h
    ../../src/ui/Fluent/FluentComboBox.h
    ../../src/ui/Fluent/FluentResultView.h
    ../../src/ui/Fluent/FluentBaseWidget.h
)

set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/onnxruntime")

target_link_libraries(OCR_Plugin PRIVATE 
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
    Qt5::Concurrent
    ${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib
)

target_include_directories(OCR_Plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ui/Fluent
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/plugins
    ${ONNXRUNTIME_ROOT}/include
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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll $<TARGET_FILE_DIR:LinDesk>/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll $<TARGET_FILE_DIR:LinDesk>/plugins/
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/models $<TARGET_FILE_DIR:LinDesk>/plugins/models
        COMMENT "Copying OCR_Plugin, onnxruntime.dll and models to plugins folder..."
    )
endif()

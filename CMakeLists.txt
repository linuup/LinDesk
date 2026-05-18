# CMakeLists.txt for LinDesk
cmake_minimum_required(VERSION 3.31)

# ========== 版本定义，单一来源 ==========
# 修改版本号只需要改这里
set(LINDESK_VERSION_MAJOR 1)
set(LINDESK_VERSION_MINOR 0)
set(LINDESK_VERSION_PATCH 7)
set(LINDESK_VERSION_TAG "Stable")  # 可选值：Alpha, Beta, RC, Stable �?
set(LINDESK_VERSION "${LINDESK_VERSION_MAJOR}.${LINDESK_VERSION_MINOR}.${LINDESK_VERSION_PATCH}")
# 生成完整版本字符�?set(LINDESK_VERSION "${LINDESK_VERSION_MAJOR}.${LINDESK_VERSION_MINOR}.${LINDESK_VERSION_PATCH}")
set(LINDESK_VERSION_FULL "${LINDESK_VERSION} ${LINDESK_VERSION_TAG}")

# 设置安装器输入目录，可自定义
set(LINDESK_PACKAGE_CONFIG "Release" CACHE STRING "Build configuration used by the installer input directory")
set(LINDESK_OUTPUT_DIR "${CMAKE_BINARY_DIR}/${LINDESK_PACKAGE_CONFIG}")
set(LINDESK_INSTALLER_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/installer")

# ========== C++ 标准 ==========
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SUPPRESS_REGENERATION ON)

# 使用 Unicode
add_definitions(-DUNICODE -D_UNICODE)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# --- vcpkg auto discovery ---
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    else()
        find_program(_VCPKG_EXE vcpkg)
        if(_VCPKG_EXE)
            get_filename_component(_VCPKG_ROOT "${_VCPKG_EXE}" DIRECTORY)
        endif()
    endif()

    if(_VCPKG_ROOT AND EXISTS "${_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
    endif()
endif()

# 如果 CMAKE_TOOLCHAIN_FILE 被设置成目录，自动修正为 vcpkg.cmake
if(IS_DIRECTORY "${CMAKE_TOOLCHAIN_FILE}")
    if(EXISTS "${CMAKE_TOOLCHAIN_FILE}/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain" FORCE)
    elseif(EXISTS "${CMAKE_TOOLCHAIN_FILE}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain" FORCE)
    endif()
endif()

if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "Unable to find vcpkg toolchain. Set VCPKG_ROOT or pass -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg.cmake")
endif()
# -------------------------

project(LinDesk VERSION "${LINDESK_VERSION}")

# 配置 Version.h
configure_file(
    "${CMAKE_SOURCE_DIR}/src/app/Version.h.in"
    "${CMAKE_SOURCE_DIR}/src/app/Version.h"
    @ONLY
)

configure_file(
    "${CMAKE_SOURCE_DIR}/src/app/LinDesk.rc.in"
    "${CMAKE_SOURCE_DIR}/src/app/LinDesk.rc"
    @ONLY
)

# 配置 package.iss，输出到 installer 目录
configure_file(
    "${CMAKE_SOURCE_DIR}/installer/package.iss.in"
    "${CMAKE_SOURCE_DIR}/installer/package.iss"
    @ONLY
)

add_compile_options("/utf-8")

# Find Qt5
find_package(Qt5 COMPONENTS Widgets WinExtras Core Network OpenGL Svg Qml Quick QuickWidgets Multimedia MultimediaWidgets LinguistTools REQUIRED)

# 启用 Qt MOC、RCC、UIC 自动生成
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_subdirectory("OS")
add_subdirectory("LinDeskUpdate")

set(LINDESK_W11_MENU_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/W11MenuUI/Win11MenuIsland.vcxproj")
set(LINDESK_HAS_W11_MENU_HOST OFF)
if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio" AND EXISTS "${LINDESK_W11_MENU_PROJECT}")
    include_external_msproject(Win11MenuIsland "${LINDESK_W11_MENU_PROJECT}" PLATFORM x64)
    set(LINDESK_HAS_W11_MENU_HOST ON)
else()
    message(WARNING "Win11 menu host subproject will not be added because Visual Studio generator or W11MenuUI project was not found.")
endif()

# 自动收集 src 下所�?.cpp�?h �?.qrc 文件
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
    "src/*.qrc"
)

list(FILTER SOURCES EXCLUDE REGEX ".*/src/desktop/ListItem\\.(cpp|h)$")

# 添加可执行文�?set(WIN32_RESOURCES)
if(WIN32)
    list(APPEND WIN32_RESOURCES
        "src/app/LinDesk.rc"
    )
endif()

add_executable(LinDesk ${SOURCES} ${WIN32_RESOURCES})
add_dependencies(LinDesk LinDeskUpdate)
if(TARGET Win11MenuIsland)
    add_dependencies(LinDesk Win11MenuIsland)
endif()

# Debug 显示控制台，Release 等版本隐藏控制台
set_target_properties(LinDesk PROPERTIES
    WIN32_EXECUTABLE "$<$<NOT:$<CONFIG:Debug>>:TRUE>"
)

target_link_libraries(LinDesk
    PRIVATE
        Shell32
        Shlwapi
        Ole32
        Uuid
        Comctl32
        dwmapi
        Winmm
        Qt5::Widgets
        Qt5::Core
        Qt5::Network
        Qt5::WinExtras
        Qt5::OpenGL
        Qt5::Svg
        Qt5::Qml
        Qt5::Quick
        Qt5::QuickWidgets
        "OS"
)

target_include_directories(LinDesk
    PRIVATE
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/desktop
        ${CMAKE_CURRENT_SOURCE_DIR}/src/box
        ${CMAKE_CURRENT_SOURCE_DIR}/src/services
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Fluent
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models/data
        ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor
        ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins
)

option(LINDESK_BUILD_OCR_PLUGIN "Build OCR plugin project" ON)
if(LINDESK_BUILD_OCR_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/OCRModule/CMakeLists.txt")
    add_subdirectory("Plugins/OCRModule")
endif()

set(TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/lindesk_en_US.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/lindesk_zh_CN.ts
)

# 复制并配�?ts 文件，替换版本号占位�?set(TS_FILES_CONFIGURED)
foreach(TS_FILE ${TS_FILES})
    get_filename_component(TS_NAME ${TS_FILE} NAME)
    set(CONFIGURED_TS_FILE "${CMAKE_BINARY_DIR}/translations/${TS_NAME}")
    configure_file(
        ${TS_FILE}
        ${CONFIGURED_TS_FILE}
        @ONLY
    )
    list(APPEND TS_FILES_CONFIGURED ${CONFIGURED_TS_FILE})
endforeach()

qt5_add_translation(QM_FILES ${TS_FILES_CONFIGURED})

add_custom_target(translations ALL DEPENDS ${QM_FILES})
add_dependencies(LinDesk translations)

add_custom_command(TARGET LinDesk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LinDesk>/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${QM_FILES}
        "$<TARGET_FILE_DIR:LinDesk>/translations"
    COMMENT "Copying translation files to output directory..."
)

# 确保构建目录存在，用�?ts 文件配置
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/translations")

# Post-build: Copy OpenSSL DLLs to executable directory
add_custom_command(TARGET LinDesk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Openssl/libcrypto-1_1-x64.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/Openssl/libssl-1_1-x64.dll"
        $<TARGET_FILE_DIR:LinDesk>
    COMMENT "Copying OpenSSL DLLs to output directory..."
)

# Post-build: Copy Qt resource source files to executable directory for installer packaging
add_custom_command(TARGET LinDesk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
        "$<TARGET_FILE_DIR:LinDesk>/resources"
    COMMENT "Copying resource files to output directory..."
)

if(LINDESK_HAS_W11_MENU_HOST)
    add_custom_command(TARGET LinDesk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/W11MenuUI/bin/x64/$<CONFIG>"
            "$<TARGET_FILE_DIR:LinDesk>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/W11MenuUI/Win11ContextMenu"
            "$<TARGET_FILE_DIR:LinDesk>/Win11ContextMenu"
        COMMENT "Copying Win11 menu host to output directory..."
    )
endif()

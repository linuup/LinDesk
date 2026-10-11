# CMakeLists.txt for LinDesk
cmake_minimum_required(VERSION 3.31)

# ========== 版本定义，单一来源 ==========
# 修改版本号只需要改这里
set(LINDESK_VERSION_MAJOR 1)
set(LINDESK_VERSION_MINOR 0)
set(LINDESK_VERSION_PATCH 8)
set(LINDESK_VERSION_TAG "Stable")  # 可选值：Alpha, Beta, RC, Stable
set(LINDESK_VERSION "${LINDESK_VERSION_MAJOR}.${LINDESK_VERSION_MINOR}.${LINDESK_VERSION_PATCH}")
# 生成完整版本字符串set(LINDESK_VERSION "${LINDESK_VERSION_MAJOR}.${LINDESK_VERSION_MINOR}.${LINDESK_VERSION_PATCH}")
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

add_compile_options("/utf-8" "/FS")

# Find Qt5
find_package(Qt5 COMPONENTS Widgets WinExtras Core Network OpenGL Svg Qml Quick QuickWidgets Multimedia MultimediaWidgets LinguistTools REQUIRED)
find_package(GTest CONFIG REQUIRED)

# 启用 Qt MOC、RCC、UIC 自动生成
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

enable_testing()

add_subdirectory("LinDeskUpdate")

set(LINDESK_W11_MENU_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/W11MenuUI/Win11MenuIsland.vcxproj")
set(LINDESK_HAS_W11_MENU_HOST OFF)
if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio" AND EXISTS "${LINDESK_W11_MENU_PROJECT}")
    include_external_msproject(Win11MenuIsland "${LINDESK_W11_MENU_PROJECT}" PLATFORM x64)
    set(LINDESK_HAS_W11_MENU_HOST ON)
else()
    message(WARNING "Win11 menu host subproject will not be added because Visual Studio generator or W11MenuUI project was not found.")
endif()

# 自动收集 src 下所?.cpp?h ?.qrc 文件
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
    "src/*.qrc"
)

list(FILTER SOURCES EXCLUDE REGEX ".*/src/desktop/ListItem\\.(cpp|h)$")
list(FILTER SOURCES EXCLUDE REGEX ".*/subprocess/.*")

file(GLOB_RECURSE LINWIDGET_SHARED_SOURCES CONFIGURE_DEPENDS
    "src/desktop/widgets/linwidget/*.cpp"
    "src/desktop/widgets/linwidget/*.h"
)

# 添加可执行文件资源 (WIN32_RESOURCES)
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

set(LINDESK_WEBVIEW2_ROOT "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/1.0.3179.45/build/native")
if(EXISTS "${LINDESK_WEBVIEW2_ROOT}/include/WebView2.h")
    target_compile_definitions(LinDesk PRIVATE LINDESK_HAS_WEBVIEW2)
    target_include_directories(LinDesk PRIVATE "${LINDESK_WEBVIEW2_ROOT}/include")
    target_link_libraries(LinDesk PRIVATE "${LINDESK_WEBVIEW2_ROOT}/x64/WebView2LoaderStatic.lib")
endif()

# ==============================================================================
# CEF (Chromium Embedded Framework) OSR Integration
# ==============================================================================
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cef" CACHE PATH "Path to CEF root directory")
if(DEFINED ENV{CEF_ROOT} AND NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
    set(CEF_ROOT "$ENV{CEF_ROOT}")
endif()

set(LINDESK_HAS_CEF OFF)
if(EXISTS "${CEF_ROOT}/include/cef_app.h")
    message(STATUS "[CEF] Found Chromium Embedded Framework at: ${CEF_ROOT}")
    set(LINDESK_HAS_CEF ON)
    target_compile_definitions(LinDesk PRIVATE LINDESK_HAS_CEF=1)
    target_include_directories(LinDesk PRIVATE "${CEF_ROOT}")

    target_link_directories(LinDesk PRIVATE
        "${CEF_ROOT}/Release"
        "${CEF_ROOT}/Debug"
        "${CEF_ROOT}/libcef_dll_wrapper/Release"
        "${CEF_ROOT}/libcef_dll_wrapper/Debug"
        "${CEF_ROOT}/libcef_dll_wrapper"
    )

    target_link_libraries(LinDesk PRIVATE
        libcef
        libcef_dll_wrapper
    )

    # 独立子进程目标 LinDeskSubprocess
    set(SUBPROCESS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/desktop/widgets/cef/subprocess/main.cpp")
    if(EXISTS "${SUBPROCESS_SRC}")
        add_executable(LinDeskSubprocess WIN32 "${SUBPROCESS_SRC}")
        target_include_directories(LinDeskSubprocess PRIVATE "${CEF_ROOT}")
        target_link_directories(LinDeskSubprocess PRIVATE
            "${CEF_ROOT}/Release"
            "${CEF_ROOT}/Debug"
            "${CEF_ROOT}/libcef_dll_wrapper/Release"
            "${CEF_ROOT}/libcef_dll_wrapper/Debug"
            "${CEF_ROOT}/libcef_dll_wrapper"
        )
        target_link_libraries(LinDeskSubprocess PRIVATE libcef libcef_dll_wrapper)
        add_dependencies(LinDesk LinDeskSubprocess)
    endif()
else()
    message(STATUS "[CEF] CEF SDK not found in 3rdparty/cef. Run scripts/download_cef.ps1 to enable CEF OSR.")
endif()

target_compile_definitions(LinDesk PRIVATE "LINDESK_SOURCE_DIR=R\"(${CMAKE_CURRENT_SOURCE_DIR})\"")

target_link_libraries(LinDesk
    PRIVATE
        Shell32

        Psapi
        Shlwapi
        Ole32
        Uuid
        Comctl32
        dwmapi
        Winmm

        Iphlpapi
        Dxgi

        Pdh
        Qt5::Widgets
        Qt5::Core
        Qt5::Network
        Qt5::WinExtras
        Qt5::OpenGL
        Qt5::Svg
        Qt5::Qml
        Qt5::Quick
        Qt5::QuickWidgets
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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/services/SystemApi
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Fluent
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models/data
        ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor
        ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins
)

add_executable(LinDeskSystemInfoTests
    tests/SystemInfoServiceTests.cpp
    src/services/SystemInfo/SystemInfoService.cpp
    src/services/SystemInfo/SystemInfoService.h
)

target_link_libraries(LinDeskSystemInfoTests
    PRIVATE
        Iphlpapi
        Dxgi
        Pdh
        Qt5::Core
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(LinDeskSystemInfoTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME LinDeskSystemInfoTests COMMAND LinDeskSystemInfoTests)
option(LINDESK_BUILD_OCR_PLUGIN "Build OCR plugin project" ON)
if(LINDESK_BUILD_OCR_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/OCRModule/CMakeLists.txt")
    add_subdirectory("Plugins/OCRModule")
endif()

set(TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/lindesk_en_US.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/lindesk_zh_CN.ts
)

# 复制并配置 ts 文件，替换版本号占位符
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

# 确保构建目录存在,并且 translations 目录存在以存放生成的 .qm 文件
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/translations")

# Post-build: Copy OpenSSL DLLs to executable directory
add_custom_command(TARGET LinDesk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Openssl/libcrypto-1_1-x64.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/Openssl/libssl-1_1-x64.dll"
        $<TARGET_FILE_DIR:LinDesk>
    COMMENT "Copying OpenSSL DLLs to output directory..."
)

if(EXISTS "${LINDESK_WEBVIEW2_ROOT}/x64/WebView2Loader.dll")
    add_custom_command(TARGET LinDesk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LINDESK_WEBVIEW2_ROOT}/x64/WebView2Loader.dll"
            $<TARGET_FILE_DIR:LinDesk>
        COMMENT "Copying WebView2Loader to output directory..."
    )
endif()

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




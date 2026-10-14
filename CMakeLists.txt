# CMakeLists.txt for LinDesk
cmake_minimum_required(VERSION 3.31)

# ========== 版本定义，单一来源 ==========
# 修改版本号只需要改这里
set(LINDESK_VERSION_MAJOR 1)
set(LINDESK_VERSION_MINOR 1)
set(LINDESK_VERSION_PATCH 2)
set(LINDESK_VERSION_TAG "Stable")  # 可选值：Alpha, Beta, RC, Stable
set(LINDESK_VERSION "${LINDESK_VERSION_MAJOR}.${LINDESK_VERSION_MINOR}.${LINDESK_VERSION_PATCH}")
# 生成完整版本字符串set(LINDESK_VERSION "${LINDESK_VERSION_MAJOR}.${LINDESK_VERSION_MINOR}.${LINDESK_VERSION_PATCH}")
set(LINDESK_VERSION_FULL "${LINDESK_VERSION} ${LINDESK_VERSION_TAG}")

# 设置安装器输入目录，可自定义
set(LINDESK_PACKAGE_CONFIG "Release" CACHE STRING "Build configuration used by the installer input directory")
set(LINDESK_OUTPUT_DIR "${CMAKE_BINARY_DIR}/${LINDESK_PACKAGE_CONFIG}")
set(LINDESK_INSTALLER_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/installer")

# ========== C++ 标准 ==========
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SUPPRESS_REGENERATION ON)

# 使用 Unicode 与 NOMINMAX
add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)

if(MSVC)
    add_compile_options(/Zc:char8_t-)
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

if(MSVC)
    add_compile_options(/Zc:char8_t-)
endif()

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

# Set 64-bit Qt path if available
if(WIN32 AND (CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_GENERATOR_PLATFORM STREQUAL "x64" OR CMAKE_CL_64))
    if(EXISTS "F:/Qt/5.15.2/msvc2019_64")
        list(PREPEND CMAKE_PREFIX_PATH "F:/Qt/5.15.2/msvc2019_64")
    endif()
endif()

# Find Qt5
find_package(Qt5 COMPONENTS Widgets WinExtras Core Network OpenGL Svg Qml Quick QuickWidgets Multimedia MultimediaWidgets LinguistTools Concurrent REQUIRED)
find_package(GTest CONFIG REQUIRED)

# 引入 spdlog (通过 vcpkg manifest 模式提供)
# 若找不到请先 vcpkg install spdlog / 重新 configure 由 manifest 自动安装
find_package(spdlog CONFIG REQUIRED)

# 启用 Qt MOC、RCC、UIC 自动生成
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(CTest)          # 内部自动 enable_testing()，并定义 BUILD_TESTING（默认 ON）

add_subdirectory("LinDeskUpdate")

set(LINDESK_W11_MENU_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/W11MenuUI/Win11MenuIsland.vcxproj")
set(LINDESK_HAS_W11_MENU_HOST OFF)
if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio" AND EXISTS "${LINDESK_W11_MENU_PROJECT}")
    include_external_msproject(Win11MenuIsland "${LINDESK_W11_MENU_PROJECT}" PLATFORM x64)
    set(LINDESK_HAS_W11_MENU_HOST ON)
else()
    message(WARNING "Win11 menu host subproject will not be added because Visual Studio generator or W11MenuUI project was not found.")
endif()

# 自动收集 src 下所有 .cpp .h .qrc 文件
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
    "src/*.qrc"
)

list(FILTER SOURCES EXCLUDE REGEX ".*/src/desktop/ListItem\\.(cpp|h)$")
list(FILTER SOURCES EXCLUDE REGEX ".*/subprocess/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/src/desktop/widgets/cef/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/src/desktop/widgets/linwidget/WidgetManager\\.(cpp|h)$")
list(FILTER SOURCES EXCLUDE REGEX ".*/src/desktop/widgets/linwidget/WidgetContainer\\.(cpp|h)$")
list(FILTER SOURCES EXCLUDE REGEX ".*/src/desktop/widgets/linwidget/WidgetSettingsDialog\\.(cpp|h)$")

# 添加可执行文件资源 (WIN32_RESOURCES)
if(WIN32)
    list(APPEND WIN32_RESOURCES
        "src/app/LinDesk.rc"
    )
endif()

add_executable(LinDesk ${SOURCES} ${WIN32_RESOURCES})
if(MSVC)
    target_compile_options(LinDesk PRIVATE /Zc:char8_t-)
endif()

add_dependencies(LinDesk LinDeskUpdate)
if(TARGET Win11MenuIsland)
    add_dependencies(LinDesk Win11MenuIsland)
endif()

# Debug 显示控制台，Release 等版本隐藏控制台
set_target_properties(LinDesk PROPERTIES
    WIN32_EXECUTABLE "$<$<NOT:$<CONFIG:Debug>>:TRUE>"
)


# ==============================================================================
# CEF (Chromium Embedded Framework) OSR Integration & LinWidgetRuntime
# ==============================================================================
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cef" CACHE PATH "Path to CEF root directory")
if(DEFINED ENV{CEF_ROOT} AND NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
    set(CEF_ROOT "$ENV{CEF_ROOT}")
endif()

set(LINDESK_HAS_CEF OFF)
if(EXISTS "${CEF_ROOT}/include/cef_app.h")
    message(STATUS "[CEF] Found Chromium Embedded Framework at: ${CEF_ROOT}")
    set(LINDESK_HAS_CEF ON)

    set(CEF_WRAPPER_LIB
        $<$<CONFIG:Debug>:${CEF_ROOT}/libcef_dll_wrapper/Debug/libcef_dll_wrapper.lib>
        $<$<NOT:$<CONFIG:Debug>>:${CEF_ROOT}/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib>
    )

    # 独立组件运行时动态库：LinWidgetRuntime.dll
    file(GLOB_RECURSE LINWIDGET_RUNTIME_SOURCES CONFIGURE_DEPENDS
        "src/desktop/widgets/cef/*.cpp"
        "src/desktop/widgets/cef/*.h"
        "src/desktop/widgets/linwidget/*.cpp"
        "src/desktop/widgets/linwidget/*.h"
    )
    list(APPEND LINWIDGET_RUNTIME_SOURCES
        "src/desktop/widgets/runtime_export.h"
        "src/desktop/widgets/runtime_export.cpp"
        "src/services/AppPaths.h"
        "src/services/IconCacheManager.h"
        "src/services/IconCacheManager.cpp"
        "src/services/DpiScaleHelper.h"
        "src/services/SystemInfo/SystemInfoService.h"
        "src/services/SystemInfo/SystemInfoService.cpp"
    )
    list(FILTER LINWIDGET_RUNTIME_SOURCES EXCLUDE REGEX ".*/subprocess/.*")

    add_library(LinWidgetRuntime SHARED ${LINWIDGET_RUNTIME_SOURCES})
    if(MSVC)
        target_compile_options(LinWidgetRuntime PRIVATE /Zc:char8_t-)
    endif()
    target_compile_definitions(LinWidgetRuntime PRIVATE
        LINWIDGET_RUNTIME_EXPORTS=1
        LINDESK_HAS_CEF=1
        SPDLOG_ENABLE_SOURCE_LOC
    )
    target_include_directories(LinWidgetRuntime PRIVATE
        "${CEF_ROOT}"
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/desktop
        ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    )
    target_link_libraries(LinWidgetRuntime PRIVATE
        "${CEF_ROOT}/Release/libcef.lib"
        ${CEF_WRAPPER_LIB}
        Qt5::Widgets
        Qt5::Core
        Qt5::Gui
        Qt5::Network
        Qt5::WinExtras
        spdlog::spdlog_header_only
        Shell32
        Psapi
        Shlwapi
        Ole32
        Uuid
        Comctl32
        dwmapi
        Winmm
        Dbghelp
        Iphlpapi
        Dxgi
        Pdh
    )

    # 独立子进程目标 LinDeskSubprocess
    set(SUBPROCESS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/desktop/widgets/cef/subprocess/main.cpp")
    if(EXISTS "${SUBPROCESS_SRC}")
        add_executable(LinDeskSubprocess WIN32 "${SUBPROCESS_SRC}")
        if(MSVC)
            target_compile_options(LinDeskSubprocess PRIVATE /Zc:char8_t-)
        endif()
        target_compile_definitions(LinDeskSubprocess PRIVATE LINDESK_HAS_CEF=1)
        target_include_directories(LinDeskSubprocess PRIVATE "${CEF_ROOT}")
        target_link_libraries(LinDeskSubprocess PRIVATE
            "${CEF_ROOT}/Release/libcef.lib"
            ${CEF_WRAPPER_LIB}
        )
        add_dependencies(LinWidgetRuntime LinDeskSubprocess)
    endif()

    add_dependencies(LinDesk LinWidgetRuntime)

    # 组织输出目录：将组件运行时部署到 runtime/linwidget 目录
    set(RUNTIME_DEPLOY_DIR "$<TARGET_FILE_DIR:LinDesk>/runtime/linwidget")
    add_custom_command(TARGET LinWidgetRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:LinWidgetRuntime>"
            "${RUNTIME_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:LinDeskSubprocess>"
            "${RUNTIME_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Resources"
            "${RUNTIME_DEPLOY_DIR}/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_ROOT}/Release/libcef.dll"
            "${CEF_ROOT}/Release/chrome_elf.dll"
            "${CEF_ROOT}/Release/d3dcompiler_47.dll"
            "${CEF_ROOT}/Release/v8_context_snapshot.bin"
            "${CEF_ROOT}/Release/vk_swiftshader.dll"
            "${CEF_ROOT}/Release/vk_swiftshader_icd.json"
            "${CEF_ROOT}/Release/vulkan-1.dll"
            "${CEF_ROOT}/Release/dxcompiler.dll"
            "${CEF_ROOT}/Release/dxil.dll"
            "${RUNTIME_DEPLOY_DIR}"
        COMMENT "Deploying LinWidget runtime and CEF binaries to runtime/linwidget..."
    )
else()
    message(STATUS "[CEF] CEF SDK not found in 3rdparty/cef. Widget runtime will not be built.")
endif()

target_compile_definitions(LinDesk PRIVATE "LINDESK_SOURCE_DIR=R\"(${CMAKE_CURRENT_SOURCE_DIR})\"" SPDLOG_ENABLE_SOURCE_LOC)

target_link_libraries(LinDesk
    PRIVATE
        Shell32
        Propsys

        Psapi
        Shlwapi
        Ole32
        Uuid
        Comctl32
        dwmapi
        Winmm
        Dbghelp

        Iphlpapi
        Dxgi

        Pdh
        spdlog::spdlog_header_only
        Qt5::Widgets
        Qt5::Core
        Qt5::Network
        Qt5::WinExtras
        Qt5::OpenGL
        Qt5::Svg
        Qt5::Qml
        Qt5::Quick
        Qt5::QuickWidgets
        Qt5::Concurrent
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

if(BUILD_TESTING)
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

add_executable(LinDeskWidgetEnvelopeTests
    tests/WidgetMessageEnvelopeTests.cpp
    src/desktop/widgets/linwidget/WidgetMessageTypes.cpp
    src/desktop/widgets/linwidget/WidgetMessageTypes.h
)

target_link_libraries(LinDeskWidgetEnvelopeTests
    PRIVATE
        Qt5::Core
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(LinDeskWidgetEnvelopeTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME LinDeskWidgetEnvelopeTests COMMAND LinDeskWidgetEnvelopeTests)

add_executable(LinDeskLogManagerTests
    tests/LogManagerTests.cpp
    src/services/LogManager.cpp
    src/services/LogManager.h
)

target_link_libraries(LinDeskLogManagerTests
    PRIVATE
        Qt5::Core
        spdlog::spdlog
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(LinDeskLogManagerTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME LinDeskLogManagerTests COMMAND LinDeskLogManagerTests)

add_executable(LinDeskPersistenceTests
    tests/PersistenceTests.cpp
    src/box/BoxManager.cpp
    src/box/BoxManager.h
    src/box/LinBox.h
    src/monitor/IListenerInterface.h
    src/monitor/IDirectoryMonitorInterface.h
    src/monitor/LinDirectoryMonitor.h
)

target_link_libraries(LinDeskPersistenceTests
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Concurrent
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(LinDeskPersistenceTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/box
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models/data
        ${CMAKE_CURRENT_SOURCE_DIR}/src/services
        ${CMAKE_CURRENT_SOURCE_DIR}/src/services/SystemApi
        ${CMAKE_CURRENT_SOURCE_DIR}/src/desktop
        ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor
)

add_test(NAME LinDeskPersistenceTests COMMAND LinDeskPersistenceTests)

add_executable(LinDeskRuntimeBridgeTests
    tests/RuntimeBridgeTests.cpp
    src/desktop/widgets/runtime_export.cpp
    src/desktop/widgets/runtime_export.h
    src/services/AppPaths.h
)

target_link_libraries(LinDeskRuntimeBridgeTests
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(LinDeskRuntimeBridgeTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME LinDeskRuntimeBridgeTests COMMAND LinDeskRuntimeBridgeTests)

add_executable(LinDeskRuntimeInstallerTests
    tests/RuntimeInstallerTests.cpp
    src/desktop/runtime/RuntimeInstaller.cpp
    src/desktop/runtime/RuntimeInstaller.h
    src/desktop/widgets/runtime_export.cpp
    src/desktop/widgets/runtime_export.h
    src/services/AppPaths.h
)

target_link_libraries(LinDeskRuntimeInstallerTests
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(LinDeskRuntimeInstallerTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME LinDeskRuntimeInstallerTests COMMAND LinDeskRuntimeInstallerTests)

add_executable(LinDeskStoreAppIconCompatTests
    tests/StoreAppIconCompatTests.cpp
    src/services/SysInfoUtil.cpp
    src/services/SysInfoUtil.h
    src/services/SystemApi/SystemApi.cpp
    src/services/SystemApi/SystemApi.h
    src/services/SystemApi/Tool.cpp
    src/services/SystemApi/Tool.h
)

target_link_libraries(LinDeskStoreAppIconCompatTests
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::WinExtras
        Shell32
        Propsys
        Ole32
        User32
        Shlwapi
        Version
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(LinDeskStoreAppIconCompatTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME LinDeskStoreAppIconCompatTests COMMAND LinDeskStoreAppIconCompatTests)
endif(BUILD_TESTING)

# ---- 解决方案文件夹分组 ----
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(TARGET LinDesk LinWidgetRuntime LinDeskSubprocess Win11MenuIsland PROPERTY FOLDER "Apps")
set_property(TARGET LinDeskUpdate PROPERTY FOLDER "Tools")
if(BUILD_TESTING)
    set_property(TARGET LinDeskSystemInfoTests LinDeskWidgetEnvelopeTests
        LinDeskLogManagerTests LinDeskPersistenceTests LinDeskRuntimeBridgeTests LinDeskRuntimeInstallerTests LinDeskStoreAppIconCompatTests PROPERTY FOLDER "Tests")
endif()

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

# 复制应用程序资源到输出目录
add_custom_command(TARGET LinDesk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
        "$<TARGET_FILE_DIR:LinDesk>/resources"
    COMMENT "Copying app resources to output directory..."
)




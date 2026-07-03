cmake_minimum_required(VERSION 3.22)

project(AnyaDance LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "AnyaDance targets Windows and SteamVR/OpenVR.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

include(FetchContent)
include(CTest)

option(ANYADANCE_BUILD_DRIVER "Build the SteamVR driver" ON)
option(ANYADANCE_BUILD_UI "Build AnyaDance.exe UI" ON)
option(ANYADANCE_BUILD_TESTS "Build CTest tests" ON)
option(ANYADANCE_FETCH_OPENVR "Fetch ValveSoftware/openvr if OPENVR_SDK_ROOT is not provided" ON)
option(ANYADANCE_FETCH_IMGUI "Fetch Dear ImGui if IMGUI_ROOT is not provided" ON)
set(OPENVR_SDK_ROOT "" CACHE PATH "Path to a local ValveSoftware/openvr SDK checkout")
set(IMGUI_ROOT "" CACHE PATH "Path to a local Dear ImGui checkout")

if(DEFINED ANYADANCE_BUILD_TOOL)
    set(ANYADANCE_BUILD_UI "${ANYADANCE_BUILD_TOOL}" CACHE BOOL "Build AnyaDance.exe UI" FORCE)
endif()

set(ANYADANCE_OUTPUT_DRIVER_ROOT "${CMAKE_BINARY_DIR}/out/anyadance")

add_library(anyadance_core STATIC
    src/core/frame_state.cpp
    src/core/input_state.cpp
    src/core/json.cpp
    src/core/manipulation.cpp
    src/core/math3d.cpp
    src/core/mmd_retarget.cpp
    src/core/nya_format.cpp
    src/core/protocol.cpp
    src/core/solved_motion.cpp
    src/core/tpose.cpp
    src/core/udp_log.cpp
)

target_include_directories(anyadance_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(anyadance_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(anyadance_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(anyadance_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(resolve_openvr)
    if(OPENVR_SDK_ROOT)
        set(OPENVR_ROOT "${OPENVR_SDK_ROOT}" PARENT_SCOPE)
    else()
        if(NOT ANYADANCE_FETCH_OPENVR)
            message(FATAL_ERROR "OPENVR_SDK_ROOT is required when ANYADANCE_FETCH_OPENVR=OFF")
        endif()
        FetchContent_Populate(
            openvr
            URL https://github.com/ValveSoftware/openvr/archive/refs/tags/v2.2.3.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(OPENVR_ROOT "${openvr_SOURCE_DIR}" PARENT_SCOPE)
    endif()
endfunction()

function(resolve_imgui)
    if(IMGUI_ROOT)
        set(IMGUI_SOURCE_ROOT "${IMGUI_ROOT}" PARENT_SCOPE)
    else()
        if(NOT ANYADANCE_FETCH_IMGUI)
            message(FATAL_ERROR "IMGUI_ROOT is required when ANYADANCE_FETCH_IMGUI=OFF")
        endif()
        FetchContent_Populate(
            imgui
            URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.9.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(IMGUI_SOURCE_ROOT "${imgui_SOURCE_DIR}" PARENT_SCOPE)
    endif()
endfunction()

if(ANYADANCE_BUILD_DRIVER)
    resolve_openvr()
    add_library(driver_anyadance SHARED
        src/driver/driver_factory.cpp
        src/driver/log.cpp
        src/driver/server_driver.cpp
        src/driver/udp_pose_receiver.cpp
        src/driver/virtual_device.cpp
    )
    target_include_directories(driver_anyadance PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/driver"
    )
    # OpenVR is a vendored third-party SDK. Mark its headers as external/system so
    # our strict warning bar is not applied to Valve's own inline stubs (C4100 in
    # openvr_driver.h), while our driver sources keep full /W4 enforcement.
    target_include_directories(driver_anyadance SYSTEM PRIVATE
        "${OPENVR_ROOT}/headers"
    )
    target_compile_definitions(driver_anyadance PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        # /external:W0 sets external (system) headers to warning level 0; the
        # SYSTEM include above maps to /external:I on MSVC.
        target_compile_options(driver_anyadance PRIVATE /W4 /permissive- /external:W0)
    endif()
    target_link_libraries(driver_anyadance PRIVATE anyadance_core "${OPENVR_ROOT}/lib/win64/openvr_api.lib" Ws2_32)
    set_target_properties(driver_anyadance PROPERTIES
        PREFIX ""
        OUTPUT_NAME "driver_anyadance"
        RUNTIME_OUTPUT_DIRECTORY "${ANYADANCE_OUTPUT_DRIVER_ROOT}/bin/win64"
        LIBRARY_OUTPUT_DIRECTORY "${ANYADANCE_OUTPUT_DRIVER_ROOT}/bin/win64"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    foreach(CONFIG_TYPE Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${CONFIG_TYPE}" CONFIG_TYPE_UPPER)
        set_target_properties(driver_anyadance PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER}" "${ANYADANCE_OUTPUT_DRIVER_ROOT}/bin/win64"
            "LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER}" "${ANYADANCE_OUTPUT_DRIVER_ROOT}/bin/win64"
            "ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER}" "${CMAKE_BINARY_DIR}/lib/${CONFIG_TYPE}"
        )
    endforeach()
    add_custom_command(TARGET driver_anyadance POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/driver/driver.vrdrivermanifest"
            "${ANYADANCE_OUTPUT_DRIVER_ROOT}/driver.vrdrivermanifest"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/driver/resources"
            "${ANYADANCE_OUTPUT_DRIVER_ROOT}/resources"
        COMMENT "Staging SteamVR driver folder at ${ANYADANCE_OUTPUT_DRIVER_ROOT}"
    )
endif()

if(ANYADANCE_BUILD_UI)
    resolve_imgui()
    add_library(imgui STATIC
        "${IMGUI_SOURCE_ROOT}/imgui.cpp"
        "${IMGUI_SOURCE_ROOT}/imgui_draw.cpp"
        "${IMGUI_SOURCE_ROOT}/imgui_tables.cpp"
        "${IMGUI_SOURCE_ROOT}/imgui_widgets.cpp"
        "${IMGUI_SOURCE_ROOT}/backends/imgui_impl_dx11.cpp"
        "${IMGUI_SOURCE_ROOT}/backends/imgui_impl_win32.cpp"
    )
    target_include_directories(imgui PUBLIC "${IMGUI_SOURCE_ROOT}" "${IMGUI_SOURCE_ROOT}/backends")
    target_compile_definitions(imgui PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

    add_executable(AnyaDanceUI WIN32
        src/ui/main.cpp
        src/ui/ui_state.cpp
        src/ui/input.cpp
        src/ui/panel_main.cpp
        src/ui/panel_body.cpp
        src/ui/panel_log.cpp
        src/ui/panel_dance.cpp
        src/ui/localization.cpp
        src/ui/driver_control.cpp
        src/ui/mmd_dance.cpp
        src/ui/theme.cpp
        src/ui/ui.rc
    )
    target_include_directories(AnyaDanceUI PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(AnyaDanceUI PRIVATE anyadance_core imgui d3d11 dxgi d3dcompiler windowscodecs ole32 shell32 comdlg32 Ws2_32)
    # Ship runtime support files beside the UI: the Dance dialog needs the
    # Blender solve script, and binary redistribution needs the license/notices.
    add_custom_command(TARGET AnyaDanceUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/blender_export_mmd.py"
            "${ANYADANCE_OUTPUT_DRIVER_ROOT}/scripts/blender_export_mmd.py"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/common_steamvr.ps1"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/unregister_driver.ps1"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/restart_steamvr.ps1"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/uninstall.ps1"
            "${ANYADANCE_OUTPUT_DRIVER_ROOT}/scripts/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/DISCLAIMER.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/TRADEMARKS.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/README.zh-CN.md"
            "${ANYADANCE_OUTPUT_DRIVER_ROOT}/"
        COMMENT "Staging UI support files"
    )
    # The UI ships inside the driver root so the registered folder and the run
    # folder are one and the same: exe + driver.vrdrivermanifest + bin/win64/dll.
    set_target_properties(AnyaDanceUI PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ANYADANCE_OUTPUT_DRIVER_ROOT}"
        OUTPUT_NAME "AnyaDance"
    )
    foreach(CONFIG_TYPE Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${CONFIG_TYPE}" CONFIG_TYPE_UPPER)
        set_target_properties(AnyaDanceUI PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER}" "${ANYADANCE_OUTPUT_DRIVER_ROOT}"
        )
    endforeach()
endif()

if(ANYADANCE_BUILD_TESTS AND BUILD_TESTING)
    # One test translation unit per core module, sharing the tiny harness in
    # tests/test_framework.*. Add a case by creating tests/test_<module>.cpp,
    # declaring it in tests/tests.h, calling it from tests/main.cpp, and listing
    # it here.
    add_executable(anyadance_tests
        tests/main.cpp
        tests/test_framework.cpp
        tests/test_math3d.cpp
        tests/test_protocol.cpp
        tests/test_frame_state.cpp
        tests/test_tpose.cpp
        tests/test_input.cpp
        tests/test_manipulation.cpp
        tests/test_finger.cpp
        tests/test_udp_log.cpp
        tests/test_json.cpp
        tests/test_mmd.cpp
        tests/test_nya.cpp
    )
    target_link_libraries(anyadance_tests PRIVATE anyadance_core)
    target_include_directories(anyadance_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    add_test(NAME anyadance_tests COMMAND anyadance_tests)
endif()

# Package the driver root into a single zip so the whole self-contained folder
# (exe + manifest + bin/win64/dll + resources) can be handed to others directly.
if(ANYADANCE_BUILD_DRIVER AND ANYADANCE_BUILD_UI)
    add_custom_target(package_driver ALL
        COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_BINARY_DIR}/out/AnyaDance.zip"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/out/package"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/out/package"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ANYADANCE_OUTPUT_DRIVER_ROOT}"
            "${CMAKE_BINARY_DIR}/out/package/anyadance"
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "${CMAKE_BINARY_DIR}/out/package/anyadance/AnyaDanceTool.exe"
            "${CMAKE_BINARY_DIR}/out/package/anyadance/AnyaDanceTool.pdb"
            "${CMAKE_BINARY_DIR}/out/package/anyadance/AnyaDanceUI.pdb"
            "${CMAKE_BINARY_DIR}/out/package/anyadance/AnyaDance.pdb"
        COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/out/package"
            ${CMAKE_COMMAND} -E tar cf "../AnyaDance.zip" --format=zip "anyadance"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/out"
        DEPENDS driver_anyadance AnyaDanceUI
        COMMENT "Packaging ${CMAKE_BINARY_DIR}/out/AnyaDance.zip"
        VERBATIM
    )
endif()

message(STATUS "Staged driver root: ${ANYADANCE_OUTPUT_DRIVER_ROOT}")

cmake_minimum_required(VERSION 3.24)
project(gx12mouse VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Pinned dependency tags — also exposed to source code for the version banner.
set(GX12_TOMLPP_TAG       "v3.4.0")
set(GX12_HIDAPI_TAG       "hidapi-0.15.0")

# Cache fetched deps inside the project tree (per the planned file map).
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/extern" CACHE PATH "" FORCE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)
include(FetchContent)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        ${GX12_TOMLPP_TAG}
    GIT_SHALLOW    TRUE
)

set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG        ${GX12_HIDAPI_TAG}
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(tomlplusplus hidapi)

add_executable(gx12mouse
    src/main.cpp
)

add_executable(gx12mouse_2x
    src/main.cpp
)

set_target_properties(gx12mouse_2x PROPERTIES OUTPUT_NAME "gx12mouse-2x")

foreach(gx12_target IN ITEMS gx12mouse gx12mouse_2x)
    target_include_directories(${gx12_target} PRIVATE src)

    target_link_libraries(${gx12_target} PRIVATE
        tomlplusplus::tomlplusplus
        hidapi::hidapi
    )
endforeach()

if(WIN32)
    foreach(gx12_target IN ITEMS gx12mouse gx12mouse_2x)
        target_link_libraries(${gx12_target} PRIVATE setupapi)
    endforeach()
    set(GX12_GAMEINPUT_ROOT "${CMAKE_SOURCE_DIR}/tool-cache/nuget/Microsoft.GameInput")
    if(EXISTS "${GX12_GAMEINPUT_ROOT}/native/include/GameInput.h")
        foreach(gx12_target IN ITEMS gx12mouse gx12mouse_2x)
            target_include_directories(${gx12_target} PRIVATE
                "${GX12_GAMEINPUT_ROOT}/native/include"
            )
            target_link_directories(${gx12_target} PRIVATE
                "${GX12_GAMEINPUT_ROOT}/native/lib/x64"
            )
            target_link_libraries(${gx12_target} PRIVATE GameInput)
            target_compile_definitions(${gx12_target} PRIVATE GX12_HAS_GAMEINPUT=1)
        endforeach()
    endif()
endif()

foreach(gx12_target IN ITEMS gx12mouse gx12mouse_2x)
    target_compile_definitions(${gx12_target} PRIVATE
        GX12_APP_VERSION="${PROJECT_VERSION}"
        GX12_TOMLPP_TAG="${GX12_TOMLPP_TAG}"
        GX12_HIDAPI_TAG="${GX12_HIDAPI_TAG}"
    )
endforeach()

foreach(gx12_target IN ITEMS gx12mouse gx12mouse_2x)
    target_compile_definitions(${gx12_target} PRIVATE GX12_ENABLE_RESOLUTION_2X=1)
endforeach()

# Project-only compile flags kept off the FetchContent dependencies because
# /permissive- and WIN32_LEAN_AND_MEAN can break third-party Windows headers.
if(MSVC)
    foreach(gx12_target IN ITEMS gx12mouse gx12mouse_2x)
        target_compile_options(${gx12_target} PRIVATE
            /W4 /permissive- /Zc:__cplusplus /utf-8
        )
        target_link_options(${gx12_target} PRIVATE /MANIFEST:NO)
        target_compile_definitions(${gx12_target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _WIN32_WINNT=0x0A00       # Windows 10/11
            NTDDI_VERSION=0x0A000004  # NTDDI_WIN10_RS3 — needed for ProcessPowerThrottling
        )
    endforeach()
endif()

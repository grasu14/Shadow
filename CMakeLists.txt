cmake_minimum_required(VERSION 3.20)
project(Shadow VERSION 1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Optional: build-time cipher key override ──────────────────────────────────
# Usage: cmake -DSHADOW_CIPHER_KEY="my_custom_key" ..
if(DEFINED SHADOW_CIPHER_KEY)
    add_compile_definitions(SHADOW_CIPHER_KEY="${SHADOW_CIPHER_KEY}")
endif()

# ── Source files ──────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/logger.cpp
    src/engine.cpp
    src/menu.cpp
    src/utils.cpp
    src/backup.cpp
    src/timestomp.cpp
    src/modules.cpp
    src/sqlite_parser.cpp
    src/prefetch_parser.cpp
    src/config.cpp
    src/gui.cpp
    src/updater.cpp
    src/shadow.rc
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/backends/imgui_impl_win32.cpp
    src/imgui/backends/imgui_impl_dx11.cpp
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

# Statically link the MSVC runtime (/MT and /MTd) so the executable runs on machines without VCRedist
set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui/backends
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/Release"
)

# ── Build-mode definitions ────────────────────────────────────────────────────
# Both Debug and Release will have SHADOW_DEBUG defined to keep the hacker console
target_compile_definitions(${PROJECT_NAME} PRIVATE SHADOW_DEBUG)

# ── Windows system libraries ─────────────────────────────────────────────────
# shell32  - SHGetFolderPathA
# advapi32 - Registry APIs, token checks
# bcrypt   - BCryptGenRandom (timestomp jitter)
target_link_libraries(${PROJECT_NAME} PRIVATE shell32 advapi32 bcrypt d3d11 d3dcompiler dwmapi)

# ── MSVC-specific flags ──────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8)

    # Debug: embed debug info for local debugging
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/Z7>)

    # Release: no debug info in the binary (prevents symbol leakage)
    # If you need Release debugging, use /Zi with a separate .pdb

    target_link_options(${PROJECT_NAME} PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

cmake_minimum_required(VERSION 3.20)
project(Shadow VERSION 1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Source files ──────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/logger.cpp
    src/engine.cpp
    src/menu.cpp
    src/utils.cpp
    src/timestomp.cpp
    src/modules.cpp
    src/sqlite_parser.cpp
    src/prefetch_parser.cpp
    src/config.cpp
    src/shadow.rc
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/Release"
)

# ── Build-mode definitions ────────────────────────────────────────────────────
#  Debug  -> SHADOW_DEBUG defined  (verbose scrolling console log)
#  Release -> nothing extra        (minimalist output)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:SHADOW_DEBUG>
)

# ── Windows system libraries ─────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE shell32 advapi32)

# ── MSVC-specific flags ──────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8 /Z7)
    target_link_options(${PROJECT_NAME} PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

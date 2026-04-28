cmake_minimum_required(VERSION 3.21)
project(xp_swiss_vfr CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build unit tests" ON)

if(APPLE)
    # Universal Binary: native arm64 + x86_64 via Rosetta.
    # Allow override from command line (e.g. make lint passes arm64 only).
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
endif()

# ── Version string (from VERSION.txt, "SNAPSHOT" for dev builds) ──────────────
file(READ "${CMAKE_SOURCE_DIR}/VERSION.txt" XP_SWISS_VFR_VERSION)
string(STRIP "${XP_SWISS_VFR_VERSION}" XP_SWISS_VFR_VERSION)
if(NOT RELEASE)
    set(XP_SWISS_VFR_VERSION "SNAPSHOT")
endif()

# ── X-Plane SDK headers (downloaded by `make setup`) ──────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_SOURCE_DIR}/sdk/XPLM
    ${CMAKE_SOURCE_DIR}/sdk/XPWidgets
    ${CMAKE_SOURCE_DIR}/vendor
)

# ── Plugin target ─────────────────────────────────────────────────────────────
# Domain logic that is reused by the test target — kept in a variable so the
# tests can link the same translation units without pulling in the SDK.
set(XP_SWISS_VFR_DATA_SOURCES
    src/data/json_loader.cpp
    src/data/validation.cpp
    src/data/vfr_airport_database.cpp
    src/data/navigraph_source.cpp
)

# Procedure data is SDK-free and reused by the test target. The runner is
# SDK-coupled (XPLM menus + FMS API) and only compiled into the plugin.
set(XP_SWISS_VFR_PROCEDURE_DATA_SOURCES
    src/procedures/build_procedure.cpp
)

# Geometry helpers (lat/lon math) are SDK-free and reused by the test target.
set(XP_SWISS_VFR_GEOMETRY_SOURCES
    src/geometry/coordinate_math.cpp
)

add_library(xp_swiss_vfr MODULE
    src/main.cpp
    src/core/plugin.cpp
    src/procedures/procedure_runner.cpp
    ${XP_SWISS_VFR_DATA_SOURCES}
    ${XP_SWISS_VFR_PROCEDURE_DATA_SOURCES}
    ${XP_SWISS_VFR_GEOMETRY_SOURCES}
)

target_include_directories(xp_swiss_vfr PRIVATE src)

set_target_properties(xp_swiss_vfr PROPERTIES
    OUTPUT_NAME "xp_swiss_vfr"
    SUFFIX      ".xpl"
    PREFIX      ""
)

# ── Platform-specific compile definitions, flags, and libraries ───────────────
if(APPLE)
    target_compile_definitions(xp_swiss_vfr PRIVATE
        APL=1 IBM=0 LIN=0
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM302=1 XPLM303=1
        XPLM400=1 XPLM410=1
        GL_SILENCE_DEPRECATION
        XP_SWISS_VFR_VERSION="${XP_SWISS_VFR_VERSION}"
    )
    target_compile_options(xp_swiss_vfr PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fvisibility=hidden
    )
    set(SDK_MAC_LIBS "${CMAKE_SOURCE_DIR}/sdk/Libraries/Mac")
    target_link_libraries(xp_swiss_vfr PRIVATE
        "${SDK_MAC_LIBS}/XPLM.framework/Versions/Current/XPLM"
        "${SDK_MAC_LIBS}/XPWidgets.framework/Versions/Current/XPWidgets"
        "-framework OpenGL"
    )
elseif(UNIX)
    target_compile_definitions(xp_swiss_vfr PRIVATE
        APL=0 IBM=0 LIN=1
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM302=1 XPLM303=1
        XPLM400=1 XPLM410=1
        XP_SWISS_VFR_VERSION="${XP_SWISS_VFR_VERSION}"
    )
    target_compile_options(xp_swiss_vfr PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fvisibility=hidden
    )
    # On Linux, XPLM/XPWidgets are loaded at runtime by X-Plane — no import libs needed.
    find_package(OpenGL REQUIRED)
    target_link_libraries(xp_swiss_vfr PRIVATE OpenGL::GL)
elseif(WIN32)
    target_compile_definitions(xp_swiss_vfr PRIVATE
        APL=0 IBM=1 LIN=0
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM302=1 XPLM303=1
        XPLM400=1 XPLM410=1
        WIN32_LEAN_AND_MEAN NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _USE_MATH_DEFINES
        XP_SWISS_VFR_VERSION="${XP_SWISS_VFR_VERSION}"
    )
    target_compile_options(xp_swiss_vfr PRIVATE /W3)
    set_target_properties(xp_swiss_vfr PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_link_libraries(xp_swiss_vfr PRIVATE
        "${CMAKE_SOURCE_DIR}/sdk/Libraries/Win/XPLM_64.lib"
        "${CMAKE_SOURCE_DIR}/sdk/Libraries/Win/XPWidgets_64.lib"
        opengl32
    )
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

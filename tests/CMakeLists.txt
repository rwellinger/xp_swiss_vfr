# Standalone test executable. Links Catch2 amalgamated source directly.
# Never links the X-Plane SDK — domain-logic tests only. The same data/*.cpp
# translation units used by the plugin target are compiled in here so the
# tests exercise the production code, not a re-implementation.

set(CATCH2_DIR ${CMAKE_SOURCE_DIR}/vendor/catch2)

add_executable(xp_swiss_vfr_tests
    ${CATCH2_DIR}/catch_amalgamated.cpp
    ${CMAKE_SOURCE_DIR}/src/data/json_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/data/validation.cpp
    ${CMAKE_SOURCE_DIR}/src/data/vfr_airport_database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/navigraph_source.cpp
    ${CMAKE_SOURCE_DIR}/src/data/nearby_airports.cpp
    ${CMAKE_SOURCE_DIR}/src/procedures/build_procedure.cpp
    ${CMAKE_SOURCE_DIR}/src/procedures/procedure_state.cpp
    ${CMAKE_SOURCE_DIR}/src/geometry/coordinate_math.cpp
    test_smoke.cpp
    test_json_loader.cpp
    test_validation.cpp
    test_database.cpp
    test_geometry.cpp
    test_build_procedure.cpp
    test_navigraph_source.cpp
    test_procedure_state.cpp
    test_nearby_airports.cpp
)

target_include_directories(xp_swiss_vfr_tests PRIVATE
    ${CATCH2_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor
)

# Path to the fixtures directory, made available to the test code.
target_compile_definitions(xp_swiss_vfr_tests PRIVATE
    XP_SWISS_VFR_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

if(MSVC)
    target_compile_options(xp_swiss_vfr_tests PRIVATE /W3)
    set_target_properties(xp_swiss_vfr_tests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
else()
    target_compile_options(xp_swiss_vfr_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Tests run on the host arch only — no Universal Binary, no Rosetta slice.
if(APPLE)
    set_target_properties(xp_swiss_vfr_tests PROPERTIES
        OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}"
    )
endif()

set_target_properties(xp_swiss_vfr_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

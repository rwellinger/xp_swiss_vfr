# Standalone test executable. Links Catch2 amalgamated source directly.
# Never links the X-Plane SDK — domain-logic tests only.

set(CATCH2_DIR ${CMAKE_SOURCE_DIR}/vendor/catch2)

add_executable(xp_swiss_vfr_tests
    ${CATCH2_DIR}/catch_amalgamated.cpp
    test_smoke.cpp
)

target_include_directories(xp_swiss_vfr_tests PRIVATE
    ${CATCH2_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor
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

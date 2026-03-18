project(ESA C)

# The "esa_public_api" target defines the include path contract for
# any module that depends on ESA headers.
add_library(esa_public_api INTERFACE)
target_include_directories(esa_public_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public_inc>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fsw/inc>
)
target_link_libraries(esa_public_api INTERFACE
    osal_public_api
)

# Build a static ESA implementation only when simulation stepping is enabled.
# With Task 5 complete, ESA-owned source files are now present for coverage testing
# and future task integration. During the transition phase, stepping implementations
# in PSP continue to serve the main build; ESA sources are used for coverage testing.
if (CFE_SIM_STEPPING)
    # For now, use a placeholder to avoid duplicate symbol conflicts with PSP module
    # ESA source files exist and will be used by coverage tests (see ut-coverage/CMakeLists.txt)
    set(ESA_PLACEHOLDER_SRC "${CMAKE_CURRENT_BINARY_DIR}/esa_placeholder.c")
    file(WRITE "${ESA_PLACEHOLDER_SRC}"
        "/* Auto-generated placeholder - ESA stepping source used in coverage tests */\n"
        "void ESA_EmptySourcePlaceholder(void)\n"
        "{\n"
        "}\n"
    )
    add_library(esa STATIC "${ESA_PLACEHOLDER_SRC}")

    target_link_libraries(esa PUBLIC
        esa_public_api
    )
else()
    # Create minimal interface when stepping not enabled
    add_library(esa INTERFACE)
    target_link_libraries(esa INTERFACE
        esa_public_api
    )
endif()

# Unit test stubs and infrastructure
if (ENABLE_UNIT_TESTS)
    add_subdirectory(ut-stubs)
    add_subdirectory(ut-coverage)
endif()

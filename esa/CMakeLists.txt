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
# The runtime source list intentionally matches esa/ut-coverage/CMakeLists.txt.
if (CFE_SIM_STEPPING)
    set(ESA_SIM_STEPPING_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fsw/src")
    add_library(esa STATIC
        "${ESA_SIM_STEPPING_SRC_DIR}/cfe_psp_sim_stepping.c"
        "${ESA_SIM_STEPPING_SRC_DIR}/cfe_psp_sim_stepping_core.c"
    )

    target_include_directories(esa PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../psp/fsw/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../psp/fsw/shared/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../cfe/cmake/target/inc"
        "${CMAKE_CURRENT_BINARY_DIR}/../psp/inc"
    )

    target_link_libraries(esa PUBLIC
        esa_public_api
    )

    # The generic-linux BSP main() (in osal_bsp) calls ESA_Init() when stepping is enabled.
    # Provide an ESA_Init fallback for OSAL unit/coverage binaries that link osal_bsp
    # without pulling the full ESA runtime (which would conflict with OSAL noop test hooks).
    if (TARGET osal_bsp)
        set(ESA_OSAL_BSP_INIT_STUB_SRC "${CMAKE_CURRENT_BINARY_DIR}/esa_osal_bsp_init_stub.c")
        file(WRITE "${ESA_OSAL_BSP_INIT_STUB_SRC}"
            "__attribute__((weak)) void ESA_Init(void)\n"
            "{\n"
            "}\n"
        )
        add_library(esa_osal_bsp_init_stub STATIC "${ESA_OSAL_BSP_INIT_STUB_SRC}")
        target_link_libraries(esa_osal_bsp_init_stub PUBLIC esa_public_api)
        set_property(TARGET osal_bsp APPEND PROPERTY INTERFACE_LINK_LIBRARIES esa_osal_bsp_init_stub)
    endif()

    # cFE/OSAL coverage runners compile stepping-enabled units directly and may not
    # link the full ESA runtime. Provide weak shim defaults through ut_coverage_link
    # so coverage binaries still link while flight/runtime binaries keep real symbols.
    if (TARGET ut_coverage_link)
        set(ESA_UT_COVERAGE_SHIM_STUB_SRC "${CMAKE_CURRENT_BINARY_DIR}/esa_ut_coverage_shim_stub.c")
        file(WRITE "${ESA_UT_COVERAGE_SHIM_STUB_SRC}"
            "#include <stdint.h>\n"
            "#include <stdbool.h>\n"
            "__attribute__((weak)) int32_t CFE_PSP_SimStepping_Shim_ReportEvent(const void *event)\n"
            "{\n"
            "    (void)event;\n"
            "    return 0;\n"
            "}\n"
            "__attribute__((weak)) bool CFE_PSP_SimStepping_Hook_TaskDelayEligible(uint32_t task_id, uint32_t delay_ms)\n"
            "{\n"
            "    (void)task_id;\n"
            "    (void)delay_ms;\n"
            "    return false;\n"
            "}\n"
            "__attribute__((weak)) int32_t CFE_PSP_SimStepping_WaitForDelayExpiry(uint32_t task_id, uint32_t delay_ms)\n"
            "{\n"
            "    (void)task_id;\n"
            "    (void)delay_ms;\n"
            "    return -1;\n"
            "}\n"
        )
        add_library(esa_ut_coverage_shim_stub STATIC "${ESA_UT_COVERAGE_SHIM_STUB_SRC}")
        target_link_libraries(esa_ut_coverage_shim_stub PUBLIC esa_public_api)
        set_property(TARGET ut_coverage_link APPEND PROPERTY INTERFACE_LINK_LIBRARIES esa_ut_coverage_shim_stub)
    endif()
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

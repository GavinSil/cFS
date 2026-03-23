######################################################################
#
# CMAKE build recipe for ESA white-box coverage tests
#
# This registers the sim_stepping coverage test for ESA.
# The test reuses the PSP sim_stepping test assets but is wired
# into the ESA build system and linked with ESA-owned stubs.
#
######################################################################

# ESA-localized coverage test registration for sim_stepping
# Adapted from psp/unit-test-coverage/modules/CMakeLists.txt add_psp_covtest()
# but simplified for ESA single-module case.

# Define test names
set(TEST_NAME "coverage-esa-sim_stepping")
set(OBJECT_TARGET "${TEST_NAME}-object")
set(RUNNER_TARGET "${TEST_NAME}-testrunner")

# Compile the source unit(s) under test as a separate library
# This is done so that special coverage-specific compile flags can be used on these files
# Note: ESA source files have been migrated to esa/fsw/src in Task 5
set(ESA_SIM_STEPPING_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/src")
add_library(${OBJECT_TARGET} OBJECT
    "${ESA_SIM_STEPPING_SRC_DIR}/esa_stepping.c"
    "${ESA_SIM_STEPPING_SRC_DIR}/esa_stepping_core.c"
)

# Apply the UT_COVERAGE_COMPILE_FLAGS to the units under test
# This should enable coverage analysis on platforms that support this
target_compile_options(${OBJECT_TARGET} PRIVATE
    ${UT_COVERAGE_COMPILE_FLAGS}
    -D_POSIX_C_SOURCE=200809L
)

target_include_directories(${OBJECT_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/unit-test-coverage/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../public_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sample_defs/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/cmake/target/inc"
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
)

# Remove -Werror temporarily for nanosleep/usleep issues (system API coverage for unit test)
target_compile_options(${OBJECT_TARGET} PRIVATE
    -Wno-implicit-function-declaration
)

# Compile a test runner application, which contains the
# actual coverage test code (test cases) and the unit under test
add_executable(${RUNNER_TARGET}
    coveragetest-sim_stepping.c
    $<TARGET_OBJECTS:${OBJECT_TARGET}>
)

# Test runner needs same include paths as the OBJECT_TARGET
target_include_directories(${RUNNER_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/unit-test-coverage/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../public_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sample_defs/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/cmake/target/inc"
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
)

# This also needs to be linked with UT_COVERAGE_LINK_FLAGS (for coverage)
# Link ESA stubs FIRST so they take precedence over PSP stubs (symbol resolution order)
# This is also linked with any other stub libraries needed,
# as well as the UT assert framework
target_link_libraries(${RUNNER_TARGET}
    ${UT_COVERAGE_LINK_FLAGS}
    ut_esa_api_stubs
    psp_module_api
    ut_psp_api_stubs
    ut_psp_libc_stubs
    ut_osapi_stubs
    ut_assert
)

# Add it to the set of tests to run as part of "make test"
add_test(${TEST_NAME} ${RUNNER_TARGET})
foreach(TGT ${INSTALL_TARGET_LIST})
    install(TARGETS ${RUNNER_TARGET} DESTINATION ${TGT}/${UT_INSTALL_SUBDIR})
endforeach()

# Define test names
set(TEST_NAME "coverage-esa-time_hooks")
set(OBJECT_TARGET "${TEST_NAME}-object")
set(RUNNER_TARGET "${TEST_NAME}-testrunner")

set(CFE_TIME_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/modules/time/fsw/src")
add_library(${OBJECT_TARGET} OBJECT
    "${CFE_TIME_SRC_DIR}/cfe_time_stepping.c"
)

target_compile_options(${OBJECT_TARGET} PRIVATE
    ${UT_COVERAGE_COMPILE_FLAGS}
)

target_include_directories(${OBJECT_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/unit-test-coverage/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../public_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sample_defs/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/cmake/target/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/modules/time/fsw/src"
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
)

add_executable(${RUNNER_TARGET}
    coveragetest-time-hooks.c
    $<TARGET_OBJECTS:${OBJECT_TARGET}>
)

target_include_directories(${RUNNER_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/unit-test-coverage/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../public_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sample_defs/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/cmake/target/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/modules/time/fsw/src"
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(${RUNNER_TARGET}
    ${UT_COVERAGE_LINK_FLAGS}
    ut_esa_api_stubs
    psp_module_api
    ut_psp_api_stubs
    ut_psp_libc_stubs
    ut_osapi_stubs
    ut_assert
)

add_test(${TEST_NAME} ${RUNNER_TARGET})
foreach(TGT ${INSTALL_TARGET_LIST})
    install(TARGETS ${RUNNER_TARGET} DESTINATION ${TGT}/${UT_INSTALL_SUBDIR})
endforeach()

# Define test names
set(TEST_NAME "coverage-esa-core_services")
set(OBJECT_TARGET "${TEST_NAME}-object")
set(RUNNER_TARGET "${TEST_NAME}-testrunner")

add_library(${OBJECT_TARGET} OBJECT
    coveragetest-core-services.c
)

target_compile_options(${OBJECT_TARGET} PRIVATE
    ${UT_COVERAGE_COMPILE_FLAGS}
)

target_compile_definitions(${OBJECT_TARGET} PRIVATE
    _UNIT_TEST_
)

target_include_directories(${OBJECT_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/modules/es/fsw/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../public_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/ut_assert/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/cmake/target/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../build/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/modules/core_api/fsw/inc"
    $<TARGET_PROPERTY:es,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:evs,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:sb,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:tbl,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:core_private,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
)

add_executable(${RUNNER_TARGET}
    $<TARGET_OBJECTS:${OBJECT_TARGET}>
)

target_link_options(${RUNNER_TARGET} PRIVATE
    -Wl,--wrap=CFE_ES_GetResetType
    -Wl,--wrap=CFE_ES_GetAppID
    -Wl,--wrap=CFE_EVS_Register
    -Wl,--wrap=CFE_EVS_SendEvent
    -Wl,--wrap=CFE_MSG_Init
    -Wl,--wrap=CFE_SB_CreatePipe
    -Wl,--wrap=CFE_SB_Subscribe
    -Wl,--wrap=CFE_ES_GetPoolBuf
    -Wl,--wrap=CFE_ES_PutPoolBuf
    -Wl,--wrap=CFE_ES_BackgroundInit
    -Wl,--wrap=CFE_ES_WaitForSystemState
    -Wl,--wrap=CFE_ES_ExitApp
    -Wl,--wrap=CFE_ES_IncrementTaskCounter
    -Wl,--wrap=CFE_ES_PerfLogAdd
    -Wl,--wrap=CFE_SB_ReceiveBuffer
    -Wl,--wrap=CFE_ES_BackgroundWakeup
    -Wl,--wrap=CFE_ES_TaskPipe
    -Wl,--wrap=CFE_EVS_ProcessCommandPacket
    -Wl,--wrap=CFE_SB_ProcessCmdPipePkt
    -Wl,--wrap=CFE_TBL_TaskPipe
)

target_link_libraries(${RUNNER_TARGET}
    ${UT_COVERAGE_LINK_FLAGS}
    es
    evs
    sb
    tbl
    sbr
    ut_esa_api_stubs
    ut_core_private_stubs
    ut_assert
)

if (CFE_EDS_ENABLED)
    target_link_libraries(${RUNNER_TARGET} ut_edslib_stubs ut_missionlib_stubs)
endif()

add_test(${TEST_NAME} ${RUNNER_TARGET})
foreach(TGT ${INSTALL_TARGET_LIST})
    install(TARGETS ${RUNNER_TARGET} DESTINATION ${TGT}/${UT_INSTALL_SUBDIR})
endforeach()

# Define test names
set(TEST_NAME "coverage-esa-osal_hooks")
set(OBJECT_TARGET "${TEST_NAME}-object")
set(RUNNER_TARGET "${TEST_NAME}-testrunner")

# Compile OSAL POSIX stepping hook unit under test as a separate object library
set(OSAL_POSIX_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/posix/src")
add_library(${OBJECT_TARGET} OBJECT
    "${OSAL_POSIX_SRC_DIR}/os-posix-stepping.c"
)

target_compile_options(${OBJECT_TARGET} PRIVATE
    ${UT_COVERAGE_COMPILE_FLAGS}
    -D_POSIX_C_SOURCE=200809L
)

target_include_directories(${OBJECT_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/unit-test-coverage/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../public_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sample_defs/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/cmake/target/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/posix/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/inc"
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_options(${OBJECT_TARGET} PRIVATE
    -Wno-implicit-function-declaration
)

add_executable(${RUNNER_TARGET}
    coveragetest-osal-hooks.c
    $<TARGET_OBJECTS:${OBJECT_TARGET}>
)

target_include_directories(${RUNNER_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/unit-test-coverage/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../public_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sample_defs/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/cmake/target/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/posix/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/inc"
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(${RUNNER_TARGET}
    ${UT_COVERAGE_LINK_FLAGS}
    ut_esa_api_stubs
    psp_module_api
    ut_psp_api_stubs
    ut_psp_libc_stubs
    ut_osapi_stubs
    ut_assert
)

add_test(${TEST_NAME} ${RUNNER_TARGET})
foreach(TGT ${INSTALL_TARGET_LIST})
    install(TARGETS ${RUNNER_TARGET} DESTINATION ${TGT}/${UT_INSTALL_SUBDIR})
endforeach()

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

######################################################################
#
# ESA-localized coverage test for OSAL stepping hooks
# Tests all 6 OSAL stepping hook functions (TaskDelay, QueueReceive, BinSemTake)
# with both ACK and COMPLETE phases
#
######################################################################

# Define test names for OSAL hooks
set(TEST_NAME "coverage-esa-osal_hooks")
set(OBJECT_TARGET "${TEST_NAME}-object")
set(RUNNER_TARGET "${TEST_NAME}-testrunner")

# Compile the OSAL POSIX stepping source under test as a separate library
set(OSAL_STEPPING_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/posix/src")
add_library(${OBJECT_TARGET} OBJECT
    "${OSAL_STEPPING_SRC_DIR}/os-posix-stepping.c"
)

# Apply the UT_COVERAGE_COMPILE_FLAGS to the units under test
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
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
)

# Remove -Werror temporarily for system API coverage for unit test
target_compile_options(${OBJECT_TARGET} PRIVATE
    -Wno-implicit-function-declaration
)

# Compile a test runner application for OSAL hooks
add_executable(${RUNNER_TARGET}
    coveragetest-osal-hooks.c
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
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/posix/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../osal/src/os/shared/inc"
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
)

# Link with UT_COVERAGE_LINK_FLAGS and required stub libraries
# ESA stubs FIRST so they take precedence over PSP stubs
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

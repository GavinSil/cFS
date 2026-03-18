#
# CMake recipe for ESA unit test stub functions
#

add_library(ut_esa_api_stubs
    src/cfe_psp_sim_stepping_shim_stubs.c
)

target_link_libraries(ut_esa_api_stubs PRIVATE
    psp_module_api
    ut_assert
)

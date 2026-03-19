# Problems

## Task: Remove OSAL-owned stepping noop helper (2026-03-19)

- Removed out-of-scope OSAL test helper file `osal/src/tests/osal_ut_sim_stepping_noop.c` and removed its injection from `add_osal_ut_exe()` in `osal/CMakeLists.txt`.
- Preserved stepping runtime hooks untouched by leaving `osal/src/os/posix/CMakeLists.txt` and `osal/src/os/posix/src/os-posix-stepping.c` unchanged.
- No ESA CMake edits were required in this task because the accepted ESA-owned weak, test-only compatibility path already exists in `esa/CMakeLists.txt`:
  - weak shim symbols are generated into `esa_ut_coverage_shim_stub` and appended to `ut_coverage_link`.
  - weak `ESA_Init` fallback is generated into `esa_osal_bsp_init_stub` and appended to `osal_bsp` interface links.
- Targeted sufficiency proof used (no broad test sweep):
  - repo search confirms no remaining references to `osal_ut_sim_stepping_noop.c`.
  - `esa/CMakeLists.txt` still declares the three weak stepping shim symbols and links their stub via `ut_coverage_link`.

- Retry note (2026-03-19): verified `GIT_MASTER=1 git diff -- osal/CMakeLists.txt` is empty before/after, so no further formatting edit was needed.

- Cleanup (2026-03-19): Removed residual blank line from `osal/CMakeLists.txt` line 419 (immediately after `add_executable()` in `add_osal_ut_exe()` function). Verified `GIT_MASTER=1 git diff -- osal/CMakeLists.txt` is now empty.

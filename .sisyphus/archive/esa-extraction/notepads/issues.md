# Issues

## F4 Scope Fidelity Check — 2026-03-19
- Rejected scope gate based on live-tree audit: multiple out-of-scope product/build changes outside `.sisyphus/*`.
- Notable drift: cfe/cmake generic build-system hardening edits (`arch_build.cmake`, `generate_git_module_version.cmake`, `global_functions.cmake`, `modules/config/tool/CMakeLists.txt`) not tied to extraction-only scope.
- Untracked product artifacts detected: `apps/sch/fsw/src/sch_stepping.c`, `osal/src/tests/osal_ut_sim_stepping_noop.c`, `tools/sim_stepping_regression.sh`, root runtime/test leftovers (`.cdskeyfile`, `.reservedkeyfile`, `.resetkeyfile`, `EEPROM.DAT`, `EOF`, `gmon.out`, `Testing/Temporary/*`).
- API rename guard check: no rename away from `CFE_PSP_SimStepping_*` found.
- Decision rule applied: any unaccounted product file or scope uncertainty => REJECT.

## Task F3: Full QA Verification — Test Defect Found

**Date**: 2026-03-19

**Issue**: ESA coverage test has 6 failing assertions due to test expectations mismatch

**Location**: `esa/ut-coverage/coveragetest-sim_stepping.c`

**Failing Test Cases**:
- Test 02: `Test_sim_stepping_NotReadyPaths` (lines 50-52)
- Test 03: `Test_sim_stepping_InProcQueryAndIllegal` (lines 63-65)

**Problem**:
Tests expect all pre-init error conditions to return generic `CFE_PSP_SIM_STEPPING_STATUS_FAILURE (-1)`.

Actual ESA behavior returns specific error codes:
- `CFE_PSP_SIM_STEPPING_STATUS_NOT_READY (-3)` when Core not initialized
- `CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_STATE (-8)` when session not started

**Root Cause**:
The test was originally written against PSP module behavior that may have been less specific about error codes. When ESA source was migrated (Task 5), the implementation correctly returns specific error codes, but test assertions were not updated to match.

**Impact**:
- 1 test fails out of 118 total tests (99% pass rate)
- Does NOT indicate source defect — ESA behavior is MORE correct (specific error codes)
- Blocks full QA approval per F3 requirements

**Required Fix**:
Update test assertions in `esa/ut-coverage/coveragetest-sim_stepping.c`:

Line 50: Change expected from `CFE_PSP_SIM_STEPPING_STATUS_FAILURE` to `CFE_PSP_SIM_STEPPING_STATUS_NOT_READY`
Line 51: Change expected from `CFE_PSP_SIM_STEPPING_STATUS_FAILURE` to `CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_STATE`
Line 52: Change expected from `CFE_PSP_SIM_STEPPING_STATUS_FAILURE` to `CFE_PSP_SIM_STEPPING_STATUS_SUCCESS` (QueryState works even when not initialized)

Line 63: Change expected from `CFE_PSP_SIM_STEPPING_STATUS_FAILURE` to `CFE_PSP_SIM_STEPPING_STATUS_NOT_READY`
Line 64: Change expected from `CFE_PSP_SIM_STEPPING_STATUS_FAILURE` to `CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_STATE`
Line 65: Change expected from `CFE_PSP_SIM_STEPPING_STATUS_FAILURE` to `CFE_PSP_SIM_STEPPING_STATUS_SUCCESS`

**Severity**: Medium (blocks QA but easy fix, not a functional defect)

## Task F4+ cFE Build-System Scope Cleanup — COMPLETED

**Date**: 2026-03-19

**Issue**: F4 scope audit flagged four cFE build-system files with generic hardening edits outside ESA-extraction scope:
- `cfe/cmake/arch_build.cmake`
- `cfe/cmake/generate_git_module_version.cmake`
- `cfe/cmake/global_functions.cmake`
- `cfe/modules/config/tool/CMakeLists.txt`

**Root Cause**: During prior tasks, generic build-system improvements were added:
- Safety directory creation (`file(MAKE_DIRECTORY)`) in three cmake scripts
- Absolute path execution + `VERBATIM` + `WORKING_DIRECTORY` in config tool

**Analysis**: NONE of these changes are required for ESA extraction functionality. Accepted ESA changes are entirely in:
- `cfe/cmake/mission_build.cmake` (CFE_SIM_STEPPING env caching)
- `cfe/cmake/mission_defaults.cmake` (ESA discovery and module order)
- `cfe/modules/es/CMakeLists.txt` (ESA public API link when stepping)

**Fix Applied**: Full revert of all four flagged files via `git checkout --`

**Verification**:
- `git status --short` in cfe submodule shows only the three accepted files modified
- `git diff --stat HEAD` confirms: +13 lines -4 lines across 3 in-scope files
- Zero deltas in the four flagged files

**Scope Compliance**:
- ✓ Flagged files fully reverted
- ✓ Preserved files untouched
- ✓ No new edits beyond reversion
- ✓ No commits created

**Impact**: cFE submodule state now strictly reflects ESA-extraction scope (env propagation, ESA discovery/order, ESA public API link) with zero out-of-scope build-system hardening.

## F4 Scope Fidelity Re-run — 2026-03-19 (cleaned tree)
- Re-audited current live product diffs only (root + cfe/osal/apps/sch submodules).
- Result: scope is CLEAN against Tasks 1-12; remaining product deltas are limited to accepted ESA env/discovery wiring, cFE ES link wiring, OSAL POSIX stepping include/link wiring, SCH stepping wiring, ESA runtime/test updates.
- Guardrails pass: no API renames away from `CFE_PSP_SimStepping_*`; no active old PSP sim_stepping path references.
- Final F4 verdict for current tree: APPROVE.

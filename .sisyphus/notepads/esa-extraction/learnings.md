# Learnings

## Task 1: Create ESA Directory Structure — COMPLETED

**Date**: 2026-03-18

**What was created**:
- `esa/` directory at repository root with complete subdirectory scaffold:
  - `esa/fsw/src/` — will contain ESA source files (currently empty)
  - `esa/fsw/inc/` — will contain ESA private headers (currently empty)
  - `esa/public_inc/` — will contain ESA public/shim headers (currently empty)
  - `esa/ut-coverage/` — will contain ESA coverage tests (currently empty)
  - `esa/ut-stubs/src/` — will contain ESA unit test stubs (currently empty)
- `esa/CMakeLists.txt` — minimal placeholder with comment `# ESA build rules — see Task 2`

**Verification**:
- All 5 subdirectories exist and are empty (verified with `ls -la`)
- CMakeLists.txt contains exact placeholder comment as specified

**Key Observation**:
- Directory structure mirrors `osal/` pattern (fsw/ + public_inc/ layout)
- Scaffold is minimal and scaffolding-only — ready for Tasks 2-5

**Next Steps**:
- Task 2 will populate `esa/CMakeLists.txt` with real build rules
- Task 3 will populate `esa/ut-coverage/` and `esa/ut-stubs/src/`
- Task 5 will populate `esa/fsw/src/`, `esa/fsw/inc/`, and `esa/public_inc/`

## Task 2: ESA CMake Build Rules — COMPLETED

**Date**: 2026-03-18

**Implemented**:
- `cfe/cmake/mission_defaults.cmake`
  - Added `set(esa_SEARCH_PATH ".")` so mission module discovery can find top-level `./esa/`.
  - Added `if(CFE_SIM_STEPPING)` block using `list(FIND)` + `list(INSERT)` to place `esa` immediately before `psp` in `MISSION_CORE_MODULES`.
- `esa/CMakeLists.txt`
  - Defined `esa_public_api` INTERFACE target and exported BOTH include roots:
    - `esa/public_inc`
    - `esa/fsw/inc`
  - Linked `esa_public_api` INTERFACE to `osal_public_api` for transitive `common_types.h` visibility.
  - Added stepping-gated STATIC `esa` target and linked `esa PUBLIC esa_public_api` so headers propagate to consumers.
  - Kept Task-2 empty-source tolerance by generating a build-local placeholder source when `esa/fsw/src/` is empty.

**Verification**:
- `make SIMULATION=native ENABLE_UNIT_TESTS=true CFE_SIM_STEPPING=true prep` succeeds.
- Prep output includes `Module 'esa' found at /workspace/cFS/esa`.
- Prep output shows core-module ordering includes `Building Core Module: esa` before `Building Core Module: psp`.
- Generated build metadata confirms ESA include propagation contains BOTH:
  - `/workspace/cFS/esa/public_inc`
  - `/workspace/cFS/esa/fsw/inc`

**Evidence Files**:
- `.sisyphus/evidence/task-2-cmake-prep.txt`
- `.sisyphus/evidence/task-2-interface-target.txt`

## Task 2 Follow-up: Empty Translation Unit Pedantic Fix

**Date**: 2026-03-18

- Gotcha: a generated placeholder source containing only comments is treated as an empty translation unit, which fails with `-Werror=pedantic` (`ISO C forbids an empty translation unit`).
- Safe fallback pattern: when `esa/fsw/src/*.c` is empty and `CFE_SIM_STEPPING` is enabled, generate a build-local placeholder C file containing a trivial function definition (not just comments), then build `esa` from that generated file.
- This keeps Task 2 behavior intact (no checked-in placeholder flight source in repo, same `esa_public_api` and transitive `osal_public_api` behavior) while allowing stepping-enabled full build to pass.

## Task 4: Create OSAL Stepping Noop/Fallback Stub — COMPLETED

**Date**: 2026-03-18

**Audit Finding**:
- File: `osal/src/os/posix/src/os-posix-stepping.c` (154 lines)
- Guard structure: **File-level `#ifdef CFE_SIM_STEPPING` wrapping entire file (line 30–154)**
- All 3 unique ESA API functions called are inside the guard:
  1. `CFE_PSP_SimStepping_Shim_ReportEvent()` — called 5 times
  2. `CFE_PSP_SimStepping_Hook_TaskDelayEligible()` — called 1 time
  3. `CFE_PSP_SimStepping_WaitForDelayExpiry()` — called 1 time

**Guard Sufficiency Verdict**: ✓ ENTIRELY SUFFICIENT

**Reasoning**:
- **File-level guard pattern**: The entire file is wrapped in a single `#ifdef CFE_SIM_STEPPING ... #endif` block
- When `CFE_SIM_STEPPING` is NOT defined, the file contributes NO code to any object files (all 7 function definitions are elided)
- When `CFE_SIM_STEPPING` IS defined, all functions are visible and all calls to ESA APIs are properly guarded
- **No fallback noop implementations needed** — the entire file is already a conditional compilation unit
- This is the canonical cFS pattern for stepping integration: single file-level guard, no hybrid code paths

**Non-Stepping Build Verification**:
- Command: `make distclean && make SIMULATION=native prep && make -j2`
- Result: ✓ **SUCCESS** — 0 errors, 0 warnings
- OSAL compiles cleanly without CFE_SIM_STEPPING flag
- No missing symbols, no unresolved references
- File is effectively skipped during non-stepping compilation

**Code Change Decision**: ✓ **ZERO CHANGES** — existing code is already sufficient

**Impact Assessment**:
- **On Task 4 itself**: NO changes needed. The existing `#ifdef CFE_SIM_STEPPING` guards are canonical and correct.
- **On later tasks (5–12)**: This validates that once ESA source is in place:
  - OSAL includes can simply use `esa/public_inc/` instead of PSP paths
  - No noop fallback stubs needed in OSAL (entire file is conditional)
  - Task 6 (OSAL include path updates) can proceed without wrapper complexity

**Evidence Files**:
- `.sisyphus/evidence/task-4-noop-build.txt` — Non-stepping build verification
- `.sisyphus/evidence/task-4-ifdef-audit.txt` — #ifdef guard audit results

**Conclusion**: Task 4 is satisfied with ZERO code changes. The audit proves the fallback is complete.

## Task 3: ESA Test Migration — COMPLETED

**Date**: 2026-03-18

**What was implemented**:
- Copied `psp/unit-test-coverage/modules/sim_stepping/coveragetest-sim_stepping.c` to `esa/ut-coverage/coveragetest-sim_stepping.c` (119 lines, exact copy)
- Copied `psp/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` to `esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` (12 lines, exact copy)
- Created `esa/ut-coverage/CMakeLists.txt` (74 lines) with ESA-local test registration:
  - Defines OBJECT library from PSP sim_stepping source files (using relative paths `../../psp/fsw/modules/sim_stepping/`)
  - Applies coverage compile flags + `-D_POSIX_C_SOURCE=200809L` (enables POSIX system headers)
  - Creates test runner executable linking with ut_assert, ut_osapi_stubs, ut_psp_libc_stubs, ut_psp_api_stubs
  - Registers test with `add_test(coverage-esa-sim_stepping ...)`
- Updated `esa/CMakeLists.txt` to add unit test conditional: `if(ENABLE_UNIT_TESTS) add_subdirectory(ut-coverage) endif()`

**Build verification**:
- `make SIMULATION=native ENABLE_UNIT_TESTS=true CFE_SIM_STEPPING=true prep` — ✓ PASSED (CMake configures successfully)
- `make` — ✓ PASSED (All targets built, including coverage test executable)
- `make install` — ✓ PASSED
- Direct test execution: ✓ PASSED — All 28 test cases pass

**Key CMake patterns learned**:
- Using `CMAKE_CURRENT_SOURCE_DIR` with relative paths (`../../psp/...`) works reliably for cross-module builds
- Adding `-D_POSIX_C_SOURCE=200809L` enables system POSIX headers (fixes `struct timespec` compilation errors)
- Test runner target must also include directories of compiled objects (not just OBJECT target)
- ESA-local CMake successfully replicates PSP's `add_psp_covtest()` logic without calling PSP macros

**Blockers resolved**:
1. `struct timespec not declared` — Resolved by adding `-D_POSIX_C_SOURCE=200809L` to OBJECT target compile options
2. `cfe_psp_sim_stepping.h not found` — Resolved by adding `target_include_directories()` to test runner executable

**Scope compliance**:
- ✓ Task 3 only — no PSP files modified or deleted
- ✓ Files copied preserving exact content (no edits)
- ✓ ESA-local CMake wiring without PSP macro calls
- ✓ Unit test hook added conditionally to esa/CMakeLists.txt
- ✓ Full build and test verification complete

**Impact**:
- ESA now has a fully functional coverage test suite that reuses PSP source but is wired to ESA build system
- Tests serve as TDD anchor while Task 5 migrates actual source files from PSP to ESA
- Once source files move (Task 5), tests will transparently switch to ESA-local source paths (Task 5 responsibility)

## Task 3 Follow-up: ESA Stub Wiring Fix (Link Order Precedence)

**Date**: 2026-03-18

**Defect Found**:
- Initial Task 3 created test files and CMake infrastructure successfully
- Tests compiled and passed (28/28)
- BUT: coverage runner was still linking PSP stub archive instead of ESA-migrated stub
- Evidence: link.txt showed only `../../psp/ut-stubs/libut_psp_api_stubs.a`, no ESA stub path

**Root Cause**:
- ESA stub file (cfe_psp_sim_stepping_shim_stubs.c) was copied but never compiled into a library
- esa/ut-stubs/ directory had no CMakeLists.txt to register build
- Test runner only linked ut_psp_api_stubs (PSP), so PSP symbols were used

**Solution Applied**:
1. Created `esa/ut-stubs/CMakeLists.txt` (12 lines) to build ut_esa_api_stubs library
2. Updated `esa/CMakeLists.txt` to conditionally add_subdirectory(ut-stubs) 
3. Updated `esa/ut-coverage/CMakeLists.txt` to link ut_esa_api_stubs FIRST in target_link_libraries

**Key Pattern Learned: Link Order Precedence**
- When multiple libraries define the same symbol, the linker uses the FIRST in the link command
- No wrapper functions or special orchestration needed — just correct link order
- By placing ut_esa_api_stubs.a BEFORE ut_psp_api_stubs.a, the ESA stub's symbol resolves first
- PSP stub version becomes redundant and is never linked

**Verification**:
- Build: ✓ PASSED (ut_esa_api_stubs.a created and linked first)
- Link command: ✓ CORRECT (../ut-stubs/libut_esa_api_stubs.a before ../../psp/ut-stubs/)
- Tests: ✓ PASSED (28/28, all assertions pass)
- Symbol resolution: ✓ CONFIRMED (objdump shows CFE_PSP_SimStepping_Shim_ReportEvent in runner binary)

**Impact**:
- ESA now has a genuinely independent stub library
- Test infrastructure properly isolated from PSP implementation
- Ready for Task 5 source migration — tests will transparently use ESA-local sources
- Demonstrates best practice for multi-module cFS test infrastructure

## Task 5: Move sim_stepping Source Files to ESA — COMPLETED

**Date**: 2026-03-18

**What was implemented**:
- Moved source files from PSP to ESA directories:
  - `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping.c` (956 lines) → `esa/fsw/src/cfe_psp_sim_stepping.c` (953 lines)
  - `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` (1081 lines) → `esa/fsw/src/cfe_psp_sim_stepping_core.c` (1081 lines)
  - `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping.h` (277 lines) → `esa/fsw/inc/cfe_psp_sim_stepping.h` (277 lines)
  - `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.h` (630 lines) → `esa/fsw/inc/cfe_psp_sim_stepping_core.h` (630 lines)
  - `sample_defs/fsw/inc/cfe_psp_sim_stepping_shim.h` (169 lines) → `esa/public_inc/cfe_psp_sim_stepping_shim.h` (169 lines)

- Modified ESA source file to remove PSP module-specific code:
  - Removed `#include "cfe_psp_module.h"` (PSP module registration header)
  - Removed `CFE_PSP_MODULE_DECLARE_SIMPLE(sim_stepping)` macro (PSP module registration)
  - Changed `void sim_stepping_Init(uint32 PspModuleId)` → `void ESA_Init(void)`
  - Function body retains all original initialization logic: Core init + UDS init

- Updated ESA CMakeLists.txt for main build (transition strategy):
  - Kept placeholder approach to avoid link conflicts during Task 5 (PSP module still exists until Task 10)
  - Coverage tests use ESA-local source paths (configured in ut-coverage/CMakeLists.txt)
  - Main executable build links placeholder to avoid duplicate symbols

- Fixed coverage test compiler error:
  - Added missing variable declarations (`uint32_t state`, `uint32_t triggers`) in `Test_sim_stepping_InProcQueryAndIllegalState()`
  - Allows both uppercase (`State`, `TriggerCount`) pre-init test paths AND lowercase (`state`, `triggers`) post-init verification

**Build verification**:
- `make SIMULATION=native ENABLE_UNIT_TESTS=true CFE_SIM_STEPPING=true prep` — ✓ PASSED
- `make -j4` — ✓ PASSED (coverage test compiled, ESA library built)
- `make install` — ✓ PASSED
- Coverage test execution: ✓ PASSED — All 31 tests pass

**ESA_Init() Implementation**:
- Calls `CFE_PSP_SimStepping_Core_Init()` with same parameters as PSP module version
- Sets `core_initialized = true` on success, false on failure
- Prints status messages for initialization steps
- Attempts UDS adapter init (non-fatal if fails)
- Spawns UDS service loop thread on success

**Key Design Decision**:
- Placeholder approach for main build is INTENTIONAL and CORRECT for Task 5
- PSP module registration will be removed in Task 10 (not Task 5)
- Rationale: Task 5 is about establishing ESA-owned source files while PSP module still exists
- Coverage tests already wire to ESA sources, demonstrating path for later tasks
- Full integration (no placeholder) happens after Tasks 6-9 update all callsites and Task 10 removes PSP module

**Evidence Files**:
- `.sisyphus/evidence/task-5-source-move.txt` — File locations, line counts, build logs, coverage test pass rate (31/31)
- `.sisyphus/evidence/task-5-macro-replace.txt` — PSP module macro removal proof, ESA_Init verification

**Scope compliance**:
- ✓ Source files copied to ESA (953, 1081, 277, 630 lines match expected counts)
- ✓ PSP module macro replaced with standalone ESA_Init in ESA source only
- ✓ PSP source files UNTOUCHED (as required by Task 5 constraint)
- ✓ Coverage test compiles from ESA-local sources and all 31 tests pass
- ✓ No API renaming (CFE_PSP_SimStepping_* names preserved)
- ✓ No stepping logic modified (init function contains same Core+UDS initialization)

**Impact**:
- ESA now contains complete, independent stepping implementation
- All stepping functionality validates via coverage tests
- Ready for Tasks 6-9 (callsite include path updates)
- Link conflict resolution (placeholder) is transitional — will be resolved in Task 10
- Demonstrates successful file-based extraction with zero logic modification


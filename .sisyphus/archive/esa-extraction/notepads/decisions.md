# Decisions


## Task 9 Cleanup: Scope Fidelity Pass — Reduce SCH Diff to Minimum

**Date**: 2026-03-19

**Task Scope**:
Reduce the live `apps/sch` diff to the minimum needed for accepted Task 9 scope. Task 9 adds stepping hook integration to SCH; the scope cleanup pass removes accidental churn.

**Changes Made**:

1. **Reverted Pure Whitespace Churn**:
   - `apps/sch/fsw/src/sch_app.c`: Reverted to baseline (3 extra blank lines)
   - `apps/sch/fsw/src/sch_custom.c`: Reverted to baseline (trailing space removal, blank line normalization throughout)

2. **Removed Unnecessary Include**:
   - `apps/sch/fsw/src/sch_stepping.h`: Removed `#include <stdint.h>`
   - Audit: Header file contains only stepping hook function prototypes and macro definitions
   - No stdint.h types (uint32_t, int32_t, etc.) used anywhere in file
   - Include was unnecessary bloat added during Task 9 development

3. **Retained Required Task 9 Changes**:
   - `CMakeLists.txt`: Stepping-gated build wiring preserved
     - Conditional compilation of sch_stepping.c when CFE_SIM_STEPPING enabled
     - ESA header path wiring via esa_public_api INTERFACE target
     - Proper source file filtering (excludes stepping when not enabled)
   - `sch_stepping.c`: New stepping hook implementation (kept, Task 9 artifact)
   - `sch_stepping.h`: Stepping hook declarations (kept, Task 9 artifact)

**Verification**:

Non-stepping build test:
```
make distclean && make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make -j4
```
✓ SUCCESS (0 errors, 0 warnings)

Stepping-enabled build test:
```
make distclean && CFE_SIM_STEPPING=true make SIMULATION=native ENABLE_UNIT_TESTS=true prep && CFE_SIM_STEPPING=true make -j4
```
✓ SUCCESS (0 errors, 0 warnings)

**Final Diff State**:
```
git diff --stat — apps/sch:
 CMakeLists.txt | 18 insertions(+), 3 deletions(-)
```

All deltas are build-system changes. No whitespace churn, no unnecessary includes, all stepping logic preserved.

**Key Decisions**:
- **Why revert sch_app.c and sch_custom.c**: Task 9 is about adding stepping integration, not reformatting unrelated code. Whitespace-only changes increase diff size without functional value.
- **Why remove #include <stdint.h>**: Stepping header is pure interface (function prototypes, macros). No stdint types are declared or used. Include adds friction to build (unnecessary dependency) without enabling any functionality.
- **Why keep CMakeLists.txt changes**: Stepping-gated conditional compilation is central to Task 9 scope. These changes enable stepping functionality only when CFE_SIM_STEPPING is enabled, and properly wire ESA headers.

**Scope Compliance**:
✓ Task 9 scope: stepping hook integration for SCH — achieved via CMakeLists.txt wiring and new stepping files
✓ No scheduler logic changes
✓ No timer behavior changes  
✓ Changes strictly within apps/sch/
✓ Minimum necessary diff size for accepted Task 9 scope


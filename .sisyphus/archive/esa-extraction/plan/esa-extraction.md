# ESA Module Extraction: sim_stepping → esa

## TL;DR

> **Quick Summary**: Extract the `sim_stepping` module from PSP into a standalone top-level `esa/` (ESA) directory, making it a peer of OSAL/PSP/CFE. This resolves the OSAL→PSP reverse dependency violation. API names preserved during extraction; renaming is a separate follow-up.
>
> **Deliverables**:
> - New `esa/` top-level directory with complete source, headers, and tests
> - Updated CMake build system integrating ESA as independent build target
> - All existing callsites updated to include ESA headers from new location
> - Migrated and passing coverage tests under `esa/ut-coverage/`
> - PSP `sim_stepping` module removed (or reduced to a thin redirect stub)
> - Clean build with `make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install && make test`
>
> **Estimated Effort**: Medium
> **Parallel Execution**: YES — 3 waves
> **Critical Path**: Task 1 (directory) → Task 2 (CMake) → Task 4 (callsite updates) → Task 12 (init integration) → Task 7 (integration test)

---

## Context

### Original Request
用户要求将 `sim_stepping` 从 PSP 提取为独立顶层模块 `esa/`（ESA），避免 OSAL 对 PSP 的反向依赖。当前 OSAL POSIX 层直接调用 PSP 的 sim_stepping API，违反了分层架构原则。

### Interview Summary
**Key Discussions**:
- 提取深度: 完全独立顶层模块（方案 B），不是最小改动
- API 命名: 最终目标 `ESA_*` 前缀，但本次提取保持 `CFE_PSP_SimStepping_*` 旧名
- 初始化时序: 纳入本计划 — 单次 `ESA_Init()` 调用置于 BSP main()，在 OSAL 之前
- 测试策略: TDD，利用已有 ut_assert 框架和 coveragetest 基础

**Research Findings**:
- sim_stepping 依赖完全单向: cFE/OSAL/Apps → sim_stepping，从不反向
- 核心状态机 (`_core.c`, 1081 行) 仅依赖标准 C + `common_types.h`
- 唯一 PSP 依赖: `CFE_PSP_MODULE_DECLARE_SIMPLE()` 宏（需替换）
- `#ifdef CFE_SIM_STEPPING` 散布约 30+ 文件
- 已有测试: `coveragetest-sim_stepping.c` (119 行) + UT stub (12 行)
- OSAL 的 `osal_public_api` INTERFACE target 可作为 CMake 模式参考

### Metis Review
**Identified Gaps** (addressed):
- Init 集成: 纳入本计划范围 — 单次 `ESA_Init()` 调用置于 BSP main() 中，匹配当前 sim_stepping 模式
- cFE 内联 ShimEvent_t 重复: 保留现状，后续修复
- API 重命名时机: 采纳建议，先提取后重命名
- Shim header 归属: 迁入 ESA `public_inc/`
- UT stubs 归属: ESA 拥有主 stub，OSAL 保留 noop

---

## Work Objectives

### Core Objective
将 `psp/fsw/modules/sim_stepping/` 完整提取为 `esa/` 独立顶层模块，保持所有现有功能不变，通过编译和测试验证。

### Concrete Deliverables
- `esa/` 目录: 包含 fsw/src/, fsw/inc/, public_inc/, ut-coverage/, ut-stubs/, CMakeLists.txt
- 更新的 CMake 构建规则: ESA 作为 INTERFACE + STATIC 库
- 所有调用点更新 include 路径
- 迁移后的覆盖测试通过
- 完整 cFS 编译通过 (`make SIMULATION=native prep && make && make install`)
- 单元测试通过 (`make test`)

### Definition of Done
- [ ] `make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install` 成功
- [ ] `make test` 所有测试通过，包括迁移后的 ESA coverage tests
- [ ] `psp/fsw/modules/sim_stepping/` 中无源文件（已移除或仅保留 redirect stub）
- [ ] `esa/` 包含所有 ESA 源码和头文件
- [ ] 无 OSAL→PSP 直接 include 路径（ESA headers 通过 ESA target 提供）

### Must Have
- 所有现有 sim_stepping 功能保持不变
- API 签名和行为完全相同（仅位置和 include 路径变化）
- TDD: 测试先迁移，编译通过后再逐步移动源码
- `#ifdef CFE_SIM_STEPPING` 编译守卫保留（不改变条件编译逻辑）
- 向下兼容: `sample_defs/` 中的 mission config 更新

### Must NOT Have (Guardrails)
- ❌ 不在此计划中重命名 API（`CFE_PSP_SimStepping_*` → `ESA_*` 是后续单独工作）
- ❌ 不修改 sim_stepping 的核心逻辑或状态机行为
- ❌ 不改变 `#ifdef CFE_SIM_STEPPING` 的语义或分布
- ❌ 不修改 cFE 内联的 ShimEvent_t 重复定义（后续修复）
- ❌ 不拆分 ESA 初始化为多阶段（单次 `ESA_Init()` 调用，匹配当前 sim_stepping 模式）
- ❌ 不引入新的外部依赖
- ❌ 不修改功能代码（仅移动文件、更新路径、调整构建规则）

---

## Verification Strategy (MANDATORY)

> **ZERO HUMAN INTERVENTION** — ALL verification is agent-executed. No exceptions.

### Test Decision
- **Infrastructure exists**: YES — ut_assert framework, existing coverage test
- **Automated tests**: TDD — 先迁移测试到新位置，确保测试可编译运行，再移动源码
- **Framework**: ut_assert (cFS 内置)
- **TDD flow**: 迁移 coveragetest → 确认编译失败（RED）→ 移动源码到 ESA（GREEN）→ 清理

### QA Policy
Every task MUST include agent-executed QA scenarios.
Evidence saved to `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`.

- **Build verification**: `make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install`
- **Test verification**: `make test` (CTest)
- **Include path verification**: `grep -r "psp/fsw/modules/sim_stepping" --include="*.c" --include="*.h" --include="*.cmake"` → expect 0 results
- **Functional verification**: `cd build/exe/cpu1 && ./core-cpu1` starts and sim stepping initializes

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 1 (Start Immediately — scaffolding + test migration):
├── Task 1: Create ESA directory structure [quick]
├── Task 2: ESA CMake build rules (INTERFACE + STATIC targets) [deep]
├── Task 3: Migrate coverage tests + UT stubs to ESA [quick]
└── Task 4: Create OSAL stepping noop/fallback stub [quick]

Wave 2 (After Wave 1 — source migration + callsite updates):
├── Task 5: Move sim_stepping source files to ESA [quick]
├── Task 6: Update OSAL include paths and hooks [unspecified-high]
├── Task 7: Update PSP include paths and timebase hooks [unspecified-high]
├── Task 8: Update cFE module include paths [unspecified-high]
└── Task 9: Update Apps (SCH) and mission config [quick]

Wave 3 (After Wave 2 — cleanup + init, parallel):
├── Task 10: Remove old PSP sim_stepping module [quick]
├── Task 12: Init sequence integration — ESA_Init in BSP main() [quick]
└── Task 11: Full build + test verification (after 10+12) [deep]

Wave FINAL (After ALL tasks — independent review):
├── Task F1: Plan compliance audit [oracle]
├── Task F2: Code quality review [unspecified-high]
├── Task F3: Full QA verification [unspecified-high]
└── Task F4: Scope fidelity check [deep]

Critical Path: Task 1 → Task 2 → Task 5 → Task 6/7/8 → Task 10+12 → Task 11 → F1-F4
Parallel Speedup: ~50% faster than sequential
Max Concurrent: 5 (Wave 2)
```

### Dependency Matrix

| Task | Depends On | Blocks |
|------|-----------|--------|
| 1 | — | 2, 3, 5 |
| 2 | 1 | 5, 6, 7, 8, 9, 11 |
| 3 | 1 | 11 |
| 4 | — | 6 |
| 5 | 1, 2 | 6, 7, 8, 9, 10 |
| 6 | 2, 4, 5 | 11 |
| 7 | 2, 5 | 11 |
| 8 | 2, 5 | 11 |
| 9 | 2, 5 | 11 |
| 10 | 5, 6, 7, 8, 9 | 11 |
| 11 | 1-10, 12 | F1-F4 |
| 12 | 2, 5 | 11 |

### Agent Dispatch Summary

- **Wave 1**: 4 tasks — T1 `quick`, T2 `deep`, T3 `quick`, T4 `quick`
- **Wave 2**: 5 tasks — T5 `quick`, T6 `unspecified-high`, T7 `unspecified-high`, T8 `unspecified-high`, T9 `quick`
- **Wave 3**: 3 tasks — T10 `quick`, T11 `deep`, T12 `quick`
- **FINAL**: 4 tasks — F1 `oracle`, F2 `unspecified-high`, F3 `unspecified-high`, F4 `deep`

---

## TODOs

- [x] 1. Create ESA Directory Structure

  **What to do**:
  - Create `esa/` at repository root with subdirectories:
    ```
    esa/
    ├── CMakeLists.txt          (placeholder, will be filled in Task 2)
    ├── fsw/
    │   ├── src/                (empty, source files moved in Task 5)
    │   └── inc/                (empty, private headers moved in Task 5)
    ├── public_inc/             (empty, public/shim headers moved in Task 5)
    ├── ut-coverage/            (empty, tests moved in Task 3)
    └── ut-stubs/
        └── src/                (empty, stubs moved in Task 3)
    ```
  - Create a minimal `CMakeLists.txt` placeholder with a comment: `# ESA build rules — see Task 2`

  **Must NOT do**:
  - Do NOT copy any source files yet (that's Task 5)
  - Do NOT create complex CMake logic yet (that's Task 2)

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Simple directory creation, no complex logic
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 2, 3, 4)
  - **Blocks**: Tasks 2, 3, 5
  - **Blocked By**: None (can start immediately)

  **References**:

  **Pattern References**:
  - `osal/` — Top-level directory structure pattern for an independent module
  - `psp/fsw/modules/sim_stepping/` — Source module being extracted (5 files)

  **WHY Each Reference Matters**:
  - `osal/` shows the canonical directory layout for a cFS peer module (fsw/, inc/, src/, public_inc/ pattern)
  - `psp/fsw/modules/sim_stepping/` is the source — directory must accommodate all its files

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: Directory structure exists with all required subdirectories
    Tool: Bash
    Preconditions: Repository root is clean
    Steps:
      1. ls -la esa/
      2. ls -la esa/fsw/src/
      3. ls -la esa/fsw/inc/
      4. ls -la esa/public_inc/
      5. ls -la esa/ut-coverage/
      6. ls -la esa/ut-stubs/src/
      7. cat esa/CMakeLists.txt
    Expected Result: All directories exist, CMakeLists.txt contains placeholder comment
    Failure Indicators: Any ls command returns "No such file or directory"
    Evidence: .sisyphus/evidence/task-1-directory-structure.txt
  ```

  **Commit**: YES (groups with Task 2)
  - Message: `feat(esa): create esa directory structure`
  - Files: `esa/`

- [x] 2. ESA CMake Build Rules

  **What to do**:
  - **Module Discovery (CRITICAL):** Wire ESA into cFS module discovery so CMake can find the `esa/` directory:
    - Add `set(esa_SEARCH_PATH ".")` to `cfe/cmake/mission_defaults.cmake` (after line 68, following `osal_SEARCH_PATH` and `psp_SEARCH_PATH` pattern). This tells the search loop at `mission_build.cmake:367-385` to find `./esa/`.
    - Conditionally **insert** `esa` into `MISSION_CORE_MODULES` **between "osal" and "psp"** in `cfe/cmake/mission_defaults.cmake`. Use `list(FIND)` + `list(INSERT)` to place it at the correct position:
      ```cmake
      if(CFE_SIM_STEPPING)
        list(FIND MISSION_CORE_MODULES "psp" _psp_idx)
        list(INSERT MISSION_CORE_MODULES ${_psp_idx} "esa")
      endif()
      ```
      This ensures ESA is built AFTER osal but BEFORE psp in the `arch_build.cmake:812-816` core module loop. **DO NOT use `list(APPEND)` — that would place ESA after psp, breaking build order.**
  - Create `esa/CMakeLists.txt` with:
    - INTERFACE target `esa_public_api` exporting **BOTH** public header directories:
      - `target_include_directories(esa_public_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/public_inc)`
      - `target_include_directories(esa_public_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fsw/inc)`
      This is CRITICAL: consumers like OSAL and PSP link `esa_public_api` (not `esa`) and need access to headers in BOTH directories — `public_inc/` for the shim API and `fsw/inc/` for `cfe_psp_sim_stepping.h`.
    - STATIC library target `esa` for source files (conditionally compiled with `CFE_SIM_STEPPING`)
    - `target_link_libraries(esa PUBLIC esa_public_api)` so anything linking `esa` also gets the headers
    - Link dependency on `osal_public_api` for `common_types.h`
    - Conditional: only build if `CFE_SIM_STEPPING` is defined
  - **NO manual `add_subdirectory` in `arch_build.cmake`**: The core module loop at `arch_build.cmake:812-816` already iterates over `MISSION_CORE_MODULES` and calls `add_subdirectory` for each (excluding osal which is handled explicitly at line 776). Since `esa` is in `MISSION_CORE_MODULES`, it will be built automatically by that loop. Adding a manual `add_subdirectory` would cause a CMake error (duplicate binary directory).
  - **NO separate link step in `target/CMakeLists.txt`**: Line 189-191 sets `CFE_LINK_WHOLE_LIBS` to `${MISSION_CORE_MODULES}`, and line 292-305 links all of these into the final `core-${TGTNAME}` executable. Since `esa` is in `MISSION_CORE_MODULES`, it is automatically linked. Adding it separately would cause duplicate linking.
  - Ensure ESA headers are visible to OSAL, PSP, cFE, and Apps via the `esa_public_api` INTERFACE target

  **Must NOT do**:
  - Do NOT modify the `add_psp_module` macro
  - Do NOT change OSAL's own CMakeLists.txt (OSAL consumes ESA headers, not the other way)
  - Do NOT add source file references yet (files moved in Task 5 — use glob or conditional)
  - Do NOT add a manual `add_subdirectory` for ESA in `arch_build.cmake` — the core module loop at lines 812-816 handles this automatically
  - Do NOT add a separate link step in `target/CMakeLists.txt` — `CFE_LINK_WHOLE_LIBS` at line 189-191 already includes `MISSION_CORE_MODULES`
  - Do NOT use `list(APPEND)` for MISSION_CORE_MODULES — use `list(INSERT)` before "psp" to ensure correct build order

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: CMake integration across 4 files requires understanding build system architecture
  - **Skills**: [`create-cfs-app`]
    - `create-cfs-app`: Contains CMake patterns for cFS module integration

  **Parallelization**:
  - **Can Run In Parallel**: YES (Wave 1, alongside Tasks 1, 3, 4 — but needs Task 1 directory)
  - **Parallel Group**: Wave 1
  - **Blocks**: Tasks 5, 6, 7, 8, 9, 11
  - **Blocked By**: Task 1

  **References**:

  **Pattern References**:
  - `cfe/cmake/mission_defaults.cmake:56-68` — `MISSION_MODULE_SEARCH_PATH` and `osal_SEARCH_PATH`/`psp_SEARCH_PATH` definitions (REPLICATE THIS PATTERN for `esa_SEARCH_PATH`)
  - `cfe/cmake/mission_defaults.cmake:26-39` — `MISSION_CORE_MODULES` list (INSERT `esa` before `psp` at index, NOT append)
  - `cfe/cmake/mission_build.cmake:367-385` — Module discovery loop that uses `${APP}_SEARCH_PATH` to find `${APPSRC}/${APP}` directories (this is WHY `esa_SEARCH_PATH` and directory name `esa/` must match)
  - `osal/CMakeLists.txt:176-184` — `osal_public_api` INTERFACE target pattern (COPY THIS PATTERN for `esa_public_api`)
  - `cfe/cmake/arch_build.cmake:810-817` — Core module loop that iterates `MISSION_CORE_MODULES` and calls `add_subdirectory` for each (DO NOT add a manual add_subdirectory — this loop handles it)
  - `cfe/cmake/target/CMakeLists.txt:189-191` — `CFE_LINK_WHOLE_LIBS` is set to `${MISSION_CORE_MODULES}` (DO NOT add a separate link — this handles it automatically)

  **API/Type References**:
  - `psp/fsw/modules/sim_stepping/CMakeLists.txt` — Current build rules (15 lines, shows what sources need building)

  **External References**:
  - CMake INTERFACE libraries: https://cmake.org/cmake/help/latest/command/add_library.html#interface-libraries

  **WHY Each Reference Matters**:
  - `mission_defaults.cmake:56-68`: Discovery mechanism — `esa_SEARCH_PATH "."` tells CMake to find `./esa/` at repo root, like `osal_SEARCH_PATH "."` finds `./osal/`
  - `mission_defaults.cmake:26-39`: Inserting `esa` before `psp` ensures correct build order (OSAL → ESA → PSP). Using `list(INSERT)` with `list(FIND "psp")` is the safe way to do this.
  - `mission_build.cmake:367-385`: This loop resolves every dependency by checking `${APPSRC}/${APP}` — directory name MUST match module name (`esa/` for module `esa`)
  - `osal/CMakeLists.txt:176-184`: This is the EXACT pattern to replicate — ESA needs same visibility as OSAL
  - `arch_build.cmake:810-817`: Understanding this loop is critical — it already builds all MISSION_CORE_MODULES, so a manual add_subdirectory would DUPLICATE the build and cause CMake errors
  - `target/CMakeLists.txt:189-191`: Understanding this is critical — `CFE_LINK_WHOLE_LIBS = MISSION_CORE_MODULES`, so ESA is automatically linked once it's in that list

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: CMake configuration succeeds with ESA module discovery
    Tool: Bash
    Preconditions: Task 1 directory `esa/` exists, no source files yet
    Steps:
      1. make distclean
      2. make SIMULATION=native ENABLE_UNIT_TESTS=true prep 2>&1 | tail -30
      3. Verify cmake output contains "Module 'esa' found at" (from mission_build.cmake:380)
    Expected Result: cmake prep completes without error, ESA module discovered and registered
    Failure Indicators: "Module esa NOT found" or CMake FATAL_ERROR about missing modules
    Evidence: .sisyphus/evidence/task-2-cmake-prep.txt

  Scenario: ESA INTERFACE target provides BOTH include directories
    Tool: Bash
    Preconditions: cmake prep completed
    Steps:
      1. grep -r "esa_public_api" build/ --include="*.cmake" | head -20
      2. grep -r "esa/public_inc" build/ --include="*.cmake" | head -10
      3. grep -r "esa/fsw/inc" build/ --include="*.cmake" | head -10
    Expected Result: INTERFACE include directories point to BOTH esa/public_inc/ AND esa/fsw/inc/
    Failure Indicators: No reference to esa_public_api, or only one of the two include dirs present
    Evidence: .sisyphus/evidence/task-2-interface-target.txt
  ```

  **Commit**: YES (groups with Task 1)
  - Message: `feat(esa): add CMake build rules for esa`
  - Files: `esa/CMakeLists.txt`, `cfe/cmake/mission_defaults.cmake`
  - Pre-commit: `make SIMULATION=native ENABLE_UNIT_TESTS=true prep`

- [x] 3. Migrate Coverage Tests and UT Stubs to ESA

  **What to do**:
  - Copy `psp/unit-test-coverage/modules/sim_stepping/coveragetest-sim_stepping.c` (119 lines) to `esa/ut-coverage/coveragetest-sim_stepping.c`
  - Copy `psp/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` (12 lines) to `esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c`
  - **Wire ESA tests into build (CRITICAL):** Create `esa/ut-coverage/CMakeLists.txt` with its own test target registration. Since `add_psp_covtest()` is PSP-specific, define a standalone test target:
    - `add_library(coverage-esa-sim_stepping-object OBJECT ...)` for source under test (with coverage flags)
    - `add_executable(coverage-esa-sim_stepping-testrunner ...)` linking test + objects + ut_assert + ut stubs
    - `add_test(coverage-esa-sim_stepping coverage-esa-sim_stepping-testrunner)`
    - Pattern: `psp/unit-test-coverage/modules/CMakeLists.txt:12-64` (`add_psp_covtest()` function body — replicate the logic, don't call the function)
  - **Hook ESA tests into build tree:** In `esa/CMakeLists.txt` (created in Task 2), add conditional inclusion:
    ```cmake
    if(ENABLE_UNIT_TESTS)
      add_subdirectory(ut-coverage)
    endif()
    ```
  - Update include paths in test files to reference ESA headers (once source moves in Task 5)
  - At this stage tests should compile but may not link (source not moved yet) — this is expected TDD RED state

  **Must NOT do**:
  - Do NOT modify test logic or assertions
  - Do NOT delete original test files yet (Task 10 cleanup)
  - Do NOT rename any API calls in tests

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Copying files and creating simple CMake test registration
  - **Skills**: [`create-cfs-unit-test`]
    - `create-cfs-unit-test`: Contains ut_assert test CMake patterns

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 2, 4)
  - **Blocks**: Task 11
  - **Blocked By**: Task 1

  **References**:

  **Pattern References**:
  - `psp/unit-test-coverage/modules/sim_stepping/coveragetest-sim_stepping.c` — Source test file (119 lines, copy to ESA)
  - `psp/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` — Source stub file (12 lines, copy to ESA)
  - `psp/unit-test-coverage/modules/sim_stepping/CMakeLists.txt` — Current test CMake (13 lines, uses `add_psp_covtest()`)
  - `psp/unit-test-coverage/modules/CMakeLists.txt:12-64` — `add_psp_covtest()` function definition (replicate the logic inside, but adapted for ESA include paths instead of PSP paths)
  - `psp/unit-test-coverage/modules/CMakeLists.txt:87-89` — Current conditional `add_subdirectory(sim_stepping)` (this is what Task 10 must remove)

  **Test References**:
  - `osal/src/unit-test-coverage/` — OSAL's test directory layout (alternative pattern reference)

  **WHY Each Reference Matters**:
  - The coverage test file IS the asset being migrated — exact content preservation required
  - The CMakeLists pattern shows how test targets link against coverage-instrumented libraries

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: Test files exist in ESA directory
    Tool: Bash
    Preconditions: Task 1 directory exists
    Steps:
      1. ls -la esa/ut-coverage/coveragetest-sim_stepping.c
      2. ls -la esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c
      3. diff psp/unit-test-coverage/modules/sim_stepping/coveragetest-sim_stepping.c esa/ut-coverage/coveragetest-sim_stepping.c
      4. diff psp/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c
    Expected Result: Files exist, diff shows only include path changes (if any), no logic changes
    Failure Indicators: Files missing, unexpected content differences
    Evidence: .sisyphus/evidence/task-3-test-migration.txt
  ```

  **Commit**: YES
  - Message: `test(esa): migrate sim_stepping coverage tests to ESA`
  - Files: `esa/ut-coverage/`, `esa/ut-stubs/`

- [x] 4. Create OSAL Stepping Noop/Fallback Stub

  **What to do**:
  - Review `osal/src/os/posix/src/os-posix-stepping.c` (154 lines) — this file calls PSP sim_stepping APIs
  - Create a noop fallback mechanism so OSAL can compile without ESA (when `CFE_SIM_STEPPING` is not defined, this already works via `#ifdef`)
  - Ensure that when `CFE_SIM_STEPPING` IS defined, OSAL's `os-posix-stepping.c` can find ESA headers via the `esa_public_api` INTERFACE target (not via PSP include path)
  - If needed, create a thin wrapper header in ESA `public_inc/` that OSAL includes
  - Verify the existing `#ifdef CFE_SIM_STEPPING` guards are sufficient for clean compilation in both modes

  **Must NOT do**:
  - Do NOT modify OSAL's stepping hook logic
  - Do NOT change any function signatures
  - Do NOT add new OSAL dependencies

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Analysis and possibly creating one header file
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 2, 3)
  - **Blocks**: Task 6
  - **Blocked By**: None (can start immediately, but practically needs Task 2 for include paths)

  **References**:

  **Pattern References**:
  - `osal/src/os/posix/src/os-posix-stepping.c` — OSAL's stepping hook implementation (154 lines). Calls `CFE_PSP_SimStepping_Shim_ReportEvent`, `CFE_PSP_SimStepping_Hook_TaskDelayEligible`, `CFE_PSP_SimStepping_WaitForDelayExpiry`
  - `sample_defs/fsw/inc/cfe_psp_sim_stepping_shim.h` — Current shim header location (will move to ESA public_inc/)

  **API/Type References**:
  - `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping.h:` — Public API header (277 lines) that OSAL currently includes via PSP

  **WHY Each Reference Matters**:
  - `os-posix-stepping.c` is the main consumer — must understand exactly which ESA APIs it needs
  - The shim header defines the ABI contract between OSAL and ESA

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: OSAL compiles without CFE_SIM_STEPPING defined
    Tool: Bash
    Preconditions: Task 2 CMake rules in place
    Steps:
      1. make distclean
      2. make SIMULATION=native prep  (without CFE_SIM_STEPPING)
      3. make 2>&1 | grep -i "error" | head -20
    Expected Result: Build succeeds, no stepping-related errors
    Failure Indicators: Compilation errors referencing sim_stepping or ESA headers
    Evidence: .sisyphus/evidence/task-4-noop-build.txt

  Scenario: Verify existing #ifdef guards are sufficient
    Tool: Bash
    Preconditions: None
    Steps:
      1. grep -n "ifdef CFE_SIM_STEPPING\|ifndef CFE_SIM_STEPPING" osal/src/os/posix/src/os-posix-stepping.c
      2. Verify every ESA API call is inside an #ifdef block
    Expected Result: All stepping API calls are guarded by #ifdef CFE_SIM_STEPPING
    Failure Indicators: Unguarded calls to CFE_PSP_SimStepping_* functions
    Evidence: .sisyphus/evidence/task-4-ifdef-audit.txt
  ```

  **Commit**: YES (groups with Task 3 if minimal changes)
  - Message: `refactor(esa): ensure OSAL stepping hooks work with ESA include paths`
  - Files: `osal/src/os/posix/src/os-posix-stepping.c` (if modified), ESA headers

- [x] 5. Move sim_stepping Source Files to ESA

  **What to do**:
  - Move (not copy) source files from `psp/fsw/modules/sim_stepping/` to `esa/`:
    - `cfe_psp_sim_stepping.c` (956 lines) → `esa/fsw/src/`
    - `cfe_psp_sim_stepping_core.c` (1081 lines) → `esa/fsw/src/`
    - `cfe_psp_sim_stepping.h` (277 lines) → `esa/fsw/inc/`
    - `cfe_psp_sim_stepping_core.h` (630 lines) → `esa/fsw/inc/`
  - Move shim header from mission config to ESA:
    - `sample_defs/fsw/inc/cfe_psp_sim_stepping_shim.h` → `esa/public_inc/`
  - Update `esa/CMakeLists.txt` to reference actual source files (created in Task 2)
  - Replace `CFE_PSP_MODULE_DECLARE_SIMPLE(sim_stepping)` macro usage:
    - This macro registers the module with PSP's init framework
    - Replace with a standalone init function: `void ESA_Init(void)` that performs Core init + Transport init in a single call
    - The init function body should contain the same logic as the current module init entry point (`sim_stepping_Init`)
  - Verify files compile in new location with ESA CMake targets

  **Must NOT do**:
  - Do NOT rename any API functions (that's a follow-up task)
  - Do NOT modify function bodies or logic
  - Do NOT delete the old PSP module directory yet (Task 10)

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: File moves and minimal CMake source list update
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 2 (first task, blocks others)
  - **Blocks**: Tasks 6, 7, 8, 9, 10
  - **Blocked By**: Tasks 1, 2

  **References**:

  **Pattern References**:
  - `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping.c:1-20` — PSP module declaration macro usage at top of file (the `CFE_PSP_MODULE_DECLARE_SIMPLE` call to replace)
  - `psp/fsw/modules/sim_stepping/CMakeLists.txt` — Current source file list (all 2 .c files)
  - `esa/CMakeLists.txt` — Target CMake file to update (from Task 2)

  **API/Type References**:
  - `cfe/cmake/arch_build.cmake:71-81` — `add_psp_module()` macro definition (understand what `CFE_PSP_MODULE_DECLARE_SIMPLE` provides so the replacement init function is equivalent)
  - `psp/fsw/inc/cfe_psp_module.h` — PSP module API header (defines the macro being replaced)

  **WHY Each Reference Matters**:
  - The PSP module macro is the ONLY PSP-specific code that needs changing — understanding it precisely prevents breaking init
  - The CMake source list must match the files actually present in new location

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: Source files exist in ESA and compile
    Tool: Bash
    Preconditions: Tasks 1-2 complete
    Steps:
      1. ls -la esa/fsw/src/
      2. ls -la esa/fsw/inc/
      3. ls -la esa/public_inc/
      4. wc -l esa/fsw/src/cfe_psp_sim_stepping.c  → expect ~956
      5. wc -l esa/fsw/src/cfe_psp_sim_stepping_core.c  → expect ~1081
      6. make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make 2>&1 | tail -30
    Expected Result: All files present with correct line counts, build may fail on include paths (fixed by Tasks 6-9)
    Failure Indicators: Files missing, wrong line counts, unexpected content changes
    Evidence: .sisyphus/evidence/task-5-source-move.txt

  Scenario: PSP module macro replaced with standalone init
    Tool: Bash
    Preconditions: Source files moved
    Steps:
      1. grep "CFE_PSP_MODULE_DECLARE_SIMPLE" esa/fsw/src/cfe_psp_sim_stepping.c
      2. grep "ESA_Init\|void.*Init" esa/fsw/src/cfe_psp_sim_stepping.c | head -5
    Expected Result: No PSP module macro found, standalone init function present
    Failure Indicators: PSP module macro still present, no init function
    Evidence: .sisyphus/evidence/task-5-macro-replace.txt
  ```

  **Commit**: YES
  - Message: `refactor(esa): move sim_stepping source files to esa`
  - Files: `esa/fsw/`, `esa/public_inc/`

- [x] 6. Update OSAL Include Paths and Hooks

  **What to do**:
  - Update `osal/src/os/posix/src/os-posix-stepping.c`:
    - Change `#include` directives from PSP paths to ESA public headers
    - The file currently includes `cfe_psp_sim_stepping_shim.h` and/or `cfe_psp_sim_stepping.h`
    - After Task 5, these headers are in `esa/public_inc/` and `fsw/inc/`
    - ESA's INTERFACE target (from Task 2) should make these findable
  - Update `osal/src/os/posix/src/os-impl-queues.c`:
    - Verify `#include` for stepping hooks resolves to ESA headers via INTERFACE target
  - Update `osal/src/os/posix/src/os-impl-binsem.c`:
    - Same include path verification
  - Update `osal/src/os/posix/src/os-impl-tasks.c`:
    - Same include path verification
  - Update OSAL CMakeLists if needed to link against `esa_public_api` INTERFACE target
  - All `#ifdef CFE_SIM_STEPPING` guards must remain unchanged

  **Must NOT do**:
  - Do NOT modify any stepping hook logic or function bodies
  - Do NOT change function signatures
  - Do NOT remove `#ifdef CFE_SIM_STEPPING` guards

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: Multiple files across OSAL, careful include path management
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 7, 8, 9)
  - **Blocks**: Task 11
  - **Blocked By**: Tasks 2, 4, 5

  **References**:

  **Pattern References**:
  - `osal/src/os/posix/src/os-posix-stepping.c:1-30` — Current include directives (find and update PSP paths)
  - `osal/src/os/posix/src/os-impl-queues.c:199-202, 250-253` — `#ifdef CFE_SIM_STEPPING` blocks calling stepping hooks
  - `osal/src/os/posix/src/os-impl-binsem.c:376, 400-401, 444-445` — `#ifdef CFE_SIM_STEPPING` blocks
  - `osal/src/os/posix/src/os-impl-tasks.c:728-766` — `#ifdef CFE_SIM_STEPPING` block with TaskDelay hook

  **API/Type References**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — New shim header location
  - `esa/fsw/inc/cfe_psp_sim_stepping.h` — New API header location

  **WHY Each Reference Matters**:
  - Each OSAL file has specific `#include` lines that currently resolve via PSP include path — must be updated to use ESA
  - The `#ifdef` blocks tell you exactly which functions are called and need header access

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: OSAL compiles with ESA headers (stepping enabled)
    Tool: Bash
    Preconditions: Tasks 2, 4, 5 complete
    Steps:
      1. make SIMULATION=native ENABLE_UNIT_TESTS=true prep
      2. make 2>&1 | grep -E "os-posix-stepping|os-impl-queues|os-impl-binsem|os-impl-tasks" | grep -i error
    Expected Result: No compilation errors in OSAL stepping files
    Failure Indicators: "file not found" errors for sim_stepping headers
    Evidence: .sisyphus/evidence/task-6-osal-compile.txt

  Scenario: No PSP include paths remain in OSAL stepping files
    Tool: Bash
    Preconditions: Include paths updated
    Steps:
      1. grep -rn "psp.*sim_stepping\|modules/sim_stepping" osal/src/os/posix/src/ --include="*.c" --include="*.h"
    Expected Result: 0 results — no references to old PSP sim_stepping path
    Failure Indicators: Any grep match
    Evidence: .sisyphus/evidence/task-6-no-psp-refs.txt
  ```

  **Commit**: YES (groups with Tasks 7, 8, 9)
  - Message: `refactor(esa): update OSAL include paths for ESA headers`
  - Files: OSAL posix stepping files, OSAL CMakeLists (if changed)

- [x] 7. Update PSP Include Paths and Timebase Hooks

  **What to do**:
  - Update `psp/fsw/modules/timebase_posix_clock/cfe_psp_timebase_posix_clock.c`:
    - Lines 88, 124 call `CFE_PSP_SimStepping_Hook_GetTime` — update include to ESA header
  - Update `psp/fsw/modules/soft_timebase/cfe_psp_soft_timebase.c` (if it has `#ifdef CFE_SIM_STEPPING`):
    - Same include path update
  - Update `psp/fsw/modules/sim_stepping/CMakeLists.txt`:
    - Either remove the module entirely (if source moved) or convert to a thin shim
    - If PSP still needs a module entry, create a stub `sim_stepping` module that calls ESA init
  - Update `psp/CMakeLists.txt` and target-specific conditional module files:
    - Remove `sim_stepping` from `psp/fsw/pc-linux/psp_conditional_modules.cmake` (line 8: `list(APPEND PSP_TARGET_MODULE_LIST sim_stepping)`)
    - Also check `psp/fsw/pc-rtems/psp_conditional_modules.cmake` and `psp/fsw/generic-qnx/psp_conditional_modules.cmake` for sim_stepping entries
  - Ensure PSP can find ESA headers via `esa_public_api` INTERFACE target

  **Must NOT do**:
  - Do NOT modify timebase logic
  - Do NOT change `CFE_PSP_GetTime` behavior
  - Do NOT alter the stepping hook call patterns

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: Multiple PSP files + CMake changes, need careful build system understanding
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 6, 8, 9)
  - **Blocks**: Task 11
  - **Blocked By**: Tasks 2, 5

  **References**:

  **Pattern References**:
  - `psp/fsw/modules/timebase_posix_clock/cfe_psp_timebase_posix_clock.c:88, 124` — `CFE_PSP_SimStepping_Hook_GetTime` calls
  - `psp/fsw/modules/sim_stepping/CMakeLists.txt` — Current PSP module CMake (15 lines, to be removed/stubbed)
  - `psp/CMakeLists.txt:66-67` — PSP module list loading
  - `psp/fsw/pc-linux/psp_conditional_modules.cmake:5-8` — Where sim_stepping is conditionally added (`list(APPEND PSP_TARGET_MODULE_LIST sim_stepping)`)
  - Also check: `psp/fsw/pc-rtems/psp_conditional_modules.cmake`, `psp/fsw/generic-qnx/psp_conditional_modules.cmake`

  **WHY Each Reference Matters**:
  - timebase file is the only PSP file that directly calls sim_stepping API — must update its includes
  - PSP conditional module files (`psp/fsw/pc-linux/psp_conditional_modules.cmake`) must stop adding sim_stepping to the PSP build

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: PSP compiles without sim_stepping module
    Tool: Bash
    Preconditions: Tasks 2, 5 complete
    Steps:
      1. make SIMULATION=native ENABLE_UNIT_TESTS=true prep
      2. make 2>&1 | grep -E "timebase_posix_clock|psp" | grep -i error | head -20
    Expected Result: No PSP compilation errors
    Failure Indicators: Missing header errors for sim_stepping
    Evidence: .sisyphus/evidence/task-7-psp-compile.txt

  Scenario: sim_stepping removed from PSP module list
    Tool: Bash
    Preconditions: PSP CMake updated
    Steps:
      1. grep -rn "sim_stepping" psp/fsw/pc-linux/psp_conditional_modules.cmake psp/fsw/pc-rtems/psp_conditional_modules.cmake psp/fsw/generic-qnx/psp_conditional_modules.cmake psp/CMakeLists.txt
    Expected Result: sim_stepping no longer in active module list (may appear in comments)
    Failure Indicators: sim_stepping still listed as active PSP module via `list(APPEND PSP_TARGET_MODULE_LIST sim_stepping)`
    Evidence: .sisyphus/evidence/task-7-psp-module-list.txt
  ```

  **Commit**: YES (groups with Tasks 6, 8, 9)
  - Message: `refactor(esa): update PSP to use ESA headers, remove sim_stepping PSP module`
  - Files: PSP timebase, PSP CMake files

- [x] 8. Update cFE Module Include Paths

  **What to do**:
  - Update all cFE modules that reference sim_stepping shim headers:
    - `cfe/modules/es/fsw/src/` — ES source files with `#ifdef CFE_SIM_STEPPING`
    - `cfe/modules/evs/fsw/src/` — EVS source files
    - `cfe/modules/sb/fsw/src/` — SB source files
    - `cfe/modules/tbl/fsw/src/` — TBL source files
    - `cfe/modules/time/fsw/src/` — TIME source files
  - Each module has `#ifdef CFE_SIM_STEPPING` blocks that include shim header and call `CFE_PSP_SimStepping_Shim_ReportEvent`
  - Update `#include` paths to resolve via ESA's INTERFACE target instead of PSP
  - May need to update cFE's `CMakeLists.txt` to link against `esa_public_api`
  - Verify the `CFE_PSP_SimStepping_ShimEvent_t` type and `CFE_PSP_SimStepping_EventKind_t` enum are accessible
  - Note: Some cFE modules have INLINE copies of ShimEvent_t — these are preserved as-is (Metis finding, deferred fix)

  **Must NOT do**:
  - Do NOT modify shim event reporting logic
  - Do NOT fix inline ShimEvent_t duplicates (deferred)
  - Do NOT change any `#ifdef CFE_SIM_STEPPING` semantics

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 5 cFE modules to update, need to identify and update each include reference
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 6, 7, 9)
  - **Blocks**: Task 11
  - **Blocked By**: Tasks 2, 5

  **References**:

  **Pattern References**:
  - `cfe/modules/es/fsw/src/cfe_es_task.c` — Example cFE module with `#ifdef CFE_SIM_STEPPING` (find the include line)
  - `cfe/modules/sb/fsw/src/cfe_sb_priv.c` — SB module stepping integration
  - `cfe/modules/time/fsw/src/cfe_time_tone.c` — TIME module stepping hooks

  **API/Type References**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — New header location for ShimEvent_t and EventKind_t

  **WHY Each Reference Matters**:
  - Each cFE module has its own `#include` line for the shim — all 5 must be found and updated
  - The shim header is in ESA public_inc/ now, so include must resolve via INTERFACE target

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: All cFE modules compile with ESA headers
    Tool: Bash
    Preconditions: Tasks 2, 5 complete
    Steps:
      1. make SIMULATION=native ENABLE_UNIT_TESTS=true prep
      2. make 2>&1 | grep -E "cfe_es|cfe_evs|cfe_sb|cfe_tbl|cfe_time" | grep -i error | head -30
    Expected Result: No cFE compilation errors related to stepping headers
    Failure Indicators: "file not found" for sim_stepping shim header
    Evidence: .sisyphus/evidence/task-8-cfe-compile.txt

  Scenario: No PSP sim_stepping references in cFE
    Tool: Bash
    Preconditions: Include paths updated
    Steps:
      1. grep -rn "psp.*sim_stepping\|modules/sim_stepping" cfe/ --include="*.c" --include="*.h"
    Expected Result: 0 results
    Failure Indicators: Any remaining PSP path references
    Evidence: .sisyphus/evidence/task-8-no-psp-refs.txt
  ```

  **Commit**: YES (groups with Tasks 6, 7, 9)
  - Message: `refactor(esa): update cFE module include paths for ESA headers`
  - Files: cFE ES, EVS, SB, TBL, TIME source files

- [x] 9. Update Apps (SCH) and Mission Config

  **What to do**:
  - Update SCH app stepping integration:
    - `apps/sch/fsw/src/sch_stepping.c` — Update include paths for ESA headers
    - `apps/sch/fsw/src/sch_stepping.h` — Same
    - `apps/sch/fsw/src/sch_custom.c` — Same (if it includes stepping headers)
    - `apps/sch/fsw/src/sch_app.c` — Same
  - Update `sample_defs/` mission configuration:
    - Note: `sample_defs/targets.cmake` has no `PSP_MODULELIST` entry for `sim_stepping` — the module is added via `psp/fsw/pc-linux/psp_conditional_modules.cmake` (handled in Task 7)
    - Verify `sample_defs/cpu1_cfe_es_startup.scr` doesn't reference sim_stepping module
  - Update `sample_defs/fsw/inc/` — Remove old `cfe_psp_sim_stepping_shim.h` (moved to ESA in Task 5)
  - Verify any other apps with `#ifdef CFE_SIM_STEPPING` blocks are updated

  **Must NOT do**:
  - Do NOT modify SCH stepping logic
  - Do NOT change startup script scheduling
  - Do NOT modify targets.cmake (sim_stepping module list is in PSP conditional modules, handled by Task 7)

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Small number of files, straightforward include path changes
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 6, 7, 8)
  - **Blocks**: Task 11
  - **Blocked By**: Tasks 2, 5

  **References**:

  **Pattern References**:
  - `apps/sch/fsw/src/sch_stepping.c` — SCH stepping integration (main consumer in apps layer)
  - `apps/sch/fsw/src/sch_stepping.h` — SCH stepping header
  - `sample_defs/cpu1_cfe_es_startup.scr` — Startup script (verify no sim_stepping reference)

  **WHY Each Reference Matters**:
  - SCH is the only app with dedicated stepping integration — its includes must resolve via ESA
  - Startup script must not reference sim_stepping as a PSP module (it's now ESA)

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: SCH app compiles with ESA headers
    Tool: Bash
    Preconditions: Tasks 2, 5 complete
    Steps:
      1. make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make 2>&1 | grep -E "sch" | grep -i error
    Expected Result: No SCH compilation errors related to stepping
    Failure Indicators: Missing header errors in sch_stepping.c
    Evidence: .sisyphus/evidence/task-9-sch-compile.txt

  Scenario: No old PSP sim_stepping references in SCH app
    Tool: Bash
    Preconditions: SCH includes updated
    Steps:
      1. grep -rn "psp.*sim_stepping\|modules/sim_stepping" apps/sch/ --include="*.c" --include="*.h"
    Expected Result: 0 results — no references to old PSP path
    Failure Indicators: Any grep match
    Evidence: .sisyphus/evidence/task-9-sch-no-psp-refs.txt
  ```

  **Commit**: YES (groups with Tasks 6, 7, 8)
  - Message: `refactor(esa): update SCH app and mission config for ESA`
  - Files: SCH stepping files (`apps/sch/fsw/src/sch_stepping.*`), sample_defs/

- [x] 10. Remove Old PSP sim_stepping Module

  **What to do**:
  - Remove `psp/fsw/modules/sim_stepping/` directory entirely (source files already moved in Task 5)
  - Remove `psp/unit-test-coverage/modules/sim_stepping/` directory (tests already migrated in Task 3)
  - Remove `psp/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` (migrated in Task 3)
  - **Update PSP test CMake (CRITICAL):** Remove or comment out the conditional `add_subdirectory(sim_stepping)` block in `psp/unit-test-coverage/modules/CMakeLists.txt:87-89`. Without this, `make test` will fail trying to enter the deleted `sim_stepping/` subdirectory.
  - **Update PSP ut-stubs CMake (CRITICAL):** Remove the line referencing `src/cfe_psp_sim_stepping_shim_stubs.c` from `psp/ut-stubs/CMakeLists.txt:24`. This file is being deleted (migrated to ESA in Task 3), but the CMakeLists still lists it as a source for `ut_psp_api_stubs`. Without this removal, `ENABLE_UNIT_TESTS=true` builds will fail with a missing source file error.
  - Verify no dangling CMake references to old sim_stepping directory
  - Verify no remaining includes pointing to old location

  **Must NOT do**:
  - Do NOT remove any files that haven't been migrated to ESA first
  - Do NOT modify any files outside the PSP sim_stepping directories **except** the two CMakeLists updates above (`psp/unit-test-coverage/modules/CMakeLists.txt` and `psp/ut-stubs/CMakeLists.txt`)

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Simple file/directory removal
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 3 (sequential after Wave 2)
  - **Blocks**: Task 11
  - **Blocked By**: Tasks 5, 6, 7, 8, 9

  **References**:

  **Pattern References**:
  - `psp/fsw/modules/sim_stepping/` — Directory to remove (should be empty after Task 5 moved all files)
  - `psp/unit-test-coverage/modules/sim_stepping/` — Test directory to remove
  - `psp/unit-test-coverage/modules/CMakeLists.txt:87-89` — `if(CFE_SIM_STEPPING) add_subdirectory(sim_stepping) endif()` block to remove
  - `psp/ut-stubs/CMakeLists.txt:24` — Line `src/cfe_psp_sim_stepping_shim_stubs.c` to remove from `ut_psp_api_stubs` target source list

  **WHY Each Reference Matters**:
  - Must verify these are EMPTY (or contain only CMakeLists that's been deactivated) before deletion
  - `psp/unit-test-coverage/modules/CMakeLists.txt:87-89`: If not removed, `make test` will FATAL_ERROR trying to enter the deleted directory
  - `psp/ut-stubs/CMakeLists.txt:24`: If not removed, `ENABLE_UNIT_TESTS=true` builds will FAIL because CMake cannot find the deleted source file in the `ut_psp_api_stubs` target

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: Old PSP sim_stepping directory removed
    Tool: Bash
    Preconditions: All Wave 2 tasks complete
    Steps:
      1. ls psp/fsw/modules/sim_stepping/ 2>&1
      2. ls psp/unit-test-coverage/modules/sim_stepping/ 2>&1
      3. ls psp/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c 2>&1
    Expected Result: "No such file or directory" for all three
    Failure Indicators: Any of the old files/dirs still exist
    Evidence: .sisyphus/evidence/task-10-cleanup.txt

  Scenario: No dangling references to old PSP sim_stepping
    Tool: Bash
    Preconditions: Old module removed
    Steps:
      1. grep -rn "psp/fsw/modules/sim_stepping\|psp/unit-test-coverage/modules/sim_stepping" . --include="*.c" --include="*.h" --include="*.cmake" --include="*.txt" | grep -v ".git/"
      2. grep -n "sim_stepping" psp/unit-test-coverage/modules/CMakeLists.txt
      3. grep -n "sim_stepping" psp/ut-stubs/CMakeLists.txt
    Expected Result: 0 results from step 1, 0 results from step 2 (add_subdirectory entry removed), 0 results from step 3 (stubs source reference removed)
    Failure Indicators: Active code references to deleted paths, CMakeLists still references sim_stepping files or directories
    Evidence: .sisyphus/evidence/task-10-no-dangling.txt
  ```

  **Commit**: YES
  - Message: `chore(esa): remove old PSP sim_stepping module (migrated to ESA)`
  - Files: psp/fsw/modules/sim_stepping/ (deleted), psp/unit-test-coverage/modules/sim_stepping/ (deleted), psp/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c (deleted), psp/unit-test-coverage/modules/CMakeLists.txt (edited), psp/ut-stubs/CMakeLists.txt (edited)

- [x] 11. Full Build + Test Verification

  **What to do**:
  - Perform clean build from scratch:
    ```bash
    make distclean
    make SIMULATION=native ENABLE_UNIT_TESTS=true prep
    make
    make install
    make test
    ```
  - Verify ALL tests pass, including:
    - Migrated ESA coverage tests
    - Existing OSAL tests
    - Existing PSP tests
    - Existing cFE tests
  - Verify functional startup:
    ```bash
    cd build/exe/cpu1 && timeout 10 ./core-cpu1 2>&1 | head -50
    ```
  - Fix any remaining compilation errors or test failures
  - Run final grep audit for old path references:
    ```bash
    grep -rn "psp/fsw/modules/sim_stepping" . --include="*.c" --include="*.h" --include="*.cmake" | grep -v ".git/"
    ```

  **Must NOT do**:
  - Do NOT skip any test suites
  - Do NOT suppress warnings or errors
  - Do NOT modify functional code to make tests pass (only fix path/include issues)

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: Full integration verification, debugging any remaining issues
  - **Skills**: []
    - No specialized skills needed

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 3 (sequential, after Task 10 and Task 12 both complete)
  - **Blocks**: F1, F2, F3, F4
  - **Blocked By**: All previous tasks (1-10, 12)

  **References**:

  **Pattern References**:
  - `Makefile` — Top-level make targets
  - `.github/workflows/build-cfs.yml` — CI build commands (reference for expected build flow)

  **WHY Each Reference Matters**:
  - Makefile defines the exact build/test commands
  - CI workflow shows what a passing build looks like

  **Acceptance Criteria**:

  **QA Scenarios (MANDATORY):**

  ```
  Scenario: Clean build succeeds
    Tool: Bash
    Preconditions: All Tasks 1-10 and 12 complete
    Steps:
      1. make distclean
      2. make SIMULATION=native ENABLE_UNIT_TESTS=true prep
      3. make 2>&1 | tail -10
      4. make install 2>&1 | tail -10
    Expected Result: Build completes with 0 errors, 0 warnings related to ESA/stepping
    Failure Indicators: Any error or stepping-related warning
    Evidence: .sisyphus/evidence/task-11-build.txt

  Scenario: All tests pass
    Tool: Bash
    Preconditions: Build and install complete
    Steps:
      1. make test 2>&1
      2. Count passed/failed tests
    Expected Result: 100% tests passed, 0 failures
    Failure Indicators: Any test failure, especially ESA coverage tests
    Evidence: .sisyphus/evidence/task-11-tests.txt

  Scenario: Functional startup verification
    Tool: Bash
    Preconditions: Install complete
    Steps:
      1. cd build/exe/cpu1 && timeout 10 ./core-cpu1 2>&1 | head -80
      2. Check output for sim_stepping/ESA initialization messages
    Expected Result: core-cpu1 starts, stepping initializes (look for stepping-related log output)
    Failure Indicators: Crash, segfault, missing init messages
    Evidence: .sisyphus/evidence/task-11-startup.txt

  Scenario: No old PSP path references remain
    Tool: Bash
    Preconditions: All files updated
    Steps:
      1. grep -rn "psp/fsw/modules/sim_stepping" . --include="*.c" --include="*.h" --include="*.cmake" | grep -v ".git/" | grep -v ".sisyphus/"
    Expected Result: 0 results
    Failure Indicators: Any active code reference to old path
    Evidence: .sisyphus/evidence/task-11-path-audit.txt
  ```

  **Commit**: YES (final commit if any fixes were needed)
  - Message: `fix(esa): resolve remaining build/test issues from ESA extraction`
  - Files: Any files that needed fixes
  - Pre-commit: `make && make install && make test`

- [x] 12. Init Sequence Integration — Add ESA_Init to OSAL BSP main()

  **What to do**:
  - Add `ESA_Init()` call to `osal/src/bsp/generic-linux/src/bsp_start.c` in `main()`, between `OS_BSP_Initialize()` and `OS_Application_Startup()`
  - Wrap in `#ifdef CFE_SIM_STEPPING` / `#endif` compile guard
  - Only modify `generic-linux` BSP — other platforms (RTEMS, VxWorks, QNX) don't support simulation stepping
  - ESA_Init performs both Core init (state machine) and Transport init (UDS socket + pthread) in single call, matching current sim_stepping behavior
  - Update ESA CMakeLists to export an `esa_public_api` INTERFACE target so BSP can include the ESA header
  - Update `osal/src/bsp/generic-linux/CMakeLists.txt` to conditionally link ESA when `CFE_SIM_STEPPING` is defined

  **Must NOT do**:
  - Do NOT modify any other BSP platform (generic-qnx, generic-rtems, generic-vxworks, pc-rtems, generic-vxworks-rtp)
  - Do NOT split into two-phase init — single `ESA_Init()` call matches current sim_stepping pattern
  - Do NOT add ESA init inside `OS_Application_Startup()` (PSP territory) — it belongs in BSP `main()`
  - Do NOT add any cFE API calls in ESA_Init — it runs before OSAL and cFE

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Small, well-defined change — add 3 lines to bsp_start.c + CMake linkage
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3 (parallel with Task 10, both after Wave 2)
  - **Blocks**: Task 11 (full build verification)
  - **Blocked By**: Task 2 (ESA CMake targets must exist), Task 5 (ESA source files must be in place)

  **References**:

  **Pattern References**:
  - `osal/src/bsp/generic-linux/src/bsp_start.c:196-248` — `main()` function, insert point between L231 (`OS_BSP_Initialize()`) and L237 (`OS_Application_Startup()`)
  - `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping.c:103-142` — Current `sim_stepping_Init()` showing Core+Transport init sequence

  **API/Type References**:
  - `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping.h` — Public API header to be moved to ESA (contains `ESA_Init` equivalent)

  **Build References**:
  - `osal/src/bsp/generic-linux/CMakeLists.txt` — BSP build rules, add conditional ESA link
  - `cfe/cmake/mission_build.cmake` — Where `CFE_SIM_STEPPING` define is propagated

  **WHY Each Reference Matters**:
  - `bsp_start.c:main()` is the exact insertion point — between BSP init and PSP/OSAL/cFE startup
  - `sim_stepping_Init()` shows what ESA_Init must do (Core_Init + UDS_Init + pthread_create)
  - BSP CMakeLists needed to add `target_link_libraries` for ESA

  **Acceptance Criteria**:
  - [ ] `osal/src/bsp/generic-linux/src/bsp_start.c` contains `ESA_Init()` call between `OS_BSP_Initialize()` and `OS_Application_Startup()`, guarded by `#ifdef CFE_SIM_STEPPING`
  - [ ] No other BSP platform files modified
  - [ ] ESA initializes before OS_API_Init (verified by log message ordering)
  - [ ] Startup log shows ESA init messages before OSAL init messages

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: ESA init order verification
    Tool: Bash
    Preconditions: Full build with CFE_SIM_STEPPING=ON
    Steps:
      1. cd build/exe/cpu1 && timeout 10 ./core-cpu1 2>&1 | head -50
      2. Check that ESA init message appears BEFORE "OS_API_Init" or "CFE_PSP:" messages
    Expected Result: ESA init log line precedes OSAL/PSP init log lines
    Failure Indicators: ESA init message appears after PSP messages, or is missing entirely
    Evidence: .sisyphus/evidence/task-12-init-order.txt

  Scenario: Build without CFE_SIM_STEPPING
    Tool: Bash
    Preconditions: None
    Steps:
      1. make distclean
      2. make SIMULATION=native prep   (without CFE_SIM_STEPPING)
      3. make
    Expected Result: Builds successfully — ESA_Init call is compiled out by #ifdef
    Failure Indicators: Build error referencing ESA_Init or ESA headers
    Evidence: .sisyphus/evidence/task-12-no-stepping-build.txt
  ```

  **Commit**: YES (separate from other tasks)
  - Message: `feat(esa): integrate ESA_Init into Linux BSP startup before OS_Application_Startup`
  - Files: `osal/src/bsp/generic-linux/src/bsp_start.c`, `osal/src/bsp/generic-linux/CMakeLists.txt`
  - Pre-commit: `make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make`

---

## Final Verification Wave (MANDATORY — after ALL implementation tasks)

> 4 review agents run in PARALLEL. ALL must APPROVE. Rejection → fix → re-run.

- [x] F1. **Plan Compliance Audit** — `oracle`
  Read the plan end-to-end. For each "Must Have": verify implementation exists (read file, run command). For each "Must NOT Have": search codebase for forbidden patterns — reject with file:line if found. Check evidence files exist in `.sisyphus/evidence/`. Compare deliverables against plan.
  Output: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT: APPROVE/REJECT`

  **QA Scenarios:**

  ```
  Scenario: Must Have verification
    Tool: Bash
    Preconditions: All implementation tasks complete
    Steps:
      1. test -d esa/fsw/src && echo "PASS: esa directory exists" || echo "FAIL"
      2. test -f esa/CMakeLists.txt && echo "PASS: CMakeLists exists" || echo "FAIL"
      3. test -f esa/fsw/src/cfe_psp_sim_stepping.c && echo "PASS: source migrated" || echo "FAIL"
      4. test -f esa/ut-coverage/coveragetest-sim_stepping.c && echo "PASS: tests migrated" || echo "FAIL"
      5. test ! -d psp/fsw/modules/sim_stepping && echo "PASS: old PSP module removed" || echo "FAIL"
      6. grep -q "esa_SEARCH_PATH" cfe/cmake/mission_defaults.cmake && echo "PASS: discovery wired" || echo "FAIL"
      7. ls .sisyphus/evidence/task-*.txt 2>/dev/null | wc -l
    Expected Result: All steps print "PASS", evidence files exist for each task
    Failure Indicators: Any "FAIL" output, zero evidence files
    Evidence: .sisyphus/evidence/F1-compliance-audit.txt

  Scenario: Must NOT Have verification
    Tool: Bash
    Preconditions: All implementation tasks complete
    Steps:
      1. grep -rn "psp/fsw/modules/sim_stepping" --include="*.c" --include="*.h" --include="*.cmake" . | grep -v ".git/" | grep -v ".sisyphus/" | wc -l
      2. grep -rn "add_psp_module.*sim_stepping\|CFE_PSP_MODULE_DECLARE" esa/ --include="*.c" --include="*.h" | wc -l
    Expected Result: 0 results for both (no old paths, no PSP module macros in ESA)
    Failure Indicators: Any non-zero count
    Evidence: .sisyphus/evidence/F1-must-not-have.txt
  ```

- [x] F2. **Code Quality Review** — `unspecified-high`
  Run build + tests. Review all changed/new files for: dangling includes, missing `#ifdef` guards, orphaned references to old paths, unused includes, broken links. Check no functional code was modified (only moved/restructured).
  Output: `Build [PASS/FAIL] | Tests [N pass/N fail] | Files [N clean/N issues] | VERDICT`

  **QA Scenarios:**

  ```
  Scenario: Clean build with tests
    Tool: Bash
    Preconditions: Clean state
    Steps:
      1. make distclean
      2. make SIMULATION=native ENABLE_UNIT_TESTS=true prep 2>&1 | tee /tmp/esa-prep.log
      3. make 2>&1 | tee /tmp/esa-build.log
      4. make install 2>&1 | tee /tmp/esa-install.log
      5. make test 2>&1 | tee /tmp/esa-test.log
      6. grep -c "FAIL\|FAILED" /tmp/esa-test.log || echo "0 failures"
    Expected Result: All make commands succeed with exit code 0, zero test failures
    Failure Indicators: Non-zero exit code from any make step, test failures
    Evidence: .sisyphus/evidence/F2-build-test.txt

  Scenario: No code quality issues in ESA files
    Tool: Bash
    Preconditions: Build complete
    Steps:
      1. grep -rn "psp/fsw/modules/sim_stepping" esa/ --include="*.c" --include="*.h" --include="*.cmake" | wc -l
      2. grep -rn "TODO\|FIXME\|HACK" esa/ --include="*.c" --include="*.h" | head -10
      3. grep -rn "#ifdef CFE_SIM_STEPPING" esa/ --include="*.c" --include="*.h" | head -10
    Expected Result: 0 old path references, any TODOs are documented, #ifdef guards present where needed
    Failure Indicators: References to old PSP paths inside ESA source
    Evidence: .sisyphus/evidence/F2-code-quality.txt
  ```

- [x] F3. **Full QA Verification** — `unspecified-high` (+ `playwright` skill if UI)
  Start from clean state (`make distclean`). Execute EVERY QA scenario from EVERY task. Test: `make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install && make test`. Verify `./core-cpu1` starts. Check no references to old PSP sim_stepping path remain.
  Output: `Scenarios [N/N pass] | Build [PASS/FAIL] | Tests [N pass/N fail] | VERDICT`
\
  **QA Scenarios:**

  ```
  Scenario: Full end-to-end build and runtime test
    Tool: Bash
    Preconditions: Clean state (make distclean)
    Steps:
      1. make distclean
      2. make SIMULATION=native ENABLE_UNIT_TESTS=true prep
      3. make
      4. make install
      5. make test 2>&1 | tail -30
      6. cd build/exe/cpu1 && timeout 5 ./core-cpu1 2>&1 | head -50 || true
      7. grep -c "sim_stepping\|ESA" build/exe/cpu1/cf/cfe_es_startup.scr || echo "0"
    Expected Result: Build succeeds, all tests pass, core-cpu1 starts without crash
    Failure Indicators: Build failure, test failures, immediate segfault on startup
    Evidence: .sisyphus/evidence/F3-full-qa.txt

  Scenario: Re-execute all task QA scenarios
    Tool: Bash
    Preconditions: Build installed
    Steps:
      1. For each task 1-12, re-run the primary QA scenario steps
      2. Collect pass/fail for each
    Expected Result: All 12 task scenarios pass
    Failure Indicators: Any task scenario fails
    Evidence: .sisyphus/evidence/F3-scenario-rerun.txt
  ```

- [x] F4. **Scope Fidelity Check** — `deep`
  For each task: read "What to do", compare with actual changes. Verify: only file moves + path updates occurred, no functional changes. Check "Must NOT do" compliance (no API renames, no logic changes). Detect any unaccounted changes.
  Output: `Tasks [N/N compliant] | Scope [CLEAN/N issues] | VERDICT`

  **QA Scenarios:**

  ```
  Scenario: No functional changes in migrated source
    Tool: Bash
    Preconditions: All tasks complete
    Steps:
      1. diff psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping.c esa/fsw/src/cfe_psp_sim_stepping.c 2>/dev/null || echo "Old file removed (expected)"
      2. git diff HEAD~5..HEAD --stat | head -30
      3. git diff HEAD~5..HEAD -- esa/fsw/src/ | grep "^[+-]" | grep -v "^[+-][+-][+-]" | grep -v "include\|#ifdef\|CFE_PSP_MODULE" | head -20
    Expected Result: Only include path changes and PSP module macro removal in source diffs, no logic changes
    Failure Indicators: Functional code changes (algorithm, control flow, data structures)
    Evidence: .sisyphus/evidence/F4-scope-fidelity.txt

  Scenario: No scope creep — no unaccounted files changed
    Tool: Bash
    Preconditions: All tasks complete
    Steps:
      1. git diff HEAD~5..HEAD --name-only | sort
      2. Compare against expected file list from plan (esa/*, psp/fsw/modules/sim_stepping/*, osal BSP, cfe cmake, apps/sch)
    Expected Result: All changed files match plan scope, no unexpected files modified
    Failure Indicators: Files changed that are not mentioned in any task
    Evidence: .sisyphus/evidence/F4-scope-creep.txt
  ```

---

## Commit Strategy

| Wave | Commit Message | Files | Pre-commit |
|------|---------------|-------|------------|
| 1 | `feat(esa): create esa directory structure and CMake` | ESA directory, CMakeLists | `make SIMULATION=native ENABLE_UNIT_TESTS=true prep` |
| 1 | `test(esa): migrate sim_stepping coverage tests to ESA` | ESA ut-coverage/, ut-stubs/ | `make prep` (tests may not compile yet) |
| 2 | `refactor(esa): move sim_stepping source files to ESA` | ESA fsw/src/, fsw/inc/, old PSP module | `make` (expect include errors until callsites update) |
| 2 | `refactor(esa): update all callsite include paths for ESA` | OSAL, PSP, cFE, Apps files | `make && make install && make test` |
| 3 | `chore(esa): remove old PSP sim_stepping module` | PSP module dir removal | `make && make install && make test` |

---

## Success Criteria

### Verification Commands
```bash
# Full clean build
make distclean
make SIMULATION=native ENABLE_UNIT_TESTS=true prep
make
make install
make test   # Expected: all tests PASS including ESA coverage tests

# No old path references
grep -r "psp/fsw/modules/sim_stepping" --include="*.c" --include="*.h" --include="*.cmake" .
# Expected: 0 results (or only in git history/comments)

# ESA directory exists with expected structure
ls esa/fsw/src/
# Expected: cfe_psp_sim_stepping.c, cfe_psp_sim_stepping_core.c

ls esa/public_inc/
# Expected: cfe_psp_sim_stepping_shim.h (moved from sample_defs)

# Functional test
cd build/exe/cpu1 && timeout 5 ./core-cpu1 || true
# Expected: starts, sim_stepping initializes (check console output)
```

### Final Checklist
- [ ] All "Must Have" present
- [ ] All "Must NOT Have" absent
- [ ] All tests pass
- [ ] No OSAL→PSP include path for sim_stepping headers
- [ ] ESA is a standalone CMake target with INTERFACE include dirs
- [ ] Old PSP sim_stepping module removed

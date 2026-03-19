# SCH App — Port to cFE v7 (Draco)

## TL;DR

> **Quick Summary**: Port the `apps/SCH/` scheduler application to compile against cFE v7 (Draco) by replacing all deprecated/removed cFE APIs, types, macros, and patterns with their v7 equivalents. Preserve existing runtime behavior — this is a mechanical API migration, not a functional redesign.
> 
> **Deliverables**:
> - All 10 SCH source files updated with v7-compatible API calls, types, and macros
> - Clean build with `OMIT_DEPRECATED=true` (zero errors, zero warnings on SCH files)
> - No deprecated symbols remaining in source code
> 
> **Estimated Effort**: Medium
> **Parallel Execution**: YES — 3 waves + final verification
> **Critical Path**: Tasks 1-6 (types/headers) → Tasks 7-8 (major source files) → Task 9 (build verification)

---

## Context

### Original Request
User reported SCH app fails to compile against cFE v7 — likely using old cFE macros and data types. Asked to find all issues and implement fixes. Source code only — unit tests excluded.

### Interview Summary
**Key Discussions**:
- Full audit of all 16 SCH files completed — 10 files need changes, 6 confirmed OK
- ~50+ individual deprecated API usages catalogued across 5 categories
- User decided: no unit tests, use `CFE_SB_MsgIdToValue()` for integer comparisons, replace `switch` with `if/else if` + `CFE_SB_MsgId_Equal()`, replace `CFE_SB_HIGHEST_VALID_MSGID` with `0xFFFF`

**Research Findings**:
- All v7 API signatures verified from actual cFE header files
- `apps/sch_lab/` confirmed as valid v7 reference pattern
- `CFE_MSG_GetMsgId`, `CFE_MSG_GetFcnCode`, `CFE_MSG_GetSize` use **out-parameter pattern** (return `CFE_Status_t`, write result via pointer) — NOT simple renames
- `CFE_SB_CMD_HDR_SIZE` / `CFE_SB_TLM_HDR_SIZE` macros completely removed in v7 — use `sizeof(CFE_MSG_CommandHeader_t)` / `sizeof(CFE_MSG_TelemetryHeader_t)`
- Table source files (`sch_def_schtbl.c`, `sch_def_msgtbl.c`) already use `CFE_SB_ValueToMsgId()` or numeric literals — no changes needed

### Metis Review
**Identified Gaps** (all addressed in plan):
- `sch_msg.h` was missing from file change list — uses `CFE_SB_CMD_HDR_SIZE`/`CFE_SB_TLM_HDR_SIZE` 5 times
- `sch_msgdefs.h` uses `CFE_SB_CMD_HDR_SIZE` at line 100 — was initially missed
- `CFE_MSG_Get*` functions use out-parameter pattern — NOT simple renames, all 11 call sites need restructuring
- `sch_custom.h` callback prototype needs `osal_id_t` fix (was only noted for `.c`)
- `sch_app.c:944` is inside a comment — grep-and-replace must skip it
- `CFE_SB_TimeStampMsg` still exists but cast target changes from `CFE_SB_Msg_t *` to `CFE_MSG_Message_t *`
- `CFE_SB_GetUserData` cast at `sch_app.c:1196` needs updating
- `SCH_UNUSED_MID` comparison at `sch_app.c:1190` needs `CFE_SB_MsgIdToValue()` treatment
- Must build with `OMIT_DEPRECATED=true` as definitive gate

---

## Work Objectives

### Core Objective
Replace every deprecated/removed cFE API usage in `apps/SCH/` source files so the app compiles cleanly against cFE v7 (Draco) with `OMIT_DEPRECATED=true`. Preserve existing runtime behavior exactly.

### Concrete Deliverables
- 10 modified source files in `apps/SCH/fsw/`
- Clean compilation: `make SIMULATION=native OMIT_DEPRECATED=true prep && make` with zero errors
- Zero deprecated symbols in source (verified by grep)

### Definition of Done
- [x] `make SIMULATION=native OMIT_DEPRECATED=true prep && make` succeeds with zero errors
- [x] `grep -rn` for all deprecated symbols across `apps/SCH/fsw/src/` and `apps/SCH/fsw/platform_inc/` returns no matches
- [x] No warnings on SCH files in build output

### Must Have
- All `CFE_SB_MsgPtr_t` → `CFE_SB_Buffer_t *` (receive) or `CFE_MSG_Message_t *` (general)
- All `CFE_SB_Msg_t` → `CFE_MSG_Message_t`
- All header byte arrays → `CFE_MSG_CommandHeader_t` / `CFE_MSG_TelemetryHeader_t`
- All `uint32` OSAL IDs → `osal_id_t`
- All removed function calls replaced with v7 equivalents
- All removed enum/macro constants replaced
- All MsgId comparisons use `CFE_SB_MsgId_Equal()` or `CFE_SB_MsgIdToValue()`
- All MsgId printf args wrapped with `CFE_SB_MsgIdToValue()`
- Build gate: `OMIT_DEPRECATED=true`

### Must NOT Have (Guardrails)
- **DO NOT** touch `sch_def_schtbl.c`, `sch_def_msgtbl.c` — table data files are OK
- **DO NOT** touch `fsw/unit_test/` — unit tests are out of scope
- **DO NOT** touch `sch_tbldefs.h` — the `uint16 MessageBuffer[]` raw array is intentional SCH table format
- **DO NOT** edit `sch_app.c:944` — it is inside a comment block, not live code
- **DO NOT** restructure algorithm/control flow — only replace API calls in-place
- **DO NOT** reorder command handlers in `sch_cmds.c`
- **DO NOT** add includes not strictly required by replacement APIs
- **DO NOT** change `SCH_UNUSED_MID` definition or value — it is an app constant, not a cFE constant
- **DO NOT** replace `CFE_SB_INVALID_MSG_ID` for `SCH_UNUSED_MID` — they are different values (0 vs 0x1897)
- **DO NOT** change `sch_msgids.h` — MsgId numeric literals are fine
- **DO NOT** over-modernize `sch_msg.h` structs — use typed struct replacement, keep minimal diff
- **DO NOT** hand-edit EVS enum renames one by one — use bulk replacement to avoid missing sites

---

## Verification Strategy (MANDATORY)

> **ZERO HUMAN INTERVENTION** — ALL verification is agent-executed. No exceptions.

### Test Decision
- **Infrastructure exists**: YES (CTest + `make test`)
- **Automated tests**: NO — user explicitly excluded unit tests from scope
- **Framework**: N/A for this task
- **Agent QA**: Build verification + source grep + symbol check

### QA Policy
Every task includes agent-executed QA scenarios. Evidence saved to `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`.

- **Build verification**: `make SIMULATION=native OMIT_DEPRECATED=true prep && make`
- **Source grep**: Search for deprecated symbols in changed files
- **Symbol check**: `nm` on compiled `.so` to verify no deprecated function references

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 1 (Start Immediately — header/type foundation, 6 tasks parallel):
├── Task 1: sch_msg.h — Replace header byte arrays with typed structs [quick]
├── Task 2: sch_app.h — Fix type declarations (MsgPtr, TimerId, TimeSemaphore, AppID) [quick]
├── Task 3: sch_msgdefs.h + sch_platform_cfg.h + sch_verify.h — Fix removed macros [quick]
├── Task 4: sch_custom.h — Fix callback prototype signature [quick]
├── Task 5: sch_custom.c — Fix callback definition + OSAL types [quick]
└── Task 6: sch_cmds.h — Update function signature types [quick]

Wave 2 (After Wave 1 — major source files, PARALLEL):
├── Task 7: sch_app.c — All API replacements (~30 changes) [deep]
└── Task 8: sch_cmds.c — All API replacements + switch→if/else (~25 changes) [deep]

Wave 3 (After Wave 2 — verification):
└── Task 9: Build verification + source grep + final validation [unspecified-high]

Wave FINAL (After ALL tasks — independent review, 3 parallel):
├── Task F1: Plan compliance audit [oracle]
├── Task F2: Code quality review [unspecified-high]
└── Task F3: Scope fidelity check [deep]
```

### Dependency Matrix

| Task | Depends On | Blocks | Wave |
|------|-----------|--------|------|
| 1 | — | 7, 8 | 1 |
| 2 | — | 7, 8 | 1 |
| 3 | — | 7, 9 | 1 |
| 4 | — | 5 | 1 |
| 5 | 4 | 9 | 1 |
| 6 | — | 8 | 1 |
| 7 | 1, 2, 3 | 9 | 2 |
| 8 | 1, 2, 6 | 9 | 2 |
| 9 | 7, 8 | F1-F3 | 3 |
| F1 | 9 | — | FINAL |
| F2 | 9 | — | FINAL |
| F3 | 9 | — | FINAL |

### Agent Dispatch Summary

- **Wave 1**: **6 tasks** — T1-T6 → `quick`
- **Wave 2**: **2 tasks** — T7 → `deep`, T8 → `deep`
- **Wave 3**: **1 task** — T9 → `unspecified-high`
- **FINAL**: **3 tasks** — F1 → `oracle`, F2 → `unspecified-high`, F3 → `deep`

---

## TODOs

- [x] 1. sch_msg.h — Replace header byte arrays with typed structs

  **What to do**:
  - Replace `uint8 CmdHeader[CFE_SB_CMD_HDR_SIZE]` with `CFE_MSG_CommandHeader_t CmdHeader` in `SCH_NoArgsCmd_t` (line ~57), `SCH_EntryCmd_t` (line ~71), `SCH_GroupCmd_t` (line ~90)
  - Replace `uint8 TlmHeader[CFE_SB_TLM_HDR_SIZE]` with `CFE_MSG_TelemetryHeader_t TlmHeader` in `SCH_HkPacket_t` (line ~110), `SCH_DiagPacket_t` (line ~232)
  - Add `#include "cfe_msg_api.h"` if not already present (needed for typed header structs)
  - Verify struct layout preserved by confirming sizeof of old arrays matches sizeof of new typed structs

  **Must NOT do**:
  - DO NOT rename struct field names (`CmdHeader`, `TlmHeader`) — downstream code references them
  - DO NOT change any non-header fields in these structs
  - DO NOT modernize to named-member access pattern — keep minimal diff

  **Recommended Agent Profile**:
  - **Category**: `quick` — Mechanical type replacement in a single header file, 5 lines to change
  - **Skills**: []

  **Parallelization**: Wave 1 (parallel with Tasks 2-6) | Blocks: 7, 8 | Blocked By: None

  **References**:
  - `apps/sch_lab/fsw/src/sch_lab_app.c` — Shows correct v7 API usage patterns including `CFE_MSG_Init`, `CFE_SB_TransmitMsg`, and `osal_id_t` timer fields
  - `cfe/modules/core_api/fsw/inc/cfe_msg_api_typedefs.h` — Defines the typed header structs
  - `apps/SCH/fsw/src/sch_msg.h` — File to modify, lines ~57, ~71, ~90, ~110, ~232

  **Acceptance Criteria**:
  - [x] No references to `CFE_SB_CMD_HDR_SIZE` or `CFE_SB_TLM_HDR_SIZE` remain in `sch_msg.h`
  - [x] All 5 header field declarations use typed structs

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: All header byte arrays replaced with typed structs
    Tool: Bash (grep)
    Steps:
      1. grep -n "CFE_SB_CMD_HDR_SIZE\|CFE_SB_TLM_HDR_SIZE" apps/SCH/fsw/src/sch_msg.h → assert no output
      2. grep -c "CFE_MSG_CommandHeader_t\|CFE_MSG_TelemetryHeader_t" apps/SCH/fsw/src/sch_msg.h → assert output is "5"
    Expected Result: Zero deprecated macros, 5 typed struct fields
    Evidence: .sisyphus/evidence/task-1-header-structs.txt
  ```

  **Commit**: YES (groups with Wave 1) | Files: `apps/SCH/fsw/src/sch_msg.h`

- [x] 2. sch_app.h — Fix type declarations (MsgPtr, TimerId, TimeSemaphore, AppID)

  **What to do**:
  - Line ~140: Change `CFE_SB_MsgPtr_t MsgPtr` to `CFE_SB_Buffer_t *MsgPtr` in `SCH_AppData_t`
  - Line ~155: Change `uint32 AppID` to `CFE_ES_AppId_t AppID`
  - Line ~156: Change `uint32 TimerId` to `osal_id_t TimerId`
  - Line ~157: Change `uint32 TimeSemaphore` to `osal_id_t TimeSemaphore`
  - Ensure `#include` for `osal_id_t` is available (likely via existing OSAL includes)

  **Must NOT do**:
  - DO NOT add or remove fields from `SCH_AppData_t`
  - DO NOT rename any fields

  **Recommended Agent Profile**:
  - **Category**: `quick` — 4 mechanical type replacements in one struct
  - **Skills**: []

  **Parallelization**: Wave 1 (parallel with Tasks 1, 3-6) | Blocks: 7, 8 | Blocked By: None

  **References**:
  - `apps/sch_lab/fsw/src/sch_lab_app.c` — Shows `osal_id_t` for timer IDs
  - `cfe/modules/core_api/fsw/inc/cfe_es_api_typedefs.h` — Defines `CFE_ES_AppId_t`
  - `osal/src/os/inc/osapi-idmap.h` — Defines `osal_id_t`
  - `apps/SCH/fsw/src/sch_app.h` — File to modify, lines ~140, ~155-157

  **Acceptance Criteria**:
  - [x] No `CFE_SB_MsgPtr_t` in `sch_app.h`
  - [x] `TimerId` and `TimeSemaphore` both typed as `osal_id_t`
  - [x] `AppID` typed as `CFE_ES_AppId_t`

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: All type declarations updated
    Tool: Bash (grep)
    Steps:
      1. grep -n "CFE_SB_MsgPtr_t" apps/SCH/fsw/src/sch_app.h → assert no output
      2. grep -c "CFE_SB_Buffer_t" apps/SCH/fsw/src/sch_app.h → assert ≥1
      3. grep -c "osal_id_t" apps/SCH/fsw/src/sch_app.h → assert ≥2 (TimerId + TimeSemaphore)
      4. grep -c "CFE_ES_AppId_t" apps/SCH/fsw/src/sch_app.h → assert ≥1
    Expected Result: All 4 fields use v7 types
    Evidence: .sisyphus/evidence/task-2-type-decls.txt
  ```

  **Commit**: YES (groups with Wave 1) | Files: `apps/SCH/fsw/src/sch_app.h`

- [x] 3. sch_msgdefs.h + sch_platform_cfg.h + sch_verify.h — Fix removed macros

  **What to do**:
  - `sch_msgdefs.h` line 100: Replace `CFE_SB_CMD_HDR_SIZE` in `#define SCH_MIN_MSG_WORDS (CFE_SB_CMD_HDR_SIZE / 2)` with `sizeof(CFE_MSG_CommandHeader_t)`
  - `sch_platform_cfg.h` line 99: Replace `CFE_SB_HIGHEST_VALID_MSGID` in `#define SCH_MDT_MAX_MSG_ID` with `0xFFFF`
  - `sch_verify.h` line 48: Replace `CFE_SB_HIGHEST_VALID_MSGID` in compile-time check with `0xFFFF`

  **Must NOT do**:
  - DO NOT change any other `#define` values in these files
  - DO NOT change any other verification checks in `sch_verify.h`

  **Recommended Agent Profile**:
  - **Category**: `quick` — 3 macro replacements across 3 small config headers
  - **Skills**: []

  **Parallelization**: Wave 1 (parallel with Tasks 1, 2, 4-6) | Blocks: 7, 9 | Blocked By: None

  **References**:
  - `cfe/modules/core_api/fsw/inc/cfe_msg_api_typedefs.h` — Confirms `CFE_MSG_CommandHeader_t` for `sizeof()`
  - `apps/SCH/fsw/src/sch_msgdefs.h:100` — `SCH_MIN_MSG_WORDS` definition to change
  - `apps/SCH/fsw/platform_inc/sch_platform_cfg.h:99` — `SCH_MDT_MAX_MSG_ID` definition to change
  - `apps/SCH/fsw/src/sch_verify.h:48` — Compile-time check to change

  **Acceptance Criteria**:
  - [x] No `CFE_SB_CMD_HDR_SIZE` in `sch_msgdefs.h`
  - [x] No `CFE_SB_HIGHEST_VALID_MSGID` in `sch_platform_cfg.h` or `sch_verify.h`

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: All removed macros replaced
    Tool: Bash (grep)
    Steps:
      1. grep -rn "CFE_SB_CMD_HDR_SIZE\|CFE_SB_HIGHEST_VALID_MSGID\|CFE_SB_TLM_HDR_SIZE" apps/SCH/fsw/src/sch_msgdefs.h apps/SCH/fsw/platform_inc/sch_platform_cfg.h apps/SCH/fsw/src/sch_verify.h → assert no output
    Expected Result: Zero references to removed cFE macros
    Evidence: .sisyphus/evidence/task-3-removed-macros.txt
  ```

  **Commit**: YES (groups with Wave 1) | Files: `sch_msgdefs.h`, `sch_platform_cfg.h`, `sch_verify.h`

- [x] 4. sch_custom.h — Fix callback prototype signature

  **What to do**:
  - Line ~169: Change `void SCH_MinorFrameCallback(uint32 TimerId)` to `void SCH_MinorFrameCallback(osal_id_t TimerId)`
  - This must match the `OS_TimerCallback_t` typedef: `void (*)(osal_id_t timer_id)`

  **Must NOT do**:
  - DO NOT change any other function prototypes in this file
  - DO NOT change the timer algorithm

  **Recommended Agent Profile**:
  - **Category**: `quick` — Single line prototype change
  - **Skills**: []

  **Parallelization**: Wave 1 (parallel with Tasks 1-3, 5-6) | Blocks: 5 | Blocked By: None

  **References**:
  - `osal/src/os/inc/osapi-timer.h` — Defines `OS_TimerCallback_t` with `osal_id_t` parameter
  - `apps/SCH/fsw/src/sch_custom.h:~169` — File to modify

  **Acceptance Criteria**:
  - [x] `SCH_MinorFrameCallback` prototype uses `osal_id_t` parameter type

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: Callback prototype updated
    Tool: Bash (grep)
    Steps:
      1. grep -n "SCH_MinorFrameCallback" apps/SCH/fsw/src/sch_custom.h → should show osal_id_t parameter
      2. grep -n "uint32.*TimerId" apps/SCH/fsw/src/sch_custom.h → assert no output (for callback)
    Expected Result: Prototype uses osal_id_t
    Evidence: .sisyphus/evidence/task-4-callback-proto.txt
  ```

  **Commit**: YES (groups with Wave 1) | Files: `apps/SCH/fsw/src/sch_custom.h`

- [x] 5. sch_custom.c — Fix callback definition + OSAL types

  **What to do**:
  - Line ~349: Change `void SCH_MinorFrameCallback(uint32 TimerId)` definition to `void SCH_MinorFrameCallback(osal_id_t TimerId)`
  - Verify all `OS_BinSemGive` calls use the correct `osal_id_t` type (should already work since the field type changes in Task 2)
  - No other changes needed — OSAL API calls (`OS_BinSemCreate`, `OS_TimerCreate`, `OS_TimerSet`) have same signatures but now use `osal_id_t` parameters which flow from the changed struct fields

  **Must NOT do**:
  - DO NOT change the timer algorithm or callback logic
  - DO NOT modify `OS_BinSemGive`/`OS_TimerCreate` call patterns — only the parameter types change

  **Recommended Agent Profile**:
  - **Category**: `quick` — 1 function signature change
  - **Skills**: []

  **Parallelization**: Wave 1 (parallel with Tasks 1-4, 6) | Blocks: 9 | Blocked By: Task 4 (header must match)

  **References**:
  - `apps/SCH/fsw/src/sch_custom.c:~349` — Callback definition to change
  - `apps/SCH/fsw/src/sch_custom.h` — Must match prototype from Task 4
  - `osal/src/os/inc/osapi-timer.h` — `OS_TimerCallback_t` definition

  **Acceptance Criteria**:
  - [x] `SCH_MinorFrameCallback` definition uses `osal_id_t` parameter
  - [x] Prototype in `.h` and definition in `.c` match

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: Callback definition matches prototype
    Tool: Bash (grep)
    Steps:
      1. grep -n "SCH_MinorFrameCallback" apps/SCH/fsw/src/sch_custom.c → should show osal_id_t parameter
      2. grep -n "uint32.*TimerId" apps/SCH/fsw/src/sch_custom.c | grep -i callback → assert no output
    Expected Result: Definition uses osal_id_t, matches .h prototype
    Evidence: .sisyphus/evidence/task-5-callback-def.txt
  ```

  **Commit**: YES (groups with Wave 1) | Files: `apps/SCH/fsw/src/sch_custom.c`

- [x] 6. sch_cmds.h — Update function signature types

  **What to do**:
  - Update ALL function prototypes that take `CFE_SB_MsgPtr_t` parameter to take `const CFE_SB_Buffer_t *` instead
  - Key functions to update (verify exact list by reading the file):
    - `SCH_AppPipe(CFE_SB_MsgPtr_t)` → `SCH_AppPipe(const CFE_SB_Buffer_t *)`
    - `SCH_HousekeepingCmd(CFE_SB_MsgPtr_t)` → `SCH_HousekeepingCmd(const CFE_SB_Buffer_t *)`
    - `SCH_NoopCmd(CFE_SB_MsgPtr_t)` → `SCH_NoopCmd(const CFE_SB_Buffer_t *)`
    - `SCH_ResetCmd(CFE_SB_MsgPtr_t)` → `SCH_ResetCmd(const CFE_SB_Buffer_t *)`
    - `SCH_EnableCmd(CFE_SB_MsgPtr_t)` → `SCH_EnableCmd(const CFE_SB_Buffer_t *)`
    - `SCH_DisableCmd(CFE_SB_MsgPtr_t)` → `SCH_DisableCmd(const CFE_SB_Buffer_t *)`
    - `SCH_EnableGroupCmd(CFE_SB_MsgPtr_t)` → `SCH_EnableGroupCmd(const CFE_SB_Buffer_t *)`
    - `SCH_DisableGroupCmd(CFE_SB_MsgPtr_t)` → `SCH_DisableGroupCmd(const CFE_SB_Buffer_t *)`
    - `SCH_SendDiagTlm(CFE_SB_MsgPtr_t)` → `SCH_SendDiagTlm(const CFE_SB_Buffer_t *)`
    - Any others using `CFE_SB_MsgPtr_t`
  - Also update `SCH_VerifyCmdLength` if it takes `CFE_SB_MsgPtr_t`

  **Must NOT do**:
  - DO NOT change function semantics or add/remove parameters
  - DO NOT reorder function prototypes

  **Recommended Agent Profile**:
  - **Category**: `quick` — Bulk type replacement in header prototypes
  - **Skills**: []

  **Parallelization**: Wave 1 (parallel with Tasks 1-5) | Blocks: 8 | Blocked By: None

  **References**:
  - `apps/SCH/fsw/src/sch_cmds.h` — File to modify (read to get exact function list)
  - `apps/sch_lab/fsw/src/sch_lab_app.c` — Shows v7 buffer pointer patterns

  **Acceptance Criteria**:
  - [x] No `CFE_SB_MsgPtr_t` in `sch_cmds.h`
  - [x] All command handler prototypes use `const CFE_SB_Buffer_t *`

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: All prototypes updated
    Tool: Bash (grep)
    Steps:
      1. grep -n "CFE_SB_MsgPtr_t" apps/SCH/fsw/src/sch_cmds.h → assert no output
      2. grep -c "CFE_SB_Buffer_t" apps/SCH/fsw/src/sch_cmds.h → assert ≥8 (one per handler)
    Expected Result: All handler prototypes use v7 buffer type
    Evidence: .sisyphus/evidence/task-6-cmds-header.txt
  ```

  **Commit**: YES (groups with Wave 1) | Files: `apps/SCH/fsw/src/sch_cmds.h`

- [x] 7. sch_app.c — All API replacements (~30 changes)

  **What to do**:

  **7a. Remove `CFE_ES_RegisterApp()` call:**
  - Line ~135: Delete or comment out `CFE_ES_RegisterApp()` — auto-registered in v7

  **7b. Replace `CFE_SB_InitMsg` → `CFE_MSG_Init`:**
  - Line ~415: `CFE_SB_InitMsg(&SCH_AppData.HkPacket, SCH_HK_TLM_MID, sizeof(SCH_HkPacket_t), TRUE)` → `CFE_MSG_Init(CFE_MSG_PTR(SCH_AppData.HkPacket.TlmHeader), CFE_SB_ValueToMsgId(SCH_HK_TLM_MID), sizeof(SCH_HkPacket_t))`
  - Line ~420: Same pattern for `DiagPacket` with `SCH_DIAG_TLM_MID`
  - Note: `CFE_MSG_Init` does NOT have the `Clear` boolean parameter — the message is not auto-cleared. If zeroing is needed, add `memset` before the `CFE_MSG_Init` call (check if original code relied on `TRUE` for clearing).

  **7c. Replace `CFE_SB_RcvMsg` → `CFE_SB_ReceiveBuffer`:**
  - Line ~988: `Status = CFE_SB_RcvMsg(&SCH_AppData.MsgPtr, SCH_AppData.CmdPipe, CFE_SB_POLL)` → `Status = CFE_SB_ReceiveBuffer(&SCH_AppData.MsgPtr, SCH_AppData.CmdPipe, CFE_SB_POLL)`
  - This works because `SCH_AppData.MsgPtr` type changes from `CFE_SB_MsgPtr_t` to `CFE_SB_Buffer_t *` in Task 2

  **7d. Replace `CFE_SB_SendMsg` → `CFE_SB_TransmitMsg`:**
  - Line ~933: `CFE_SB_SendMsg((CFE_SB_Msg_t *)Message)` → `CFE_SB_TransmitMsg((CFE_MSG_Message_t *)Message, true)`
  - **DO NOT edit line ~944** — it is inside a comment block (`*` prefix in source), not live code. Verify by reading the surrounding context before editing.

  **7e. Replace ES RunStatus enums:**
  - Line ~125: `CFE_ES_APP_RUN` → `CFE_ES_RunStatus_APP_RUN`
  - Lines ~165, ~232: `CFE_ES_APP_ERROR` → `CFE_ES_RunStatus_APP_ERROR`

  **7f. Replace ALL EVS event type enums (bulk replace):**
  - `CFE_EVS_ERROR` → `CFE_EVS_EventType_ERROR` (all occurrences)
  - `CFE_EVS_INFORMATION` → `CFE_EVS_EventType_INFORMATION` (all occurrences)
  - `CFE_EVS_DEBUG` → `CFE_EVS_EventType_DEBUG` (all occurrences)
  - `CFE_EVS_CRITICAL` → `CFE_EVS_EventType_CRITICAL` (line ~245)
  - **Use bulk search-and-replace** — do NOT hand-edit one by one (risk of missing)
  - **Caution**: Only replace when used as function argument to `CFE_EVS_SendEvent`, not if the string appears in comments or other contexts

  **7g. Fix MsgId comparisons in `SCH_ValidateMessageTable` (~line 1170-1220):**
  - Line ~1171: `CFE_SB_MsgId_t MaxValue = (CFE_SB_MsgId_t) SCH_MDT_MAX_MSG_ID` → `CFE_SB_MsgId_t MaxValue = CFE_SB_ValueToMsgId(SCH_MDT_MAX_MSG_ID)`
  - Line ~1172-1173: Same pattern for `MinValue` with `SCH_MDT_MIN_MSG_ID`
  - Line ~1190: `MessageID == SCH_UNUSED_MID` → `CFE_SB_MsgIdToValue(MessageID) == SCH_UNUSED_MID`
  - Lines ~1209-1210: `MessageID <= MaxValue && MessageID >= MinValue` → `CFE_SB_MsgIdToValue(MessageID) <= CFE_SB_MsgIdToValue(MaxValue) && CFE_SB_MsgIdToValue(MessageID) >= CFE_SB_MsgIdToValue(MinValue)`

  **7h. Fix `CFE_SB_GetMsgId` → `CFE_MSG_GetMsgId` (out-parameter pattern):**
  - Line ~1187: `MessageID = CFE_SB_GetMsgId((CFE_SB_MsgPtr_t)MessageBuffer)` →
    ```c
    CFE_MSG_GetMsgId((CFE_MSG_Message_t *)MessageBuffer, &MessageID);
    ```
  - Note: The old function returned the value directly; the new one writes via pointer

  **7i. Fix `CFE_SB_GetTotalMsgLength` → `CFE_MSG_GetSize` (out-parameter pattern):**
  - Line ~1188: `MessageLength = CFE_SB_GetTotalMsgLength((CFE_SB_MsgPtr_t)MessageBuffer)` →
    ```c
    CFE_MSG_Size_t MsgSize;
    CFE_MSG_GetSize((CFE_MSG_Message_t *)MessageBuffer, &MsgSize);
    MessageLength = (int32)MsgSize;
    ```
  - Note: `CFE_MSG_GetSize` writes a `CFE_MSG_Size_t` (which is `size_t`), and the existing code uses `int32 MessageLength` — cast explicitly.

  **7j. Fix `CFE_SB_GetUserData` cast:**
  - Line ~1196: Change cast from `(CFE_SB_MsgPtr_t)` to `(CFE_MSG_Message_t *)` — the function signature now takes `CFE_MSG_Message_t *`

  **7k. Fix MsgId printf format args:**
  - All `CFE_EVS_SendEvent` calls that format `MessageID` with `%d`/`%u`/`0x%04X` must wrap: `CFE_SB_MsgIdToValue(MessageID)`
  - Search for `MessageID` in format args throughout the file (e.g., lines ~1216, ~1242)

  **Must NOT do**:
  - DO NOT restructure the main run loop algorithm
  - DO NOT edit line ~944 (it's in a comment block)
  - DO NOT change the dispatch logic structure (just replace API calls within it)
  - DO NOT change the `SCH_ValidateMessageTable` algorithm — only fix the API calls and comparisons within it

  **Recommended Agent Profile**:
  - **Category**: `deep` — ~30 interconnected changes across a 1272-line file requiring careful context awareness
  - **Skills**: []

  **Parallelization**: Wave 2 (parallel with Task 8) | Blocks: 9 | Blocked By: Tasks 1, 2, 3

  **References**:
  - `apps/SCH/fsw/src/sch_app.c` — File to modify (1272 lines)
  - `apps/sch_lab/fsw/src/sch_lab_app.c` — v7 reference for `CFE_MSG_Init`, `CFE_SB_TransmitMsg`, `CFE_SB_ReceiveBuffer` patterns
  - `cfe/modules/core_api/fsw/inc/cfe_msg.h` — `CFE_MSG_Init`, `CFE_MSG_GetMsgId`, `CFE_MSG_GetSize` signatures
  - `cfe/modules/core_api/fsw/inc/cfe_sb.h` — `CFE_SB_TransmitMsg`, `CFE_SB_ReceiveBuffer`, `CFE_SB_MsgIdToValue`, `CFE_SB_ValueToMsgId` signatures
  - `cfe/modules/core_api/fsw/inc/cfe_es.h` — `CFE_ES_RunStatus_APP_RUN`, `CFE_ES_RunStatus_APP_ERROR`
  - `cfe/modules/core_api/fsw/inc/cfe_evs.h` — `CFE_EVS_EventType_ERROR`, `CFE_EVS_EventType_INFORMATION`, etc.

  **Acceptance Criteria**:
  - [x] No deprecated function calls remain in `sch_app.c`
  - [x] No deprecated enum/macro constants remain
  - [x] All MsgId comparisons use `CFE_SB_MsgId_Equal()` or `CFE_SB_MsgIdToValue()`
  - [x] Line ~944 comment block is untouched
  - [x] `CFE_ES_RegisterApp()` call removed

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: Happy path — no deprecated APIs in sch_app.c
    Tool: Bash (grep)
    Steps:
      1. grep -n "CFE_SB_SendMsg\b\|CFE_SB_RcvMsg\b\|CFE_ES_RegisterApp\|CFE_SB_InitMsg\b\|CFE_SB_GetMsgId\b\|CFE_SB_GetTotalMsgLength\b" apps/SCH/fsw/src/sch_app.c → assert no output
      2. grep -n "CFE_ES_APP_RUN\b\|CFE_ES_APP_ERROR\b" apps/SCH/fsw/src/sch_app.c → assert no output
      3. grep -n "CFE_EVS_ERROR\b\|CFE_EVS_INFORMATION\b\|CFE_EVS_DEBUG\b\|CFE_EVS_CRITICAL\b" apps/SCH/fsw/src/sch_app.c → assert no output
      4. grep -n "CFE_SB_MsgPtr_t\|CFE_SB_Msg_t\b" apps/SCH/fsw/src/sch_app.c → assert no output (except comments)
    Expected Result: Zero deprecated symbols in active code
    Evidence: .sisyphus/evidence/task-7-sch-app-clean.txt

  Scenario: Edge case — line 944 comment preserved
    Tool: Bash (grep)
    Steps:
      1. Read sch_app.c around line 944 (±5 lines)
      2. Verify the comment block containing "CFE_SB_SendMsg" is intact and untouched
    Expected Result: Comment block still present with original text
    Failure Indicators: Comment modified, deleted, or replaced with live code
    Evidence: .sisyphus/evidence/task-7-comment-preserved.txt
  ```

  **Commit**: YES (groups with Wave 2) | Files: `apps/SCH/fsw/src/sch_app.c`

- [x] 8. sch_cmds.c — All API replacements + switch→if/else (~25 changes)

  **What to do**:

  **8a. Replace `switch(MessageID)` with `if/else if` using `CFE_SB_MsgId_Equal()`:**
  - In `SCH_AppPipe()` (~line 73): The current `switch` on `MessageID` must become:
    ```c
    CFE_SB_MsgId_t MessageID;
    CFE_MSG_GetMsgId(&BufPtr->Msg, &MessageID);
    
    if (CFE_SB_MsgId_Equal(MessageID, CFE_SB_ValueToMsgId(SCH_SEND_HK_MID))) {
        SCH_HousekeepingCmd(BufPtr);
    } else if (CFE_SB_MsgId_Equal(MessageID, CFE_SB_ValueToMsgId(SCH_CMD_MID))) {
        // command code dispatch stays as switch on FcnCode
    } else {
        // default: unknown MsgId error
    }
    ```
  - The inner `switch` on command code (`CFE_MSG_GetFcnCode`) can remain a `switch` since `CFE_MSG_FcnCode_t` is a plain integer

  **8b. Replace `CFE_SB_GetMsgId` → `CFE_MSG_GetMsgId` (out-parameter):**
  - Line ~72: `MessageID = CFE_SB_GetMsgId(MsgPtr)` → `CFE_MSG_GetMsgId(&BufPtr->Msg, &MessageID)`
  - Lines ~687, ~693: Same pattern for message table entries: `CFE_SB_GetMsgId((CFE_SB_MsgPtr_t)&...)` → `CFE_MSG_GetMsgId((const CFE_MSG_Message_t *)&..., &MsgId)`
  - Line ~784: Same in `SCH_VerifyCmdLength`

  **8c. Replace `CFE_SB_GetCmdCode` → `CFE_MSG_GetFcnCode` (out-parameter):**
  - Line ~87: `CommandCode = CFE_SB_GetCmdCode(MsgPtr)` → `CFE_MSG_GetFcnCode(&BufPtr->Msg, &CommandCode)` (with `CFE_MSG_FcnCode_t CommandCode`)
  - Line ~785: Same in `SCH_VerifyCmdLength`

  **8d. Replace `CFE_SB_GetTotalMsgLength` → `CFE_MSG_GetSize` (out-parameter):**
  - Line ~780: `ActualLength = CFE_SB_GetTotalMsgLength(MsgPtr)` →
    ```c
    CFE_MSG_Size_t MsgSize;
    CFE_MSG_GetSize(&BufPtr->Msg, &MsgSize);
    ActualLength = (uint16)MsgSize;
    ```

  **8e. Replace `CFE_SB_SendMsg` → `CFE_SB_TransmitMsg`:**
  - Line ~193: `CFE_SB_SendMsg((CFE_SB_Msg_t *)&SCH_AppData.HkPacket)` → `CFE_SB_TransmitMsg(&SCH_AppData.HkPacket.TlmHeader.Msg, true)`
  - Line ~704: Same for DiagPacket

  **8f. Fix `CFE_SB_TimeStampMsg` casts:**
  - Lines ~192, ~703: Change cast from `(CFE_SB_Msg_t *)` to use `.TlmHeader.Msg` member access:
    `CFE_SB_TimeStampMsg(&SCH_AppData.HkPacket.TlmHeader.Msg)`

  **8g. Update all function signatures (match Task 6 header):**
  - Every function definition that takes `CFE_SB_MsgPtr_t MsgPtr` → `const CFE_SB_Buffer_t *BufPtr`
  - Inside each function, replace `MsgPtr` usage with `&BufPtr->Msg` for API calls
  - Key functions: `SCH_AppPipe`, `SCH_HousekeepingCmd`, `SCH_NoopCmd`, `SCH_ResetCmd`, `SCH_EnableCmd`, `SCH_DisableCmd`, `SCH_EnableGroupCmd`, `SCH_DisableGroupCmd`, `SCH_SendDiagTlm`, `SCH_VerifyCmdLength`

  **8h. Replace EVS event type enums (bulk replace):**
  - Same as Task 7f — `CFE_EVS_ERROR` → `CFE_EVS_EventType_ERROR`, etc. across all `CFE_EVS_SendEvent` calls

  **8i. Fix MsgId initialization:**
  - Line ~69: `CFE_SB_MsgId_t MessageID = 0` → `CFE_SB_MsgId_t MessageID = CFE_SB_INVALID_MSG_ID`
  - Line ~776: Same if applicable

  **8j. Fix MsgId printf format args:**
  - All `CFE_EVS_SendEvent` calls that format `MessageID` or `MsgId` with `%d`/`%u`/`0x%04X` must wrap with `CFE_SB_MsgIdToValue()`
  - Check lines ~128, ~141, and any other EVS calls referencing MsgId values

  **Must NOT do**:
  - DO NOT reorder command handlers
  - DO NOT change the inner command-code switch statement structure (only the outer MsgId dispatch)
  - DO NOT change command handler logic — only replace API calls

  **Recommended Agent Profile**:
  - **Category**: `deep` — ~25 interconnected changes across 826 lines, including structural switch→if/else conversion
  - **Skills**: []

  **Parallelization**: Wave 2 (parallel with Task 7) | Blocks: 9 | Blocked By: Tasks 1, 2, 6

  **References**:
  - `apps/SCH/fsw/src/sch_cmds.c` — File to modify (826 lines)
  - `apps/SCH/fsw/src/sch_cmds.h` — Must match prototypes from Task 6
  - `apps/sch_lab/fsw/src/sch_lab_app.c` — v7 dispatch pattern with `CFE_SB_MsgId_Equal()`
  - `cfe/modules/core_api/fsw/inc/cfe_msg.h` — Out-parameter API signatures
  - `cfe/modules/core_api/fsw/inc/cfe_sb.h` — `CFE_SB_MsgId_Equal`, `CFE_SB_INVALID_MSG_ID`, `CFE_SB_TransmitMsg`

  **Acceptance Criteria**:
  - [x] No `switch` on `CFE_SB_MsgId_t` — replaced with `if/else if` + `CFE_SB_MsgId_Equal()`
  - [x] No deprecated function calls remain
  - [x] No `CFE_SB_MsgPtr_t` in function signatures
  - [x] All MsgId init/compare/printf sites fixed
  - [x] All EVS enum constants replaced

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: Happy path — no deprecated APIs in sch_cmds.c
    Tool: Bash (grep)
    Steps:
      1. grep -n "CFE_SB_MsgPtr_t\|CFE_SB_Msg_t\b\|CFE_SB_SendMsg\b\|CFE_SB_GetMsgId\b\|CFE_SB_GetCmdCode\b\|CFE_SB_GetTotalMsgLength\b" apps/SCH/fsw/src/sch_cmds.c → assert no output
      2. grep -n "CFE_EVS_ERROR\b\|CFE_EVS_INFORMATION\b\|CFE_EVS_DEBUG\b" apps/SCH/fsw/src/sch_cmds.c → assert no output
      3. grep -n "switch.*MessageID\|switch.*MsgId" apps/SCH/fsw/src/sch_cmds.c → assert no output (MsgId dispatch is now if/else)
    Expected Result: Zero deprecated symbols, no MsgId switch statements
    Evidence: .sisyphus/evidence/task-8-sch-cmds-clean.txt

  Scenario: Edge case — MsgId initialization uses INVALID_MSG_ID
    Tool: Bash (grep)
    Steps:
      1. grep -n "CFE_SB_MsgId_t.*=.*0\b" apps/SCH/fsw/src/sch_cmds.c → assert no output
      2. grep -n "CFE_SB_INVALID_MSG_ID" apps/SCH/fsw/src/sch_cmds.c → assert ≥1 match
    Expected Result: MsgId initialized with CFE_SB_INVALID_MSG_ID, not literal 0
    Evidence: .sisyphus/evidence/task-8-msgid-init.txt
  ```

  **Commit**: YES (groups with Wave 2) | Files: `apps/SCH/fsw/src/sch_cmds.c`

- [x] 9. Build verification + source grep + final validation

  **What to do**:
  - Run full build: `cd /workspace/cFS && make distclean && make SIMULATION=native OMIT_DEPRECATED=true prep && make 2>&1 | tee /tmp/sch-build.log`
  - Check for SCH-related errors: `grep -i "error" /tmp/sch-build.log | grep -i "sch"`
  - Check for SCH-related warnings: `grep -i "warning" /tmp/sch-build.log | grep -i "sch"`
  - Run comprehensive deprecated symbol grep across all SCH source files
  - If errors found: diagnose, fix, and re-run build
  - If warnings found: assess and fix if they indicate incomplete migration

  **Must NOT do**:
  - DO NOT modify files not in the 10-file change list
  - DO NOT suppress warnings — fix them

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high` — Build diagnostics and iterative fixing requires judgment
  - **Skills**: []

  **Parallelization**: Wave 3 (sequential) | Blocks: F1-F3 | Blocked By: Tasks 7, 8

  **References**:
  - All 10 modified files from Tasks 1-8
  - `Makefile` — Build commands
  - `cfe/cmake/` — Build system for understanding error messages

  **Acceptance Criteria**:
  - [x] `make SIMULATION=native OMIT_DEPRECATED=true prep && make` → zero errors
  - [x] Zero SCH-related warnings
  - [x] Deprecated symbol grep → zero matches in source (excluding comments)

  **QA Scenarios (MANDATORY):**
  ```
  Scenario: Happy path — clean build with OMIT_DEPRECATED=true
    Tool: Bash
    Steps:
      1. cd /workspace/cFS && make distclean
      2. make SIMULATION=native OMIT_DEPRECATED=true prep
      3. make 2>&1 | tee /tmp/sch-build.log
      4. grep -ic "error" /tmp/sch-build.log | xargs test 0 -eq
      5. grep -i "warning" /tmp/sch-build.log | grep -i "sch" → assert no output
    Expected Result: Build succeeds, zero errors, zero SCH warnings
    Evidence: .sisyphus/evidence/task-9-build-log.txt

  Scenario: Source grep — zero deprecated symbols
    Tool: Bash (grep)
    Steps:
      1. grep -rn "CFE_SB_MsgPtr_t\|CFE_SB_Msg_t\b\|CFE_SB_SendMsg\b\|CFE_SB_RcvMsg\b\|CFE_ES_RegisterApp\|CFE_SB_InitMsg\b\|CFE_SB_GetMsgId\b\|CFE_SB_GetCmdCode\b\|CFE_SB_GetTotalMsgLength\b\|CFE_ES_APP_RUN\b\|CFE_EVS_ERROR\b\|CFE_EVS_INFORMATION\b\|CFE_EVS_DEBUG\b\|CFE_EVS_CRITICAL\b\|CFE_SB_HIGHEST_VALID_MSGID\|CFE_SB_CMD_HDR_SIZE\|CFE_SB_TLM_HDR_SIZE" apps/SCH/fsw/src/ apps/SCH/fsw/platform_inc/ → assert no output
    Expected Result: Zero deprecated symbols in any SCH source file
    Failure Indicators: Any match except inside comment blocks
    Evidence: .sisyphus/evidence/task-9-source-grep.txt

  Scenario: Edge case — build also works WITHOUT OMIT_DEPRECATED
    Tool: Bash
    Steps:
      1. make distclean && make SIMULATION=native prep && make 2>&1 | tee /tmp/sch-build-compat.log
      2. grep -i "error" /tmp/sch-build-compat.log | grep -i "sch" → assert no output
    Expected Result: Standard build also succeeds (backward compatible)
    Evidence: .sisyphus/evidence/task-9-build-compat.txt
  ```

  **Commit**: YES (if fixups needed) | Message: `fix(SCH): address build verification findings`

---

## Final Verification Wave (MANDATORY — after ALL implementation tasks)

> 3 review agents run in PARALLEL. ALL must APPROVE. Rejection → fix → re-run.

- [x] F1. **Plan Compliance Audit** — `oracle`
  Read the plan end-to-end. For each "Must Have": verify implementation exists (read file, grep for new API). For each "Must NOT Have": search codebase for forbidden patterns — reject with file:line if found. Check evidence files exist in `.sisyphus/evidence/`. Compare deliverables against plan.
  Output: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT: APPROVE/REJECT`

- [x] F2. **Code Quality Review** — `unspecified-high`
  Run `make SIMULATION=native OMIT_DEPRECATED=true prep && make`. Review all changed files for: empty catches, `printf` format mismatches, incomplete casts, `CFE_SB_MsgId_t` used with raw operators. Check for grep false-positives (deprecated names in comments are OK). Verify zero warnings on SCH files.
  Output: `Build [PASS/FAIL] | Warnings [N] | Cast Issues [N] | VERDICT`

- [x] F3. **Scope Fidelity Check** — `deep`
  For each task: read "What to do", read actual diff (`git diff`). Verify 1:1 — everything in spec was built (no missing), nothing beyond spec was built (no creep). Check "Must NOT do" compliance — especially: no changes to table files, no changes to unit tests, `sch_app.c:944` comment line untouched. Detect cross-task contamination: Task N touching Task M's files. Flag unaccounted changes.
  Output: `Tasks [N/N compliant] | Contamination [CLEAN/N issues] | Unaccounted [CLEAN/N files] | VERDICT`

---

## Commit Strategy

- **Wave 1 commit**: `fix(SCH): update header types and config macros for cFE v7` — sch_msg.h, sch_app.h, sch_msgdefs.h, sch_platform_cfg.h, sch_verify.h, sch_custom.h, sch_custom.c, sch_cmds.h
- **Wave 2 commit**: `fix(SCH): replace deprecated cFE APIs in app and command sources` — sch_app.c, sch_cmds.c
- **Final commit** (if any fixups needed after verification): `fix(SCH): address build verification findings`

---

## Success Criteria

### Verification Commands
```bash
# 1. Clean build with OMIT_DEPRECATED=true
cd /workspace/cFS && make distclean && make SIMULATION=native OMIT_DEPRECATED=true prep && make 2>&1 | tee /tmp/sch-build.log
# Expected: BUILD SUCCESSFUL, zero errors

# 2. No warnings on SCH files
grep -i "warning" /tmp/sch-build.log | grep -i "sch"
# Expected: no output

# 3. No deprecated symbols in source
grep -rn "CFE_SB_MsgPtr_t\|CFE_SB_Msg_t\b\|CFE_SB_SendMsg\b\|CFE_SB_RcvMsg\b\|CFE_ES_RegisterApp\|CFE_SB_InitMsg\b\|CFE_SB_GetMsgId\b\|CFE_SB_GetCmdCode\b\|CFE_SB_GetTotalMsgLength\b\|CFE_ES_APP_RUN\b\|CFE_EVS_ERROR\b\|CFE_EVS_INFORMATION\b\|CFE_EVS_DEBUG\b\|CFE_EVS_CRITICAL\b\|CFE_SB_HIGHEST_VALID_MSGID\|CFE_SB_CMD_HDR_SIZE\|CFE_SB_TLM_HDR_SIZE" apps/SCH/fsw/src/ apps/SCH/fsw/platform_inc/
# Expected: no output (ignore comments if any — verify manually)

# 4. SCH object has no deprecated function references
nm build/exe/cpu1/cf/sch.so 2>/dev/null | grep -E "CFE_SB_SendMsg|CFE_SB_RcvMsg|CFE_ES_RegisterApp|CFE_SB_InitMsg|CFE_SB_GetMsgId$|CFE_SB_GetCmdCode$|CFE_SB_GetTotalMsgLength"
# Expected: no output
```

### Final Checklist
- [x] All "Must Have" present
- [x] All "Must NOT Have" absent
- [x] Build passes with `OMIT_DEPRECATED=true`
- [x] Zero deprecated symbols in source
- [x] Zero SCH-related warnings in build

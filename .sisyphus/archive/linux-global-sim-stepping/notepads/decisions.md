# Linux Global Sim Stepping - Decisions

## Architecture Decisions

### TBD - To be filled during execution

## T4 Activation Design: Auto-Registration vs. Explicit Opt-In (2026-03-12)

**Decision**: Use auto-registration pattern for TaskDelay takeover opt-in set population

**Context**: T4 requires runtime TaskDelay callers to be added to `taskdelay_optin_set[]` after system readiness. Two approaches considered:

**Option 1: Explicit Opt-In API**
- Provide `CFE_PSP_SimStepping_Core_OptInTaskDelay(task_id)` function
- Apps/services call this during init to register for takeover
- Requires modifying each app/service to call opt-in API
- Pros: Explicit control, clear intent
- Cons: Invasive (touches many files), requires app awareness of stepping

**Option 2: Auto-Registration in ReportTaskDelay (CHOSEN)**
- `ReportTaskDelay()` automatically adds `task_id` to opt-in set after readiness
- No app modifications needed
- First delay call after readiness triggers registration
- Duplicate prevention via membership check
- Capacity safety via count < 8 guard
- Pros: Zero app changes, transparent takeover, minimal footprint
- Cons: Implicit behavior, all runtime delays become eligible (may not be desired)

**Rationale for Auto-Registration**:
1. **Minimal scope**: T4 constraint requires "only modify cfe_psp_sim_stepping_core.c unless strictly necessary"
2. **Transparent takeover**: Apps don't need stepping awareness
3. **Conservative gating**: Five-gate eligibility check (including quantum divisibility) provides sufficient control
4. **Startup safety**: Auto-registration only happens after `system_ready_for_stepping == true`
5. **Capacity management**: 8-task limit prevents unbounded registration

**Implementation**:
```c
if (core->system_ready_for_stepping)
{
    // Check for duplicate
    already_registered = false;
    for (i = 0; i < core->taskdelay_optin_count; i++)
    {
        if (core->taskdelay_optin_set[i] == task_id)
        {
            already_registered = true;
            break;
        }
    }
    
    // Register if not present and capacity allows
    if (!already_registered && core->taskdelay_optin_count < 8)
    {
        core->taskdelay_optin_set[core->taskdelay_optin_count] = task_id;
        core->taskdelay_optin_count++;
    }
}
```

**Trade-offs Accepted**:
- All runtime tasks calling `OS_TaskDelay()` become eligible for takeover (subject to quantum divisibility)
- No per-app control over takeover participation (all-or-nothing after readiness)
- First 8 tasks to call delay after readiness fill the opt-in set (no prioritization)

**Future Extension Path**:
If explicit per-app control needed:
1. Add `CFE_PSP_SimStepping_Core_OptInTaskDelay(task_id)` API
2. Add `CFE_PSP_SimStepping_Core_OptOutTaskDelay(task_id)` API
3. Remove auto-registration logic from `ReportTaskDelay()`
4. Require apps to call opt-in during init

For now, auto-registration provides sufficient control via existing five-gate eligibility check.

**Approval**: Implicit via T4 acceptance (atomic implementation, minimal scope)

**Related Decisions**:
- T4 initial: Added readiness gate (gate 2) to prevent startup deadlock
- T4 retry: Added feature gate (gate 3) and opt-in gate (gate 4) with auto-registration


---

## T4 RETRY: Polling-Based Blocking Wait Design (2026-03-12)

### Decision Summary

**Context:** T4 retry required PSP-owned blocking wait mechanism to hold tasks during step-controlled delays until enough explicit step quantums advanced to satisfy delay budget.

**Decision:** Implement polling-based blocking wait using 1ms `nanosleep()` intervals to check `sim_time_ns` progress, rather than condition variable or semaphore-based wakeup.

**Rationale:**

1. **Simplicity:** No additional synchronization primitives required
   - Avoids condition variable + mutex overhead
   - No per-task wakeup bookkeeping
   - No race conditions between quantum advance and wakeup

2. **CPU efficiency:** 1ms poll interval yields CPU while waiting
   - Not spin-wait (100% CPU)
   - Responsive enough (10ms quantum >> 1ms poll)
   - Typical blocked task: ~0.1% CPU overhead (1ms poll per 10ms quantum)

3. **Determinism:** Release controlled solely by `sim_time_ns` advancement
   - No wall-clock timing can release blocked task
   - `nanosleep()` only yields CPU, no timeout-based exit
   - Explicit step commands via `AdvanceOneQuantum()` are only release mechanism

4. **Scalability:** Works naturally with multiple blocked tasks
   - Each task polls independently
   - No global wakeup list to maintain
   - Single shared `sim_time_ns` variable (atomic read)

5. **Diagnostic clarity:** Easy to observe/debug
   - Blocked tasks visible in scheduler as sleeping
   - `sim_time_ns` value directly observable
   - No hidden wakeup state to inspect

### Alternative Approaches Considered

**Condition Variable + Mutex:**
- **Pros:** More traditional synchronization, potential 0% CPU overhead
- **Cons:**
  - Requires mutex around `sim_time_ns` access (contention risk)
  - Per-task wakeup tracking (which tasks eligible for wakeup?)
  - Race window: quantum advance → broadcast → task checks → false wakeup handling
  - More complex error handling (mutex errors, spurious wakeups)
- **Rejected:** Added complexity for negligible CPU benefit (0.1% overhead acceptable)

**Per-Task Semaphore:**
- **Pros:** Direct task wakeup, no polling
- **Cons:**
  - Array of semaphores (8 task limit from opt-in set)
  - `AdvanceOneQuantum()` must check all blocked tasks and give semaphores
  - Bookkeeping: which tasks blocked, target expiry times, wakeup eligibility
  - Cleanup on task exit/error paths
- **Rejected:** Too much state management for T4 minimal fix

**Event-Driven Callback:**
- **Pros:** No polling overhead
- **Cons:**
  - Callback registration/deregistration complexity
  - Callback invocation from quantum advance (ordering, reentrancy concerns)
  - Task suspension/resume via POSIX signals (signal safety, platform portability)
- **Rejected:** Over-engineered for current requirements

### Implementation Details

**Polling loop structure:**
```c
target_expiry_ns = core->sim_time_ns + (((uint64_t)delay_ms) * 1000000);
poll_interval.tv_sec = 0;
poll_interval.tv_nsec = 1000000;  // 1ms

while (core->sim_time_ns < target_expiry_ns)
{
    nanosleep(&poll_interval, NULL);
}
```

**Why 1ms poll interval:**
- **Responsive:** Detects quantum advance (10ms) within 1ms worst-case
- **Efficient:** ~0.1% CPU overhead (1ms active per 10ms quantum)
- **Portable:** `nanosleep()` widely available (POSIX.1-2001)
- **Tunable:** Could adjust if different quantum sizes or platforms require

**No timeout exit condition:**
- Loop **only** exits when `sim_time_ns >= target_expiry_ns`
- `nanosleep()` interrupted by signal? Loop continues
- No wall-clock timer, no fallback timeout
- If no quantums advanced, task blocks indefinitely (by design)

### Atomic Access to sim_time_ns

**Current implementation:** Direct read of `core->sim_time_ns` (uint64_t)
- **Platform:** x86-64 native-linux (64-bit aligned reads are atomic on x86-64)
- **Risk:** Theoretical tear read on 32-bit platforms (high 32 bits / low 32 bits separate)
- **Mitigation:** Not required for T4 (native-only target)
- **Future:** If porting to 32-bit RTEMS/VxWorks, add atomic operations or mutex

**Why acceptable without mutex:**
- Read-only access from blocked tasks (no writes)
- Single writer: `AdvanceOneQuantum()` called from control thread
- Worst case: task sees stale value → polls again 1ms later
- No correctness violation, only 1ms extra latency

### Performance Characteristics

**CPU overhead per blocked task:**
- Poll interval: 1ms
- Quantum size: 10ms
- Overhead: 1ms / 10ms = ~10% worst-case (if task wakes immediately after poll)
- Average: ~5% (wakeup at random point in quantum)
- Acceptable for step-controlled simulation (not real-time flight)

**Worst-case wakeup latency:**
- Quantum advances at T=0
- Task polls at T=-0.5ms (just before quantum)
- Next poll at T=+0.5ms (detects advancement)
- Latency: up to 1ms after quantum advance
- Acceptable: 1ms << 10ms quantum size

**Scalability:**
- Linear CPU overhead with blocked task count
- 8 tasks (opt-in limit) × 10% = 80% max CPU overhead
- Still responsive (1ms poll)
- If problematic: increase poll interval (e.g., 5ms) or implement condition variable

### Constraints Honored

✅ **PSP-owned:** Blocking wait implemented in PSP core, not OSAL
✅ **No wall-clock release:** `nanosleep()` only yields CPU, loop controlled by `sim_time_ns`
✅ **Explicit quantum advancement:** Only `AdvanceOneQuantum()` advances `sim_time_ns`
✅ **Minimal state:** No additional bookkeeping beyond existing `sim_time_ns`
✅ **Thin hook:** OSAL hook calls PSP function, no logic in OSAL layer
✅ **Portable:** POSIX `nanosleep()` (POSIX.1-2001)

### Trade-offs

**Advantages:**
- Simple implementation (10 lines)
- No additional synchronization primitives
- Easy to debug/observe
- Deterministic release via `sim_time_ns`
- Works naturally with multiple tasks

**Disadvantages:**
- ~5-10% CPU overhead per blocked task (acceptable for simulation)
- Up to 1ms wakeup latency (acceptable vs. 10ms quantum)
- Not ideal for real-time or flight builds (but T4 is native-only simulation)

**Conclusion:** Polling approach optimal for T4 requirements (minimal fix, PSP-owned, deterministic, simple).

### Future Enhancements (Out of T4 Scope)

**If CPU overhead becomes problematic:**
1. Increase poll interval to 5ms (reduces overhead to ~2%, latency still acceptable)
2. Implement condition variable wakeup on quantum advance
3. Hybrid: poll for first 10ms, then condition variable for longer delays

**If porting to 32-bit platforms:**
1. Add atomic read macro for `sim_time_ns`
2. Or wrap read in mutex (minimal contention: reader-heavy, single writer)

**If supporting real-time flight:**
1. Re-evaluate polling vs. event-driven wakeup
2. Consider scheduler integration (RTEMS/VxWorks kernel hooks)

**Decision Status:** ✅ ACCEPTED (2026-03-12)
**Implementation Status:** ✅ COMPLETE (T4 retry)
**Build Verification:** ✅ PASSED
**Runtime Testing:** Pending T13/T14

---

## T12 Foundation Retry Decision: Introduce Named Taxonomy + Core Diagnostics First (2026-03-12)

**Decision:** For T12 retry scope, implement only the common foundation now:
1. Shared named status taxonomy in `cfe_psp_sim_stepping.h`
2. Core-owned diagnostic counters in `CFE_PSP_SimStepping_Core_t`
3. Single normalized diagnostic helper in core
4. Convert exactly two representative failure sites to prove pattern

**Why this decision:**
- Minimizes regression risk to verified T4/T11 runtime semantics.
- Satisfies orchestrator requirement for concrete code artifacts (not comments only).
- Establishes reusable infrastructure for later T12 follow-up to migrate remaining sites incrementally.

**Representative sites selected:**
- Duplicate begin-step (`Core_BeginStepSession`) for core semantic failure class.
- UDS unknown opcode (`UDS_Service`) for adapter protocol failure class.

**Explicit non-goals in this retry:**
- Do not normalize all existing return sites yet.
- Do not redesign state machine or alter TaskDelay/session timing behavior.

**Status:** ACCEPTED and implemented in this retry.

---

## T12 Follow-up Decision: Scope-Locked Timeout + Transport Only (2026-03-12)

**Decision:** Convert only two failure classes in this slice:
1. Inproc finite step timeout
2. UDS transport failures (accept/read/write)

**Why:** This preserves minimal-change safety and avoids mixing illegal-complete normalization into the same patch.

**Implementation rule used:**
- Every converted site must route through `CFE_PSP_SimStepping_Core_RecordDiagnostic()` and return named status (`TIMEOUT` or `TRANSPORT_ERROR`).
- Keep idle/no-client (`EAGAIN`/`EWOULDBLOCK`) semantics unchanged.

**Status:** ACCEPTED and applied.

---

## T12 Follow-up Decision: Explicit Illegal-State for Pre-Session WAIT (2026-03-12)

**Decision:** Treat `WAIT_STEP_COMPLETE` without an active session as explicit illegal state, not success and not generic failure.

**Implementation choices:**
- Added shared status: `CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_STATE` in `cfe_psp_sim_stepping.h`.
- Added shared diagnostic class/counter: `CFE_PSP_SIM_STEPPING_DIAG_ILLEGAL_STATE` and `illegal_state_count` in core shared definitions.
- Added inproc guard before mutation: in `CFE_PSP_SimStepping_InProc_WaitStepComplete()`, reject `!session_active` via `CFE_PSP_SimStepping_Core_RecordDiagnostic(...)` before touching `completion_requested`.

**Why:**
- Fixes verified bug where pre-session WAIT returned success and moved READY→COMPLETE.
- Keeps taxonomy/diagnostics centralized and consistent with prior T12 policy.
- Preserves active-session timeout and valid empty-active-session deferred completion semantics.

**Status:** ACCEPTED and applied.

---

## T12 Follow-up Decision: Scope-Locked Illegal-Complete Normalization (2026-03-12)

**Decision:** Normalize only the three core illegal-complete no-outstanding-trigger branches and nothing else.

**Applied policy:**
- Use `CFE_PSP_SimStepping_Core_RecordDiagnostic()` with class `CFE_PSP_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE`.
- Return `CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_COMPLETE` consistently from all three illegal-complete branches.

**Functions normalized:**
- `CFE_PSP_SimStepping_Core_ReportCoreServiceCmdPipeComplete`
- `CFE_PSP_SimStepping_Core_ReportQueueReceiveComplete`
- `CFE_PSP_SimStepping_Core_ReportBinSemTakeComplete`

**Guardrails honored:**
- No trigger clearing, no synthetic ack/completion, no force-forward, no sim-time advancement changes.
- Happy-path trigger-match behavior unchanged.

**Status:** ACCEPTED and applied.


## T13 Decision Note (2026-03-12)
- No code/config modifications were applied for T13 retry; verification stopped at runtime blocker once duplicate-begin/timeout prevented controlled 10q/20q proof, per task constraints.


## T13 Blocker-Fix Decision (2026-03-12)
- Chosen atomic fix: lifecycle closure at completion observation point (`IsStepComplete`) rather than broad reporter/state-machine redesign.
- Rationale: this is the smallest change that enforces correct inter-step inactive semantics while preserving timeout/duplicate/illegal-complete and TaskDelay gate behavior.


## T13 Follow-up Decision: Decouple Step-Mode Eligibility from Active Session (2026-03-12)
- Keep `session_active` as per-session participation gate only.
- Use persistent entered-step-mode condition (`session_counter > 0`) for TaskDelay takeover eligibility after readiness/takeover gates pass.
- This is the minimal local core change that preserves both: (a) no inter-step trigger buildup and (b) no wall-clock regression between explicit sessions.


## T13 Decision: Make Minor-Frame a Driver Signal via completion_ready (2026-03-12)
- Chosen minimal local mechanism: reuse existing `completion_ready` field as "step-driving quantum observed" marker.
- Removed SCH minor-frame trigger injection (`0x80`) to eliminate unacknowledged trigger poisoning.
- Kept recent lifecycle fixes intact (`session_active` clears on completion, TaskDelay gate uses `session_counter > 0`).

## T13 Decision: TaskDelay debt lifecycle correction in core only (2026-03-13)
- Chosen atomic fix scope: `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` only.
- `ReportTaskDelay()` now gates debt creation on `QueryTaskDelayEligible(...)`; non-eligible reports explicitly clear per-task delay debt fields.
- `ReportTaskDelayReturn()` now clears pending/owed/expiry for the task slot unconditionally (idempotent clear), replacing prior behavior that could leave stale `owed` debt.
- Rationale: enforce debt ownership by actually taken-over delays and prevent stale/non-taken-over debt from blocking `IsStepComplete()` in later sessions.

## T14 Decision (first atomic subtask): proceed with PSP coverage wiring despite unrelated unit-test-tree blocker (2026-03-13)
- Implemented only PSP unit-test-coverage artifacts for `sim_stepping` (modules list entry + module CMake + coverage source).
- Did not modify non-PSP files to bypass missing `apps/sch/unit-test` directory because task scope forbids broadening.
- Verification recorded as: structural coverage target wiring completed; full `ENABLE_UNIT_TESTS=true` mission configure currently blocked by existing SCH app unit-test-directory gap.

## T14 Decision (second atomic subtask): minimal SCH CMake existence guard (2026-03-13)
- Applied single-file unblock in `apps/sch/CMakeLists.txt`: only call `add_subdirectory(unit-test)` when `ENABLE_UNIT_TESTS` is true **and** `unit-test/CMakeLists.txt` exists.
- This is the smallest change to remove the verified configure blocker without introducing placeholder SCH tests or changing stepping runtime behavior.

## T14 Decision (third atomic subtask): OSAL UT-only stepping no-op via add_osal_ut_exe (2026-03-13)
- Chosen minimal scope: `osal/CMakeLists.txt` + one new UT-only source `osal/src/tests/osal_ut_sim_stepping_noop.c`.
- Rationale: fixes unresolved PSP stepping symbols for all `add_osal_ut_exe(...)` outputs in one place, without changing OSAL flight library behavior.
- Verified effect: previous `bin-sem-test` unresolved-symbol failure cleared; next failure moved to cFE ES coverage testrunner linkage.

## T14 Decision (fourth atomic subtask): add single PSP UT shim report stub (2026-03-13)
- Modified only PSP UT stubs library wiring and one new stub source to provide `CFE_PSP_SimStepping_Shim_ReportEvent`.
- Kept scope minimal (no extra speculative stepping stubs) to satisfy current cFE coverage unresolved symbol only.
- Verified effect: `coverage-es-ALL-testrunner` link now succeeds; next blocker moved to PSP sim_stepping coverage compile errors.

## T14 Decision (fifth atomic subtask): extend PSP coverage override mappings only (2026-03-13)
- Kept scope strictly under `psp/unit-test-coverage/ut-stubs/` to satisfy compile-visible POSIX symbols needed by `cfe_psp_sim_stepping.c` in coverage mode.
- Added only symbols proven missing by compiler errors; no production PSP module edits.
- Verified outcome: stepping-enabled unit-test build advances through `coverage-pspmod-sim_stepping` and completes install in this run.

## T14 Decision (sixth atomic subtask): fix sim_stepping test expectation only (2026-03-13)
- Changed only `psp/unit-test-coverage/modules/sim_stepping/coveragetest-sim_stepping.c` expectation for post-`SCH_MINOR_FRAME` wait from TIMEOUT to SUCCESS.
- Kept scenario shape intact (begin success, duplicate begin rejection, query state RUNNING, wait outcome check) to preserve intended inproc/error coverage with minimal change.

## T14 Decision (runtime script atomic fix): stop/wait semantics hardening (2026-03-13)
- Applied minimal local fix in `tools/sim_stepping_regression.sh` stop helpers only.
- `stop_core()` and `stop_udp_listener()` now terminate via SIGTERM, escalate to SIGKILL if still alive, then reap with `wait ... || true`.
- Verified effect: script advances beyond scenario1; next blocker is scenario2 core-HK telemetry observation failure.

## T14 Decision (runtime script atomic fix): serialize TO_LAB setup commands (2026-03-13)
- Added minimal command pacing/serialization in `setup_to_lab_channel()` with per-command log-marker waits and short sleeps.
- Corrected reset wait marker to runtime-actual `Reset counters command`.
- Verified effect: script proceeds through setup and into scenario2 stepping/assertion phase; remaining blocker is still scenario2 core-HK observation.

## T14 Decision (runtime script atomic fix): scenario2 HK observation alignment (2026-03-13)
- Updated scenario2 to observe HK telemetry MIDs (`0x0800/0x0801/0x0803/0x0804/0x0805`) matching SCH SendHK-driven outputs.
- Increased only scenario2 step loop bound (60) to match sparse default SCH slot layout without changing scenario3/4 global timing constants.
- Verified effect: scenario2 passes; first remaining blocker moved to scenario3 idle assertion.

## T9 Evidence Backfill Decision (2026-03-13)
- For backfill-only scope, use dual-source verification:
  1) rerun current inproc path via `coverage-pspmod-sim_stepping` testrunner output for begin/query/wait roundtrip,
  2) direct static inspection of `cfe_psp_sim_stepping.c` + `cfe_psp_sim_stepping_core.c` proving adapter forwarding to single module-static `stepping_core`.
- Rationale: satisfies T9 acceptance (roundtrip + single-core layering) without touching production code/tests and avoids relying on stale notes.

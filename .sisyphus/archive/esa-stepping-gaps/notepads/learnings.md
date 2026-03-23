# esa-stepping-gaps 学习记录

## 初始化

- 待补充。
# ESA Stepping Gaps — Findings (automated append)

Summary of inspected files and exact references (auto-generated).

Declarations (header):
- /workspace/cFS/osal/src/os/posix/inc/os-posix-stepping.h:54  OS_PosixStepping_Hook_TaskDelay(uint32_t ms)
- /workspace/cFS/osal/src/os/posix/inc/os-posix-stepping.h:65  OS_PosixStepping_Hook_QueueReceive(void)
- /workspace/cFS/osal/src/os/posix/inc/os-posix-stepping.h:76  OS_PosixStepping_Hook_BinSemTake(void)

Implementations (posix):
- /workspace/cFS/osal/src/os/posix/src/os-posix-stepping.c:48  void OS_PosixStepping_Hook_TaskDelay(uint32_t ms) — stub (no-op)
- /workspace/cFS/osal/src/os/posix/src/os-posix-stepping.c:60  void OS_PosixStepping_Hook_QueueReceive(void) — stub (no-op)
- /workspace/cFS/osal/src/os/posix/src/os-posix-stepping.c:71  void OS_PosixStepping_Hook_BinSemTake(void) — stub (no-op)

Pre-blocking callsites (where the hooks are invoked):
- /workspace/cFS/osal/src/os/posix/src/os-impl-tasks.c:733  #ifdef CFE_SIM_STEPPING -> OS_PosixStepping_Hook_TaskDelay(millisecond);
  Blocking call: clock_nanosleep at /workspace/cFS/osal/src/os/posix/src/os-impl-tasks.c:749-751
  Return paths: status != 0 -> return OS_ERROR (/workspace/cFS/osal/src/os/posix/src/os-impl-tasks.c:753-756); else OS_SUCCESS (758-760).

- /workspace/cFS/osal/src/os/posix/src/os-impl-queues.c:198  #ifdef CFE_SIM_STEPPING -> OS_PosixStepping_Hook_QueueReceive();
  Blocking calls: mq_receive at /workspace/cFS/osal/src/os/posix/src/os-impl-queues.c:214; mq_timedreceive at /workspace/cFS/osal/src/os/posix/src/os-impl-queues.c:243
  Return paths (on failure): sizeCopied == -1 -> maps errno to OS_QUEUE_INVALID_SIZE (EMSGSIZE) or OS_ERROR/OS_QUEUE_EMPTY/OS_QUEUE_TIMEOUT at /workspace/cFS/osal/src/os/posix/src/os-impl-queues.c:249-273
  Success path: sizeCopied >=0 -> set *size_copied and return OS_SUCCESS (/workspace/cFS/osal/src/os/posix/src/os-impl-queues.c:277-279).

- /workspace/cFS/osal/src/os/posix/src/os-impl-binsem.c:371  #ifdef CFE_SIM_STEPPING -> OS_PosixStepping_Hook_BinSemTake();
  Blocking calls: pthread_cond_wait at /workspace/cFS/osal/src/os/posix/src/os-impl-binsem.c:420; pthread_cond_timedwait at /workspace/cFS/osal/src/os/posix/src/os-impl-binsem.c:422
  Return paths: timedwait ETIMEDOUT -> return_code = OS_SEM_TIMEOUT (/workspace/cFS/osal/src/os/posix/src/os-impl-binsem.c:423-426); otherwise on success set sem->current_value=0 (/workspace/cFS/osal/src/os/posix/src/os-impl-binsem.c:429-433) and function returns return_code at /workspace/cFS/osal/src/os/posix/src/os-impl-binsem.c:442.

Scope at pre-blocking hook sites (are task_id / token / timeout in scope):
- TaskDelay (os-impl-tasks.c:728-736): parameter 'millisecond' is in scope and is passed to the hook; no OS token or task_id variable is available in this function scope.
- QueueGet (os-impl-queues.c:189-201): function parameters include 'const OS_object_token_t *token' and 'int32 timeout' — they are in scope at the hook call site but NOT passed; the hook signature has no parameters.
- BinSemTake (os-impl-binsem.c:363-374): function parameters include 'const OS_object_token_t *token' and 'const struct timespec *timeout' (for timed version). They are in scope at the hook call site but NOT passed; the hook has no params.

Notes on completeness and *_Complete hooks:
- Currently only PRE-blocking hooks exist (single no-arg/stub call before the blocking operation). There are NO post-blocking or *_Complete hooks implemented in these files.
- Therefore any success or error return paths from the blocking calls do NOT invoke any stepping hook after the operation. This means a stepping implementation cannot learn whether the pend succeeded/failed/timeout unless the stepping hook is extended or post-hooks added.

CFE_SIM_STEPPING guard placement (must preserve):
- Hook declarations are in header without #ifdef, but implementations in os-posix-stepping.c are inside #ifdef CFE_SIM_STEPPING (src: /workspace/cFS/osal/src/os/posix/src/os-posix-stepping.c:30-76).
- Each hook callsite is wrapped with #ifdef CFE_SIM_STEPPING in callers: tasks.c (733-735), queues.c (198-200), binsem.c (371-373). T1 must preserve these guards exactly at these call points.

Reference shim pattern already present:
- os-posix-stepping.c includes cfe_psp_sim_stepping_shim.h and cfe_psp_sim_stepping.h (/workspace/cFS/osal/src/os/posix/src/os-posix-stepping.c:33-34). This indicates an existing thin-shim forwarding pattern: stepping hook impls are stubs that forward to a PSP/CFE shim layer when CFE_SIM_STEPPING is enabled.

Actionable gaps for T1/T4/T5/T6 (concise):
1) No post-blocking hooks exist — add *_Complete hooks or change callers to invoke post-hooks on all exit paths (success, timeout, error) to guarantee the stepping layer sees completion.
2) Current pre-hooks have no arguments — if stepping requires context (task id, token, timeout), either pass parameters or ensure stepping layer can query them reliably (but no such query exists today).
3) Preserve #ifdef guards at exact locations; stepping hooks are NO-OP in production builds.

End of automated findings.

## T1 实施补充（本次追加）

- `os-posix-stepping.h` 已按计划扩展 3 个 pre-hook 签名，并新增 3 个 `_Complete` 声明：
  - `OS_PosixStepping_Hook_TaskDelay(uint32_t ms, osal_id_t task_id)`
  - `OS_PosixStepping_Hook_TaskDelay_Complete(uint32_t ms, osal_id_t task_id)`
  - `OS_PosixStepping_Hook_QueueReceive(const OS_object_token_t *token, int32 timeout)`
  - `OS_PosixStepping_Hook_QueueReceive_Complete(const OS_object_token_t *token, int32 timeout, int32 return_code)`
  - `OS_PosixStepping_Hook_BinSemTake(const OS_object_token_t *token, const struct timespec *timeout)`
  - `OS_PosixStepping_Hook_BinSemTake_Complete(const OS_object_token_t *token, const struct timespec *timeout, int32 return_code)`
- `os-posix-stepping.c` 对应新增 `_Complete` 空实现，所有新旧 hook 仍为 no-op，无业务逻辑、无转发逻辑。
- `os-impl-tasks.c`：
  - pre-hook 传入 `OS_TaskGetId_Impl()` 获取的 `task_id`。
  - 在 `status != 0` 与 `status == 0` 两条返回路径都调用 `TaskDelay_Complete`，且都在 `#ifdef CFE_SIM_STEPPING` 内。
- `os-impl-queues.c`：
  - pre-hook 传入现成作用域参数 `token/timeout`。
  - 在统一 `return return_code;` 前调用 `QueueReceive_Complete(token, timeout, return_code)`，覆盖成功/失败映射所有路径。
- `os-impl-binsem.c`：
  - pre-hook 传入现成 `token/timeout`。
  - 早返回路径（互斥获取失败）先调用 `BinSemTake_Complete(..., OS_SEM_FAILURE)` 再返回。
  - 常规退出路径在 `pthread_cleanup_pop(true)` 后调用 `BinSemTake_Complete(..., return_code)`。
- 两套构建验证通过：
  - `make SIMULATION=native CFE_SIM_STEPPING=1 prep && make`
  - `make distclean && make SIMULATION=native prep && make`

## T2 SERVICE_ID 常量实施（本次追加）

- 四个核心服务任务文件中已添加 `#ifdef CFE_SIM_STEPPING` 保护的 SERVICE_ID 常量定义：
  - `cfe/modules/es/fsw/src/cfe_es_task.c`: 已修复 ES SERVICE_ID 从 `0x01` → `0`
  - `cfe/modules/evs/fsw/src/cfe_evs_task.c`: EVS SERVICE_ID = `1`
  - `cfe/modules/sb/fsw/src/cfe_sb_task.c`: SB SERVICE_ID = `2`
  - `cfe/modules/tbl/fsw/src/cfe_tbl_task.c`: TBL SERVICE_ID = `3`
- 所有四个文件同时添加了 `#include "cfe_psp_sim_stepping_shim.h"` 在 `#ifdef CFE_SIM_STEPPING` 内，位于包括标准头的之后。
- 常量值严格遵循 `esa/fsw/inc/cfe_psp_sim_stepping_core.h` 中的位映射约定：ES bit0, EVS bit1, SB bit2, TBL bit3。
- 构建验证通过（stepping 构建）：`make SIMULATION=native CFE_SIM_STEPPING=1 prep && make` 成功，所有四个模块编译无错误。

## T3 TIME Stepping 事件类型确认 + 常量实施（本次追加）

### 验证工作完成

1. **三个 TIME 事件类型常量已存在于 shim 头部**（`esa/public_inc/cfe_psp_sim_stepping_shim.h`）：
   - `CFE_PSP_SIM_STEPPING_EVENT_TIME_TASK_CYCLE` (line 59)
   - `CFE_PSP_SIM_STEPPING_EVENT_1HZ_BOUNDARY` (line 60)
   - `CFE_PSP_SIM_STEPPING_EVENT_TONE_SIGNAL` (line 61)

2. **Shim 转发器已实现所有三个 TIME 事件的处理**（`esa/fsw/src/cfe_psp_sim_stepping.c`）：
   - Case for `CFE_PSP_SIM_STEPPING_EVENT_TIME_TASK_CYCLE` → `CFE_PSP_SimStepping_Core_ReportTimeTaskCycle()` (line 244-246)
   - Case for `CFE_PSP_SIM_STEPPING_EVENT_1HZ_BOUNDARY` → `CFE_PSP_SimStepping_Core_Report1HzBoundary()` (line 248-250)
   - Case for `CFE_PSP_SIM_STEPPING_EVENT_TONE_SIGNAL` → `CFE_PSP_SimStepping_Core_ReportToneSignal()` (line 252-254)

3. **TIME 模块编译单元现可包含 shim 头部**（`cfe/modules/time/fsw/src/cfe_time_stepping.c`）：
   - 已添加 `#include "cfe_psp_sim_stepping_shim.h"` 于 `#ifdef CFE_SIM_STEPPING` 内 (line 32)
   - 包含位置：在 `#ifdef CFE_SIM_STEPPING` 保护内、在 stepping hook 实现前

### 构建验证

- `make SIMULATION=native CFE_SIM_STEPPING=1 prep && make` ✓ 通过
- `make distclean && make SIMULATION=native prep && make` ✓ 通过（无 stepping）
- 所有模块编译成功，无错误

### 关键发现

- 三个 TIME 事件类型在 shim 中以枚举形式定义，与其他服务（OSAL TASK_DELAY、QUEUE_RECEIVE、BINSEM_TAKE 等）统一管理
- Shim 转发器采用统一的 switch-case 分发模式，每个事件类型对应一个核心报告函数
- TIME stepping 头文件已定义三个 hook 声明（任务循环、1Hz 边界、tone 信号），实现为 no-op 存根
- T9 可在此基础上扩展 hook 实现，调用 shim forwarder 或直接与核心交互

## T4 TaskDelay 双阶段 shim 转发实施（本次追加）

- 在 `esa/public_inc/cfe_psp_sim_stepping_shim.h` 中，已紧跟 legacy `CFE_PSP_SIM_STEPPING_EVENT_TASK_DELAY` 新增：
  - `CFE_PSP_SIM_STEPPING_EVENT_TASK_DELAY_ACK`
  - `CFE_PSP_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE`
- 在 `esa/fsw/src/cfe_psp_sim_stepping.c` 的 unified shim switch 中新增两条分发：
  - `TASK_DELAY_ACK -> CFE_PSP_SimStepping_Core_ReportTaskDelayAck(&stepping_core, event->task_id, event->optional_delay_ms)`
  - `TASK_DELAY_COMPLETE -> CFE_PSP_SimStepping_Core_ReportTaskDelayComplete(&stepping_core, event->task_id, event->optional_delay_ms)`
- 在 `esa/fsw/inc/cfe_psp_sim_stepping_core.h` 新增两个 core 声明，位置紧邻现有 TaskDelay 相关 API。
- 在 `esa/fsw/src/cfe_psp_sim_stepping_core.c` 新增两个 core 实现，风格对齐 queue/binsem：
  - Ack 路径：`session_active && completion_ready` 时，按 `source_mask=0x100` + `task_id` 去重后 `AddTrigger`
  - Complete 路径：同条件下按 `source_mask=0x100` + `task_id` 执行 `AcknowledgeTrigger`
  - 与既有 queue/binsem 行为一致：未匹配触发器时返回 0，不改变 legacy API 语义
- 在 `osal/src/os/posix/src/os-posix-stepping.c` 将 TaskDelay 两个 hook 从 no-op 改为 thin forwarding（仅构造事件并上报，无等待/调度逻辑）：
  - `OS_PosixStepping_Hook_TaskDelay()` 上报 `TASK_DELAY_ACK`
  - `OS_PosixStepping_Hook_TaskDelay_Complete()` 上报 `TASK_DELAY_COMPLETE`
  - 均设置 `event.task_id=(uint32_t)task_id` 与 `event.optional_delay_ms=ms`
- Queue/BinSem hooks 未改动，符合 T4 单任务边界。

## T4 链接失败修复（ENABLE_UNIT_TESTS + CFE_SIM_STEPPING）（本次追加）

- 根因确认：`osal/src/tests/*` 的 black-box 可执行文件通过 `target_link_libraries(... ut_assert osal)` 仅链接 `libosal.a`，不保证链接到 ESA runtime；因此 `os-posix-stepping.c` 中直接调用 `CFE_PSP_SimStepping_Shim_ReportEvent` 会在链接 `bin-sem-test` 时出现 undefined reference。
- 本次采用最小修复：仅改 `osal/src/os/posix/src/os-posix-stepping.c`，将 shim 符号声明为 weak extern，并在 TaskDelay ACK/COMPLETE 两个 hook 中做符号存在性判定。
  - 有真实 shim 实现时：继续 thin forwarding，行为不变。
  - 无真实 shim 实现时：安全 no-op，避免测试目标链接失败。
- 选择该策略的原因：
  1) 不需要把 ESA runtime 强行注入 OSAL tests（避免改变测试拓扑/引入耦合）；
  2) 不破坏运行时路径（强符号存在时仍走真实 shim）；
  3) 改动面最小，满足“优先只改 os-posix-stepping.c”。

## T5 OSAL QueueReceive Hook 双阶段 Shim 转发实施（本次追加）

- 文件范围保持最小：仅修改 `osal/src/os/posix/src/os-posix-stepping.c`。
- `OS_PosixStepping_Hook_QueueReceive(const OS_object_token_t *token, int32 timeout)` 已从 no-op 改为 thin fact forwarder：
  - 构造栈上零初始化 `CFE_PSP_SimStepping_ShimEvent_t event = {0}`；
  - `event.event_kind = CFE_PSP_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK`；
  - `event.entity_id = (uint32_t)OS_ObjectIdToInteger(OS_ObjectIdFromToken(token))`；
  - `event.task_id = (uint32_t)OS_ObjectIdToInteger(OS_TaskGetId())`；
  - `event.optional_delay_ms = (uint32_t)timeout`；
  - 复用既有 weak-symbol 兼容路径：仅在 `CFE_PSP_SimStepping_Shim_ReportEvent != NULL` 时调用 shim。
- `OS_PosixStepping_Hook_QueueReceive_Complete(const OS_object_token_t *token, int32 timeout, int32 return_code)` 已改为 COMPLETE 事件转发：
  - `event.event_kind = CFE_PSP_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE`；
  - `entity_id/task_id/optional_delay_ms` 提取模式与 ACK 相同；
  - 不根据 `return_code` 分支，始终尝试上报 COMPLETE（随后 `(void)return_code;` 保持参数使用）。
- 为使上述 API 在该编译单元显式可见，按最小原则补充 include：
  - `#include "os-shared-idmap.h"`（`OS_ObjectIdFromToken`）
  - `#include "osapi-idmap.h"`（`OS_ObjectIdToInteger`）
  - `#include "osapi-task.h"`（`OS_TaskGetId`）
- 关键提取模式（精确记录）：
  - `entity_id`：`OS_ObjectIdToInteger(OS_ObjectIdFromToken(token))`
  - `task_id`：`OS_ObjectIdToInteger(OS_TaskGetId())`

## T6 OSAL BinSemTake Hook 双阶段 Shim 转发实施（本次追加）

- 仅修改 `osal/src/os/posix/src/os-posix-stepping.c`，未触及任何 ESA/OSAL 其他产品文件。
- `OS_PosixStepping_Hook_BinSemTake()` 已按薄转发模式上报 `CFE_PSP_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK`：
  - `event.entity_id = (uint32_t)OS_ObjectIdToInteger(OS_ObjectIdFromToken(token))`
  - `event.task_id = (uint32_t)OS_ObjectIdToInteger(OS_TaskGetId())`
  - `event.optional_delay_ms` 保持零初始化默认值（未做 timespec 转换）。
- `OS_PosixStepping_Hook_BinSemTake_Complete()` 已上报 `CFE_PSP_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE`，并复用相同 entity/task 提取模式。
- COMPLETE 上报不依赖 `return_code` 分支：无论成功/超时/失败都尝试发送 COMPLETE；函数末尾保留 `(void)return_code`。
- T4/T5 既有 weak 符号兼容路径保持不变：仅在 `CFE_PSP_SimStepping_Shim_ReportEvent != NULL` 时调用 shim，兼容仅链接 `libosal.a` 的测试目标。

## T7: ES 和 EVS RECEIVE+COMPLETE 集成

**完成时间**: 2026-03-20

### 实施要点

1. **ES 模块** (`cfe/modules/es/fsw/src/cfe_es_task.c`):
   - RECEIVE 事件已存在于 T2 (line 176)
   - 新增 COMPLETE 事件于 line 184，紧跟 `CFE_ES_TaskPipe(SBBufPtr)` 之后、`CFE_ES_BackgroundWakeup()` 之前
   - 使用 `CFE_ES_SERVICE_ID = 0`

2. **EVS 模块** (`cfe/modules/evs/fsw/src/cfe_evs_task.c`):
   - 新增 RECEIVE 事件于 line 244，在 `if (Status == CFE_SUCCESS)` 内、`CFE_EVS_ProcessCommandPacket()` 之前
   - 新增 COMPLETE 事件于 line 253，紧跟 `CFE_EVS_ProcessCommandPacket(SBBufPtr)` 之后
   - 使用 `CFE_EVS_SERVICE_ID = 1`

### 模式一致性

- 两个模块均完全复用 T2 建立的样式：零初始化的 `CFE_PSP_SimStepping_ShimEvent_t` 结构体
- 仅设置 `event_kind` 和 `entity_id` 字段
- 保持了原有循环结构、性能日志、状态处理和唤醒行为
- 所有stepping代码均在 `#ifdef CFE_SIM_STEPPING` 守卫内

### 验证结果

- `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make` ✓ 通过
- `make distclean && make SIMULATION=native prep && make` ✓ 通过
- grep 确认：ES 和 EVS 均有 RECEIVE 和 COMPLETE 事件，且使用正确的 service ID
- 无 TODO/FIXME/HACK/XXX 标记

## T8: SB 和 TBL RECEIVE+COMPLETE 集成

**完成时间**: 2026-03-20

### 实施要点

1. **SB 模块** (`cfe/modules/sb/fsw/src/cfe_sb_task.c`):
   - 新增 RECEIVE 事件于 line 117-121，在 `if (Status == CFE_SUCCESS)` 内、`CFE_SB_ProcessCmdPipePkt(SBBufPtr)` 之前
   - 新增 COMPLETE 事件于 line 123-127，紧跟 `CFE_SB_ProcessCmdPipePkt(SBBufPtr)` 之后
   - 使用 `CFE_SB_SERVICE_ID = 2`

2. **TBL 模块** (`cfe/modules/tbl/fsw/src/cfe_tbl_task.c`):
   - 新增 RECEIVE 事件于 line 99-103，在 `if (Status == CFE_SUCCESS)` 内、`CFE_TBL_TaskPipe(SBBufPtr)` 之前
   - 新增 COMPLETE 事件于 line 105-109，紧跟 `CFE_TBL_TaskPipe(SBBufPtr)` 之后
   - 使用 `CFE_TBL_SERVICE_ID = 3`

### 模式一致性

- 两个模块均完全复用 T7 建立的样式：零初始化的 `CFE_PSP_SimStepping_ShimEvent_t` 结构体
- 仅设置 `event_kind` 和 `entity_id` 字段
- 保持了原有循环结构、性能日志、状态处理和错误行为
- 所有 stepping 代码均在 `#ifdef CFE_SIM_STEPPING` 守卫内
- 与 ES/EVS 模块的实施完全对齐（T7 模式）

### 验证结果

- `make SIMULATION=native CFE_SIM_STEPPING=1 prep && make` ✓ 通过
- `make distclean && make SIMULATION=native prep && make` ✓ 通过
- grep 确认：SB 和 TBL 均有 RECEIVE 和 COMPLETE 事件，且使用正确的 service ID
- 保持了与 ES/EVS 模块相同的 zero-initialize + dual-field 设置模式

### 依赖关系

- T2 已完成：SB/TBL SERVICE_ID 常量与 shim include 已存在
- T7 验证模式可直接复用：零初始化结构体、双字段赋值、分离 ifdef 块
- T14 可依赖 SB/TBL RECEIVE+COMPLETE 事件已正确集成

## T9: TIME 三个 Stepping Hook 真实实现

**完成时间**: 2026-03-20

### 实施要点

1. **文件**: `cfe/modules/time/fsw/src/cfe_time_stepping.c`
   - 修改三个 hook 函数从 no-op 变为 thin shim 转发
   - 新增 `#include "cfe_psp_sim_stepping_core.h"` 以访问 entity_id 常量

2. **三个 Hook 实现**:
   - `CFE_TIME_Stepping_Hook_TaskCycle()`:
     - 事件类型: `CFE_PSP_SIM_STEPPING_EVENT_TIME_TASK_CYCLE`
     - entity_id: `CFE_PSP_SIM_STEPPING_SERVICE_BIT_TIME` (1U << 4)
     - 调用点: TIME 主任务循环开始（`cfe_time_task.c:98`）
   
   - `CFE_TIME_Stepping_Hook_1HzBoundary()`:
     - 事件类型: `CFE_PSP_SIM_STEPPING_EVENT_1HZ_BOUNDARY`
     - entity_id: `CFE_PSP_SIM_STEPPING_CHILDPATH_BIT_TIME_LOCAL_1HZ` (1U << 6)
     - 调用点: 1Hz 状态机更新前（`cfe_time_task.c:414`）
   
   - `CFE_TIME_Stepping_Hook_ToneSignal()`:
     - 事件类型: `CFE_PSP_SIM_STEPPING_EVENT_TONE_SIGNAL`
     - entity_id: `CFE_PSP_SIM_STEPPING_CHILDPATH_BIT_TIME_TONE` (1U << 5)
     - 调用点: tone 信号命令处理函数内（`cfe_time_task.c:367`）

### 模式一致性

- 三个 hook 均完全复用 `apps/sch/fsw/src/sch_stepping.c` 的 thin forwarder 模式：
  ```c
  CFE_PSP_SimStepping_ShimEvent_t event = {0};
  event.event_kind = <对应事件类型>;
  event.entity_id  = <对应实体位>;
  CFE_PSP_SimStepping_Shim_ReportEvent(&event);
  ```
- 零初始化结构体，仅设置两个字段（event_kind + entity_id）
- 无等待逻辑、无调度逻辑、无条件分支，纯粹转发事实
- 所有代码在 `#ifdef CFE_SIM_STEPPING` 守卫内

### 验证结果

- `make SIMULATION=native CFE_SIM_STEPPING=1 prep && make` ✓ 通过
- `make distclean && make SIMULATION=native prep && make` ✓ 通过（无 stepping）
- grep 确认：三个 hook 均有 `CFE_PSP_SimStepping_Shim_ReportEvent` 调用，且使用正确的 event_kind 和 entity_id
- 无功能性或构建问题

### 关键发现

- TIME hook 必须 include `cfe_psp_sim_stepping_core.h` 才能访问 `CFE_PSP_SIM_STEPPING_SERVICE_BIT_TIME` 和 `CFE_PSP_SIM_STEPPING_CHILDPATH_BIT_TIME_*` 常量定义（仅 include shim 头不足）
- T3 验证过 TIME 事件类型和 shim dispatch case 已存在，本任务仅需填充 hook body
- TIME 三个 hook 不使用 `task_id` 或 `optional_delay_ms` 字段（与 OSAL hook 不同），仅依赖 `entity_id` 位映射标识事件来源

### 依赖关系

- T3 完成：TIME 事件枚举和 shim include 已验证
- T15 可依赖本任务的三个 TIME hook 真实实现完成


## T10 兼容别名头文件 + ESA 内部重命名（本次追加）

- 已按 T10 仅在 `esa/` 内完成内部命名迁移（不改外部调用方）：
  - 文件重命名：
    - `esa/fsw/src/cfe_psp_sim_stepping.c` → `esa/fsw/src/esa_stepping.c`
    - `esa/fsw/src/cfe_psp_sim_stepping_core.c` → `esa/fsw/src/esa_stepping_core.c`
    - `esa/fsw/inc/cfe_psp_sim_stepping.h` → `esa/fsw/inc/esa_stepping.h`
    - `esa/fsw/inc/cfe_psp_sim_stepping_core.h` → `esa/fsw/inc/esa_stepping_core.h`
    - `esa/public_inc/cfe_psp_sim_stepping_shim.h` → `esa/public_inc/esa_stepping_shim.h`
    - `esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` → `esa/ut-stubs/src/esa_stepping_shim_stubs.c`
- 新增兼容层：`esa/public_inc/cfe_psp_sim_stepping_compat.h`，集中提供 old→new 别名：
  - 函数前缀：`CFE_PSP_SimStepping_*` → `ESA_Stepping_*`
  - 宏/枚举前缀：`CFE_PSP_SIM_STEPPING_*` → `ESA_SIM_STEPPING_*`
  - 类型别名：`CFE_PSP_SimStepping_*_t` → `ESA_Stepping_*_t`
- 新内部头 `esa_stepping.h` / `esa_stepping_core.h` / `esa_stepping_shim.h` 均包含 compat 头，确保旧名在 T10 期间仍可编译。
- 旧路径转发头已重建（单行 include + 中文 Doxygen `@deprecated`）：
  - `esa/fsw/inc/cfe_psp_sim_stepping.h`
  - `esa/fsw/inc/cfe_psp_sim_stepping_core.h`
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h`
- `esa/CMakeLists.txt` 已迁移到新源文件名，并调整弱符号策略：
  - 默认弱符号导出新名 `ESA_Stepping_*`
  - 同时保留旧名弱符号并转发到新名，保障过渡期链接兼容。
- `esa/ut-stubs/CMakeLists.txt` 和 `esa/ut-coverage/CMakeLists.txt` 已更新为新文件路径。
- 构建验证通过：
  - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make`
  - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make`

## T11 外部调用者重命名（本次追加）

- 本次仅改外部调用面 9 个目标文件，保持机械重命名，不改变控制流/事件语义/位值：
  - `cfe/modules/es/fsw/src/cfe_es_task.c`
  - `cfe/modules/evs/fsw/src/cfe_evs_task.c`
  - `cfe/modules/sb/fsw/src/cfe_sb_task.c`
  - `cfe/modules/tbl/fsw/src/cfe_tbl_task.c`
  - `cfe/modules/time/fsw/src/cfe_time_stepping.c`
  - `osal/src/os/posix/src/os-posix-stepping.c`
  - `apps/sch/fsw/src/sch_stepping.c`
  - `psp/fsw/modules/timebase_posix_clock/cfe_psp_timebase_posix_clock.c`
  - `psp/fsw/modules/soft_timebase/cfe_psp_soft_timebase.c`（仅注释旧名清理）
- include 迁移模式：
  - `cfe_psp_sim_stepping_shim.h` → `esa_stepping_shim.h`
  - `cfe_psp_sim_stepping_core.h` → `esa_stepping_core.h`
  - `cfe_psp_sim_stepping.h` → `esa_stepping.h`
- 符号迁移模式：
  - `CFE_PSP_SimStepping_*` → `ESA_Stepping_*`
  - `CFE_PSP_SIM_STEPPING_*` → `ESA_SIM_STEPPING_*`
- `osal/src/os/posix/src/os-posix-stepping.c` 的 weak extern + NULL guard 已完整保留，仅改为新符号名：
  - weak extern `ESA_Stepping_Shim_ReportEvent(...)`
  - 调用前 `if (ESA_Stepping_Shim_ReportEvent != NULL)` 路径保持不变
- 验收 grep（限定 `cfe/ osal/ psp/ apps/`）结果为 0 命中：
  - `grep -r "CFE_PSP_SimStepping\|CFE_PSP_SIM_STEPPING\|cfe_psp_sim_stepping" --include="*.c" --include="*.h" cfe/ osal/ psp/ apps/`
- stepping 构建链验证通过：
  - `make distclean`
  - `make SIMULATION=native CFE_SIM_STEPPING=1 prep`
  - `make`

## T12: 删除兼容性别名 + 最终清理

**完成时间**: 2026-03-20

### 实施要点

本任务完成了 T10/T11 引入的过渡兼容层的最终移除，确保 ESA stepping 内部接口完全使用新名称。

#### 1. 移除兼容层 includes（三个新内部头）

- `esa/fsw/inc/esa_stepping.h`: 删除 `#include "cfe_psp_sim_stepping_compat.h"`
- `esa/fsw/inc/esa_stepping_core.h`: 删除 `#include "cfe_psp_sim_stepping_compat.h"`
- `esa/public_inc/esa_stepping_shim.h`: 删除 `#include "cfe_psp_sim_stepping_compat.h"`

#### 2. 覆盖测试文件迁移

- `esa/ut-coverage/coveragetest-sim_stepping.c`:
  - Include 迁移：`cfe_psp_sim_stepping*.h` → `esa_stepping*.h`
  - 符号迁移（机械替换）：
    - `CFE_PSP_SIM_STEPPING_STATUS_*` → `ESA_SIM_STEPPING_STATUS_*`
    - `CFE_PSP_SIM_STEPPING_STATE_*` → `ESA_SIM_STEPPING_STATE_*`
    - `CFE_PSP_SIM_STEPPING_EVENT_*` → `ESA_SIM_STEPPING_EVENT_*`
    - `CFE_PSP_SimStepping_*` → `ESA_Stepping_*`

#### 3. CMakeLists.txt 弱符号清理

- `esa/CMakeLists.txt` 的 ut_coverage_shim_stub 生成块：
  - 移除旧名弱符号转发（lines 74-85）：
    - `CFE_PSP_SimStepping_Shim_ReportEvent` → no-op（旧转发已删除）
    - `CFE_PSP_SimStepping_Hook_TaskDelayEligible` → no-op（旧转发已删除）
    - `CFE_PSP_SimStepping_WaitForDelayExpiry` → no-op（旧转发已删除）
  - 保留新名弱符号（lines 57-73）供覆盖率测试使用

#### 4. 头文件删除

- 删除四个过渡/兼容头：
  - `esa/fsw/inc/cfe_psp_sim_stepping.h`（旧路径转发）
  - `esa/fsw/inc/cfe_psp_sim_stepping_core.h`（旧路径转发）
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h`（旧路径转发）
  - `esa/public_inc/cfe_psp_sim_stepping_compat.h`（兼容别名）

#### 5. 清理验证

- ESA 目录内旧名检查：`grep -r "CFE_PSP_SimStepping|CFE_PSP_SIM_STEPPING|cfe_psp_sim_stepping" esa/` = 0 匹配
- 全仓库旧名检查（含 cfe/osal/psp/apps）：0 匹配（T11 已完成外部迁移）
- 最终完整全仓库检查：0 匹配

### 构建验证

三套完整构建均通过：

1. **stepping 构建 + 单元测试**:
   - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make && make install`
   - ✓ 成功（所有测试编译、链接通过）

2. **stepping 构建**（无单元测试）:
   - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make`
   - ✓ 成功（ESA stepping 库编译通过）

3. **正常构建**（无 stepping）:
   - `make distclean && make SIMULATION=native prep && make`
   - ✓ 成功（接口库构建通过）

### 清理成果

- ESA 内部接口完全统一使用新名称 `ESA_Stepping_*` 和 `ESA_SIM_STEPPING_*`
- 所有兼容层（compat 头、旧路径转发、旧名弱符号）已移除
- 代码库中零残留的旧名引用
- 构建系统已完全清理，无额外的兼容转发逻辑

### 后续依赖

- T13-T15（测试相关任务）现可依赖干净的 ESA 接口
- 无外部调用需要进一步迁移（T11 已处理）
- 不再需要在构建时提供 compat 别名或弱符号转发


## 术语统一修正返工（本次追加）

### 修正文件列表
- `esa/fsw/inc/esa_stepping.h`
  - Line 27: "at the ESA level" → "within the External Simulation Adapter"
  - Line 65: "Initialize ESA stepping module" → "Initialize the External Simulation Adapter (ESA) stepping module"
  - Line 124: "PSP-owned" → "ESA-owned"
- `esa/fsw/inc/esa_stepping_core.h`
  - Line 51: "PSP-LOCAL" → "ESA-LOCAL"
  - Line 436: "PSP-owned" → "ESA-owned"
  - Line 553, 572: "internal PSP API" → "internal ESA API"
- `osal/src/bsp/generic-linux/src/bsp_start.c`
  - Line 234: "Initialize ESA stepping module" → "Initialize the External Simulation Adapter (ESA) stepping module"

### 统一口径
- 中文场景：`外部仿真适配器（ESA）`
- 英文场景：`External Simulation Adapter (ESA)` 或简称 `ESA`
- 模块归属描述：`ESA-owned`, `ESA-LOCAL`, `internal ESA API`

### 验证结果
`grep -nE "PSP-LOCAL|PSP-owned|internal PSP API|ESA level|Initialize ESA stepping module|Event-Step-Advance"` 在目标文件中无命中。

## T13 entity_id 精确断言修复（追加）
- QueueReceive/BinSemTake 测试采用确定性 ID 策略：直接设置 `OS_object_token_t token` 的 `token.obj_id = OS_ObjectIdFromInteger(<固定值>)`，使 `OS_ObjectIdToInteger(OS_ObjectIdFromToken(token))` 返回可预测对象 ID。
- task_id 采用确定性桩返回：`UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), <固定值>)`，并以 `ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId())` 计算期望值后做精确断言。
- 结果：Queue/BinSem 的 ACK/COMPLETE 相关测试均改为显式断言 `entity_id` 精确值，同时保留 `event_kind`、`task_id` 与调用计数断言。

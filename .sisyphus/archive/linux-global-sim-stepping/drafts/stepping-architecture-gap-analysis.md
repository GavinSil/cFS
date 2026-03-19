# CFE_SIM_STEPPING 架构差距分析

> **目标**: 将 cFS 改造为"外部仿真引擎是全系统唯一全局时间推进源"的模式
> **基准**: 当前 `CFE_SIM_STEPPING` 实现（linux-global-sim-stepping 计划 F1-F4 已通过）
> **日期**: 2026-03-17

---

## 执行摘要

当前 `CFE_SIM_STEPPING` 实现已完成了 **基础架构搭建**（统一 sim-time 读取路径、inproc+UDS 控制通道、hook/shim 架构），但距离"外部引擎唯一推进源"的目标仍存在 **12 项架构差距**（原始分析 7 项 + 代码审查新发现 5 项）。核心问题是：系统内部仍保留多条自主推进路径（SCH 本地 timer、TIME 自主 1Hz/tone、OSAL wall-clock fallback），stepping core 只是"叠加"在这些路径之上，而非真正"替代"它们。

**代码验证状态**: 全部 7 项原始差距已通过源码逐行验证，确认描述准确（差距 4/5/7 的措辞已精确化）。5 项新发现差距来自对 `cfe_time_tone.c` 回调链、`issues.md` 运行时记录、OSAL timer 机制和并发安全的交叉分析。

**2026-03-17 更新**: shared 层约束已移除。新增**统一 Shared 层 Stepping Shim 方案**，可同时解决差距 6（TaskDelay）、9（QueueGet 超时）、10（TimeBase tick 门控）。差距 9 从"已知限制"升级为"可解决"，差距 10 从"评估可行性"升级为"可实现"。差距 6/9/10 均纳入实施波次。CFE_SB_ReceiveBuffer 完整超时链路已追踪并记入差距 9 证据。

---

## 分层差距分析

### 差距 1: SCH 回调仍自驱动（严重度: 🔴 Critical）

**现状**: stepping 模式下 `SCH_CustomLateInit()` 仍调用 `OS_TimerSet()` 启动本地 timer，`SCH_MinorFrameCallback()` 仍直接 `OS_BinSemGive()` 唤醒 SCH 主循环。

**问题**: SCH 并非由外部引擎"推一步走一步"，而是本地 timer 自主触发，stepping core 只是在旁边记录。外部引擎的 `BeginStepSession()` 开始 session 后，SCH 仍可能因本地 timer 而自行前进。

**目标状态**: stepping 模式下，SCH minor frame callback 的 `OS_BinSemGive()` 应由 stepping core 授权，而非本地 timer 直接给出。

**涉及文件**:
- `apps/sch/fsw/src/sch_custom.c` — `SCH_MinorFrameCallback()`, `SCH_CustomLateInit()`
- `apps/sch/fsw/src/sch_stepping.c` — stepping 集成层
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` — 授权逻辑

---

### 差距 2: Quantum 推进所有权未统一（严重度: 🔴 Critical）

**现状**: 实际的 quantum（仿真时间步长）推进发生在 `ReportSchMinorFrame()` 中。`BeginStepSession()` 只是开启 session 状态，但不直接驱动时间前进。

**问题**: quantum 推进绑定在 SCH 的次帧报告上，意味着：
- 如果 SCH 没有报告次帧，时间不会前进
- 如果有多个需要推进的实体，它们之间没有统一的推进点
- 外部引擎"推一步"并不直接等同于"时间前进一个 quantum"

**目标状态**: `BeginStepSession()` 或专用的 `AdvanceQuantum()` 应是唯一推进时间的入口。SCH 次帧报告变为该推进的"消费者"，而非"驱动者"。

**涉及文件**:
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` — `BeginStepSession()`, `ReportSchMinorFrame()`
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.h` — API 定义

---

### 差距 3: TIME 保留自主推进路径（严重度: 🟠 High）

**现状**: `cfe_time_task.c` 中 `OS_TimerAdd()`/`OS_TimerSet()` 创建独立 timer；`cfe_time_tone.c` 中 `OS_BinSemGive(ToneSemaphore/LocalSemaphore)` 自主唤醒 TIME 子任务。

**问题**: TIME 模块有独立的 1Hz tone 和 local 时钟推进路径，在 stepping 模式下这些路径不受外部引擎控制。即使 stepping core 暂停了 SCH，TIME 仍可能独立推进，造成时间源不一致。

**目标状态**: stepping 模式下，TIME 的 1Hz/tone 路径应降级为"纯报告"模式。TIME 的时间推进应完全依赖 stepping core 提供的 sim-time，而非自主 timer。

**涉及文件**:
- `cfe/modules/time/fsw/src/cfe_time_task.c` — timer 创建
- `cfe/modules/time/fsw/src/cfe_time_tone.c` — tone/local semaphore
- `cfe/modules/time/fsw/src/cfe_time_stepping.c` — stepping 集成层（已存在但未完整接线）

---

### 差距 4: Completion 语义偏 SCH-centric（严重度: 🟠 High）

**现状**: `completion_ready` 标志由 `ReportSchMinorFrame()` (L510) 置位。stepping core 已具备多实体 trigger/ack/complete 追踪机制（source masks: `0x02` QueueReceive, `0x200` QueueReceiveAck, `0x800` BinSemAck, `0x2000` SchSendTrigger, `0x8000` CoreServiceCmdPipe, `0x10000` TIME Tone, `0x20000` TIME Local1Hz），`IsStepComplete()` 也会检查 wait-set 中所有 triggered 实体的 ack+complete 状态。

**问题**: 虽然 completion **判断逻辑**已支持多实体，但 completion 的 **启动门控**（`completion_ready`）仍由 SCH 单点触发。这意味着：
- `IsStepComplete()` 的多实体等待仅在 `completion_ready=true` 后才生效
- 如果 SCH 未报告次帧，`completion_ready` 永远为 false，其他实体的 ack/complete 无从参与
- 对外部引擎而言，"一步是否完成"的启动条件绑定在 SCH 上，而非全局

**精确区分**: completion 的"启动条件"（gate）绑定在 SCH 上，而非"完成条件"（convergence）。后者已有 wait-set 机制支撑。

**目标状态**: completion 的启动门控应与 quantum 推进统一（见差距 2），而非依赖 SCH 的 `ReportSchMinorFrame()` 作为唯一触发。wait-set 全局收敛机制应直接由 `BeginStepSession()` 激活。

**涉及文件**:
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` — `completion_ready` 门控逻辑、`IsStepComplete()`
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.h` — wait-set API

---

### 差距 5: TIME 子路径未完整纳入 Completion（严重度: 🟡 Medium）

**现状**: stepping core 中 `ReportTimeToneSemConsume()` (L662-684) 和 `ReportTimeLocal1HzSemConsume()` (L686-706) 是**完整实现**（非桩/stub）——它们添加 trigger（masks `0x10000`/`0x20000`）并具有 ack 逻辑。但 TIME 模块侧的 `cfe_time_stepping.c` 只包含 3 个 hook（`TaskCycle`→event kind 3, `1HzBoundary`→event kind 4, `ToneSignal`→event kind 5），这些上报的是**不同的事件类型**（fact report），不是 sem-consume completion 事件。

**问题**: stepping core 已准备好接收 TIME 子路径的 completion 上报（接收端完整），但 TIME 模块缺少对应的**调用点**来触发它们。sem-consume 上报需要插入在 `OS_BinSemTake(ToneSemaphore/LocalSemaphore)` 返回处（tone task 和 local1Hz task 的唤醒点），但 `cfe_time_tone.c` 中没有此类 hook。

**目标状态**: TIME stepping 集成层应在 tone/local 子任务的 `OS_BinSemTake` 返回后调用 `ReportTimeToneSemConsume()`/`ReportTimeLocal1HzSemConsume()`，在处理完成后调用相应的 complete 上报。

**涉及文件**:
- `cfe/modules/time/fsw/src/cfe_time_stepping.c` — 需要增加 sem-consume hook（当前只有 fact-report hook）
- `cfe/modules/time/fsw/src/cfe_time_tone.c` — `OS_BinSemTake(ToneSemaphore)` (L1033 附近) 和 `OS_BinSemTake(LocalSemaphore)` (L1238 附近) 的返回点
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` — 接收端（已完整实现，无需修改）

---

### 差距 6: OSAL Delay 默认仍可 Wall-Clock Fallback（严重度: 🟡 Medium）

**现状**: `OS_TaskDelay_Impl` 在 stepping hook 未接管时执行 `clock_nanosleep`（真实 wall-clock sleep）。系统启动阶段这是正确的（启动先正常完成再进入 step mode），但进入 stepping 后如果 hook 因任何原因未生效，任务会 sleep 真实时间。

**问题**: 存在"安静失败"风险——如果某个 APP 的 `OS_TaskDelay` 未被 hook 拦截，它会按 wall-clock 执行，破坏 stepping 同步但不报错。

**目标状态**: 进入 stepping 模式后，默认行为应切换为"被 stepping core 管理"。如果 hook 未注册，应报告 warning 而非静默 fallback 到 wall-clock。

**新方案（shared 层拦截）**: 约束已移除，可在 `osal/src/os/shared/src/osapi-task.c` 的 `OS_TaskDelay()` (L279) 中，在调用 `OS_TaskDelay_Impl()` 前插入 stepping 逻辑。stepping 活跃时直接接管（上报 core + 等待授权），不调用 `_Impl`。这完全替代当前 POSIX 层 hook，且平台无关。

**涉及文件**:
- `osal/src/os/shared/src/osapi-task.c` — `OS_TaskDelay()` L279（**新增拦截点**）
- `osal/src/os/posix/src/os-impl-tasks.c` — `OS_TaskDelay_Impl`（现有 hook 降级为 fallback）
- `osal/src/os/posix/src/os-posix-stepping.c` — 现有 hook 保留为 POSIX 特定 fallback

---

### 差距 7: 动态 Trigger Set 未实现（严重度: 🟢 Low）

**现状**: stepping core 已具备多种编译时 source mask（`0x02` QueueReceive, `0x200` QueueReceiveAck, `0x800` BinSemAck, `0x2000` SchSendTrigger, `0x8000` CoreServiceCmdPipe, `0x10000` TIME Tone, `0x20000` TIME Local1Hz 等 7+ 种），但 trigger 集合在编译时确定，缺少运行时动态注册/注销能力。

**问题**: 如果未来需要将其他实体加入 stepping 控制（如特定 APP 的周期任务），没有动态注册/注销机制。当前要增加新的 trigger 类型需要修改 core 代码和 source mask 定义。

**历史教训**: issues.md T12 记录了一次完整 wait-set/ack-polling 的 scope creep 被回退，说明此功能的复杂度需谨慎对待。

**目标状态**: stepping core 提供 `RegisterTrigger()` / `UnregisterTrigger()` API，支持运行时动态调整哪些实体参与 stepping 同步。

**涉及文件**:
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` — 注册 API
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.h` — API 声明

---

### 差距 8: TIME→SCH 回调链隐式依赖（严重度: 🟠 High）

> **代码审查新发现 — 原文档未覆盖**

**现状**: `cfe_time_tone.c` L1055 中 `CFE_TIME_Tone1HzISR()` 末尾调用 `CFE_TIME_NotifyTimeSynchApps()`，该函数（L1310-1333）遍历所有注册的同步回调。SCH 通过 `CFE_TIME_RegisterSynchCallback()` 注册了 `SCH_MajorFrameCallback`，意味着 **TIME 的 1Hz tone ISR 直接触发 SCH 的 MajorFrame 处理**。

**问题**: 这条 TIME→SCH 跨模块回调链在 stepping 模式下完全不受控：
- 即使差距 1 修复了 SCH minor frame 的门控，SCH 的 **MajorFrame** 仍可能被 TIME 自主 timer 独立触发
- 差距 3 讨论了 TIME 的自主路径问题，但未提及这条隐式的跨模块回调链
- 这意味着仅修复差距 1 + 差距 3 各自的门控**不足以**完全控制 SCH 行为

**目标状态**: stepping 模式下 `CFE_TIME_NotifyTimeSynchApps()` 应被门控或 SCH 的 `MajorFrameCallback` 应在 stepping 模式下忽略非授权触发。

**涉及文件**:
- `cfe/modules/time/fsw/src/cfe_time_tone.c` — `CFE_TIME_Tone1HzISR()` L1055, `CFE_TIME_NotifyTimeSynchApps()` L1310-1333
- `apps/sch/fsw/src/sch_custom.c` — `SCH_MajorFrameCallback()` 注册与响应

**与其他差距关系**: 应纳入差距 3（第二波）的修改范围。

---

### 差距 9: CI_LAB 500ms SB 接收超时的 Wall-Clock 依赖（严重度: 🟡 Medium）

> **代码审查新发现 — 原文档未覆盖**

**现状**: CI_LAB 使用 `CFE_SB_ReceiveBuffer(..., 500)` 带 500ms wall-clock 超时，而非 `CFE_SB_PEND_FOREVER`。issues.md T13 明确记录此行为为"environmental constraint, not a bug"。

**问题**: 在 stepping 模式下，500ms 超时是 wall-clock 时间而非 sim-time：
- CI_LAB 可能在 step 之间因 wall-clock 超时返回，产生不受控的处理循环
- 对于需要通过 CI_LAB UDP 注入命令的 QA 场景（如 Scenario4），这个 wall-clock 窗口会引入不确定性
- 这不是 stepping 架构本身的 bug，而是 stepping 模式下的**已知兼容性限制**

**超时链路追踪（代码验证）**:
```
CFE_SB_ReceiveBuffer(pipe, 500)                         [cfe_sb_api.c:1296]
  → CFE_SB_MessageTxn_SetTimeout(Txn, 500)               [cfe_sb_priv.c:543]
      → CFE_PSP_GetTime(&AbsTimeout)                      [返回 sim-time]
      → AbsTimeout += 500ms                               [sim-time 域截止期限]
  → CFE_SB_ReceiveTxn_Execute(Txn)                        [cfe_sb_priv.c:1456]
    → CFE_SB_ReceiveTxn_PipeHandler()                     [cfe_sb_priv.c:1401]
      → CFE_SB_MessageTxn_GetOsTimeout(TxnPtr)            [cfe_sb_priv.c:1067]
          → CFE_PSP_GetTime(&TimeNow)                      [再次 sim-time]
          → remaining = AbsTimeout - TimeNow               [≈500ms 增量]
      → OS_QueueGet_Impl(token, data, size, &copied, 500) [os-impl-queues.c:190]
          → OS_Posix_CompAbsDelayTime(500, &ts)            [os-impl-common.c:147]
              → clock_gettime(CLOCK_REALTIME, &ts)         [⚠️ WALL-CLOCK 域切换!]
              → ts += 500ms
          → mq_timedreceive(id, data, size, NULL, &ts)     [POSIX wall-clock 等待]
```

**关键发现**:
1. SB 层用 `CFE_PSP_GetTime()` 计算 sim-time 域增量（≈500ms）
2. OSAL shared 层将此增量传入 POSIX `_Impl`
3. POSIX 层 `OS_Posix_CompAbsDelayTime()` 用 `clock_gettime(CLOCK_REALTIME)` 做 wall-clock 绝对截止
4. 增量传递方式**意外抵消**了 sim/wall 时钟差异 → CI_LAB 始终等待 500ms wall-clock
5. EINTR 重试循环 (L243-246) 吞噬信号中断，阻塞信号方案

**方案评估**:

| 方案 | 可行性 | 评估 |
|------|--------|------|
| A: CI_LAB 改用 PEND_FOREVER | ❌ 不可行 | 破坏 UDP 上行链路功能（`CI_LAB_ReadUpLink()` 依赖超时返回触发） |
| B: OSAL QueueGet hook 拦截 | ⚠️ 复杂 | 需修改 POSIX 层 QueueGet_Impl 添加超时接管，涉及 EINTR 循环改造 |
| C: CI_LAB APP 层 stepping 集成 | ⚠️ 实用 | CI_LAB 代码改造：stepping 下用 PEND_FOREVER + 外部信号唤醒触发 ReadUpLink |
| **D: Shared 层 QueueGet shim** | **✅ 推荐** | 约束已移除，在 `OS_QueueGet()` L174 拦截，stepping 下替换为 `OS_CHECK` 轮询循环 |

**推荐方案 D 详解**:
- 在 `osal/src/os/shared/src/osapi-queue.c` 的 `OS_QueueGet()` 中：
  - stepping 活跃 + timeout > 0 → 循环调用 `OS_QueueGet_Impl(token, data, size, copied, OS_CHECK)` + stepping core 等待
  - 每次 step boundary 返回 `OS_QUEUE_TIMEOUT` → CI_LAB 正常执行 `ReadUpLink()`
  - 完全绕过 `OS_Posix_CompAbsDelayTime` 的 wall-clock 转换
- **保留 CI_LAB UDP 功能**：stepping 的 step boundary 替代 wall-clock timeout 作为 `ReadUpLink()` 触发

**目标状态**: shared 层 `OS_QueueGet()` 内置 stepping 感知超时，CI_LAB 无需修改。

**涉及文件**:
- `osal/src/os/shared/src/osapi-queue.c` — `OS_QueueGet()` L148-179（**新增拦截点**）
- `apps/ci_lab/fsw/src/ci_lab_app.c` — 无需修改（stepping shim 透明处理）
- `cfe/modules/sb/fsw/src/cfe_sb_priv.c` — 无需修改（增量传递方式兼容）

**实施建议**: 纳入统一 shared 层 stepping shim（见下方方案），与差距 6/10 同步实施。

---

### 差距 10: OS_Timer 回调路径缺少 Stepping 集成（严重度: 🟡 Medium）

> **代码审查新发现 — 原文档未覆盖**

**现状**: 差距 1 讨论了 SCH 的 timer，差距 3 讨论了 TIME 的 timer，但问题更根本：**所有 `OS_TimerAdd()`/`OS_TimerSet()` 创建的 OSAL timer 在 stepping 模式下都自主触发**。OSAL timer 回调运行在独立信号线程（POSIX `sigev_thread` 或 `timer_create`），完全不受 stepping core 控制。当前只有 `OS_TaskDelay` 被 hook（差距 6），但 `OS_Timer` callback 路径没有任何 stepping 集成。

**问题**: 在差距 1 和差距 3 的修改中，如果只在 SCH/TIME 层面门控 `OS_BinSemGive`，这是"症状处理"而非"源头治理"。timer callback 仍然会被 OS 级 timer 触发，只是门控了其下游效果。如果未来有其他模块使用 `OS_Timer`（如自定义 APP），同样的问题会复现。

**新方案（shared 层门控）**: 约束已移除。在 `osal/src/os/shared/src/osapi-timebase.c` 的 `OS_TimeBase_CallbackThread()` (L365) 中：
- `syncfunc()` 返回后检查 stepping 状态
- stepping 活跃时：门控 `tick_time` 累加，只在 stepping core 授权时让 `freerun_time` 前进
- 这使 timer callback（SCH minor frame, TIME 1Hz）只在授权的 tick 边界上触发
- **源头治理**：差距 1（SCH）和差距 3（TIME）的消费端门控可简化或作为冗余安全层

**关键代码路径** (`osapi-timebase.c` L460-506):
```c
timebase->freerun_time += tick_time;  // ← 门控点：stepping 活跃时只有授权的 tick_time 才累加
// 遍历 timer callback 链表
for (timecb = ...; timecb != NULL; timecb = timecb->next) {
    timecb->wait_time -= tick_time;   // ← tick_time 为 0 或门控值时，callback 不触发
    if (timecb->wait_time < 0 && saved_wait_time > 0)
        (*timecb->callback_ptr)(...); // SCH_MinorFrameCallback / TIME 1Hz callback
}
```

**目标状态**: shared 层 `OS_TimeBase_CallbackThread()` 内置 stepping 门控，timer callback 只在 stepping core 授权时触发。差距 1/3 的消费端门控保留为冗余安全层。

**涉及文件**:
- `osal/src/os/shared/src/osapi-timebase.c` — `OS_TimeBase_CallbackThread()` L365-509（**新增门控点**）
- `osal/src/os/posix/src/os-impl-timebase.c` — POSIX 层 syncfunc（无需修改，门控在 shared 层）

**与差距 1/3 的关系**: 差距 10 的 shared 层门控是**源头治理**，差距 1（SCH 消费端门控）和差距 3（TIME 消费端门控）变为**纵深防御**。两层都保留以确保安全。

---

### 差距 11: Stepping Core 全局状态的并发安全（严重度: 🟡 Medium）

> **代码审查新发现 — 原文档未覆盖**

**现状**: stepping core 的全局状态结构体（`SteppingState`）被多个线程并发访问：
- **Timer 信号线程**: SCH callback (`ReportSchMinorFrame`)、TIME callback (通过 hook 上报)
- **APP 任务线程**: 各 app 调用 `OS_TaskDelay` → `QueryTaskDelayEligible()`
- **UDS 控制线程**: 外部引擎通过 socket 调用 `BeginStepSession()`/`IsStepComplete()`
- **InProc 调用线程**: 同进程测试代码直接调用控制 API

代码中未发现显式的 mutex/spinlock 保护全局状态。当前可能因为"事件上报顺序由 `session_active` 门控"而意外安全（大部分操作在 `session_active=false` 时直接 early-return）。

**问题**: 差距 1-3 的修改（增加门控逻辑、改变 completion 语义）可能引入新的并发路径，打破当前的隐式安全假设。例如：
- 差距 1：stepping core 授权 `OS_BinSemGive` 需要在 timer 信号线程和控制线程之间安全通信
- 差距 2：`BeginStepSession()` 推进 quantum 与 `ReportSchMinorFrame()` 消费 quantum 之间的竞争

**目标状态**: 在实施第一波修改前进行并发安全审计，明确哪些操作需要原子化或互斥保护。

**涉及文件**:
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` — 全局状态访问点
- `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.h` — 状态结构体定义

---

### 差距 12: Scenario4 运行时超时问题（严重度: 🟡 Medium — 已知未解决）

> **issues.md 发现 — 原文档未覆盖**

**现状**: issues.md T14 记录 Scenario4 `InProc_WaitStepComplete` 超时，`scenario4_to_datatypes_udp.log` 为空。Scenario1-3 已通过，但 Scenario4（涉及 TO_LAB 遥测输出验证）始终超时。

**问题**: 此问题可能是差距 4/5 的实际表现 — completion 语义不够全局化，导致某些情况下 `IsStepComplete()` 无法正确收敛。也可能是 CI_LAB/TO_LAB 的 wall-clock 依赖（差距 9）导致。

**目标状态**: 作为差距 4/5 修复的验证基准 — 如果全局 completion 语义正确，Scenario4 应该能够通过。

**涉及文件**: 运行时问题，不直接对应文件修改。验证相关：
- QA 场景脚本 / stepping 控制通道
- `apps/to_lab/fsw/src/to_lab_app.c` — TO_LAB 的 `OS_TaskDelay` 驱动循环

---

## 严重度排序

| 排序 | 差距 | 严重度 | 理由 | 验证状态 |
|------|------|--------|------|----------|
| 1 | 差距 1: SCH 回调仍自驱动 | 🔴 Critical | 直接违反"外部引擎唯一推进源"的核心目标 | ✅ `sch_custom.c` L462 无条件 `OS_BinSemGive` 确认 |
| 2 | 差距 2: Quantum 推进所有权未统一 | 🔴 Critical | 推进语义绑定在 SCH 上，无法扩展到全局 | ✅ `ReportSchMinorFrame()` L490-521 唯一推进点确认 |
| 3 | 差距 3: TIME 保留自主推进路径 | 🟠 High | 时间源不一致会导致系统行为不可预测 | ✅ `cfe_time_task.c` L338-341 无守卫 timer 确认 |
| 4 | 差距 8: TIME→SCH 回调链隐式依赖 | 🟠 High | 差距 1+3 各自修复不足以控制 SCH 行为 | 🆕 `cfe_time_tone.c` L1055 + L1310-1333 发现 |
| 5 | 差距 4: Completion 启动门控偏 SCH-centric | 🟠 High | completion 的"启动条件"而非"完成条件"绑定 SCH | ✅ 精确化：gate vs convergence 区分 |
| 6 | 差距 5: TIME 子路径未纳入 Completion | 🟡 Medium | 全局收敛的必要条件之一 | ✅ 精确化：core 侧完整实现，TIME 侧缺调用点 |
| 7 | 差距 11: Stepping Core 并发安全 | 🟡 Medium | 多线程访问全局状态，修改可能破坏隐式安全 | 🆕 代码审查发现 |
| 8 | 差距 10: OS_Timer 回调 → Shared 层 Stepping 门控 | 🟡 Medium | 源头治理差距 1/3，shared 层可门控 | 🆕→✅ 方案 D: shared 层 `OS_TimeBase_CallbackThread` 门控 |
| 9 | 差距 9: CI_LAB Wall-Clock 超时 → Shared 层 QueueGet Shim | 🟡 Medium | 从"已知限制"升级为"可解决" | 🆕→✅ 方案 D: shared 层 `OS_QueueGet` stepping 感知超时 |
| 10 | 差距 6: OSAL Delay → Shared 层接管 | 🟡 Medium | shared 层接管替代 POSIX fallback warning | ✅→✅ shared 层方案替代 POSIX 层 hook |
| 11 | 差距 12: Scenario4 运行时超时 | 🟡 Medium | 可能是差距 4/5 的实际表现 | 🆕 issues.md T14 记录 |
| 12 | 差距 7: 动态 Trigger Set 未实现 | 🟢 Low | 扩展性问题，不影响基本功能 | ✅ 精确化：7+ mask 类型已存在 |

---

## 推荐实施顺序

### 前置条件（第一波之前）
- **并发安全审计**（差距 11）— 明确 stepping core 全局状态的线程安全边界，确定哪些操作需要原子化/互斥保护。此审计的结论直接影响第一波和第二波的实现方式。
- **Shared 层 stepping shim 基础框架** — 在 `osal/src/os/shared/src/` 建立函数指针回调注册机制（`OS_SteppingRegisterHooks()`），PSP init 时注入。这是差距 6/9/10 的共用基础设施。

### 第一波（核心推进语义改造 + OSAL 源头治理）
1. **OSAL TimeBase 门控**（差距 10）— shared 层 `OS_TimeBase_CallbackThread()` 门控 tick 传递。这是差距 1/3 的**源头治理**，应先于消费端门控。
2. **SCH 回调接管**（差距 1）— 移除本地 `OS_BinSemGive`，改由 stepping core 授权唤醒。与差距 10 形成**源头+消费端双层防御**。
3. **Quantum 推进入口统一**（差距 2）— `BeginStepSession()` 直接驱动，`ReportSchMinorFrame()` 变为消费者
- ⚠️ **风险最高的一波**：修改系统调度心跳，建议拆分为独立 commit + 准备紧急回退编译开关

### 第二波（TIME 集成 + OSAL Delay/Queue 接管）
4. **TIME 自主路径降级**（差距 3）— 1Hz/tone 在 stepping 下为纯报告。差距 10 的源头门控已拦截 timer tick，此处为纵深防御。
5. **TIME→SCH 回调链门控**（差距 8）— `CFE_TIME_NotifyTimeSynchApps()` 在 stepping 下门控或 SCH `MajorFrameCallback` 忽略非授权触发
6. **TIME Completion 接线**（差距 5）— tone/local `OS_BinSemTake` 返回后调用 stepping core 上报
7. **OSAL TaskDelay shared 层接管**（差距 6）— `OS_TaskDelay()` shared 层拦截，替代 POSIX 层 hook
8. **OSAL QueueGet shared 层接管**（差距 9）— `OS_QueueGet()` shared 层拦截，stepping 感知超时
- ⚠️ 差距 8 必须与差距 3 同步实施，否则差距 3 的修改产生不完整行为
- 差距 7/8 可并行（无依赖），利用 shared 层 shim 基础设施

### 第三波（全局收敛）
9. **Completion 改为 Wait-Set**（差距 4）— 启动门控与 quantum 推进统一，wait-set 全局收敛
- ⚠️ 差距 4 实施需吸取 T12 scope creep 回退的教训，建议采用最小化固定 wait-set

### 第四波（扩展性）
10. **动态 Trigger Set**（差距 7）— 注册/注销 API
- 可推迟：在有明确第二消费者需求前不是必需

### 已知限制（不在实施范围内，但需记录）
- **Scenario4 超时**（差距 12）— 作为差距 4/5 修复的验证基准

---

## 统一 Shared 层 Stepping Shim 方案

> **2026-03-17 更新** — 移除 shared 层约束后的新方案。同时解决差距 6、9、10。

### 核心思路

在 `osal/src/os/shared/src/` 建立统一拦截层，在 OSAL API 调用传入平台 `_Impl` 之前插入 stepping 逻辑。平台无关（POSIX/RTEMS/VxWorks 通用），通过函数指针回调避免 shared 层对 PSP 的直接依赖。

### 架构层次

```
App 层
  ↓ (timeout=500ms / delay=200ms)
cFE SB 层 (CFE_PSP_GetTime=sim-time → 增量传递)
  ↓ (remaining_ms)
OSAL Shared 层 ═══ [Stepping Shim 拦截点] ═══
  │
  ├─ OS_QueueGet():       stepping 活跃 → OS_CHECK 轮询循环 (差距 9)
  ├─ OS_TaskDelay():      stepping 活跃 → core 接管, 跳过 _Impl (差距 6)
  └─ OS_TimeBase_CallbackThread(): stepping 活跃 → 门控 tick_time (差距 10)
  │
  ↓ (stepping 不活跃 → 透传)
OSAL POSIX _Impl 层 (wall-clock)
```

### 函数指针回调机制

```c
/* osal/src/os/shared/inc/os-stepping-hooks.h (新增) */
#ifdef CFE_SIM_STEPPING

typedef struct {
    /* 差距 6: TaskDelay 接管 — 返回 true 表示已处理 */
    bool (*TaskDelayHook)(uint32 task_id, uint32 millisecond);

    /* 差距 9: QueueGet 超时接管 — 返回 true 表示已处理, OS_result 通过指针返回 */
    bool (*QueueGetTimeoutHook)(uint32 queue_id, int32 timeout, int32 *os_result);

    /* 差距 10: TimeBase tick 门控 — 返回授权的 tick_time (0=暂停) */
    uint32 (*TimeBaseTickGateHook)(uint32 timebase_id, uint32 raw_tick_time);

    /* 查询 stepping 是否活跃 */
    bool (*IsSteppingActive)(void);
} OS_SteppingHooks_t;

void  OS_SteppingRegisterHooks(const OS_SteppingHooks_t *hooks);
bool  OS_SteppingIsActive(void);  /* 快速查询，inline 友好 */

#endif /* CFE_SIM_STEPPING */
```

### 注册时机

PSP init (`CFE_PSP_ModuleInit`) 时调用 `OS_SteppingRegisterHooks(&psp_hooks)`，将 stepping core 的实现注入 OSAL shared 层。

### 性能保证

- **编译时**: 全部 `#ifdef CFE_SIM_STEPPING`，非 stepping 构建零开销
- **运行时**: stepping 未激活时只有一次 `if (hooks && hooks->IsSteppingActive())` 检查
- **热路径**: `OS_QueueGet` 是最热路径（所有 APP 的 SB receive），必须确保 stepping 不活跃时的检查成本 ≈ 1 次指针 NULL 检查

### 与现有 POSIX 层 hooks 的关系

| 功能 | 当前位置 | 新方案后 |
|------|----------|----------|
| TaskDelay 接管 | POSIX `os-posix-stepping.c` (功能完整) | Shared 层接管，POSIX 降级为 fallback |
| QueueGet 事件上报 | POSIX `os-impl-queues.c` (纯上报) | Shared 层接管超时，POSIX 保留上报 |
| BinSem 事件上报 | POSIX `os-impl-binsem.c` (纯上报) | 不变（暂不需要 shared 层接管） |
| TimeBase tick 门控 | 无 | Shared 层新增 |

### 实施为前置条件

Shared 层 shim 基础框架（头文件 + 回调注册 + 空 hook 桩）应作为**第一波前置条件**，与并发安全审计（差距 11）同步完成。第一波/第二波的具体差距修复直接使用此框架。

---

## 涉及文件修改清单

| 文件 | 修改类型 | 相关差距 |
|------|----------|----------|
| `apps/sch/fsw/src/sch_custom.c` | 修改 | 差距 1, 8 |
| `apps/sch/fsw/src/sch_stepping.c` | 修改 | 差距 1 |
| `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.c` | 修改 | 差距 1, 2, 4, 5, 7, 11 |
| `psp/fsw/modules/sim_stepping/cfe_psp_sim_stepping_core.h` | 修改 | 差距 2, 4, 7, 11 |
| `cfe/modules/time/fsw/src/cfe_time_task.c` | 修改 | 差距 3 |
| `cfe/modules/time/fsw/src/cfe_time_tone.c` | 修改 | 差距 3, 5, 8 |
| `cfe/modules/time/fsw/src/cfe_time_stepping.c` | 修改 | 差距 3, 5 |
| `osal/src/os/shared/src/osapi-queue.c` | **新增拦截** | 差距 9（stepping 感知超时） |
| `osal/src/os/shared/src/osapi-task.c` | **新增拦截** | 差距 6（stepping 感知延迟） |
| `osal/src/os/shared/src/osapi-timebase.c` | **新增门控** | 差距 10（tick 传递门控） |
| `osal/src/os/shared/inc/` (新文件) | **新增** | 差距 6, 9, 10（stepping shim 回调注册头文件） |
| `osal/src/os/posix/src/os-impl-tasks.c` | 简化 | 差距 6（POSIX hook 降级为 fallback） |
| `osal/src/os/posix/src/os-posix-stepping.c` | 简化 | 差距 6（部分逻辑上移 shared 层） |
| `osal/src/os/posix/src/os-impl-timebase.c` | 不修改 | 差距 10（门控在 shared 层完成） |

---

## 风险评估

### 第一波风险: 核心推进语义改造（差距 1 + 差距 2）

#### 差距 1 — SCH 回调接管

| 风险类型 | 等级 | 分析 |
|----------|------|------|
| **回归风险** | 🔴 高 | `SCH_MinorFrameCallback` 是整个 cFS 调度心跳。修改无条件 `OS_BinSemGive` 直接影响所有 APP 调度。stepping core 授权逻辑有 bug 则整个系统冻结。T1-T14 全部受影响。 |
| **死锁风险** | 🔴 高 | stepping core 授权信号丢失/延迟 → SCH 主循环永久阻塞在 `OS_BinSemTake(TimeSemaphore)`。需超时保护或看门狗。 |
| **非 stepping 模式影响** | 🟡 中 | 必须确保 `#ifndef CFE_SIM_STEPPING` 下原有行为完全不变。建议编译时宏+运行时标志双重隔离。 |
| **启动序列风险** | 🟡 中 | "先正常启动再进入 step mode"过渡期间，SCH callback 行为切换必须原子化，否则可能丢帧。 |

**缓解**: ①保留原有路径作为 `#ifndef` 默认行为 ②stepping 下增加 5s 超时守卫 ③新增诊断计数器对比授权次数 vs 唤醒次数

#### 差距 2 — Quantum 推进入口统一

| 风险类型 | 等级 | 分析 |
|----------|------|------|
| **语义变更风险** | 🔴 高 | `ReportSchMinorFrame()` 当前同时做 ①推进 quantum ②设 completion_ready ③转状态 RUNNING。拆分逻辑 = 重构核心状态机。T12 scope creep 回退历史说明此区域易出副作用。 |
| **时序风险** | 🟠 高 | quantum 推进（`BeginStepSession` 中）与 SCH 次帧执行（timer callback 中）不在同一线程/上下文，需确保时序正确。当前隐式保证将被打破。 |
| **向后兼容风险** | 🟡 中 | UDS/InProc 控制通道协议语义可能需调整。Scenario1-3 QA 脚本也需适配。 |

**缓解**: ①`ReportSchMinorFrame()` 内部移除 `AdvanceOneQuantum()` 但接口不变 ②`BeginStepSession()` 增加"quantum 已推进"标志 ③增加断言：`AdvanceOneQuantum()` 只从 `BeginStepSession()` 调用

#### 第一波整体: 🔴 高风险
**关键缓解**: 全部 Scenario1-3 回归 + 紧急回退编译开关 + 差距 1/2 拆为两个独立 commit

---

### 第二波风险: TIME 集成（差距 3 + 差距 8 + 差距 5）

#### 差距 3 — TIME 自主路径降级

| 风险类型 | 等级 | 分析 |
|----------|------|------|
| **TIME 功能破坏风险** | 🔴 高 | TIME 1Hz timer 驱动整个 cFS 时间同步。降级不当 → ①时间不更新 ②tone 信号丢失 ③`CFE_TIME_GetTime()` 返回陈旧值。 |
| **跨模块影响（差距 8）** | 🟠 高 | `CFE_TIME_NotifyTimeSynchApps()` 在 tone ISR 末尾触发 SCH `MajorFrameCallback`。tone ISR 降级/门控 → SCH MajorFrame 也受影响。必须与差距 8 同步处理。 |
| **约束边界风险** | 🟡 中 | TIME timer 在 cFE 核心模块创建，门控应在调用侧（TIME）而非 OSAL 层。需确认不触碰 shared 层。 |

**缓解**: ①"降级"≠"禁用" — timer 仍触发，`OS_BinSemGive` 替换为 stepping core 条件授权 ②同步处理差距 8 的 `NotifyTimeSynchApps` 门控 ③验证 `CFE_TIME_GetTime()` 返回 sim-time 不受影响

#### 差距 8 — TIME→SCH 回调链门控

| 风险类型 | 等级 | 分析 |
|----------|------|------|
| **SCH 调度完整性** | 🟠 高 | 门控 `NotifyTimeSynchApps()` 或 SCH 的 `MajorFrameCallback` 可能影响 SCH 的 Major/Minor 帧对齐逻辑。SCH 依赖 MajorFrame 回调来同步其调度表。 |
| **遗漏路径风险** | 🟡 中 | 其他 APP 也可能注册了 TimeSynchCallback，门控策略需考虑所有注册者而非仅 SCH。 |

**缓解**: ①优先在 SCH `MajorFrameCallback` 内增加 stepping 状态检查，而非在 TIME 通知层全面门控 ②审查所有 `CFE_TIME_RegisterSynchCallback` 调用方

#### 差距 5 — TIME Completion 接线

| 风险类型 | 等级 | 分析 |
|----------|------|------|
| **接线复杂度** | 🟡 中 | 需在 `OS_BinSemTake` 返回后和处理完成后两个位置插入 hook。涉及 ISR→task 跨上下文通信。 |
| **完成时序** | 🟡 中 | TIME completion 上报晚于 `IsStepComplete()` 查询 → "假完成"或"永不完成"。需确保时序正确。 |
| **与差距 4 前置依赖** | 🟡 中 | 接线真正生效取决于第三波差距 4 的 wait-set 实现。第二波可能是"提前准备但暂不生效"。 |

**缓解**: ①第二波只完成接线（hook→core 上报），不改 completion 判断逻辑 ②第三波启用 wait-set 参与 ③接线处增加 stepping 状态检查，非 stepping 下零开销

#### 第二波整体: 🟠 高风险
**关键缓解**: 验证 `CFE_TIME_GetTime()` 正确 + 验证 `NotifyTimeSynchApps` 回调链行为 + TIME 专用 QA 场景

---

### 第三波风险: 全局收敛（差距 4）

#### 差距 4 — Completion 改为 Wait-Set

| 风险类型 | 等级 | 分析 |
|----------|------|------|
| **复杂度爆炸** | 🟠 高 | issues.md T12 记录完整 wait-set/ack-polling 的 scope creep 被回退。复杂度已被实践验证且失败过一次。 |
| **永不完成风险** | 🟠 高 | wait-set 中任一实体 complete 信号丢失 → `IsStepComplete()` 永远 false。需超时/降级机制。 |
| **配置复杂度** | 🟡 中 | "可配置的 wait-set"引入配置管理问题。错误配置可能导致系统冻结。 |

**缓解**: ①吸取 T12 教训 — 最小化设计：固定 wait-set（SCH + TIME tone + TIME local1Hz），不做完全动态 ②每个实体独立超时（5s），超时 log warning 并视为 complete ③wait-set 编译时确定

#### 第三波整体: 🟠 中高风险
**关键缓解**: 差距 4 必须从 T12 失败中吸取教训，采用最小化固定 wait-set

---

### 第四波风险: 扩展性（差距 7）

| 风险类型 | 等级 | 分析 |
|----------|------|------|
| **过度设计风险** | 🟡 中 | 没有明确第二消费者前实现动态注册可能是 YAGNI。 |
| **并发安全** | 🟡 中 | 运行时 `RegisterTrigger()`/`UnregisterTrigger()` 需多线程保护 trigger 列表。 |
| **回归风险** | 🟢 低 | 如正确实现为增量添加能力，不改变现有硬编码 trigger 行为。 |

#### 第四波整体: 🟢 低风险
如果第三波固定 wait-set 足够，第四波可推迟。

---

### 跨波次系统性风险

| 风险 | 等级 | 影响范围 | 说明 |
|------|------|----------|------|
| **并发安全（差距 11）** | 🟠 高 | 全波次 | stepping core 全局状态多线程访问，第一波/第二波修改可能引入竞争条件 |
| **回归测试覆盖** | 🟠 高 | 第一波+第二波 | Scenario1-3 需每波完整回归，Scenario4 超时可能恶化或改善 |
| **约束边界** | 🟢 低 | 第一波 | shared 层约束已移除；需确保 shared 层修改通过函数指针回调，不直接依赖 PSP 头文件 |
| **范围控制** | 🟡 中 | 第三波 | T12 scope creep 历史表明 wait-set/completion 改造容易失控 |
| **调试难度** | 🟡 中 | 全波次 | clangd LSP 不可用（issues.md 记录），增加代码修改出错概率 |
| **Scenario4 稳定性（差距 12）** | 🟡 中 | 验证阶段 | 现有超时未解决，新修改可能改变表现但难判断改善/恶化 |

---

### 风险总结与建议

1. **第一波是关键路径且风险最高** — 增加"紧急回退编译开关"和更细粒度的 commit 拆分
2. **差距 8（TIME→SCH 回调链）必须纳入第二波** — 否则差距 3 的修改产生不完整行为
3. **差距 4 应从 T12 失败中吸取教训** — 采用最小化固定 wait-set，不做完全动态
4. **并发安全审计（差距 11）应作为第一波前置条件** — 修改核心路径前明确线程安全边界
5. **第四波可推迟** — 无明确第二消费者需求前，动态 trigger set 不是必需

---

## 约束提醒

- ✅ Linux/native only
- ✅ SCH 次帧为 step 粒度
- ✅ 启动先正常完成再进入 step mode
- ~~⚠️ **不修改** `osal/src/os/shared/src/`~~ **已移除**（用户确认 shared 层允许修改）
- ⚠️ **不把静态 allowlist 作为核心同步语义**
- ⚠️ **不让 SBN 参与控制通道或默认加载**
- ⚠️ **不让 OSAL/PSP 直接依赖高层 socket/状态机实现**（shared 层通过函数指针回调，不直接 `#include` PSP 头文件）

---

## 附录: 代码验证证据

> 以下为源码逐行审查的关键证据摘要。每条证据对应差距分析中的一项结论。

### 差距 1 证据
- `sch_custom.c` L104-106: `#ifdef CFE_SIM_STEPPING` 块中 `OS_TimerSet(TimerId, SCH_NORMAL_SLOT_PERIOD, SCH_NORMAL_SLOT_PERIOD)` — stepping 模式仍启动周期 timer
- `sch_custom.c` L462: `OS_BinSemGive(SCH_AppData.TimeSemaphore)` — **无条件执行**，不受 stepping 模式限制
- `sch_stepping.c`: 全部 5 个 hook 均为纯事实上报（`CFE_PSP_SimStepping_Shim_ReportEvent`），无门控逻辑

### 差距 2 证据
- `cfe_psp_sim_stepping_core.c` L490-521: `ReportSchMinorFrame()` 调用 `AdvanceOneQuantum()` — **唯一** quantum 推进路径
- `cfe_psp_sim_stepping_core.c` L229-264: `BeginStepSession()` 只设 `session_active=true`，不推进时间
- `cfe_psp_sim_stepping_core.c` L840-856: `AdvanceOneQuantum()` 只做 `sim_time_ns += quantum_ns`

### 差距 3 证据
- `cfe_time_task.c` L338: `OS_TimerAdd(&TimerId, "cFS-1Hz", ...)` — 无 stepping 守卫
- `cfe_time_task.c` L341: `OS_TimerSet(TimerId, 500000, 1000000)` — 1Hz 周期，无 stepping 守卫
- `cfe_time_tone.c` L1033: `OS_BinSemGive(CFE_TIME_Global.ToneSemaphore)` — 无 stepping 守卫
- `cfe_time_tone.c` L1238-1246: `OS_BinSemGive(CFE_TIME_Global.LocalSemaphore)` — 无 stepping 守卫
- `cfe_time_stepping.c`: 3 个 hook 均为纯事实上报，不门控 TIME 行为

### 差距 4 证据
- `cfe_psp_sim_stepping_core.c` L510: `completion_ready = true` 由 `ReportSchMinorFrame()` 置位
- `cfe_psp_sim_stepping_core.c`: source masks 7+ 种（`0x02`, `0x200`, `0x800`, `0x2000`, `0x8000`, `0x10000`, `0x20000`），wait-set 机制存在但门控绑定 SCH

### 差距 5 证据
- `cfe_psp_sim_stepping_core.c` L662-706: `ReportTimeToneSemConsume()`/`ReportTimeLocal1HzSemConsume()` **完整实现**，masks `0x10000`/`0x20000`
- `cfe_time_stepping.c`: hook 上报 event kinds 3/4/5 — **不同于** sem-consume 事件类型
- `cfe_time_tone.c`: `OS_BinSemTake(ToneSemaphore/LocalSemaphore)` 返回处无 stepping hook

### 差距 6 证据
- `cfe_psp_sim_stepping_core.c` L775-838: `QueryTaskDelayEligible()` 5 门控条件，任一失败 → wall-clock fallback

### 差距 8 证据
- `cfe_time_tone.c` L1055: `CFE_TIME_NotifyTimeSynchApps()` 在 `Tone1HzISR()` 末尾调用
- `cfe_time_tone.c` L1310-1333: 遍历注册回调，SCH 注册了 `MajorFrameCallback`

### 差距 9 证据
- issues.md T13: "CI_LAB UDP injection is NOT pure step-driven (500ms SB receive timeout creates wall-clock gate)"
- `cfe_sb_api.c` L1296: `CFE_SB_ReceiveBuffer` 入口，转 `CFE_SB_MessageTxn_SetTimeout`
- `cfe_sb_priv.c` L543: `SetTimeout` 用 `CFE_PSP_GetTime()` 获取 sim-time 基准 + 500ms
- `cfe_sb_priv.c` L1067: `GetOsTimeout` 再次 `CFE_PSP_GetTime()` 计算 remaining ms（sim-time 域增量）
- `os-impl-queues.c` L236: `OS_Posix_CompAbsDelayTime(timeout, &ts)` — 增量传入 POSIX wall-clock 转换
- `os-impl-common.c` L147: `clock_gettime(CLOCK_REALTIME, tm)` — **wall-clock 域切换点**
- `os-impl-queues.c` L243-246: EINTR 重试循环使用预计算的同一个 `ts`，吞噬信号中断
- **关键发现**: sim-time 增量传递意外抵消时钟差异 → 始终等待 500ms wall-clock

### 差距 10 证据
- `osapi-timebase.c` L460: `timebase->freerun_time += tick_time` — tick 累加点（shared 层门控目标）
- `osapi-timebase.c` L489-491: `if (saved_wait_time > 0 && timecb->callback_ptr != NULL) (*timecb->callback_ptr)(...)` — callback 触发取决于 `tick_time` 是否推进 `wait_time`
- `osapi-timebase.c` L365-509: `OS_TimeBase_CallbackThread` 主循环完整路径
- `os-posix-stepping.c`: 现有 POSIX hooks 中无 TimeBase 相关 hook

### 差距 11 证据
- `cfe_psp_sim_stepping_core.c`: 全局 `SteppingState` 被 timer 信号线程、APP 线程、UDS/InProc 线程并发访问，未见显式 mutex/spinlock

### 差距 12 证据
- issues.md T14: Scenario4 `InProc_WaitStepComplete` 超时，`scenario4_to_datatypes_udp.log` 为空

### Shared 层拦截点证据
- `osapi-queue.c` L174: `OS_QueueGet_Impl(&token, data, size, size_copied, timeout)` — 拦截点在此行前
- `osapi-task.c` L279: `return OS_TaskDelay_Impl(millisecond)` — 拦截点替换此行
- `osapi-timebase.c` L460: `timebase->freerun_time += tick_time` — 门控点替换此行

---

*文档更新日期: 2026-03-17 — shared 层约束移除，统一 Shim 方案纳入，差距 9/10 方案升级，超时链路追踪证据纳入*

---

## 🆕 架构变更讨论：sim_stepping 独立模块化（进行中）

### 动机

当前 sim_stepping 作为 PSP 模块存在分层违规：
- OSAL shared 层需要调用 stepping hooks → 但 OSAL 不应依赖 PSP
- cFE 需要调用 stepping API → cFE 可以调用 PSP，但语义上 stepping 不是"平台支持"
- 统一 shared 层 shim 方案中的函数指针回调本质上是在回避这个依赖问题

### 用户方向

> "考虑将 sim_stepping 从 psp 的模块编成与 OSAL\PSP\CFE 统一级别的独立模块，这样可以避免各层对PSP的依赖"

### 研究结果

#### 1. sim_stepping 模块结构与依赖

**文件清单** (`psp/fsw/modules/sim_stepping/`):
| 文件 | 行数 | 依赖 |
|------|------|------|
| `cfe_psp_sim_stepping.c` | 956 | POSIX socket/thread + PSP headers |
| `cfe_psp_sim_stepping_core.c` | 1081 | 仅标准 C + `common_types.h` + 自身头文件 |
| `cfe_psp_sim_stepping.h` | 277 | 仅 `<stdint.h>`, `<stdbool.h>` |
| `cfe_psp_sim_stepping_core.h` | 630 | 仅 `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` |
| `CMakeLists.txt` | 15 | `add_psp_module()` 宏 |

**关键发现：依赖方向单向**
- sim_stepping → 无 OSAL/cFE 调用（仅 POSIX + standard C + `common_types.h`）
- cFE/OSAL/Apps → sim_stepping（单向调用）
- 唯一的 PSP 依赖：`CFE_PSP_MODULE_DECLARE_SIMPLE(sim_stepping)` 宏

**谁调用 sim_stepping（入向依赖）**:
- OSAL POSIX 层: `os-posix-stepping.c` → `Shim_ReportEvent`, `Hook_TaskDelayEligible`, `WaitForDelayExpiry`
- OSAL POSIX impl: `os-impl-queues.c`, `os-impl-binsem.c`, `os-impl-tasks.c` → `OS_PosixStepping_Hook_*`
- PSP timebase: `cfe_psp_timebase_posix_clock.c` → `Hook_GetTime`
- cFE 模块: ES, EVS, SB, TBL, TIME → shim event reporting
- Apps: SCH → `sch_stepping.c`
- Mission header: `cfe_psp_sim_stepping_shim.h` in `sample_defs/fsw/inc/`

#### 2. 构建系统分析

**当前 PSP 模块构建流程**:
1. `psp/CMakeLists.txt` → 读取 `psp_module_list.cmake` + `psp_conditional_modules.cmake`
2. 每个模块 → `add_psp_module(name sources...)` → 静态库
3. `psp-${TARGET}` → 链接所有模块静态库

**`add_psp_module` 实现** (`cfe/cmake/arch_build.cmake` L71-81):
- `add_library(STATIC)` + `target_link_libraries(PRIVATE psp_module_api)`

**独立模块化方案**: 两种路径
- 方案 A: 保持 PSP 模块，但通过 INTERFACE target 暴露头文件 → 最小改动
- 方案 B: 完全提取到顶层目录 → 类似 OSAL 的独立构建 → 架构更干净

**OSAL 的 INTERFACE target 模式** (可参考):
- `osal_public_api` INTERFACE target + INTERFACE include dirs
- PSP 通过 `$<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>` 消费

#### 3. 提取关键挑战

1. **PSP 模块注册宏**: `CFE_PSP_MODULE_DECLARE_SIMPLE` 需要替换为独立的初始化机制
2. **API 命名**: `CFE_PSP_SimStepping_*` → 需要统一重命名（如 `SIM_Stepping_*`）
3. **`#ifdef CFE_SIM_STEPPING` 散布**: PSP/OSAL/cFE/Apps 约 30+ 处条件编译
4. **链接顺序**: 独立模块需在 OSAL 之后、cFE 之前可见
5. **Shim header 位置**: 当前在 `sample_defs/fsw/inc/`，需迁入模块或保持 mission config

### 用户决策（已确认）

1. **提取深度**: ✅ 方案 B — 完全独立顶层模块
2. **目录名称**: ✅ `external_simulation_adaptor/`（缩写 ESA）
3. **API 命名前缀**: ✅ `ESA_*` — 但**分两步执行**：先提取保持旧名，后续单独重命名
4. **初始化时序**: ✅ 已决策 — ESA_Init() 在 OSAL BSP `main()` 中 `OS_Application_Startup()` 之前调用。仅修改 `osal/src/bsp/generic-linux/src/bsp_start.c`（Linux 仿真专属）。Core + Transport 在同一次 init 完成（和当前 sim_stepping 一致）。ESA 在所有 cFS 层之前就绪，零窗口期。
5. **工作范围**: ✅ 单独工作项（不纳入 gap analysis 实施计划）
6. **API 重命名时机**: ✅ 先提取后重命名（Metis 建议）— 每步 diff 更小，更易 review

### Metis 审查结果

| # | 问题 | 分类 | 解决方式 |
|---|------|------|----------|
| 1 | Init 时序 | ✅ 已决策 | ESA_Init 在 BSP main() 中 OS_Application_Startup 之前，仅 generic-linux |
| 2 | cFE 内联 ShimEvent_t 重复 | ✅ 自动解决 | 保留现状，后续修复 |
| 3 | API 重命名时机 | ✅ 已决策 | 先提取后重命名 |
| 4 | Shim header 归属 | ✅ 默认值 | 迁入 ESA public_inc/ |
| 5 | UT stubs 归属 | ✅ 默认值 | ESA 拥有主 stub，OSAL 保留 noop |

### 提取影响分析

**命名变更清单** (`CFE_PSP_SimStepping_*` → `ESA_*`):
- 公共 API: `CFE_PSP_SimStepping_Hook_GetTime` → `ESA_Hook_GetTime`
- 公共 API: `CFE_PSP_SimStepping_Shim_ReportEvent` → `ESA_Shim_ReportEvent`
- 公共 API: `CFE_PSP_SimStepping_Hook_TaskDelayEligible` → `ESA_Hook_TaskDelayEligible`
- 公共 API: `CFE_PSP_SimStepping_WaitForDelayExpiry` → `ESA_WaitForDelayExpiry`
- 类型: `CFE_PSP_SimStepping_EventKind_t` → `ESA_EventKind_t`
- 类型: `CFE_PSP_SimStepping_ShimEvent_t` → `ESA_ShimEvent_t`
- 宏: `CFE_SIM_STEPPING` → 考虑保持（编译开关独立于模块命名）或改为 `ESA_ENABLED`
- 文件名: `cfe_psp_sim_stepping_*` → `esa_*`

**OSAL shared 层 hook 命名**:
- `OS_PosixStepping_Hook_*` → 考虑改为 `OS_Stepping_Hook_*`（不再 POSIX 特定，因为在 shared 层）

**构建系统变更**:
- 新增 `external_simulation_adaptor/CMakeLists.txt`
- 新增 `external_simulation_adaptor/` INTERFACE target（类似 `osal_public_api`）
- `arch_build.cmake` 新增 ESA 子目录（在 OSAL 之后，PSP 之前）
- `mission_build.cmake` 新增 ESA 依赖
- `targets.cmake` 新增 ESA 配置
- 移除 `psp/fsw/modules/sim_stepping/` 或保留为 stub/redirect

**初始化时序变更**:
- 当前: PSP module init 自动调用 `CFE_PSP_SimSteppingInit()`
- 目标: ESA 独立 init，在 startup sequence 中显式调用 `ESA_Init()` — PSP init 之后，OSAL init 之前
- 需要修改 `cfe/modules/core_api/` 或 startup bootstrap 序列来插入 ESA init 调用

**与 shared 层 shim 的关系**:
- 独立模块化后，shared 层的函数指针回调直接指向 ESA API — 架构上合理
- OSAL → ESA 依赖合法（ESA 是独立底层模块，与 OSAL 同级或更低）
- 不再需要通过 PSP 间接传递

### 测试策略决策

- **测试基础设施**: ✅ 已有 ut_assert 框架 + 覆盖测试 + UT stubs
- **自动化测试**: ✅ TDD — 先迁移/重写测试，确保核心行为不因提取而改变
- **现有测试资产**:
  - `psp/unit-test-coverage/modules/sim_stepping/coveragetest-sim_stepping.c` (119 行)
  - `psp/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` (12 行)
- **测试迁移**: 测试文件从 PSP 目录迁入 `external_simulation_adaptor/ut-coverage/`
- **API 重命名测试**: 确保所有 `CFE_PSP_SimStepping_*` → `ESA_*` 后测试仍然通过

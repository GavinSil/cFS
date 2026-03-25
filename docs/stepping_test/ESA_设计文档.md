# ESA 仿真步进引擎 — 设计文档

> **模块**: ESA (External Simulation Adapter)
> **版本**: 基于 cFS Draco v7.0 集成包
> **日期**: 2026-03-24

## 目录

- [第一章：引言与动机](#第一章引言与动机)
- [第二章：架构总览](#第二章架构总览)
- [第三章：Shim 层 — 统一事件报告 ABI](#第三章shim-层--统一事件报告-abi)
- [第四章：Core 状态机 — 步进语义的唯一所有者](#第四章core-状态机--步进语义的唯一所有者)
- [第五章：双阶段事件模型 — ACK + COMPLETE](#第五章双阶段事件模型--ack--complete)
- [第六章：时间推进机制](#第六章时间推进机制)
- [第七章：控制适配器](#第七章控制适配器)
- [第八章：构建系统集成](#第八章构建系统集成)
- [第九章：ESA Stepping Terminal 工具](#第九章esa-stepping-terminal-工具)
- [第十章：演进历史](#第十章演进历史)

---

## 第一章：引言与动机

### 1.1 为什么需要仿真步进？

cFS（核心飞行系统）是一个实时多任务飞行软件框架。在飞行硬件上，调度器（SCH）由硬件定时器驱动，以固定周期（通常 10ms 次要帧）触发消息分发，各应用在 OSAL 任务中并发运行，时间由 PSP 时基模块从硬件时钟获取。

但在 **仿真/测试环境** 中，这种实时架构带来了根本性矛盾：

| 维度 | 飞行模式 | 仿真需求 |
|------|----------|----------|
| 时间源 | 硬件时钟（wall-clock） | 需要确定性仿真时间 |
| 调度触发 | 硬件定时器中断 | 需要外部控制器精确触发 |
| 任务同步 | 操作系统调度器 | 需要可预测的执行顺序 |
| 可重复性 | 依赖运行时条件 | 要求严格确定性 |

传统做法是让 cFS 以实时速度运行，然后通过日志分析行为。但对于需要与外部仿真器（如 Simulink、MATLAB、STK）联合仿真的场景，这种方法无法满足以下需求：

1. **时间同步**：外部仿真器需要和 cFS 共享同一时间轴
2. **因果确定性**：每个步进周期的输出必须仅取决于输入，不受 wall-clock 抖动影响
3. **单步调试**：开发者需要逐帧观察飞行软件行为
4. **自动化测试**：测试框架需要精确控制"推进 N 个时间片"

### 1.2 ESA 解决什么问题？

ESA（External Simulation Adapter，外部仿真适配器）步进引擎是 cFS 的 **可选仿真步进控制模块**，它允许外部控制器通过精确的命令-响应协议控制飞行软件的时间推进。

核心能力：

- **确定性时间推进**：仿真时间仅通过显式步进命令前进（每次 10ms 量子），不依赖 wall-clock
- **同步调度控制**：步进命令触发 SCH 执行一个完整的次要帧调度周期
- **阻塞操作跟踪**：精确监控所有 OSAL 阻塞操作（TaskDelay、QueueReceive、BinSemTake）的进入和退出
- **完成判定**：通过触发器/确认计数机制判定每个步进周期何时完成
- **外部控制接口**：通过 Unix Domain Socket 暴露控制协议，支持任意外部进程控制步进

### 1.3 设计目标

| 目标 | 实现方式 |
|------|----------|
| **确定性** | 仿真时间由 `AdvanceOneQuantum()` 唯一写入路径控制，每次推进固定 10ms |
| **最小侵入** | OSAL/cFE/PSP 的修改仅为"事实报告"Hook，不改变原有控制流 |
| **可选编译** | 通过 `CFE_SIM_STEPPING` 编译开关控制，未启用时零代码参与 |
| **零运行时开销** | 未启用时所有 Hook 为空函数或 `return false`，编译器可内联消除 |
| **双通道控制** | 支持进程内（InProc）和进程外（UDS）两种控制方式 |

---

## 第二章：架构总览

### 2.1 分层架构

ESA 步进引擎采用严格的分层架构，从上到下共五层：

```
┌─────────────────────────────────────────────────────────┐
│                    外部控制层                             │
│  ┌─────────────────────┐  ┌──────────────────────────┐  │
│  │  InProc 适配器       │  │  UDS 适配器               │  │
│  │  (进程内 API 调用)   │  │  (/tmp/cfe_sim_stepping   │  │
│  │  BeginStep()        │  │   .sock)                  │  │
│  │  WaitStepComplete() │  │  后台 pthread 服务线程     │  │
│  │  QueryState()       │  │  3 种操作码               │  │
│  └─────────────────────┘  └──────────────────────────┘  │
├─────────────────────────────────────────────────────────┤
│                    ESA 引擎层                             │
│  ┌───────────────────────────────────────────────────┐  │
│  │              ESA Stepping Core                     │  │
│  │  状态机: INIT → READY → RUNNING → WAITING → COMPLETE│
│  │  触发器跟踪 (最多 32 个)                           │  │
│  │  仿真时间管理 (纳秒精度)                           │  │
│  │  TaskDelay 接管控制                                │  │
│  │  诊断计数器                                        │  │
│  └───────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────┐  │
│  │              ESA Stepping Shim                     │  │
│  │  统一事件转发: ReportEvent() → Core Report*()      │  │
│  │  22 种事件类型, 单入口点                           │  │
│  └───────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────┤
│                 cFE 核心 Hook 层                         │
│  ES/EVS/SB/TBL: CmdPipeReceive + CmdPipeComplete       │
│  TIME: TaskCycle + 1HzBoundary + ToneSignal             │
│        ToneSemConsume + Local1HzSemConsume               │
│  SCH:  SemaphoreWait + MinorFrame + MajorFrame          │
│        SendTrigger + DispatchComplete                    │
├─────────────────────────────────────────────────────────┤
│                OSAL POSIX Hook 层                        │
│  OS_TaskDelay:    Hook_Ack + Hook_Complete               │
│  OS_QueueGet:     Hook_Ack + Hook_Complete               │
│  OS_BinSemTake:   Hook_Ack + Hook_Complete               │
├─────────────────────────────────────────────────────────┤
│                 PSP 时基层                               │
│  ESA_Stepping_Hook_GetTime() → 返回仿真时间             │
│  替代 clock_gettime() 的 wall-clock 时间                │
└─────────────────────────────────────────────────────────┘
```

### 2.2 核心设计原则

**原则一：薄层转发（Thin Shim）**

所有 OSAL/cFE/PSP 模块中的 Hook 仅负责 **事实报告**（fact reporting），不实现任何业务逻辑。Hook 构造一个 `ESA_Stepping_ShimEvent_t` 事件描述符，调用 `ESA_Stepping_Shim_ReportEvent()` 后立即返回。这确保了：
- Hook 代码极其简单，易于审计
- 所有语义集中在 Core 中，避免分散逻辑
- 新增 Hook 只需添加事件类型，不需要理解状态机

**原则二：集中状态机**

全部步进语义由单个 `ESA_Stepping_Core_t` 实例维护。Core 是状态、触发器、确认计数、仿真时间的唯一所有者（single owner）。InProc 和 UDS 适配器都是 Core 的薄包装，不维护独立状态。

**原则三：双阶段模型（ACK + COMPLETE）**

每个阻塞操作被拆分为两个事件：
- **ACK**（确认）：阻塞操作开始前报告，表示任务即将进入等待
- **COMPLETE**（完成）：阻塞操作返回后报告，表示任务已从等待状态恢复

这允许 Core 精确知道在任意时刻有多少任务正在等待，从而判定步进周期是否完成。

**原则四：多控制通道**

支持两种控制方式，共享同一个 Core 实例：
- **InProc**（进程内）：C 函数调用，适用于单元测试和进程内仿真器
- **UDS**（Unix Domain Socket）：网络协议，适用于外部控制进程

### 2.3 关键数据流路径

一个典型步进周期的数据流：

```
外部控制器                    ESA 引擎                        cFS 运行时
    │                           │                               │
    │── BEGIN_STEP ──────────>  │                               │
    │                           │── BeginStepSession()          │
    │                           │── BinSemGive(SCH_TIME_SEM) ──>│
    │                           │                               │── SCH 执行次要帧
    │                           │                               │── 发送调度消息
    │                           │<── ReportSchMinorFrame() ─────│
    │                           │   (AdvanceOneQuantum: +10ms)  │
    │                           │<── ReportSchSendTrigger() ────│
    │                           │<── ReportCoreServiceReceive()─│
    │                           │<── ReportCoreServiceComplete()│
    │                           │<── ReportSchDispatchComplete()│
    │                           │   → 状态: RUNNING → WAITING   │
    │                           │                               │
    │── WAIT_COMPLETE ───────>  │                               │
    │                           │── IsStepComplete()            │
    │                           │   acks_received >= expected   │
    │                           │   → 状态: COMPLETE            │
    │<── SUCCESS ───────────────│                               │
```

---

## 第三章：Shim 层 — 统一事件报告 ABI

### 3.1 设计意图

Shim 层是 ESA 步进引擎与 cFS 运行时之间的 **唯一接触面**。它的职责只有一个：将分散在 OSAL、cFE、PSP 各层的 Hook 调用 **统一为单一入口点**，由 Core 层集中处理。

为什么需要这一层？原因有三：

1. **ABI 隔离**：Hook 调用者（OSAL/cFE/PSP）只需链接 Shim 头文件，无需了解 Core 内部结构。如果 Core 的数据结构或 API 发生变化，Hook 层代码不受影响。
2. **事件描述统一**：22 种事件共用同一个 `ESA_Stepping_ShimEvent_t` 结构体，4 个字段即可描述所有事件。
3. **编译解耦**：Shim 头文件位于 `esa/public_inc/`，是 ESA 模块的公共接口；Core 头文件位于 `esa/fsw/inc/`，是内部实现。

### 3.2 事件描述符

```c
typedef struct
{
    ESA_Stepping_EventKind_t event_kind;     /* 事件类型（22 种之一） */
    uint32                   entity_id;      /* 实体标识：队列 ID、信号量 ID、服务 ID 等 */
    uint32                   task_id;        /* 触发事件的任务 ID */
    uint32                   optional_delay_ms; /* 仅 TaskDelay 类事件使用 */
} ESA_Stepping_ShimEvent_t;
```

**设计取舍**：4 个字段足以覆盖所有 22 种事件。`entity_id` 的语义随事件类型变化 — 对于 QueueReceive 它是队列 ID，对于 BinSemTake 它是信号量 ID，对于 CoreService 它是服务枚举值。这种"多态字段"避免了使用 union 或继承，保持了结构体的简单性。

### 3.3 22 种事件类型分类

事件按来源分为五个类别：

#### OSAL 阻塞操作事件（6 种）

| 事件 | 索引 | 含义 |
|------|------|------|
| `TASK_DELAY_CALL` | 0 | 任务调用 OS_TaskDelay()，报告延时量 |
| `TASK_DELAY_ACK` | 1 | TaskDelay 阻塞操作进入前确认 |
| `TASK_DELAY_COMPLETE` | 2 | TaskDelay 阻塞操作完成 |
| `QUEUE_RECEIVE_ACK` | 3 | OS_QueueGet() 进入前确认 |
| `QUEUE_RECEIVE_COMPLETE` | 4 | OS_QueueGet() 返回后完成 |
| `BINSEM_TAKE_ACK` | 5 | OS_BinSemTake() 进入前确认 |
| `BINSEM_TAKE_COMPLETE` | 6 | OS_BinSemTake() 返回后完成 |

#### SCH 调度器事件（6 种）

| 事件 | 索引 | 含义 |
|------|------|------|
| `SCH_SEMAPHORE_WAIT` | 7 | SCH 等待时间信号量（调度循环入口） |
| `SCH_MINOR_FRAME` | 8 | SCH 开始次要帧处理（**唯一时间推进点**） |
| `SCH_MAJOR_FRAME` | 9 | SCH 检测到主帧边界 |
| `SCH_SEND_TRIGGER` | 10 | SCH 向目标应用发送调度消息 |
| `SCH_DISPATCH_COMPLETE` | 11 | SCH 一轮调度消息发送完毕 |
| `SCH_NOOPS_DISPATCH_COMPLETE` | 12 | SCH 调度轮次中无消息发送 |

#### cFE 核心服务事件（2 种）

| 事件 | 索引 | 含义 |
|------|------|------|
| `CORE_SERVICE_CMD_PIPE_RECEIVE` | 13 | cFE 核心服务收到命令管道消息 |
| `CORE_SERVICE_CMD_PIPE_COMPLETE` | 14 | cFE 核心服务处理完命令管道消息 |

五个核心服务（ES=0, EVS=1, SB=2, TBL=3, TIME=4）共用这两个事件类型，通过 `entity_id` 区分。

#### TIME 子任务事件（6 种）

| 事件 | 索引 | 含义 |
|------|------|------|
| `TIME_TASK_CYCLE` | 15 | TIME 主任务循环开始 |
| `TIME_1HZ_BOUNDARY` | 16 | TIME 检测到 1Hz 边界 |
| `TIME_TONE_SIGNAL` | 17 | TIME 收到 Tone 信号 |
| `TIME_TONE_SEM_CONSUME` | 18 | TIME Tone 子任务获取信号量 |
| `TIME_LOCAL_1HZ_SEM_CONSUME` | 19 | TIME Local1Hz 子任务获取信号量 |
| `TASK_DELAY_RETURN` | 20 | TaskDelay 到期返回（用于 TaskDelay 接管机制） |

#### 系统事件（1 种）

| 事件 | 索引 | 含义 |
|------|------|------|
| `TASK_DELAY_ELIGIBLE_QUERY` | 21 | 查询任务是否参与 TaskDelay 接管 |

### 3.4 单入口分发

整个 Shim 层在 PSP 侧的实现（`esa_stepping.c`）中表现为一个 22 路 switch 语句：

```c
void ESA_Stepping_Shim_ReportEvent(const ESA_Stepping_ShimEvent_t *event)
{
    switch (event->event_kind)
    {
        case ESA_STEPPING_EVENT_TASK_DELAY_CALL:
            ESA_Stepping_Core_ReportTaskDelay(&stepping_core, ...);
            break;
        case ESA_STEPPING_EVENT_TASK_DELAY_ACK:
            ESA_Stepping_Core_ReportTaskDelayAck(&stepping_core, ...);
            break;
        /* ... 共 22 个 case ... */
    }
}
```

每个 case 最多做一次参数解包 + 一次 Core 函数调用，无额外逻辑。这使得 Shim 分发表在形式上等同于 **虚函数表**（vtable），但用 C switch 实现，无运行时多态开销。

### 3.5 Hook 侧使用模式

所有 Hook 遵循相同的 3 行模式：

```c
/* 在 OSAL/cFE/PSP 的 Hook 点 */
ESA_Stepping_ShimEvent_t ev = {
    .event_kind       = ESA_STEPPING_EVENT_XXX,
    .entity_id        = relevant_id,
    .task_id          = OS_TaskGetId(),
    .optional_delay_ms = 0
};
ESA_Stepping_Shim_ReportEvent(&ev);
```

构造栈上事件描述符 → 调用单入口 → 返回。Hook 不需要了解 Core 状态机、触发器计数、还是完成判定的任何细节。

---

## 第四章：Core 状态机 — 步进语义的唯一所有者

### 4.1 Core 的角色

`ESA_Stepping_Core_t` 是整个步进引擎的 **唯一状态持有者**。所有步进语义 — 状态转换、触发器跟踪、确认计数、仿真时间推进、TaskDelay 接管、完成判定 — 全部集中在这个结构体及其操作函数中。

InProc 适配器和 UDS 适配器都是 Core 的薄包装层：它们将外部请求转换为 Core API 调用，不维护额外状态。这种设计确保了无论通过哪种通道控制步进，语义完全一致。

### 4.2 五状态机

```
                    BeginStepSession()
                    ┌──────────────┐
                    ▼              │
    ┌──────┐    ┌───────┐    ┌─────────┐    ┌─────────┐    ┌──────────┐
    │ INIT │───>│ READY │───>│ RUNNING │───>│ WAITING │───>│ COMPLETE │
    └──────┘    └───────┘    └─────────┘    └─────────┘    └──────────┘
       │         Init()      SchMinorFrame   SchDispatch     IsStep
       │                     SchSendTrigger  Complete()      Complete()
       │                                                       │
       │                                                       │
       └───────── MarkSystemReadyForStepping() ────────────────┘
                  (INIT → READY, 仅一次)
```

| 状态 | 含义 | 进入条件 |
|------|------|----------|
| **INIT** | 初始化完成但系统尚未就绪 | `Core_Init()` |
| **READY** | 等待新步进周期 | `MarkSystemReadyForStepping()` 或 `BeginStepSession()` |
| **RUNNING** | 步进周期正在执行 | 收到 `SchMinorFrame` 或 `SchSendTrigger` 事件 |
| **WAITING** | 等待所有触发器确认完成 | `SchDispatchComplete()` 后仍有未确认触发器 |
| **COMPLETE** | 步进周期完成 | `IsStepComplete()` 判定所有确认到齐 |

**状态转换规则**：
- INIT → READY：仅通过 `MarkSystemReadyForStepping()`，在系统启动完成后调用一次
- READY → RUNNING：收到首个运行时事件（SchMinorFrame 或 SchSendTrigger）
- RUNNING → WAITING：`SchDispatchComplete()` 后存在已确认触发器
- RUNNING → COMPLETE：`IsStepComplete()` 中 acks_received ≥ acks_expected
- WAITING → COMPLETE：同上，由 `IsStepComplete()` 驱动
- COMPLETE → READY：下一次 `BeginStepSession()` 重置

### 4.3 触发器机制

触发器（Trigger）是 Core 跟踪步进周期内活跃操作的核心数据结构。

```c
typedef struct
{
    uint32 trigger_id;       /* 自增序号 */
    uint32 source_mask;      /* 事件来源掩码 */
    uint32 entity_id;        /* 操作实体标识 */
    bool   is_acknowledged;  /* 是否已收到 COMPLETE 确认 */
} ESA_Stepping_Trigger_t;
```

**触发器生命周期**：
1. **创建**：事件的 ACK 阶段调用 `AddTrigger(source_mask, entity_id)` 分配触发器槽位
2. **确认**：事件的 COMPLETE 阶段调用 `AcknowledgeTrigger(source_mask, entity_id)` 设置 `is_acknowledged = true`
3. **清除**：`BeginStepSession()` 调用 `ClearTriggers()` 重置所有槽位

每个步进周期最多 32 个触发器（`ESA_SIM_STEPPING_MAX_TRIGGERS`）。超出时 `AddTrigger()` 返回 `-1`。

### 4.4 来源掩码表（Source Mask）

来源掩码用于区分不同类型的触发器，是触发器匹配和去重的关键字段：

| 来源 | 掩码值 | 用途 |
|------|--------|------|
| TaskDelay | `0x0100` | OSAL 任务延时操作 |
| QueueReceive | `0x0200` | OSAL 消息队列接收操作 |
| BinSemTake | `0x0800` | OSAL 二值信号量获取操作 |
| SchSendTrigger | `0x2000` | SCH 调度器发送的调度消息 |
| CoreService | `0x8000` | cFE 核心服务命令管道操作 |
| TimeTone | `0x10000` | TIME Tone 子任务信号量操作 |
| TimeLocal1Hz | `0x20000` | TIME Local1Hz 子任务信号量操作 |

**去重逻辑**：`AddTrigger()` 在添加前通过 `HasTrigger(source_mask, entity_id)` 检查是否已存在相同的触发器。对于 QueueReceive 和 BinSemTake，同一实体在同一步进周期内只创建一个触发器。

### 4.5 确认计数与完成判定

Core 维护两个关键计数器：

- `acks_expected`：步进周期内创建的触发器总数
- `acks_received`：已收到 COMPLETE 确认的触发器数

完成判定（`IsStepComplete()`）的逻辑：

```
if (completion_requested && completion_ready) {
    if (acks_expected == 0 && state == RUNNING) {
        /* 空会话延迟完成：无触发器产生 → 直接完成 */
        state = COMPLETE;
        return true;
    }
    if (HasTaskDelayDebt()) {
        return false;  /* 仍有 TaskDelay 债务未清 */
    }
    if (acks_received >= acks_expected) {
        state = COMPLETE;
        session_active = false;
        return true;
    }
}
return false;
```

两个关键标志：
- `completion_ready`：由 `ReportSchMinorFrame()` 设置，表示时间已推进
- `completion_requested`：由控制适配器设置，表示外部已请求等待完成

### 4.6 TaskDelay 接管

TaskDelay 接管是 ESA 步进引擎最精妙的机制之一。在正常运行中，`OS_TaskDelay(ms)` 由操作系统调度器控制延时。但在步进模式下，wall-clock 时间没有意义 — 延时必须基于仿真时间。

**接管机制**：

- 任务主动注册：首次调用 `ReportTaskDelay()` 时自动加入 `optin_set[8]`（最多 8 个任务）
- 到期判定：每次步进时计算 `expiry_ns`，当 `sim_time_ns >= expiry_ns` 时延时到期
- 债务跟踪：`owed[8]` 记录各任务尚欠的 TaskDelay 调用次数
- 完成阻塞：`HasTaskDelayDebt()` 为 true 时，`IsStepComplete()` 不会返回完成

**数组结构**（每个最多 8 个条目）：
- `optin_set[8]`：已注册参与接管的任务 ID
- `expiry_ns[8]`：各任务当前延时的仿真到期时间
- `pending[8]`：各任务是否有挂起的延时
- `owed[8]`：各任务尚欠的延时次数

### 4.7 核心服务成员位掩码

Core 通过 `core_service_membership_mask` 跟踪哪些 cFE 核心服务已注册。五个核心服务对应 5 个位：

| 服务 | 位索引 | 服务 ID |
|------|--------|---------|
| ES (Executive Services) | 0 | `CFE_ES_SERVICE` |
| EVS (Event Services) | 1 | `CFE_EVS_SERVICE` |
| SB (Software Bus) | 2 | `CFE_SB_SERVICE` |
| TBL (Table Services) | 3 | `CFE_TBL_SERVICE` |
| TIME (Time Services) | 4 | `CFE_TIME_SERVICE` |

此外还有两个子路径位用于 TIME 子任务：
- 位 5：`CHILDPATH_BIT_TIME_TONE`
- 位 6：`CHILDPATH_BIT_TIME_LOCAL_1HZ`

### 4.8 会话生命周期

```
BeginStepSession()
    ├── 前置检查: system_ready_for_stepping == true?
    ├── 重复检查: IsStepComplete_ReadOnly() == false? → 诊断 DUPLICATE_BEGIN
    ├── ClearTriggers(): 重置所有触发器槽位
    ├── session_active = true
    ├── session_counter++
    ├── completion_ready = false
    ├── completion_requested = false
    ├── acks_expected = 0, acks_received = 0
    └── state = READY

    ... 运行时事件报告 ...

IsStepComplete()
    ├── 检查 completion_requested && completion_ready
    ├── 检查 TaskDelay 债务
    ├── 比较 acks_received >= acks_expected
    ├── state = COMPLETE
    └── session_active = false
```

### 4.9 诊断子系统

Core 维护 6 类诊断计数器，用于检测和记录异常情况：

| 诊断类 | 含义 |
|--------|------|
| `TIMEOUT` | 等待步进完成超时 |
| `DUPLICATE_BEGIN` | 前一步进未完成即发起新步进 |
| `ILLEGAL_COMPLETE` | 收到无匹配触发器的 COMPLETE 事件 |
| `ILLEGAL_STATE` | 在不合法状态下收到事件 |
| `TRANSPORT_ERROR` | UDS 传输层错误 |
| `PROTOCOL_ERROR` | UDS 协议层错误 |

`RecordDiagnostic()` 递增计数器并通过 `printf()` 输出标准化日志行，格式为：

```
[ESA-DIAG] <class>: <description> (count=N)
```

---

## 第五章：双阶段事件模型 — ACK + COMPLETE

### 5.1 为什么需要两个阶段？

cFS 是多任务并发系统。当步进引擎推进一个时间量子后，多个任务会被唤醒并执行。问题在于：**引擎如何知道所有任务都完成了本周期的工作？**

如果只用一个事件（"任务完成了操作"），引擎无法区分两种情况：
- 任务还没开始操作（尚未调度到）
- 任务已经完成了操作

双阶段模型解决了这个问题：

- **ACK**（确认阶段）：在阻塞操作 **进入前** 报告。Core 创建一个触发器，`acks_expected++`。这告诉引擎"有一个操作即将发生"。
- **COMPLETE**（完成阶段）：在阻塞操作 **返回后** 报告。Core 确认对应触发器，`acks_received++`。这告诉引擎"那个操作已经完成了"。

当 `acks_received >= acks_expected` 时，所有已知操作都已完成，步进周期可以结束。

### 5.2 三个 OSAL 操作对

#### TaskDelay (source_mask = 0x0100)

```
任务代码          OSAL Hook                    Core
  │                │                            │
  │── OS_TaskDelay(ms) ──>│                     │
  │                │── TASK_DELAY_CALL ───────>  │  ReportTaskDelay(): 注册 optin_set
  │                │── TASK_DELAY_ACK ────────>  │  ReportTaskDelayAck(): AddTrigger(0x100, task_id)
  │                │    [实际延时/等待仿真到期]  │
  │                │── TASK_DELAY_COMPLETE ───>  │  ReportTaskDelayComplete(): AcknowledgeTrigger
  │<── 返回 ───────│                            │
```

TaskDelay 有三个事件（比其他 OSAL 操作多一个 `CALL`），因为它需要额外报告延时量（`optional_delay_ms`），用于 TaskDelay 接管机制的到期计算。

**守卫条件**：ACK 和 COMPLETE 都要求 `session_active && completion_ready`，确保只在活跃步进周期的时间推进后才跟踪。

#### QueueReceive (source_mask = 0x0200)

```
任务代码          OSAL Hook                    Core
  │                │                            │
  │── OS_QueueGet() ──>│                        │
  │                │── QUEUE_RECEIVE_ACK ────>  │  AddTrigger(0x200, queue_id)
  │                │    [实际队列等待]           │
  │                │── QUEUE_RECEIVE_COMPLETE ─> │  AcknowledgeTrigger(0x200, queue_id)
  │<── 返回 ───────│                            │
```

**去重保护**：ACK 阶段通过 `HasTrigger(0x200, queue_id)` 检查。如果同一队列在同一步进周期内多次调用 QueueGet，只创建一个触发器。

#### BinSemTake (source_mask = 0x0800)

```
任务代码          OSAL Hook                    Core
  │                │                            │
  │── OS_BinSemTake() ─>│                       │
  │                │── BINSEM_TAKE_ACK ──────>  │  AddTrigger(0x800, sem_id)
  │                │    [实际信号量等待]          │
  │                │── BINSEM_TAKE_COMPLETE ──>  │  AcknowledgeTrigger(0x800, sem_id)
  │<── 返回 ───────│                            │
```

与 QueueReceive 结构相同，去重逻辑相同。

### 5.3 CoreService 对 (source_mask = 0x8000)

cFE 的五个核心服务（ES、EVS、SB、TBL、TIME）各有一个命令管道。当 SCH 调度消息到达时：

```
SCH 分发             cFE 核心服务                Core
  │                     │                        │
  │── 调度消息 ────────>│                        │
  │                     │── CMD_PIPE_RECEIVE ──> │  AddTrigger(0x8000, service_id)
  │                     │   [处理命令]            │  设置 membership_mask 对应位
  │                     │── CMD_PIPE_COMPLETE ──>│  AcknowledgeTrigger(0x8000, service_id)
  │                     │                        │
```

**成员位映射**：Core 将 `service_id` 映射为位索引（0-4），设置 `core_service_membership_mask` 的对应位。这使得 Core 能感知哪些核心服务参与了当前步进周期。

**ILLEGAL_COMPLETE 检测**：如果收到 `CMD_PIPE_COMPLETE` 但找不到匹配的触发器（`AcknowledgeTrigger` 返回失败），Core 记录 `ILLEGAL_COMPLETE` 诊断。

### 5.4 TIME 子任务对

TIME 模块有两个子任务，各通过信号量获取触发：

#### TimeTone 对 (source_mask = 0x10000)

```
TIME 主任务           TIME Tone 子任务            Core
  │                     │                          │
  │── SemGive ────────>│                          │
  │                     │── TONE_SEM_CONSUME ───> │  AddTrigger(0x10000, sem_id)
  │                     │                          │  设置 CHILDPATH_BIT_TIME_TONE
  │                     │   [处理 Tone 信号]       │
  │── TONE_SIGNAL ──────────────────────────────> │  批量确认所有 0x10000 触发器
```

#### TimeLocal1Hz 对 (source_mask = 0x20000)

```
TIME 主任务           TIME 1Hz 子任务             Core
  │                     │                          │
  │── SemGive ────────>│                          │
  │                     │── LOCAL1HZ_SEM_CONSUME ─>│  AddTrigger(0x20000, sem_id)
  │                     │                          │  设置 CHILDPATH_BIT_TIME_LOCAL_1HZ
  │                     │   [处理 1Hz 边界]        │
  │── 1HZ_BOUNDARY ─────────────────────────────> │  批量确认所有 0x20000 触发器
```

**注意**：TIME 子任务的 COMPLETE 事件不由子任务自身报告，而是由 TIME 主任务在处理 ToneSignal 或 1HzBoundary 时批量确认。这是因为子任务的完成时机与主任务的处理流程耦合。

### 5.5 SCH 对 (source_mask = 0x2000)

SCH 的调度消息是特殊的 — 它不是阻塞操作，而是 **主动推送**：

```
SCH 调度循环                               Core
  │                                         │
  │── SCH_SEND_TRIGGER(target_id) ───────> │  AddTrigger(0x2000, target_id)
  │── SCH_SEND_TRIGGER(target_id) ───────> │  AddTrigger(0x2000, target_id)
  │── SCH_SEND_TRIGGER(target_id) ───────> │  AddTrigger(0x2000, target_id)
  │── SCH_DISPATCH_COMPLETE ─────────────> │  批量确认所有 0x2000 触发器
  │                                         │  如有已确认触发器 → 状态转 WAITING
```

**批量确认**：`ReportSchDispatchComplete()` 遍历所有触发器，将 `source_mask == 0x2000` 的触发器全部确认。这与单操作的点对点确认不同。

**NOOPS 分支**：如果 SCH 在一个次要帧中没有发送任何调度消息，报告 `SCH_NOOPS_DISPATCH_COMPLETE`（索引 12），Core 当前不对此事件做特殊处理，但保留了扩展接口。

### 5.6 总结：确认模式对比

| 来源 | ACK 时机 | COMPLETE 时机 | 确认方式 |
|------|----------|---------------|----------|
| TaskDelay | 阻塞前 | 阻塞返回后 | 点对点（task_id） |
| QueueReceive | 阻塞前 | 阻塞返回后 | 点对点（queue_id），去重 |
| BinSemTake | 阻塞前 | 阻塞返回后 | 点对点（sem_id），去重 |
| CoreService | 管道接收 | 命令处理完 | 点对点（service_id） |
| TimeTone | 信号量获取 | 主任务 ToneSignal | 批量确认（0x10000） |
| TimeLocal1Hz | 信号量获取 | 主任务 1HzBoundary | 批量确认（0x20000） |
| SchSendTrigger | 消息发送 | DispatchComplete | 批量确认（0x2000） |

---

## 第六章：时间推进机制

### 6.1 唯一写入路径

ESA 步进引擎的仿真时间有且仅有一个写入路径：

```c
void ESA_Stepping_Core_AdvanceOneQuantum(ESA_Stepping_Core_t *core)
{
    core->sim_time_ns      += core->step_quantum_ns;
    core->next_sim_time_ns += core->step_quantum_ns;
}
```

此函数 **只被一个地方调用**：`ReportSchMinorFrame()`。这意味着：

- 仿真时间只在 SCH 次要帧事件时前进
- 每次前进固定 `step_quantum_ns`（默认 10,000,000 ns = 10ms）
- 没有任何其他代码路径可以修改仿真时间
- 即使运行 1000 个步进周期，时间推进始终是确定性的：`N × 10ms`

### 6.2 SchMinorFrame 作为唯一触发点

`ReportSchMinorFrame()` 是步进引擎中最关键的函数之一：

```c
void ESA_Stepping_Core_ReportSchMinorFrame(ESA_Stepping_Core_t *core)
{
    ESA_Stepping_Core_AdvanceOneQuantum(core);   /* 推进仿真时间 */
    core->completion_ready = true;                /* 标记"时间已推进" */

    if (core->state == ESA_STEPPING_STATE_READY)
    {
        core->state = ESA_STEPPING_STATE_RUNNING; /* READY → RUNNING */
    }
}
```

它完成三件事：
1. **推进仿真时间**：调用 `AdvanceOneQuantum()`
2. **设置 completion_ready**：这是完成判定的前提条件（见第四章 4.5 节）
3. **状态转换**：如果当前在 READY 状态，转入 RUNNING

**为什么由 SCH 驱动？** 在 cFS 架构中，SCH 是整个系统的"心跳"。每个次要帧由 SCH 发起，SCH 决定本帧中哪些应用需要被调度。因此 SCH 的次要帧处理天然代表了"一个时间片的开始"。

### 6.3 量子粒度

默认量子为 10ms（10,000,000 纳秒），与 cFS 标准次要帧周期一致。这个值在 `Core_Init()` 中设置：

```c
core->step_quantum_ns = 10000000;  /* 10ms */
```

纳秒精度的选择是为了与 `CFE_TIME_SysTime_t` 的子秒精度对齐，同时避免浮点运算。所有时间计算都使用 `uint64` 整数算术。

### 6.4 仿真时间与 Wall-Clock 时间的关系

```
                Wall-Clock 时间轴（不可控）
    ──────────────────────────────────────────────────>
    t=0       t=5ms     t=200ms    t=201ms   t=500ms

                仿真时间轴（确定性）
    ──────────────────────────────────────────────────>
    t=0       t=10ms    t=20ms     t=30ms    t=40ms
              ▲         ▲          ▲         ▲
              步进1     步进2      步进3     步进4

    每次步进推进恰好 10ms，与 wall-clock 经过多少时间无关。
```

当 PSP 时基模块调用 `ESA_Stepping_Hook_GetTime()` 获取当前时间时，返回的是仿真时间而非 wall-clock 时间。这确保了：
- cFE TIME 模块报告的时间与步进引擎同步
- 应用中的时间戳反映仿真时间轴
- 时间比较和超时判断基于仿真时间

### 6.5 TaskDelay 接管与时间推进的交互

TaskDelay 接管机制与时间推进紧密关联。当任务调用 `OS_TaskDelay(ms)` 时：

1. **到期时间计算**：`expiry_ns = sim_time_ns + (delay_ms × 1,000,000)`
2. **每次步进后检查**：`IsStepComplete()` 调用 `HasTaskDelayDebt()` 检查是否有任务的延时仍未到期
3. **到期判定**：当 `sim_time_ns >= expiry_ns` 时，延时到期，债务清除

```
    步进1 (t=10ms)   步进2 (t=20ms)   步进3 (t=30ms)
    ─────────────────────────────────────────────>
         │                │                │
         │  TaskDelay(25ms)               │
         │  expiry = 10+25 = 35ms         │
         │                │                │
         │  debt? YES     debt? YES        debt? YES
         │                │                │
    步进4 (t=40ms)
    ─────────>
         │
         │  40ms >= 35ms → debt清除
         │  debt? NO → 可以完成步进
```

这确保了即使 wall-clock 时间远快于仿真时间，TaskDelay 的行为仍然严格基于仿真时间轴。

### 6.6 WaitStepComplete 中的自动触发

`InProc_WaitStepComplete()` 实现了一个关键的自动化逻辑：

```c
if (!stepping_core.completion_ready)
{
    /* 时间尚未推进，自动触发 SchMinorFrame */
    ESA_Stepping_Core_ReportSchMinorFrame(&stepping_core);
}
```

**为什么需要自动触发？** 在 InProc 控制模式下，`BeginStep()` 通过 `OS_BinSemGive("SCH_TIME_SEM")` 唤醒 SCH 任务。但由于 POSIX 调度的非确定性，SCH 可能还没来得及处理次要帧。如果控制器立即调用 `WaitStepComplete()`，`completion_ready` 还是 `false`。

自动触发避免了控制器需要显式等待 SCH 处理的复杂性。这是 InProc 适配器的便利特性 — UDS 适配器也有相同逻辑。

---

## 第七章：控制适配器

### 7.1 双通道设计

ESA 步进引擎提供两种控制通道，共享同一个 `stepping_core` 实例：

| 特性 | InProc 适配器 | UDS 适配器 |
|------|---------------|------------|
| 调用方式 | C 函数调用 | Unix Domain Socket |
| 调用者位置 | 同进程 | 任意进程 |
| 典型场景 | 单元测试、功能测试 | 外部仿真器、调试终端 |
| 线程模型 | 调用者线程 | 后台 pthread 服务线程 |
| 延迟 | 纳秒级 | 微秒级（IPC 开销） |
| 协议 | 直接参数传递 | 二进制线协议 |

### 7.2 InProc 适配器

InProc 适配器提供三个函数：

#### BeginStep

```c
int32 ESA_Stepping_InProc_BeginStep(void)
{
    int32 status = ESA_Stepping_Core_BeginStepSession(&stepping_core);
    if (status == ESA_STEPPING_SUCCESS)
    {
        OS_BinSemGive_by_name("SCH_TIME_SEM");  /* 唤醒 SCH 调度器 */
    }
    return status;
}
```

关键步骤：
1. 调用 Core 开始新步进会话
2. 成功后通过 `OS_BinSemGive("SCH_TIME_SEM")` 唤醒 SCH
3. SCH 被唤醒后执行次要帧处理，触发 `ReportSchMinorFrame` 等事件

**SCH_TIME_SEM** 是 SCH 应用的时间信号量。在飞行模式下由硬件定时器中断释放；在步进模式下由 BeginStep 显式释放。这是步进引擎 **唯一直接触及 cFS 运行时** 的操作。

#### WaitStepComplete

```c
int32 ESA_Stepping_InProc_WaitStepComplete(int32 timeout_ms)
```

三种超时语义：
- `timeout_ms > 0`：最多等待指定毫秒数，超时返回 `ESA_STEPPING_TIMEOUT`
- `timeout_ms == 0`：非阻塞，立即返回当前状态
- `timeout_ms < 0`：无限等待，直到步进完成

**实现流程**：
1. 设置 `completion_requested = true`
2. 如果 `completion_ready == false`，自动调用 `ReportSchMinorFrame()`（见第六章 6.6 节）
3. 进入轮询循环，每 1ms `usleep(1000)` 检查一次 `IsStepComplete()`
4. 完成或超时后返回

#### QueryState

```c
int32 ESA_Stepping_InProc_QueryState(
    ESA_Stepping_StateEnum_t *out_state,
    uint32 *out_trigger_count)
```

非阻塞查询，返回当前状态枚举和活跃触发器数量。

### 7.3 UDS 适配器

UDS（Unix Domain Socket）适配器允许外部进程通过 IPC 控制步进。

#### Socket 配置

```c
#define ESA_STEPPING_UDS_SOCKET_PATH  "/tmp/cfe_sim_stepping.sock"
```

- 协议族：`AF_UNIX`
- 类型：`SOCK_STREAM`（面向连接）
- 标志：`SOCK_NONBLOCK`（非阻塞接受连接）
- 监听队列：`listen(fd, 5)`（最多 5 个待处理连接）

#### 服务线程

UDS 适配器在 `ESA_Init()` 中启动一个 POSIX 线程：

```c
pthread_create(&service_thread, NULL, ESA_Stepping_UDS_ServiceLoop, NULL);
```

服务线程以 10ms 轮询间隔运行（`nanosleep(10ms)`），负责：
1. 接受新连接（`accept4()` 非阻塞）
2. 从已连接客户端读取请求
3. 分发请求到 Core API
4. 将结果写回客户端

#### 线协议

**请求格式**（固定长度）：

```c
typedef struct
{
    uint8_t  opcode;      /* 操作码 */
    uint32_t timeout_ms;  /* 仅 WAIT 操作使用 */
} __attribute__((packed)) UDS_WaitStepCompleteRequest_t;
```

**三种操作码**：

| 操作码 | 值 | 含义 | 响应大小 |
|--------|-----|------|----------|
| `BEGIN` | 1 | 开始新步进周期 | 4 字节（status） |
| `QUERY` | 2 | 查询当前状态 | 12 字节（status + state + trigger_count） |
| `WAIT` | 3 | 等待步进完成 | 4 字节（status） |

**响应格式**：

BEGIN 和 WAIT 返回：
```c
int32_t status;  /* ESA_STEPPING_SUCCESS (0) 或错误码 */
```

QUERY 返回：
```c
struct {
    int32_t  status;         /* 查询是否成功 */
    uint32_t state;          /* ESA_Stepping_StateEnum_t 值 */
    uint32_t trigger_count;  /* 当前活跃触发器数 */
};
```

### 7.4 状态码

所有适配器共用统一的状态码体系：

| 状态码 | 值 | 含义 |
|--------|-----|------|
| `ESA_STEPPING_SUCCESS` | 0 | 操作成功 |
| `ESA_STEPPING_NOT_INITIALIZED` | -1 | 引擎未初始化 |
| `ESA_STEPPING_INVALID_STATE` | -2 | 当前状态不允许此操作 |
| `ESA_STEPPING_NOT_READY` | -3 | 系统尚未就绪 |
| `ESA_STEPPING_TIMEOUT` | -4 | 等待超时 |
| `ESA_STEPPING_ALREADY_COMPLETE` | -5 | 步进已完成（重复等待） |
| `ESA_STEPPING_NOT_COMPLETE` | -6 | 非阻塞查询时步进未完成 |
| `ESA_STEPPING_TRANSPORT_ERROR` | -7 | UDS 传输错误 |
| `ESA_STEPPING_PROTOCOL_ERROR` | -8 | UDS 协议错误 |

### 7.5 弱符号桩

当 `CFE_SIM_STEPPING` 未定义时，所有公共 API 函数提供弱符号（weak symbol）实现：

```c
__attribute__((weak))
int32 ESA_Stepping_InProc_BeginStep(void)
{
    return -1;
}
```

这确保了：
- 未启用步进时，链接不会失败
- 调用这些函数会得到 `-1`（失败）返回值
- 零运行时开销 — 编译器可能完全消除对弱符号的调用

---

## 第八章：构建系统集成

ESA 模块的构建深度集成于 cFE cmake 体系，通过 `CFE_SIM_STEPPING` 编译开关实现条件编译，确保步进功能在非仿真构建中完全零开销。

### 8.1 条件编译开关

步进功能由顶层 cmake 变量 `CFE_SIM_STEPPING` 控制，通过构建命令传入：

```bash
make SIMULATION=native CFE_SIM_STEPPING=1 prep
make
make install
```

未传入 `CFE_SIM_STEPPING` 时，ESA 模块退化为纯头文件接口库，不编译任何运行时代码。

### 8.2 CMake 构建结构

`esa/CMakeLists.txt` 定义了两个互斥的构建路径：

#### 8.2.1 公共头文件接口（始终存在）

```cmake
add_library(esa_public_api INTERFACE)
target_include_directories(esa_public_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public_inc>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fsw/inc>
)
target_link_libraries(esa_public_api INTERFACE osal_public_api)
```

`esa_public_api` 是纯 INTERFACE 目标，仅传播头文件搜索路径。所有依赖 ESA 头文件的模块（OSAL、PSP、cFE）都通过此目标获取包含路径，无论步进是否启用。

#### 8.2.2 启用步进时（`CFE_SIM_STEPPING=ON`）

```cmake
if (CFE_SIM_STEPPING)
    add_library(esa STATIC
        "${ESA_SIM_STEPPING_SRC_DIR}/esa_stepping.c"
        "${ESA_SIM_STEPPING_SRC_DIR}/esa_stepping_core.c"
    )
    target_link_libraries(esa PUBLIC esa_public_api)
endif()
```

编译为静态库，包含完整的步进实现（Core 状态机 + PSP 胶水层）。

#### 8.2.3 未启用步进时

```cmake
else()
    add_library(esa INTERFACE)
    target_link_libraries(esa INTERFACE esa_public_api)
endif()
```

`esa` 退化为 INTERFACE 库。配合弱符号桩（见 §7.5），确保链接成功而无运行时代码。

### 8.3 弱符号桩策略

CMake 构建系统生成两组弱符号桩，解决特定链接场景：

#### 8.3.1 BSP 初始化桩

```cmake
if (TARGET osal_bsp)
    file(WRITE "${ESA_OSAL_BSP_INIT_STUB_SRC}"
        "__attribute__((weak)) void ESA_Init(void)\n"
        "{\n}\n"
    )
    add_library(esa_osal_bsp_init_stub STATIC ...)
    set_property(TARGET osal_bsp APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES esa_osal_bsp_init_stub)
endif()
```

**问题**：`osal_bsp`（generic-linux BSP 的 `main()`）在步进启用时调用 `ESA_Init()`，但 OSAL 自身的单元测试二进制不链接完整 ESA 运行时（否则会与 OSAL 的 noop 测试 hook 冲突）。

**解决**：生成弱符号 `ESA_Init()` 空实现并附加到 `osal_bsp` 的 INTERFACE 链接库。运行时二进制链接真实 ESA 库时，强符号覆盖弱符号；OSAL 测试二进制则使用空桩。

#### 8.3.2 覆盖率测试桩

```cmake
if (TARGET ut_coverage_link)
    file(WRITE "${ESA_UT_COVERAGE_SHIM_STUB_SRC}"
        "__attribute__((weak)) int32_t ESA_Stepping_Shim_ReportEvent(...) { return 0; }\n"
        "__attribute__((weak)) bool ESA_Stepping_Hook_TaskDelayEligible(...) { return false; }\n"
        "__attribute__((weak)) int32_t ESA_Stepping_WaitForDelayExpiry(...) { return -1; }\n"
    )
    add_library(esa_ut_coverage_shim_stub STATIC ...)
    set_property(TARGET ut_coverage_link APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES esa_ut_coverage_shim_stub)
endif()
```

**问题**：cFE/OSAL 覆盖率测试直接编译启用步进的源单元，但不链接完整 ESA 运行时。缺少 `ReportEvent`、`TaskDelayEligible`、`WaitForDelayExpiry` 符号会导致链接失败。

**解决**：生成三个弱符号默认实现：`ReportEvent` 返回 0（无操作）、`TaskDelayEligible` 返回 false（不接管）、`WaitForDelayExpiry` 返回 -1（失败）。覆盖率二进制使用这些安全默认值，运行时二进制使用真实实现。

### 8.4 单元测试构建

```cmake
if (ENABLE_UNIT_TESTS)
    add_subdirectory(ut-stubs)
    add_subdirectory(ut-coverage)
endif()
```

当 `ENABLE_UNIT_TESTS=true` 时：
- `ut-stubs/`：为外部模块提供 ESA API 的测试桩
- `ut-coverage/`：ESA 自身的覆盖率测试，使用 `ut_assert` 框架

完整测试构建命令：

```bash
make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep
make
make test
```

测试结果：121/121 步进模式通过，117/117 非步进模式通过。

### 8.5 构建命令速查

| 场景 | 命令 |
|------|------|
| 仿真构建（带步进） | `make SIMULATION=native CFE_SIM_STEPPING=1 prep && make && make install` |
| 仿真构建（无步进） | `make SIMULATION=native prep && make && make install` |
| 带单元测试 | `make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make && make test` |
| 覆盖率报告 | `make lcov` |
| 清理重建 | `make distclean` |
| 运行 | `cd build/exe/cpu1 && ./core-cpu1` |

---

## 第九章：ESA Stepping Terminal 工具

`tools/esa_stepping_terminal/` 是一个独立的 C 语言交互式命令行工具，通过 UDS 协议连接 ESA 步进引擎，用于手动调试和自动化测试。

### 9.1 定位与用途

ESA Stepping Terminal 模拟外部控制器角色，为开发者提供：

- **手动步进控制**：逐步执行 SCH 次帧，观察系统行为
- **状态实时监控**：查询引擎状态、触发器计数
- **批量自动化**：`repeat` 命令连续执行多步操作
- **协议调试**：直接观察 UDS 请求/响应原始数据

该工具独立于 cFS 主构建系统，有自己的 CMakeLists.txt。

### 9.2 源码结构

```
tools/esa_stepping_terminal/
├── CMakeLists.txt          # 独立构建配置（cmake >= 3.5）
├── README.md               # 中文使用文档
└── src/
    ├── main.c              # 入口、CLI 参数解析、信号处理
    ├── protocol.h          # 协议常量、结构体、静态断言
    ├── uds_client.h/.c     # UDS 连接管理（短连接模式）
    └── repl.h/.c           # REPL 循环、命令解析与分发
```

### 9.3 连接模式

采用**短连接**（short-lived connection）模式：每个命令独立经历完整的连接生命周期：

```
connect() → send(request) → recv(response) → close()
```

这种设计简化了错误恢复 — 任何连接异常只影响当前命令，不会导致会话状态损坏。

### 9.4 命令集

#### 基础命令

| 命令 | 语法 | 说明 |
|------|------|------|
| `help` | `help` | 显示可用命令列表 |
| `connect` | `connect [path]` | 查看/修改 Socket 路径 |
| `quit` / `exit` | `quit` | 退出 REPL |

#### 查询命令

| 命令 | 语法 | 说明 |
|------|------|------|
| `query` | `query` | 显示原始数值（status, state, trigger_count） |
| `status` | `status` | 显示友好文本（如 "READY" 而非 "1"） |

两者都发送 `QUERY` 操作码（opcode=2），区别仅在客户端显示格式。

#### 步进命令

| 命令 | 语法 | 说明 |
|------|------|------|
| `begin` | `begin` | 发送 `BEGIN`（opcode=1），启动步进周期 |
| `wait` | `wait <timeout_ms>` | 发送 `WAIT`（opcode=3），阻塞等待完成 |
| `step` | `step <timeout_ms>` | 组合命令：`begin` + `wait` |
| `repeat` | `repeat <count> <timeout_ms> [interval_ms]` | 循环执行 `step`，可设间隔 |

`step` 和 `repeat` 是**客户端组合命令**，不引入新的协议操作码。

### 9.5 典型工作流

```
$ ./esa_stepping_terminal
est> status
状态: SUCCESS
步进状态: READY
触发计数: 0

est> step 5000
begin 结果: SUCCESS
wait 结果: SUCCESS

est> repeat 10 5000 100
[1/10] begin 结果: SUCCESS
wait 结果: SUCCESS
[2/10] begin 结果: SUCCESS
wait 结果: SUCCESS
...
[10/10] begin 结果: SUCCESS
wait 结果: SUCCESS
```

### 9.6 构建与运行

```bash
# 构建
cd tools/esa_stepping_terminal
mkdir build && cd build
cmake ..
make

# 运行（默认连接 /tmp/cfe_sim_stepping.sock）
./esa_stepping_terminal

# 指定 Socket 路径
./esa_stepping_terminal -s /tmp/my_test.sock
```

**前置条件**：cFS 必须以 `CFE_SIM_STEPPING=1` 模式运行，否则 UDS 服务端不会启动。

---

## 第十章：修改清单 — 全量变更点索引

本章以 **模块 → 文件 → 函数/类型** 的粒度，穷举列出 ESA 步进引擎涉及的所有代码变更点。每一行对应一个具体的新增或修改动作，可直接用于代码审查和影响评估。

> **标记约定**：🆕 = 新增文件或符号 ｜ ✏️ = 修改已有代码 ｜ 📦 = 构建系统变更
>
> **条件编译**：所有变更点均受 `CFE_SIM_STEPPING` 宏控制。未定义该宏时，全部修改不参与编译，对原系统零影响。

---

### 10.1 ESA 模块（🆕 新增顶层模块）

ESA 模块位于 `esa/`，与 `cfe/`、`osal/`、`psp/` 平级，是步进引擎的独立归属地。全部文件均为新增。

#### 10.1.1 公共 Shim 头文件

| 文件 | 符号 | 类别 | 说明 |
|------|------|------|------|
| `esa/public_inc/esa_stepping_shim.h` | `ESA_Stepping_EventKind_t` | 🆕 枚举 | 22 种 Shim 事件类型，涵盖 TASK_DELAY、QUEUE_RECEIVE、BINSEM_TAKE 的 ACK/COMPLETE 对，CORE_SERVICE 的 ACK/COMPLETE 对，TIME 三事件（TASK_CYCLE / 1HZ_BOUNDARY / TONE_SIGNAL），SCH 三事件（SEMAPHORE_WAIT / MINOR_FRAME / MAJOR_FRAME）等 |
| | `ESA_Stepping_ShimEvent_t` | 🆕 结构体 | 统一事件载荷：`event_kind` + `entity_id` + `task_id` + `detail`（联合体，按事件类型携带 ms / timeout / return_code 等） |
| | `ESA_Stepping_Shim_ReportEvent()` | 🆕 函数声明 | Shim 层唯一入口点；所有 hook 通过此函数向 Core 报告事件 |

#### 10.1.2 Core 头文件

| 文件 | 符号 | 类别 | 说明 |
|------|------|------|------|
| `esa/fsw/inc/esa_stepping_core.h` | `ESA_Stepping_StateEnum_t` | 🆕 枚举 | 状态机四状态：IDLE / STEPPING / WAITING / COMPLETE |
| | `ESA_Stepping_Trigger_t` | 🆕 结构体 | 触发器描述：触发类型 + 关联实体 + 回调 |
| | `ESA_Stepping_DiagnosticClass_t` | 🆕 枚举 | 诊断分类（用于调试输出） |
| | `ESA_Stepping_Core_t` | 🆕 结构体 | Core 单例：状态机 + 等待集位图 + 触发器列表 + 时间基准 + 诊断计数器 |
| | `ESA_Stepping_Core_Init()` | 🆕 函数 | 初始化 Core 单例 |
| | `ESA_Stepping_Core_ReportEvent()` | 🆕 函数 | 处理 Shim 事件：状态转换 + 等待集更新 |
| | `ESA_Stepping_Core_ReportAck()` | 🆕 函数 | 处理 ACK 阶段事件 |
| | `ESA_Stepping_Core_ReportComplete()` | 🆕 函数 | 处理 COMPLETE 阶段事件 |
| | `ESA_Stepping_Core_BeginStep()` | 🆕 函数 | 开始一个步进周期 |
| | `ESA_Stepping_Core_IsStepComplete()` | 🆕 函数 | 查询当前步进是否完成 |
| | `ESA_Stepping_Core_QueryState()` | 🆕 函数 | 查询状态机当前状态 |
| | `ESA_Stepping_Core_QueryWaitSet()` | 🆕 函数 | 查询等待集内容 |
| | `ESA_Stepping_Core_QueryDiagnostics()` | 🆕 函数 | 查询诊断统计信息 |
| | `ESA_STEPPING_CORE_SERVICE_BIT_*` | 🆕 宏组 | 核心服务位掩码（ES / EVS / SB / TBL / TIME 各占一位） |
| | `ESA_STEPPING_CORE_CHILDPATH_BIT_*` | 🆕 宏组 | 子路径位掩码（OSAL / SCH 各占一位） |
| | 其余 20+ 辅助函数声明 | 🆕 函数 | 触发器管理、时间查询、等待集操作等内部 API |

#### 10.1.3 公共 API 头文件

| 文件 | 符号 | 类别 | 说明 |
|------|------|------|------|
| `esa/fsw/inc/esa_stepping.h` | `ESA_Init()` | 🆕 函数 | 模块初始化入口（由 BSP `main()` 调用） |
| | `ESA_Stepping_Hook_GetTime()` | 🆕 函数 | PSP 时间钩子：返回模拟时间 |
| | `ESA_Stepping_Hook_TaskDelayEligible()` | 🆕 函数 | 判断任务延迟是否由步进引擎管理 |
| | `ESA_Stepping_WaitForDelayExpiry()` | 🆕 函数 | 步进模式下的延迟等待实现 |
| | `ESA_Stepping_InProc_BeginStep()` | 🆕 函数 | 同进程适配器：开始步进 |
| | `ESA_Stepping_InProc_IsComplete()` | 🆕 函数 | 同进程适配器：查询完成 |
| | `ESA_Stepping_InProc_QueryState()` | 🆕 函数 | 同进程适配器：查询状态 |
| | `ESA_Stepping_UDS_Init()` | 🆕 函数 | UDS 适配器：创建监听 socket |
| | `ESA_Stepping_UDS_Accept()` | 🆕 函数 | UDS 适配器：接受客户端连接 |
| | `ESA_Stepping_UDS_HandleCommand()` | 🆕 函数 | UDS 适配器：解析命令并返回 JSON 响应 |
| | `ESA_Stepping_UDS_ServiceLoop_Task()` | 🆕 函数 | UDS 适配器：服务循环（作为独立 OSAL 任务运行） |
| | `ESA_STEPPING_STATUS_*` | 🆕 宏组 | 9 个状态码宏（OK / ERROR / TIMEOUT / BUSY 等） |

#### 10.1.4 Core 实现

| 文件 | 函数 | 类别 | 说明 |
|------|------|------|------|
| `esa/fsw/src/esa_stepping_core.c` | `ESA_Stepping_Core_Init()` | 🆕 | 清零 Core 单例，状态置为 IDLE |
| | `ESA_Stepping_Core_ReportEvent()` | 🆕 | 主事件分发：按 `event_kind` 路由到 ACK 或 COMPLETE 处理器 |
| | `ESA_Stepping_Core_ReportAck()` | 🆕 | ACK 处理：将实体加入等待集，记录诊断 |
| | `ESA_Stepping_Core_ReportComplete()` | 🆕 | COMPLETE 处理：从等待集移除实体，检查步进完成条件 |
| | `ESA_Stepping_Core_BeginStep()` | 🆕 | 状态转换 IDLE→STEPPING，重置等待集，广播触发 |
| | `ESA_Stepping_Core_IsStepComplete()` | 🆕 | 检查等待集是否为空（所有实体均已 COMPLETE） |
| | `ESA_Stepping_Core_QueryState()` | 🆕 | 返回当前 `StateEnum` |
| | `ESA_Stepping_Core_QueryWaitSet()` | 🆕 | 返回等待集位图快照 |
| | `ESA_Stepping_Core_QueryDiagnostics()` | 🆕 | 返回诊断计数器快照 |
| | 7 个 `static` 辅助函数 | 🆕 | 等待集位操作、触发器匹配、状态转换验证等内部逻辑 |

#### 10.1.5 主实现（Shim 转发 + 适配器）

| 文件 | 函数 | 类别 | 说明 |
|------|------|------|------|
| `esa/fsw/src/esa_stepping.c` | `ESA_Init()` | 🆕 | 初始化 Core + 注册触发器 + 启动 UDS 服务任务（若启用） |
| | `ESA_Stepping_Shim_ReportEvent()` | 🆕 | 22-case switch：将 `ShimEvent` 按事件类型路由到对应的 Core API 调用 |
| | `ESA_Stepping_Hook_GetTime()` | 🆕 | 返回 Core 管理的模拟时间（供 PSP 时间基准调用） |
| | `ESA_Stepping_Hook_TaskDelayEligible()` | 🆕 | 检查当前任务 ID 是否在步进管理范围内 |
| | `ESA_Stepping_WaitForDelayExpiry()` | 🆕 | 步进模式延迟：向 Core 报告延迟事件，等待模拟时间推进 |
| | `ESA_Stepping_InProc_BeginStep()` | 🆕 | 同进程适配器：直接调用 `Core_BeginStep` |
| | `ESA_Stepping_InProc_IsComplete()` | 🆕 | 同进程适配器：直接调用 `Core_IsStepComplete` |
| | `ESA_Stepping_InProc_QueryState()` | 🆕 | 同进程适配器：直接调用 `Core_QueryState` |
| | `ESA_Stepping_UDS_Init()` | 🆕 | 创建 UDS 监听 socket，绑定路径 |
| | `ESA_Stepping_UDS_Accept()` | 🆕 | 接受客户端连接 |
| | `ESA_Stepping_UDS_HandleCommand()` | 🆕 | 解析 UDS 命令字符串，执行对应操作，返回 JSON 响应 |
| | `ESA_Stepping_UDS_ServiceLoop_Task()` | 🆕 | UDS 服务循环：accept → handle → close（独立 OSAL 任务） |
| | 弱符号存根（`__attribute__((weak))`） | 🆕 | 默认空实现，供未链接 ESA 时安全回退 |

#### 10.1.6 构建系统

| 文件 | 类别 | 说明 |
|------|------|------|
| `esa/CMakeLists.txt` | 📦 | 定义三个构建目标：`esa_public_api`（INTERFACE 纯头文件库）、`esa`（STATIC 库，`CFE_SIM_STEPPING` 启用时编译）、`esa_osal_bsp_init_stub`（UT 存根） |

---

### 10.2 OSAL 模块（✏️ 修改）

OSAL POSIX 实现层新增步进 hook，在任务延迟、队列接收、二值信号量获取三个同步边界注入双阶段（ACK + COMPLETE）事件上报。hook 通过弱符号 `ESA_Stepping_Shim_ReportEvent()` 转发，保持 OSAL 与 ESA 的编译期解耦。

#### 10.2.1 新增文件

| 文件 | 函数 | 类别 | 说明 |
|------|------|------|------|
| `osal/src/os/posix/inc/os-posix-stepping.h` | `OS_PosixStepping_Hook_TaskDelay()` | 🆕 | 声明：任务延迟前调用，参数为 ms 和 task_id |
| | `OS_PosixStepping_Hook_TaskDelay_Complete()` | 🆕 | 声明：任务延迟完成后调用 |
| | `OS_PosixStepping_Hook_QueueReceive()` | 🆕 | 声明：队列接收前调用，参数为 token 和 timeout |
| | `OS_PosixStepping_Hook_QueueReceive_Complete()` | 🆕 | 声明：队列接收完成后调用，携带 return_code |
| | `OS_PosixStepping_Hook_BinSemTake()` | 🆕 | 声明：二值信号量获取前调用 |
| | `OS_PosixStepping_Hook_BinSemTake_Complete()` | 🆕 | 声明：二值信号量获取完成后调用，携带 return_code |
| `osal/src/os/posix/src/os-posix-stepping.c` | 上述 6 个函数的实现 | 🆕 | 构造 `ESA_Stepping_ShimEvent_t`，通过弱符号 `ESA_Stepping_Shim_ReportEvent()` 转发；整个文件受 `#ifdef CFE_SIM_STEPPING` 保护 |

#### 10.2.2 修改文件

| 文件 | 函数 | 类别 | 变更描述 |
|------|------|------|----------|
| `osal/src/os/posix/src/os-impl-tasks.c` | `OS_TaskDelay_Impl()` | ✏️ | `nanosleep` 前插入 `Hook_TaskDelay(ms, task_id)`，完成后插入 `Hook_TaskDelay_Complete(ms, task_id)`；通过 `OS_TaskGetId_Impl()` 获取任务 ID 传递给 hook（2 个插入点） |
| `osal/src/os/posix/src/os-impl-queues.c` | `OS_QueueGet_Impl()` | ✏️ | `mq_receive` 前插入 `Hook_QueueReceive(token, timeout)`，计算 return_code 后插入 `Hook_QueueReceive_Complete(token, timeout, return_code)`（2 个插入点） |
| `osal/src/os/posix/src/os-impl-binsem.c` | `OS_GenericBinSemTake_Impl()` | ✏️ | 入口处插入 `Hook_BinSemTake(token, timeout)`；mutex 获取失败的提前返回处插入 `Hook_BinSemTake_Complete(token, timeout, OS_SEM_FAILURE)`；正常返回前插入 `Hook_BinSemTake_Complete(token, timeout, return_code)`（共 3 个插入点）。`OS_BinSemTake_Impl()` 和 `OS_BinSemTimedWait_Impl()` 通过调用此辅助函数间接获得步进行为 |
| `osal/src/bsp/generic-linux/src/bsp_start.c` | `main()` | ✏️ | `OS_Application_Startup()` 之后，`#ifdef CFE_SIM_STEPPING` 保护下调用 `ESA_Init()` 初始化步进模块 |

#### 10.2.3 构建系统

| 文件 | 类别 | 变更描述 |
|------|------|----------|
| `osal/src/os/posix/CMakeLists.txt` | 📦 | `CFE_SIM_STEPPING` 启用时将 `os-posix-stepping.c` 加入 POSIX 源文件列表，链接 `esa_public_api` |
| `osal/src/bsp/generic-linux/CMakeLists.txt` | 📦 | `CFE_SIM_STEPPING` 启用时链接 `esa_public_api`（供 BSP `main()` 调用 `ESA_Init()`） |

---

### 10.3 cFE 模块（✏️ 修改）

cFE 四个核心服务（ES / EVS / SB / TBL）的命令管道循环注入等待集事件（CORE_SERVICE_ACK + CORE_SERVICE_COMPLETE），使步进引擎能感知核心服务的处理周期。TIME 服务新增三个步进 hook，在任务循环、1Hz 边界、Tone 信号三个时间关键点报告事件。

#### 10.3.1 新增文件

| 文件 | 函数 | 类别 | 说明 |
|------|------|------|------|
| `cfe/modules/time/fsw/src/cfe_time_stepping.h` | `CFE_TIME_Stepping_Hook_TaskCycle()` | 🆕 | 声明：TIME 任务主循环 hook |
| | `CFE_TIME_Stepping_Hook_1HzBoundary()` | 🆕 | 声明：1Hz 边界 hook |
| | `CFE_TIME_Stepping_Hook_ToneSignal()` | 🆕 | 声明：Tone 信号 hook |
| | 无操作宏（`CFE_SIM_STEPPING` 未定义时） | 🆕 | 三个同名宏展开为空语句，实现零开销禁用 |
| `cfe/modules/time/fsw/src/cfe_time_stepping.c` | `CFE_TIME_Stepping_Hook_TaskCycle()` | 🆕 | 构造 ShimEvent（`TIME_TASK_CYCLE`），调用 `ESA_Stepping_Shim_ReportEvent()` |
| | `CFE_TIME_Stepping_Hook_1HzBoundary()` | 🆕 | 构造 ShimEvent（`TIME_1HZ_BOUNDARY`），调用 Shim |
| | `CFE_TIME_Stepping_Hook_ToneSignal()` | 🆕 | 构造 ShimEvent（`TIME_TONE_SIGNAL`），调用 Shim |

#### 10.3.2 修改文件 — 核心服务命令循环

以下四个文件遵循统一修改模式：在 `CFE_SB_ReceiveBuffer` 调用前后分别插入 `CORE_SERVICE_ACK` 和 `CORE_SERVICE_COMPLETE` Shim 事件。每个服务使用独立的 `entity_id` 标识自身。

| 文件 | 函数/位置 | 类别 | 变更描述 |
|------|-----------|------|----------|
| `cfe/modules/es/fsw/src/cfe_es_task.c` | ES 命令管道循环 | ✏️ | 插入 `CORE_SERVICE_ACK` + `CORE_SERVICE_COMPLETE` 事件对（entity = `CFE_ES_SERVICE_ID`） |
| `cfe/modules/evs/fsw/src/cfe_evs_task.c` | EVS 命令管道循环 | ✏️ | 同上模式（entity = `CFE_EVS_SERVICE_ID`） |
| `cfe/modules/sb/fsw/src/cfe_sb_task.c` | SB 命令管道循环 | ✏️ | 同上模式（entity = `CFE_SB_SERVICE_ID`） |
| `cfe/modules/tbl/fsw/src/cfe_tbl_task.c` | TBL 命令管道循环 | ✏️ | 同上模式（entity = `CFE_TBL_SERVICE_ID`） |

#### 10.3.3 修改文件 — TIME 服务

| 文件 | 函数/位置 | 类别 | 变更描述 |
|------|-----------|------|----------|
| `cfe/modules/time/fsw/src/cfe_time_task.c` | TIME 任务主循环 | ✏️ | 在循环体调用 `CFE_TIME_Stepping_Hook_TaskCycle()` |
| | 1Hz 边界处理 | ✏️ | 在 1Hz 处理路径调用 `CFE_TIME_Stepping_Hook_1HzBoundary()` |
| | Tone 信号处理 | ✏️ | 在 Tone 信号处理路径调用 `CFE_TIME_Stepping_Hook_ToneSignal()` |

#### 10.3.4 构建系统

| 文件 | 类别 | 变更描述 |
|------|------|----------|
| `cfe/modules/es/CMakeLists.txt` | 📦 | `CFE_SIM_STEPPING` 启用时链接 `esa_public_api` |
| `cfe/modules/time/CMakeLists.txt` | 📦 | `CFE_SIM_STEPPING` 启用时将 `cfe_time_stepping.c` 加入源文件列表 |
| `cfe/cmake/mission_build.cmake` | 📦 | 从环境变量读取 `CFE_SIM_STEPPING`，传播为 CMake 变量和全局编译定义 |
| `cfe/cmake/mission_defaults.cmake` | 📦 | `CFE_SIM_STEPPING` 启用时将 `esa` 插入 `MISSION_CORE_MODULES` 列表，设置 `esa_SEARCH_PATH` 指向顶层 `esa/` 目录 |

---

### 10.4 PSP 模块（✏️ 修改）

PSP 时间基准模块接入步进引擎的模拟时间，软时基模块在步进模式下跳过墙钟调度。

| 文件 | 函数 | 类别 | 变更描述 |
|------|------|------|----------|
| `psp/fsw/modules/timebase_posix_clock/` | | | |
| `cfe_psp_timebase_posix_clock.c` | `CFE_PSP_Get_Timebase()` | ✏️ | `#ifdef CFE_SIM_STEPPING` 分支下调用 `ESA_Stepping_Hook_GetTime()` 返回模拟时间，替代 `clock_gettime(CLOCK_MONOTONIC)` |
| | `CFE_PSP_GetTime()` | ✏️ | 同上：步进模式下返回模拟时间而非墙钟时间 |
| `psp/fsw/modules/soft_timebase/` | | | |
| `cfe_psp_soft_timebase.c` | `soft_timebase_Init()` | ✏️ | `CFE_SIM_STEPPING` 启用时跳过 `OS_TimeBaseSet()`（不启动墙钟驱动的周期性调度，由步进引擎控制时间推进） |
| `psp/fsw/modules/timebase_posix_clock/` | | | |
| `CMakeLists.txt` | — | 📦 | `CFE_SIM_STEPPING` 启用时链接 `esa_public_api` |
| `psp/fsw/pc-linux/` | | | |
| `psp_conditional_modules.cmake` | — | 📦 | 注释说明 `sim_stepping` 模块已移至 ESA 管理 |

---

### 10.5 SCH 应用（✏️ 修改）

调度器在信号量等待、次帧回调、主帧回调三个时序关键点注入步进事件，使步进引擎能感知调度周期的起止边界。

#### 10.5.1 新增文件

| 文件 | 函数 | 类别 | 说明 |
|------|------|------|------|
| `apps/sch/fsw/src/sch_stepping.h` | `SCH_Stepping_Hook_SemaphoreWait()` | 🆕 | 声明 + 无操作内联回退（`CFE_SIM_STEPPING` 未定义时零开销） |
| | `SCH_Stepping_Hook_MinorFrame()` | 🆕 | 同上 |
| | `SCH_Stepping_Hook_MajorFrame()` | 🆕 | 同上 |
| `apps/sch/fsw/src/sch_stepping.c` | `SCH_Stepping_Hook_SemaphoreWait()` | 🆕 | 构造 ShimEvent（`SCH_SEMAPHORE_WAIT`），调用 `ESA_Stepping_Shim_ReportEvent()` |
| | `SCH_Stepping_Hook_MinorFrame()` | 🆕 | 构造 ShimEvent（`SCH_MINOR_FRAME`），调用 Shim |
| | `SCH_Stepping_Hook_MajorFrame()` | 🆕 | 构造 ShimEvent（`SCH_MAJOR_FRAME`），调用 Shim |

#### 10.5.2 修改文件

| 文件 | 函数 | 类别 | 变更描述 |
|------|------|------|----------|
| `apps/sch/fsw/src/sch_app.c` | `SCH_AppMain()` | ✏️ | 新增 `#include "sch_stepping.h"`；主循环中 `OS_BinSemTake(TimeSemaphore)` 之前调用 `SCH_Stepping_Hook_SemaphoreWait()` |
| `apps/sch/fsw/src/sch_custom.c` | `SCH_MinorFrameCallback()` | ✏️ | 新增 `#include "sch_stepping.h"`；`OS_BinSemGive` 之前调用 `SCH_Stepping_Hook_MinorFrame()` |
| | `SCH_MajorFrameCallback()` | ✏️ | 有效主帧处理路径中，`OS_BinSemGive` 之前调用 `SCH_Stepping_Hook_MajorFrame()` |

#### 10.5.3 构建系统

| 文件 | 类别 | 变更描述 |
|------|------|----------|
| `apps/sch/CMakeLists.txt` | 📦 | 默认排除 `sch_stepping.c`；`CFE_SIM_STEPPING` 启用时追加到源文件列表，并添加 `esa_public_api` 头文件包含目录 |

---

### 10.6 构建系统与全局配置

| 文件 | 类别 | 变更描述 |
|------|------|----------|
| `sample_defs/global_build_options.cmake` | 📦 | 新增 `CFE_SIM_STEPPING` CMake 缓存选项（`BOOL`，默认 OFF）；启用时全局添加 `-DCFE_SIM_STEPPING` 编译定义 |
| `cfe/cmake/mission_build.cmake` | 📦 | 读取 `CFE_SIM_STEPPING` 环境变量 / 缓存变量，传播为全局编译定义 |
| `cfe/cmake/mission_defaults.cmake` | 📦 | `CFE_SIM_STEPPING` 启用时自动将 `esa` 注入 `MISSION_CORE_MODULES`，配置 `esa_SEARCH_PATH` 指向顶层 `esa/` 目录 |

---

### 10.7 统计摘要

| 维度 | 数量 |
|------|------|
| 新增源文件（.c / .h） | 12 |
| 修改源文件（.c） | 15 |
| 新增函数 / 类型 / 宏 | 80+ |
| 修改函数 | 13 |
| 涉及 CMakeLists / .cmake 文件 | 10 |
| 涉及模块 / 应用 | 6（ESA、OSAL、cFE、PSP、SCH、构建系统） |
| 条件编译控制宏 | `CFE_SIM_STEPPING`（所有变更点的唯一全局开关） |

---

### 10.8 变更点全景图

```
sample_defs/
  └─ global_build_options.cmake ·········· CFE_SIM_STEPPING 全局开关

esa/ (🆕 整个模块)
  ├─ public_inc/esa_stepping_shim.h ······ Shim ABI（22 种事件 + 统一载荷）
  ├─ fsw/inc/esa_stepping_core.h ········· Core 状态机 + 等待集 + 触发器
  ├─ fsw/inc/esa_stepping.h ·············· 公共 API（Init / Hook / InProc / UDS）
  ├─ fsw/src/esa_stepping_core.c ········· Core 实现（~40 函数）
  ├─ fsw/src/esa_stepping.c ·············· Shim 转发 + 适配器实现
  └─ CMakeLists.txt ······················ 三目标构建

osal/
  ├─ src/os/posix/inc/os-posix-stepping.h · 🆕 6 个 hook 声明
  ├─ src/os/posix/src/os-posix-stepping.c · 🆕 6 个 hook 实现（弱符号转发）
  ├─ src/os/posix/src/os-impl-tasks.c ···· ✏️ OS_TaskDelay_Impl +2 hook 调用
  ├─ src/os/posix/src/os-impl-queues.c ··· ✏️ OS_QueueGet_Impl +2 hook 调用
  ├─ src/os/posix/src/os-impl-binsem.c ··· ✏️ OS_GenericBinSemTake_Impl +3 hook 调用
  ├─ src/bsp/generic-linux/src/bsp_start.c  ✏️ main() +ESA_Init()
  ├─ src/os/posix/CMakeLists.txt ········· 📦 条件编译 stepping 源
  └─ src/bsp/generic-linux/CMakeLists.txt · 📦 链接 esa_public_api

cfe/
  ├─ modules/time/fsw/src/cfe_time_stepping.h · 🆕 3 hook 声明 + no-op 宏
  ├─ modules/time/fsw/src/cfe_time_stepping.c · 🆕 3 hook 实现
  ├─ modules/es/fsw/src/cfe_es_task.c ········ ✏️ +CORE_SERVICE ACK/COMPLETE
  ├─ modules/evs/fsw/src/cfe_evs_task.c ······ ✏️ +CORE_SERVICE ACK/COMPLETE
  ├─ modules/sb/fsw/src/cfe_sb_task.c ········ ✏️ +CORE_SERVICE ACK/COMPLETE
  ├─ modules/tbl/fsw/src/cfe_tbl_task.c ······ ✏️ +CORE_SERVICE ACK/COMPLETE
  ├─ modules/time/fsw/src/cfe_time_task.c ···· ✏️ +3 stepping hook 调用
  ├─ modules/es/CMakeLists.txt ················ 📦 链接 esa_public_api
  ├─ modules/time/CMakeLists.txt ·············· 📦 条件编译 stepping 源
  ├─ cmake/mission_build.cmake ················ 📦 传播 CFE_SIM_STEPPING
  └─ cmake/mission_defaults.cmake ············· 📦 注入 esa 到核心模块列表

psp/
  ├─ fsw/modules/timebase_posix_clock/
  │   ├─ cfe_psp_timebase_posix_clock.c ······ ✏️ +模拟时间分支（2 函数）
  │   └─ CMakeLists.txt ······················ 📦 链接 esa_public_api
  ├─ fsw/modules/soft_timebase/
  │   └─ cfe_psp_soft_timebase.c ············· ✏️ +跳过墙钟调度
  └─ fsw/pc-linux/
      └─ psp_conditional_modules.cmake ······· 📦 注释更新

apps/sch/
  ├─ fsw/src/sch_stepping.h ·················· 🆕 3 hook 声明 + inline no-op
  ├─ fsw/src/sch_stepping.c ·················· 🆕 3 hook 实现
  ├─ fsw/src/sch_app.c ······················· ✏️ SCH_AppMain +SemaphoreWait hook
  ├─ fsw/src/sch_custom.c ···················· ✏️ +MinorFrame/MajorFrame hook
  └─ CMakeLists.txt ·························· 📦 条件编译 stepping 源
```

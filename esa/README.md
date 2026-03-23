# ESA Stepping 引擎

## 概述

外部仿真适配器（External Simulation Adapter，ESA）stepping 引擎是 cFS 的仿真步进控制模块，用于实现确定性时间推进和同步调度。它通过拦截 OSAL 阻塞操作（TaskDelay、QueueReceive、BinSemTake）和 cFE 核心模块事件，实现对整个飞行软件栈的精细化步进控制。

核心设计理念：
- **薄层转发（Thin Shim）**：Hook 层仅负责事实报告，不实现业务逻辑
- **集中状态机**：所有状态维护在 Core 中，确保语义一致性
- **双阶段模型**：ACK（确认）+ COMPLETE（完成）两阶段事件报告
- **多控制通道**：支持 InProc（进程内）和 UDS（Unix Domain Socket）两种控制方式

## 架构图

```
+-----------------------------------------------------------------------------+
|                           cFS 应用程序层                                     |
|  +----------------+  +----------------+  +----------------+                 |
|  |    ci_lab      |  |    to_lab      |  |   sample_app   |                 |
|  +----------------+  +----------------+  +----------------+                 |
+-----------------------------------+-----------------------------------------+
|                                   |                                         |
|                                   v                                         |
|  +----------------+  +----------------+  +----------------+                 |
|  |  ES 任务循环    |  |  EVS 任务循环   |  |  SB 任务循环    |                 |
|  | RECEIVE+COMPLETE|  | RECEIVE+COMPLETE|  | RECEIVE+COMPLETE|                 |
|  +----------------+  +----------------+  +----------------+                 |
|  +----------------+  +----------------+  +----------------+                 |
|  |  TBL 任务循环   |  | TIME 任务循环   |  |  SCH 调度器     |                 |
|  | RECEIVE+COMPLETE|  | Hook_TaskCycle  |  | SemWait+Frame   |                 |
|  |                 |  | 1HzBoundary     |  |                 |                 |
|  |                 |  | ToneSignal      |  |                 |                 |
|  +----------------+  +----------------+  +----------------+                 |
+-----------------------------------+-----------------------------------------+
|                                   |                                         |
|                                   v                                         |
|  +------------------------------------------------------------+             |
|  |              cFE 软件总线 (CFE_SB)                          |             |
|  +------------------------------------------------------------+             |
+-----------------------------------+-----------------------------------------+
|                                   |                                         |
|                                   v                                         |
|  +------------------------------------------------------------+             |
|  |              OSAL 抽象层                                    |             |
|  |  +----------------+  +----------------+  +----------------+ |             |
|  |  | OS_TaskDelay   |  | OS_QueueReceive|  |OS_BinSemTake   | |             |
|  |  |   (ACK+COMPLETE)|  |   (ACK+COMPLETE)|  |  (ACK+COMPLETE)| |             |
|  |  +----------------+  +----------------+  +----------------+ |             |
|  +------------------------------------------------------------+             |
+-----------------------------------+-----------------------------------------+
|                                   |                                         |
|                                   v                                         |
|  +------------------------------------------------------------+             |
|  |              POSIX 平台层 (OSAL POSIX)                      |             |
|  |  +----------------+  +----------------+  +----------------+ |             |
|  |  | TaskDelay Hook |  | QueueReceive   |  | BinSemTake     | |             |
|  |  |  _Ack          |  |   Hook_Ack     |  |   Hook_Ack     | |             |
|  |  | TaskDelay Hook |  | QueueReceive   |  | BinSemTake     | |             |
|  |  |  _Complete     |  |   Hook_Complete|  |   Hook_Complete| |             |
|  |  +----------------+  +----------------+  +----------------+ |             |
|  +------------------------------------------------------------+             |
+-----------------------------------+-----------------------------------------+
|                                   |                                         |
|                                   v                                         |
|  +------------------------------------------------------------+             |
|  |                    ESA Stepping Shim                        |             |
|  |         ESA_Stepping_Shim_ReportEvent()                    |             |
|  |                    （统一事件转发）                          |             |
|  +------------------------------------------------------------+             |
+-----------------------------------+-----------------------------------------+
|                                   |                                         |
|                                   v                                         |
|  +------------------------------------------------------------+             |
|  |                   ESA Stepping Core                         |             |
|  |  +----------------------------------------------------+    |             |
|  |  |              状态机（State Machine）                 |    |             |
|  |  |   INIT -> READY -> RUNNING -> WAITING -> COMPLETE  |    |             |
|  |  +----------------------------------------------------+    |             |
|  |  +----------------------------------------------------+    |             |
|  |  |              触发器跟踪（Trigger Tracking）          |    |             |
|  |  |  最多支持 32 个并发触发器（编译时固定容量）            |    |             |
|  |  +----------------------------------------------------+    |             |
|  +------------------------------------------------------------+             |
+-----------------------------------+-----------------------------------------+
|                                   |                                         |
|                    +--------------+--------------+                          |
|                    |                             |                          |
|                    v                             v                          |
|  +----------------------------+  +----------------------------+             |
|  |     InProc 控制适配器       |  |      UDS 控制适配器         |             |
|  |  ESA_Stepping_InProc_*()   |  |  ESA_Stepping_UDS_*()       |             |
|  |                            |  |                             |             |
|  |  BeginStep()               |  |  Unix Domain Socket         |             |
|  |  WaitStepComplete()        |  |  /tmp/cfe_sim_stepping.sock |             |
|  |  QueryState()              |  |  （后台 pthread 线程服务）    |             |
|  +----------------------------+  +----------------------------+             |
+-----------------------------------------------------------------------------+
```

## 配置说明

### 构建开关

ESA stepping 引擎通过 CMake 构建选项控制：

```bash
# 启用 Stepping 构建
make SIMULATION=native CFE_SIM_STEPPING=1 prep

# 标准构建（禁用 Stepping）
make SIMULATION=native prep
```

### 关键宏定义

| 宏 | 含义 | 默认值 |
|----|------|--------|
| `CFE_SIM_STEPPING` | 启用仿真步进功能 | 未定义（禁用） |
| `ESA_SIM_STEPPING_MAX_TRIGGERS` | 最大并发触发器数量 | 32 |

### 服务位掩码

核心服务使用位掩码标识：

```c
#define ESA_SIM_STEPPING_SERVICE_BIT_ES   (1U << 0)   /* Executive Services */
#define ESA_SIM_STEPPING_SERVICE_BIT_EVS  (1U << 1)   /* Event Services */
#define ESA_SIM_STEPPING_SERVICE_BIT_SB   (1U << 2)   /* Software Bus */
#define ESA_SIM_STEPPING_SERVICE_BIT_TBL  (1U << 3)   /* Table Services */
#define ESA_SIM_STEPPING_SERVICE_BIT_TIME (1U << 4)   /* Time Services */
```

## 两阶段模型（ACK + COMPLETE）

ESA 采用双阶段事件报告模型，精确跟踪阻塞操作的生命周期：

### ACK 阶段（Pre-blocking）

在阻塞操作**开始之前**调用，表示任务即将进入等待状态：

```c
// TaskDelay 示例
void OS_PosixStepping_Hook_TaskDelay(uint32_t ms, osal_id_t task_id)
{
    ESA_Stepping_ShimEvent_t event = {0};
    event.event_kind = ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK;
    event.task_id = (uint32_t)task_id;
    event.optional_delay_ms = ms;
    ESA_Stepping_Shim_ReportEvent(&event);
}
```

### COMPLETE 阶段（Post-blocking）

在阻塞操作**完成之后**调用（无论成功/失败/超时），表示任务已从等待状态返回：

```c
// TaskDelay 示例
void OS_PosixStepping_Hook_TaskDelay_Complete(uint32_t ms, osal_id_t task_id)
{
    ESA_Stepping_ShimEvent_t event = {0};
    event.event_kind = ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE;
    event.task_id = (uint32_t)task_id;
    event.optional_delay_ms = ms;
    ESA_Stepping_Shim_ReportEvent(&event);
}
```

### 两阶段的意义

| 阶段 | 触发时机 | 用途 |
|------|----------|------|
| ACK | 阻塞前 | 标记任务即将等待，可被纳入步进同步集合 |
| COMPLETE | 阻塞后 | 标记任务已完成等待，可继续执行下一步 |

## 事件类型清单

共支持 22 种事件类型：

### OSAL 事件

| 事件类型 | 说明 | 触发源 |
|----------|------|--------|
| `ESA_SIM_STEPPING_EVENT_TASK_DELAY` | TaskDelay 请求（兼容旧版单事件） | OSAL TaskDelay |
| `ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK` | TaskDelay 确认（阻塞前） | OSAL TaskDelay |
| `ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE` | TaskDelay 完成（阻塞后） | OSAL TaskDelay |
| `ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE` | QueueReceive 请求（兼容旧版单事件） | OSAL QueueGet |
| `ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK` | QueueReceive 确认（阻塞前） | OSAL QueueGet |
| `ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE` | QueueReceive 完成（阻塞后） | OSAL QueueGet |
| `ESA_SIM_STEPPING_EVENT_BINSEM_TAKE` | BinSemTake 请求（兼容旧版单事件） | OSAL BinSemTake |
| `ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK` | BinSemTake 确认（阻塞前） | OSAL BinSemTake |
| `ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE` | BinSemTake 完成（阻塞后） | OSAL BinSemTake |

### TIME 模块事件

| 事件类型 | 说明 | 触发源 |
|----------|------|--------|
| `ESA_SIM_STEPPING_EVENT_TIME_TASK_CYCLE` | TIME 任务周期开始 | TIME 主循环 |
| `ESA_SIM_STEPPING_EVENT_1HZ_BOUNDARY` | 1Hz 时钟边界 | TIME 1Hz 处理 |
| `ESA_SIM_STEPPING_EVENT_TONE_SIGNAL` | Tone 信号 | TIME 信号处理 |

### cFE 核心服务事件

| 事件类型 | 说明 | 触发源 |
|----------|------|--------|
| `ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_RECEIVE` | 命令管道接收完成 | ES/EVS/SB/TBL |
| `ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_COMPLETE` | 命令处理完成 | ES/EVS/SB/TBL |

### SCH 调度器事件

| 事件类型 | 说明 | 触发源 |
|----------|------|--------|
| `ESA_SIM_STEPPING_EVENT_SCH_SEMAPHORE_WAIT` | SCH 信号量等待 | SCH |
| `ESA_SIM_STEPPING_EVENT_SCH_MINOR_FRAME` | 次要帧边界 | SCH |
| `ESA_SIM_STEPPING_EVENT_SCH_MAJOR_FRAME` | 主要帧边界 | SCH |
| `ESA_SIM_STEPPING_EVENT_SCH_SEND_TRIGGER` | 发送触发器 | SCH |
| `ESA_SIM_STEPPING_EVENT_SCH_DISPATCH_COMPLETE` | 分发完成 | SCH |

### 信号量消费事件

| 事件类型 | 说明 | 触发源 |
|----------|------|--------|
| `ESA_SIM_STEPPING_EVENT_TIME_TONE_SEM_CONSUME` | Tone 信号量消费 | TIME/SCH |
| `ESA_SIM_STEPPING_EVENT_TIME_LOCAL_1HZ_SEM_CONSUME` | 本地 1Hz 信号量消费 | TIME/SCH |

### 系统事件

| 事件类型 | 说明 | 触发源 |
|----------|------|--------|
| `ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING` | 系统就绪 | 系统初始化完成 |

## 控制通道说明

ESA 支持两种控制通道，满足不同场景需求：

### InProc 控制适配器（进程内）

适用于测试框架、进程内仿真器等场景：

```c
/* 开始一个新的步进周期 */
int32_t ESA_Stepping_InProc_BeginStep(void);

/* 等待当前步进周期完成 */
int32_t ESA_Stepping_InProc_WaitStepComplete(uint32_t timeout_ms);

/* 查询当前步进状态 */
int32_t ESA_Stepping_InProc_QueryState(uint32_t *state_out, uint32_t *trigger_count);
```

**参数说明**：
- `timeout_ms`: 超时时间（毫秒）
  - `0` = 无限等待（阻塞直到完成）
  - `~0U` (0xFFFFFFFF) = 非阻塞查询
  - 其他值 = 有限超时等待

### UDS 控制适配器（Unix Domain Socket）

适用于外部控制器、独立进程控制等场景：

```c
/* 初始化 UDS 适配器（创建监听 socket） */
int32_t ESA_Stepping_UDS_Init(void);

/* 服务一次 UDS 请求（非阻塞） */
int32_t ESA_Stepping_UDS_Service(void);

/* 关闭 UDS 适配器 */
int32_t ESA_Stepping_UDS_Shutdown(void);

/* 便捷包装：服务一次 UDS 请求 */
int32_t ESA_Stepping_UDS_RunOnce(void);
```

**UDS 实现细节**：
- Socket 路径：`/tmp/cfe_sim_stepping.sock`
- 服务线程：启用 stepping 时自动创建后台 pthread 线程运行 `ESA_Stepping_UDS_ServiceLoop_Task`
- 请求格式：固定大小结构（opcode + timeout_ms），使用本地字节序
- 响应格式：状态码（int32_t）或完整状态结构（status + state + trigger_count）

**支持命令**：
- `UDS_BEGIN_STEP_OPCODE` (1): 开始步进
- `UDS_QUERY_STATE_OPCODE` (2): 查询状态
- `UDS_WAIT_STEP_COMPLETE_OPCODE` (3): 等待完成

## 状态机转换图

```
                    +---------+
                    |  INIT   |  <-- 初始化状态
                    +----+----+
                         |
                         | ESA_Stepping_Core_Init() 成功
                         v
                    +---------+
         +--------->|  READY  |  <-- 等待步进命令
         |         +----+----+
         |              |
         |              | 触发器报告 / BeginStepSession()
         |              v
         |         +---------+
         |         | RUNNING |  <-- 正在执行步进（触发器添加时进入）
         |         +----+----+
         |              |
         |              | 调度器分发完成 + 已确认触发器
         |              v
         |         +---------+
         |         | WAITING |  <-- 等待（代码特定路径进入）
         |         +----+----+
         |              |
         |              | IsStepComplete() 判定为真
         |              v
         |         +---------+
         +---------| COMPLETE|  <-- 步进完成（条件满足时进入）
                   +---------+
```

**注意**：上图仅为示意，实际状态转换由代码中具体条件决定：
- `READY→RUNNING`：由各类 Report 函数在添加触发器时触发
- `RUNNING→WAITING`：仅由 `ESA_Stepping_Core_ReportSchDispatchComplete()` 在特定条件下触发
- `WAITING→COMPLETE`：仅当 `ESA_Stepping_Core_IsStepComplete()` 判定条件满足时触发

### 状态说明

| 状态 | 含义 | 转换条件 |
|------|------|----------|
| INIT | 未初始化 | Core 初始状态，Init 成功后变为 READY |
| READY | 就绪等待 | 等待步进命令或触发器到达 |
| RUNNING | 运行中 | 步进已开始，正在收集触发器 |
| WAITING | 等待中 | 调度器分发完成且存在已确认触发器时进入（代码特定路径） |
| COMPLETE | 完成 | `IsStepComplete()` 判定条件满足（`completion_requested`、`completion_ready`、ack 计数等） |

**生命周期就绪状态**：
- `system_ready_for_stepping` 是独立于状态机的持久标志
- 通过 `ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING` 事件设置
- 在步进会话重置和步进周期完成后仍然保持

## 构建方法

### 完整构建（带 Stepping）

```bash
# 清理构建目录
make distclean

# 配置（启用 Stepping）
make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep

# 编译
make

# 安装
make install

# 运行测试
make test
```

### 标准构建（不带 Stepping）

```bash
make distclean
make SIMULATION=native ENABLE_UNIT_TESTS=true prep
make
make install
make test
```

### 验证构建结果

```bash
# 运行测试验证
make test 2>&1 | grep -E "tests passed|tests failed"
```

## API 参考

### 初始化与 Hook API

```c
/* 初始化 ESA 模块（在 BSP main() 中调用） */
void ESA_Init(void);

/* 获取模拟时间 */
bool ESA_Stepping_Hook_GetTime(uint64_t *sim_time_ns);

/* 查询 TaskDelay 是否可被 stepping 处理 */
bool ESA_Stepping_Hook_TaskDelayEligible(uint32_t task_id, uint32_t delay_ms);

/* 等待延迟到期（阻塞直到模拟时间满足） */
int32_t ESA_Stepping_WaitForDelayExpiry(uint32_t task_id, uint32_t delay_ms);
```

### Shim API

```c
/* 统一事件报告（所有 Hook 调用此函数转发事件） */
int32_t ESA_Stepping_Shim_ReportEvent(const ESA_Stepping_ShimEvent_t *event);
```

### InProc 适配器 API

```c
/* 进程内步进控制 */
int32_t ESA_Stepping_InProc_BeginStep(void);
int32_t ESA_Stepping_InProc_WaitStepComplete(uint32_t timeout_ms);
int32_t ESA_Stepping_InProc_QueryState(uint32_t *state_out, uint32_t *trigger_count);
```

### UDS 适配器 API

```c
/* UDS 步进控制 */
int32_t ESA_Stepping_UDS_Init(void);
int32_t ESA_Stepping_UDS_Service(void);
int32_t ESA_Stepping_UDS_Shutdown(void);
int32_t ESA_Stepping_UDS_RunOnce(void);
```

### 核心 API（内部使用）

```c
/* 核心初始化 */
int32_t ESA_Stepping_Core_Init(ESA_Stepping_Core_t *core, 
                               uint64_t initial_time_ns, 
                               uint32_t trigger_capacity);

/* 核心重置 */
int32_t ESA_Stepping_Core_Reset(ESA_Stepping_Core_t *core);

/* 开始步进会话 */
int32_t ESA_Stepping_Core_BeginStepSession(ESA_Stepping_Core_t *core);

/* 查询模拟时间 */
int32_t ESA_Stepping_Core_QuerySimTime(ESA_Stepping_Core_t *core, 
                                       uint64_t *sim_time_ns);

/* 检查步进是否完成 */
bool ESA_Stepping_Core_IsStepComplete(ESA_Stepping_Core_t *core);

/* 查询当前状态 */
int32_t ESA_Stepping_Core_QueryState(ESA_Stepping_Core_t *core,
                                     ESA_Stepping_CoreState_t *state_out);
```

## 使用示例

### 示例 1：进程内步进控制

```c
#include "esa_stepping.h"

void simulation_main_loop(void)
{
    uint32_t state, trigger_count;
    int32_t status;
    
    /* 查询初始状态 */
    status = ESA_Stepping_InProc_QueryState(&state, &trigger_count);
    if (status != 0) {
        printf("Stepping 未初始化\n");
        return;
    }
    
    /* 主仿真循环 */
    for (int step = 0; step < 1000; step++) {
        /* 开始新的步进周期 */
        status = ESA_Stepping_InProc_BeginStep();
        if (status != 0) {
            printf("步进 %d 开始失败\n", step);
            break;
        }
        
        /* 等待步进完成（无限等待） */
        status = ESA_Stepping_InProc_WaitStepComplete(0);
        if (status != 0) {
            printf("步进 %d 等待失败\n", step);
            break;
        }
        
        /* 查询状态 */
        ESA_Stepping_InProc_QueryState(&state, &trigger_count);
        printf("步进 %d 完成，状态=%u，触发器=%u\n", 
               step, state, trigger_count);
    }
}
```

### 示例 2：UDS 外部控制（伪代码示例）

```
UDS 请求/响应协议使用本地字节序（host byte order）和固定大小结构：

请求格式（8 字节）：
- opcode: uint8_t (1 字节) + 填充
- timeout_ms: uint32_t (4 字节)

BEGIN_STEP / WAIT_STEP_COMPLETE 响应（4 字节）：
- status: int32_t

QUERY_STATE 响应（12 字节）：
- status: int32_t
- state: uint32_t
- trigger_count: uint32_t

注意：实际结构布局由编译器决定，包含可能的填充字节。
使用此协议时需确保客户端和服务端在同一平台上运行。
```

---

## 文件列表

| 文件 | 说明 |
|------|------|
| `esa/public_inc/esa_stepping_shim.h` | Shim 层公共头文件（事件类型、事件结构、Shim API） |
| `esa/fsw/inc/esa_stepping_core.h` | Core 层内部头文件（状态机、Core API） |
| `esa/fsw/inc/esa_stepping.h` | 公共头文件（ESA_Init、Hook API、适配器 API） |
| `esa/fsw/src/esa_stepping.c` | 主实现（Shim、InProc/UDS 适配器、后台服务线程） |
| `esa/fsw/src/esa_stepping_core.c` | Core 状态机实现 |
| `esa/CMakeLists.txt` | 构建配置 |

## 调试与诊断

ESA Core 维护以下诊断计数器：

```c
typedef struct ESA_Stepping_Diagnostics {
    uint32_t timeout_count;           /* 超时次数 */
    uint32_t duplicate_begin_count;   /* 重复开始次数 */
    uint32_t illegal_complete_count;  /* 非法完成次数 */
    uint32_t illegal_state_count;     /* 非法状态次数 */
    uint32_t transport_error_count;   /* 传输错误次数 */
    uint32_t protocol_error_count;    /* 协议错误次数 */
} ESA_Stepping_Diagnostics_t;
```

诊断日志输出格式：
```
CFE_PSP: SIM_STEPPING_DIAG class=<class> status=<status> site=<site> detail_a=<a> detail_b=<b>
```

示例：
```
CFE_PSP: SIM_STEPPING_DIAG class=timeout status=-4 site=InProc_WaitStepComplete detail_a=1000 detail_b=1001
```

## 注意事项

1. **编译时开关**：`CFE_SIM_STEPPING` 是编译时宏，不能在运行时动态开启/关闭
2. **后台线程**：启用 stepping 时，ESA_Init() 会创建后台 pthread 线程处理 UDS 请求
3. **资源限制**：最大支持 32 个并发触发器（编译时固定，由 `ESA_SIM_STEPPING_MAX_TRIGGERS` 定义）
4. **UDS 路径**：固定使用 `/tmp/cfe_sim_stepping.sock`，确保目录可写
5. **弱符号**：测试构建使用弱符号存根，避免链接依赖问题

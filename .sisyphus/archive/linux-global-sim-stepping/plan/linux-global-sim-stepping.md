# Linux-only cFS 全局仿真步进方案

## TL;DR

> **Quick Summary**: 在 `SIMULATION=native` 下，为整个 cFS 执行体规划一套尽量对齐 TrickCFS 的全局仿真步进机制：外部引擎按 **SCH 次帧** 推进，每个 step 只等待**本 step 实际被触发**的对象完成 `trigger -> ack -> complete`。
>
> **Deliverables**:
> - Linux/POSIX 仿真专用的步进 hook/shim 与 native-only 状态机实现
> - 统一仿真时间来源（PSP/TIME/OSAL 一致）
> - `sch` 主调度接入与 cFE 核心模块动态等待模型
> - 两种控制通道：同进程函数调用版 + Unix 域 socket 版
> - tests-after + agent 可执行 QA 场景
>
> **Estimated Effort**: XL
> **Parallel Execution**: YES - 4 waves
> **Critical Path**: T1 → T2/T3 → T6/T8 → T11/T12 → T14 → F1-F4

---

## Context

### Original Request
参考 TrickCFS，使 cFS 在 Linux 仿真模式下由外部仿真引擎统一调度，并确保整个 cFS 的时间与 step 完成判定一致。

### Interview Summary
**Key Discussions**:
- 目标已从“只让 `sch` 接外部调度”升级为“**整个 cFS 执行体**的 Linux-only 全局步进”。
- 完成判定严格采用 TrickCFS 语义：`trigger -> ack -> complete`。
- 外部引擎推进时间前，只等待 **当前 step 实际触发的对象集合**，而不是更强的“系统全局空闲”。
- `sch` 是 canonical scheduler，step 粒度按 **SCH 次帧**。
- `sbn` 不加载，也不承担控制接口。
- 需要同时规划两种控制通道：**同进程函数调用版** 与 **Unix 域 socket 版**。
- cFS 启动阶段采用 **先正常启动，再进入 step 模式**。
- `OS_TaskDelay` 改造是首期必需，因为统一时间语义必须覆盖 delay/sleep 路径。

**Research Findings**:
- `apps/sch/fsw/src/sch_app.c` 当前靠 `OS_BinSemTake(SCH_AppData.TimeSemaphore)` 驱动次帧处理。
- `apps/sch/fsw/src/sch_custom.c` 当前通过 timer + TIME sync callback 驱动调度。
- `cfe/modules/time/fsw/src/cfe_time_task.c` 当前 Draco v7.0.0 实现表现为单任务 `CFE_SB_ReceiveBuffer` 循环；因此 TIME 在首期更接近普通 pipe-based core service，而不是 TrickCFS 文档中那种必须协调多个子任务/信号量的形态。
- `apps/to_lab/fsw/src/to_lab_app.c` 是典型 `OS_TaskDelay()` 驱动 app，证明 delay 路径不能被忽略。
- `cfe/modules/es/fsw/src/cfe_es_backgroundtask.c` 是后台维护任务，缺少稳定的 step 级 request/ack/complete 语义，因此默认不纳入 wait-set。
- TrickCFS 的系统完成判定并不等价于 `main_complete()`；它还要等待所有被触发对象完成 `acknowledgePipeTrigger()` 与 `markPipeAsComplete()`。

### Metis Review
**Identified Gaps** (addressed):
- 不能把“静态 app 覆盖范围”误当作步进核心语义；已改为 **动态 trigger set**。
- 不能假设 cFE 核心模块天然不参与 wait-set；`sch` 默认就会触发 ES/EVS/SB/TIME/TBL 的 HK 请求。
- OSAL/PSP 不应直接依赖高层 stepping core；已收敛为 **宏裁剪 + 极薄 hook/shim + native-only 实现文件**。
- ES background 不适合作为默认 participant；计划中明确只纳入 ES 主任务，不默认等待 background task。

---

## Work Objectives

### Core Objective
为 Linux-only 的 cFS 原生仿真构建一套接近 TrickCFS 的全局步进机制，使外部引擎以 SCH 次帧为单位推进系统，并且在推进前确认当前 step 中所有被实际触发的对象都已完成 `trigger -> ack -> complete`。

### Concrete Deliverables
- native-only 仿真步进宏与构建装配。
- POSIX OSAL / PSP / TIME / `sch` 的仿真步进接入点。
- 两种控制通道适配：函数调用版、Unix 域 socket 版。
- 统一仿真时间获取路径。
- tests-after 与可复现的 Linux 回归/QA 方案。

### Definition of Done
- [ ] `SIMULATION=native` 下，可在启动后切换到 step 模式。
- [ ] 一个 SCH 次帧 step 只在当前 step 实际触发对象都完成 `ack` 与 `complete` 后才返回。
- [ ] `OS_TaskDelay`、queue/pipe、binsem、timebase 相关路径都受统一仿真时间控制。
- [ ] 默认非仿真构建行为不变，且不链接任何仿真控制逻辑。

### Must Have
- 动态 trigger set，核心语义对齐 TrickCFS。
- `sch` 次帧为 step 粒度。
- 启动期与运行期分离：先正常启动，再进入 step 模式。
- Unix 域 socket 与同进程函数调用两种控制通道。
- Linux/POSIX only，不修改 OSAL shared 跨平台层。

### Must NOT Have (Guardrails)
- 不修改 `osal/src/os/shared/src/`。
- 不让 `sbn` 参与控制通道或默认加载。
- 不把静态 allowlist 作为核心同步语义。
- 不把 ES background 默认纳入 completion wait。
- 不让 OSAL/PSP 直接依赖高层 socket/状态机实现。
- 不把“全局系统空闲”升级为 step 完成标准。

---

## Verification Strategy

> **ZERO HUMAN INTERVENTION** — 全部通过 agent 执行命令、日志检查、socket 驱动和构建/测试命令完成。

### Test Decision
- **Infrastructure exists**: YES
- **Automated tests**: Tests-after
- **Framework**: 现有 `ENABLE_UNIT_TESTS=true` + `make test` / CTest + Linux 仿真运行脚本

### QA Policy
- OSAL/PSP/TIME/SCH 关键接入点需同时有：
  - 构建隔离检查
  - 运行时握手日志/计数检查
  - 超时/错误场景检查
- Evidence 保存到 `.sisyphus/evidence/task-{N}-{slug}.{ext}`。

---

## Execution Strategy

### Parallel Execution Waves

```text
Wave 1 (foundation)
├── T1 构建门控与仿真模式边界 [quick]
├── T2 最小 hook/shim ABI 与 native-only stepping core 骨架 [deep]
├── T3 PSP/TIME 统一仿真时间源 [deep]
├── T4 POSIX TaskDelay 接管 [unspecified-high]
└── T5 POSIX Queue/BinSem 接管与运行时绑定 [unspecified-high]

Wave 2 (scheduler + core integration)
├── T6 SCH 次帧 step 驱动与 send-side trigger 标记 [deep]
├── T7 cFE 核心模块 wait-set 语义接入 [deep]
├── T8 TIME 主任务 / 1Hz / tone 子任务协同 [deep]
├── T9 同进程函数调用控制通道 [quick]
└── T10 Unix 域 socket 控制通道 [unspecified-high]

Wave 3 (startup / hardening)
├── T11 启动后进入 step 模式与 ready barrier [deep]
├── T12 timeout / diagnostics / error policy [unspecified-high]
├── T13 OS_TaskDelay 型 app 验证（to_lab 等） [quick]
├── T14 tests-after 与 Linux 回归 [deep]
└── T15 运行手册与证据规范 [writing]

Wave FINAL
├── F1 Plan compliance audit (oracle)
├── F2 Code quality review (unspecified-high)
├── F3 Runtime QA replay (unspecified-high)
└── F4 Scope fidelity check (deep)
```

### Dependency Matrix

- **T1**: — → T2,T3,T4,T5,T9,T10
- **T2**: T1 → T4,T5,T6,T7,T8,T9,T10,T12
- **T3**: T1 → T6,T8,T11,T14
- **T4**: T1,T2 → T13,T14
- **T5**: T1,T2 → T6,T7,T8,T12,T14
- **T6**: T1,T2,T3,T5 → T11,T12,T14
- **T7**: T2,T5 → T11,T12,T14
- **T8**: T2,T3,T5 → T11,T12,T14
- **T9**: T1,T2 → T11,T14,T15
- **T10**: T1,T2 → T11,T14,T15
- **T11**: T3,T6,T7,T8,T9,T10 → T14,T15
- **T12**: T2,T5,T6,T7,T8 → T14,T15
- **T13**: T4 → T14,T15
- **T14**: T4,T5,T6,T7,T8,T9,T10,T11,T12,T13 → F1-F4
- **T15**: T9,T10,T11,T12,T13 → F1-F4

### Agent Dispatch Summary

- **Wave 1**: T1 → `quick`, T2/T3 → `deep`, T4/T5 → `unspecified-high`
- **Wave 2**: T6/T7/T8 → `deep`, T9 → `quick`, T10 → `unspecified-high`
- **Wave 3**: T11/T14 → `deep`, T12 → `unspecified-high`, T13 → `quick`, T15 → `writing`
- **FINAL**: F1 → `oracle`, F2/F3 → `unspecified-high`, F4 → `deep`

---

## TODOs

- [x] T1. 构建门控与仿真模式边界

  **What to do**:
  - 在 `SIMULATION=native` 下引入单独的 stepping 构建开关（如 `CFE_SIM_STEPPING`），并确保默认构建完全不包含该逻辑。
  - 统一规划宏裁剪边界：OSAL POSIX、PSP native/timebase、TIME、`sch`、控制通道适配层。
  - 明确仿真模式入口：先正常启动，后切换 step mode。

  **Must NOT do**:
  - 不修改 `osal/src/os/shared/src/`。
  - 不让默认构建暴露仿真符号或链接仿真控制实现。

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 以构建开关、宏边界与装配点规划为主。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: T2,T3,T4,T5,T9,T10
  - **Blocked By**: None

  **References**:
  - `sample_defs/targets.cmake` - native 构建/模块装配入口。
  - `sample_defs/cpu1_cfe_es_startup.scr` - 标准启动顺序基线。
  - `Makefile` / mission build 路径 - `SIMULATION=native` 现有构建方式。

  **Acceptance Criteria**:
  - [ ] native-only stepping 宏与装配路径在构建配置中可检出。
  - [ ] 默认构建不包含 stepping 相关符号/字符串。

  **QA Scenarios**:
  ```text
  Scenario: native stepping 构建启用
    Tool: Bash
    Preconditions: 构建配置已更新
    Steps:
      1. make SIMULATION=native prep && make
      2. nm build/exe/cpu1/core-cpu1 | grep "CFE_SIM_STEPPING\|SIM_STEP"
    Expected Result: native 构建成功且能检出 stepping 相关符号/字符串
    Failure Indicators: 构建失败；无任何 stepping 痕迹
    Evidence: .sisyphus/evidence/task-t1-native-gate.txt

  Scenario: 默认构建不泄漏 stepping
    Tool: Bash
    Preconditions: 默认构建可执行
    Steps:
      1. make distclean || true
      2. make prep && make
      3. nm build/exe/cpu1/core-cpu1 | grep "CFE_SIM_STEPPING\|SIM_STEP" && exit 1 || true
    Expected Result: 默认构建成功且不命中 stepping 符号
    Failure Indicators: 命中 stepping 符号或默认构建失败
    Evidence: .sisyphus/evidence/task-t1-default-clean.txt
  ```

  **Commit**: YES
  - Message: `build(sim): gate native stepping mode`

- [x] T2. 最小 hook/shim ABI 与 native-only stepping core 骨架

  **What to do**:
  - 定义一个极薄的 native-only hook/shim 接口，供 OSAL/PSP/TIME/SCH 调用，默认实现为 no-op。
  - 把真正的 step 状态机、动态 trigger set、ack/complete 跟踪、timeout、控制通道适配放进单独的 native-only stepping core 实现文件。
  - 明确 hook/shim 与 core 的边界：低层只上报事实事件，不维护全局判定语义。

  **Must NOT do**:
  - 不让 OSAL/PSP 直接依赖 socket 协议或高层状态机对象。
  - 不在多个模块各自维护一套 trigger/ack/complete 状态。

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 这是整个方案的核心分层决策与 ABI 设计。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: T4,T5,T6,T7,T8,T9,T10,T12
  - **Blocked By**: T1

  **References**:
  - `models/TrickCFSInterface.hh/.cpp` (TrickCFS) - 动态 trigger set、ack/complete 与协调核心语义来源。
  - `apps/sample_app/fsw/src/sample_app.c` - 典型 `ReceiveBuffer -> process -> wait` 循环。
  - 本计划的 Must Have / Guardrails - 约束 ABI 设计边界。

  **Acceptance Criteria**:
  - [ ] 存在单一 stepping core 入口与单一 hook/shim ABI。
  - [ ] 非 sim 构建下 hook/shim 为空实现。
  - [ ] 同进程与 UDS 两种控制通道共享同一 core 状态机。

  **QA Scenarios**:
  ```text
  Scenario: hook/shim 与 core 分层清晰
    Tool: Bash
    Preconditions: stepping 代码骨架已落位
    Steps:
      1. grep -R "SimStepHook_\|SimStepCore_\|trigger_set\|ack\|complete" osal psp cfe apps libs sample_defs
      2. 核对 OSAL/PSP 中只出现 hook 调用，不出现 socket/trigger-set 细节
    Expected Result: 低层仅调用 hook/shim，状态机逻辑集中在 native-only stepping core
    Failure Indicators: OSAL/PSP 内出现分散的状态机或 socket 逻辑
    Evidence: .sisyphus/evidence/task-t2-layering.txt

  Scenario: no-op 默认实现存在
    Tool: Bash
    Preconditions: 非 sim stub 已提供
    Steps:
      1. grep -R "no-op\|noop\|stub" osal psp cfe apps libs | grep "SimStepHook"
    Expected Result: 可定位默认空实现路径
    Failure Indicators: 无默认空实现，导致非 sim 也需链接 stepping core
    Evidence: .sisyphus/evidence/task-t2-noop.txt
  ```

  **Commit**: YES
  - Message: `feat(sim): add native stepping hook core skeleton`

- [x] T3. PSP/TIME 统一仿真时间源

  **What to do**:
  - 规划 native-only 仿真时间来源，使 `CFE_PSP_GetTime`、TIME 服务和 OSAL/timebase 看到同一仿真时间。
  - 使用 POSIX timebase / PSP native 路径的受控接入点替换 wall-clock 语义。
  - 确保 step 粒度按 SCH 次帧推进，但 TIME 主任务 / 1Hz / tone 子任务仍有一致的仿真时间视图。

  **Must NOT do**:
  - 不改 TIME 跨平台公共语义。
  - 不让 wall-clock 与 sim-clock 并存竞争。

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 时间源一致性是整个步进语义的根基。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: T6,T8,T11,T14
  - **Blocked By**: T1

  **References**:
  - `psp/fsw/modules/timebase_posix_clock/cfe_psp_timebase_posix_clock.c` - 当前 native 取时基线。
  - `psp/fsw/modules/soft_timebase/cfe_psp_soft_timebase.c` - 软件 timebase 接入点。
  - `cfe/modules/time/fsw/src/cfe_time_task.c` - TIME 主任务与子任务协同。

  **Acceptance Criteria**:
  - [ ] 所有仿真取时路径共享同一 sim time 源。
  - [ ] 一个 step 后的时间增量可与 SCH 次帧配置一致对应。

  **QA Scenarios**:
  ```text
  Scenario: 仿真时间单源一致
    Tool: Bash
    Preconditions: native sim 构建完成
    Steps:
      1. 启动 cFS 到 step 模式
      2. 连续推进 5 个 step
      3. 采集 TIME/HK/日志中的时间戳并对比步数
    Expected Result: 时间随 step 单调推进，且不跟随 wall-clock 漂移
    Failure Indicators: 时间回退、双源竞争、与 step 数不一致
    Evidence: .sisyphus/evidence/task-t3-sim-time.txt

  Scenario: 默认构建仍用原生时间
    Tool: Bash
    Preconditions: 默认构建可运行
    Steps:
      1. 运行默认构建并检查 timebase/取时日志路径
    Expected Result: 默认构建不进入 sim time 路径
    Failure Indicators: 默认构建命中 sim time 分支
    Evidence: .sisyphus/evidence/task-t3-default-time.txt
  ```

  **Commit**: YES
  - Message: `feat(time): align native sim time source`

- [x] T4. POSIX `OS_TaskDelay` 接管

  **What to do**:
  - 在 `OS_TaskDelay_Impl` 中加入 native-only 仿真 hook，使 delay/sleep 语义受 sim step 控制。
  - 明确 startup 阶段与 step 运行阶段的行为切换，避免阻塞启动同步逻辑。
  - 覆盖 `to_lab` 这类 delay-driven app 的时间一致性路径。

  **Must NOT do**:
  - 不让启动期的 `OS_TaskDelay` 直接被 step barrier 卡死。
  - 不改变非 sim 路径的 sleep 语义。

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 涉及底层阻塞原语与启动期切换，风险较高。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: T13,T14
  - **Blocked By**: T1,T2

  **References**:
  - `osal/src/os/posix/src/os-impl-tasks.c` - `OS_TaskDelay_Impl` 现有实现。
  - `apps/to_lab/fsw/src/to_lab_app.c` - delay-driven 典型 app。
  - `cfe/modules/es/fsw/src/cfe_es_apps.c` - 启动期/后台路径中对 delay 的依赖需谨慎处理。

  **Acceptance Criteria**:
  - [ ] step 模式下 `OS_TaskDelay` 不再按 wall-clock 推进。
  - [ ] 启动期仍可正常完成，不因 stepping hook 死锁。

  **QA Scenarios**:
  ```text
  Scenario: to_lab 不因 wall-clock 自行推进
    Tool: Bash
    Preconditions: to_lab 已加载，step 模式启用
    Steps:
      1. 启动 cFS 并进入 step 模式
      2. 暂不推进 step，观察一段 wall-clock 时间
      3. 再推进 3 个 step，观察 to_lab 输出/计数
    Expected Result: 未 step 时无额外推进；step 后输出与 step 数匹配
    Failure Indicators: 未 step 时仍周期性输出
    Evidence: .sisyphus/evidence/task-t4-delay-control.txt

  Scenario: 启动期不被 TaskDelay hook 阻断
    Tool: Bash
    Preconditions: native sim 构建完成
    Steps:
      1. 启动 cFS
      2. 检查进入 OPERATIONAL 状态的日志
    Expected Result: 系统先正常启动，再进入可步进状态
    Failure Indicators: 启动卡死或未进入 OPERATIONAL
    Evidence: .sisyphus/evidence/task-t4-startup.txt
  ```

  **Commit**: YES
  - Message: `feat(osal): control native task delay stepping`

- [x] T5. POSIX Queue/BinSem 接管与运行时绑定

  **What to do**:
  - 在 queue receive / binsem wait 路径增加 native-only stepping hook，支持运行时 `task/pipe/queue` 绑定与 wait-boundary 上报。
  - 对齐 TrickCFS 语义：接收时可形成 `ack` 候选，回到等待边界形成 `complete` 候选。
  - 明确哪些 binsem 路径只共享时间语义、哪些路径真正进入 completion wait 模型。

  **Must NOT do**:
  - 不在 OSAL 中直接实现完整 trigger set 状态机。
  - 不把 ES background 的 timed maintenance 路径误纳入默认 completion wait。

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 跨 queue/binsem 的统一绑定和边界判定较复杂。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: T6,T7,T8,T12,T14
  - **Blocked By**: T1,T2

  **References**:
  - `osal/src/os/posix/src/os-impl-queues.c` - `OS_QueueGet_Impl` 语义接入点。
  - `osal/src/os/posix/src/os-impl-binsem.c` - `OS_BinSemTake_Impl` 等待边界接入点。
  - `apps/sample_app/fsw/src/sample_app.c` - 标准 SB receive 循环范式。
  - `models/TrickCFSInterface.cpp` - `acknowledgePipeTrigger` / `markPipeAsComplete` 语义参考。

  **Acceptance Criteria**:
  - [ ] queue/binsem 路径能够上报 ack/complete 所需运行时事实事件。
  - [ ] ES background 默认不进入 completion wait。

  **QA Scenarios**:
  ```text
  Scenario: SB receive 重入形成 complete 边界
    Tool: Bash
    Preconditions: 相关 hook 已启用
    Steps:
      1. 启动 cFS，推进一个 step，触发 sample_app/核心模块处理一条消息
      2. 检查日志中 receive/complete 事件顺序
    Expected Result: 先有 receive/ack，再在重入 wait 边界时出现 complete
    Failure Indicators: 无 complete；或 complete 早于 receive
    Evidence: .sisyphus/evidence/task-t5-queue-complete.txt

  Scenario: ES background 未默认进入 wait-set
    Tool: Bash
    Preconditions: ES background 仍存在
    Steps:
      1. 推进 step 并检查 wait-set/日志
      2. 搜索是否出现 background task 被强制计入当前 step 完成条件
    Expected Result: ES background 默认不在当前 step completion wait 集中
    Failure Indicators: background task 阻塞 step 完成
    Evidence: .sisyphus/evidence/task-t5-esbg-excluded.txt
  ```

  **Commit**: YES
  - Message: `feat(osal): add native queue and sem stepping hooks`

- [x] T6. SCH 次帧 step 驱动与 send-side trigger 标记

  **What to do**:
  - 在正式 `sch` 的次帧路径中接入 step 开始/结束语义，保持 `SCH_AppData.TimeSemaphore` 与当前 slot 推进模型不变。
  - 在调度发送路径中为每个目标 pipe/message 标记本 step 实际触发对象，形成动态 trigger set。
  - 区分 scheduler dispatch complete 与 system step complete，避免把 `sch` 自身完成误判为全系统完成。

  **Must NOT do**:
  - 不改用 `sch_lab` 作为主实现路径。
  - 不把静态 app 清单当作 trigger set。

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 涉及 canonical scheduler 行为与全局语义对齐。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: T11,T12,T14
  - **Blocked By**: T1,T2,T3,T5

  **References**:
  - `apps/sch/fsw/src/sch_app.c` - `OS_BinSemTake(SCH_AppData.TimeSemaphore)` 与主循环基线。
  - `apps/sch/fsw/src/sch_custom.c` - major/minor frame 与 slot 计算基线。
  - `apps/sch/fsw/tables/sch_def_msgtbl.c` / `sch_def_schtbl.c` - 当前默认会触发哪些目标的证据。
  - `apps/sch_trick/fsw/src/BaseTrickCFSScheduler.cpp` (TrickCFS) - send-side `mark_pipe_as_tiggered` 语义参考。

  **Acceptance Criteria**:
  - [ ] 每个次帧 step 都能形成准确的 send-side trigger 集合。
  - [ ] scheduler complete 与 system complete 在日志/状态中可区分。

  **QA Scenarios**:
  ```text
  Scenario: 次帧 step 只标记本次实际触发对象
    Tool: Bash
    Preconditions: sch 已接入 stepping，默认 schedule table 已加载
    Steps:
      1. 启动 cFS 并进入 step 模式
      2. 推进 1 个次帧 step
      3. 检查 trigger-set 日志/证据，确认只出现本次帧实际发送的消息目标
    Expected Result: trigger-set 与本次发送动作一致，无额外 participant
    Failure Indicators: 触发集合包含未发送目标，或漏掉已发送目标
    Evidence: .sisyphus/evidence/task-t6-trigger-set.txt

  Scenario: scheduler complete 不等于 system complete
    Tool: Bash
    Preconditions: 至少一个被触发目标需要随后 ack/complete
    Steps:
      1. 推进 1 个 step
      2. 检查日志中 scheduler dispatch complete 与最终 step complete 的顺序
    Expected Result: 先出现 scheduler complete，再在所有目标 complete 后结束 step
    Failure Indicators: scheduler complete 直接放行下一 step
    Evidence: .sisyphus/evidence/task-t6-complete-order.txt
  ```

  **Commit**: YES
  - Message: `feat(sch): drive dynamic trigger-set stepping`

- [x] T7. cFE 核心模块 wait-set 语义接入

  **What to do**:
  - 将 ES/EVS/SB/TBL/TIME 主任务纳入“被当前 step 实际触发时才等待”的动态模型。
  - 以 `CFE_SB_ReceiveBuffer(..., PEND_FOREVER)` / queue receive 重入边界作为主完成语义来源。
  - 明确核心模块触发来源，尤其是 `sch` 默认 HK 请求触发链。

  **Must NOT do**:
  - 不把所有 cFE 任务无差别纳入 wait-set。
  - 不把 ES background 作为默认 participant。

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 涉及核心服务任务语义与动态等待边界。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: T11,T12,T14
  - **Blocked By**: T2,T5

  **References**:
  - `cfe/modules/es/fsw/src/cfe_es_task.c` - ES 主任务命令接收循环。
  - `cfe/modules/evs/fsw/src/cfe_evs_task.c` - EVS 主任务命令接收循环。
  - `cfe/modules/sb/fsw/src/cfe_sb_task.c` - SB 主任务命令接收循环。
  - `cfe/modules/tbl/fsw/src/cfe_tbl_task.c` - TBL 主任务命令接收循环。
  - `cfe/modules/time/fsw/src/cfe_time_task.c` - TIME 主任务接收循环。
  - `apps/sch/fsw/tables/sch_def_msgtbl.c` - 默认核心 HK 触发源。

  **Acceptance Criteria**:
  - [ ] 被本 step 触发的核心主任务会进入 wait-set。
  - [ ] 未被本 step 触发的核心主任务不会阻塞当前 step。

  **QA Scenarios**:
  ```text
  Scenario: 核心 HK 请求触发对应核心任务进入 wait-set
    Tool: Bash
    Preconditions: 默认 SCH schedule table 生效
    Steps:
      1. 推进足以命中 CFE HK 槽位的 step
      2. 检查 trigger/ack/complete 证据中是否出现 ES/EVS/SB/TIME/TBL 相关任务
    Expected Result: 被命中的核心任务进入当前 step 动态等待集合
    Failure Indicators: 核心任务被触发却未被等待
    Evidence: .sisyphus/evidence/task-t7-core-hk.txt

  Scenario: 未触发核心任务不阻塞 step
    Tool: Bash
    Preconditions: 选择一个不触发核心 HK 的 step
    Steps:
      1. 推进该 step
      2. 检查最终 wait-set 为空或不包含无关核心任务
    Expected Result: 仅实际触发对象被等待
    Failure Indicators: 无关核心任务也进入 wait-set
    Evidence: .sisyphus/evidence/task-t7-dynamic-only.txt
  ```

  **Commit**: YES
  - Message: `feat(core): add dynamic step wait for triggered services`

- [x] T8. TIME 服务步进语义协同

  **What to do**:
  - 规划当前 TIME 服务在次帧 step 下的步进语义，按 Draco v7.0.0 现状优先视其为 pipe-based core service。
  - 若实现中仍存在额外 tone/1Hz 边界，则明确哪些对象在当前 step 中需要被视为被触发对象，哪些只是共享统一仿真时间。
  - 保证 TIME 相关输出、MET/时钟视图与次帧推进语义一致。

  **Must NOT do**:
  - 不让 TIME 相关内部边界在未被当前 step 合法触发时自主破坏步进收敛。
  - 不用 wall-clock 偷跑 tone/1Hz 逻辑。

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: TIME 服务仍是 cFE 核心边界，但当前实现较 TrickCFS 假设更简化，仍需单独收敛其步进语义。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: T11,T12,T14
  - **Blocked By**: T2,T3,T5

  **References**:
  - `cfe/modules/time/fsw/src/cfe_time_task.c` - 当前 TIME 主任务与 pipe-based 接收循环基线。
  - `cfe/modules/time/fsw/src/cfe_time_tone.c` - 若仍需考虑 tone/1Hz 相关边界时的补充参考。
  - `apps/sch/fsw/src/sch_custom.c` - 次帧与 TIME 同步耦合点。

  **Acceptance Criteria**:
  - [ ] TIME 服务在 step 模式下行为可解释且不自发漂移。
  - [ ] 次帧推进与 TIME 相关可观测输出一致。

  **QA Scenarios**:
  ```text
  Scenario: TIME 服务不脱离 step 独立推进
    Tool: Bash
    Preconditions: native step 模式启用
    Steps:
      1. 启动 cFS，暂停 step 推进
      2. 观察 TIME 相关日志/计数一段 wall-clock 时间
      3. 再推进若干 step 比较变化
    Expected Result: 未推进 step 时 TIME 输出不自行前进；推进后与步数一致
    Failure Indicators: TIME 服务脱离 step 自主运行
    Evidence: .sisyphus/evidence/task-t8-time-children.txt

  Scenario: 命中 TIME 相关触发时正确进入 wait-set
    Tool: Bash
    Preconditions: 当前 step 触发 TIME 相关工作
    Steps:
      1. 推进命中 TIME 触发的 step
      2. 检查 TIME 主任务/必要子任务的 ack/complete 证据
    Expected Result: 仅必要的 TIME 对象被等待并完成
    Failure Indicators: TIME 对象漏等或过度纳入
    Evidence: .sisyphus/evidence/task-t8-time-waitset.txt
  ```

  **Commit**: YES
  - Message: `feat(time): coordinate main and child tasks in step mode`

- [x] T9. 同进程函数调用控制通道

  **What to do**:
  - 提供与 TrickCFS 同进程协调语义等价的函数调用版控制通道。
  - 让外部/上层驱动代码可直接调用 step begin / wait complete / query state。
  - 与 UDS 版本共享同一 stepping core，不复制状态机。

  **Must NOT do**:
  - 不复制一套独立状态机。
  - 不把函数调用版专有逻辑塞回 OSAL/PSP。

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 主要是 adapter 层接口与调用约定。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: T11,T14,T15
  - **Blocked By**: T1,T2

  **References**:
  - `models/TrickCFSInterface.cpp` - Linux 下 `eventfd` 协调语义参考。
  - `models/TrickCFSInterface.hh` - 协调核心暴露的方法集合参考。

  **Acceptance Criteria**:
  - [ ] 同进程驱动方可调用统一 API 推进 step 并等待完成。
  - [ ] 与 UDS 版本共享同一状态机与错误语义。

  **QA Scenarios**:
  ```text
  Scenario: 函数调用版可完成单 step 往返
    Tool: Bash
    Preconditions: inproc adapter 已接入测试驱动
    Steps:
      1. 启动 cFS 到可步进状态
      2. 通过测试驱动直接调用 step begin/wait complete
      3. 检查返回值与 step 完成证据
    Expected Result: API 调用成功并等待到本 step 真正完成
    Failure Indicators: 提前返回；无法等待；状态机与日志不一致
    Evidence: .sisyphus/evidence/task-t9-inproc-roundtrip.txt

  Scenario: 函数调用版与核心状态机单一
    Tool: Bash
    Preconditions: 代码已落位
    Steps:
      1. grep -R "step begin\|wait complete\|query state" native sim adapter/core 相关目录
      2. 核对只有一个核心状态机实现被引用
    Expected Result: adapter 只是薄层转发
    Failure Indicators: 出现第二套状态机/计数器
    Evidence: .sisyphus/evidence/task-t9-single-core.txt
  ```

  **Commit**: YES
  - Message: `feat(sim): add inproc control adapter`

- [x] T10. Unix 域 socket 控制通道

  **What to do**:
  - 提供 Linux-only 的 UDS 控制通道，用于独立进程外部仿真引擎驱动 cFS step。
  - 约定最小协议：begin step、wait result、query state、timeout/error。
  - 确保 UDS adapter 仅做传输适配，不复制步进状态机。

  **Must NOT do**:
  - 不使用 `sbn` 作为控制接口。
  - 不让 socket 版与 inproc 版产生不同完成语义。

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 需要处理独立进程 IPC、超时与错误传播。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: T11,T14,T15
  - **Blocked By**: T1,T2

  **References**:
  - `models/TrickCFSInterface.cpp` - 同进程协调语义的等价参考。
  - 本计划已锁定的 Linux-only/独立进程约束 - 决定 UDS 而不是 TCP 或 SBN。

  **Acceptance Criteria**:
  - [ ] 外部独立进程可通过 UDS 成功驱动 step。
  - [ ] UDS 返回结果与 inproc 版本的完成语义一致。

  **QA Scenarios**:
  ```text
  Scenario: UDS 版完成单 step 往返
    Tool: Bash
    Preconditions: UDS adapter 已接入
    Steps:
      1. 启动 cFS 到 step 模式并监听 Unix 域 socket
      2. 用客户端发送 begin-step 请求
      3. 等待 complete/timeout 响应并检查退出码
    Expected Result: UDS 请求可驱动一个完整 step 并得到正确结果
    Failure Indicators: socket 阻塞无响应；结果与 inproc 版不一致
    Evidence: .sisyphus/evidence/task-t10-uds-roundtrip.txt

  Scenario: UDS 超时传播正确
    Tool: Bash
    Preconditions: 可制造卡住 step 的 participant
    Steps:
      1. 发送 begin-step 请求
      2. 故意让一个被触发对象不 complete
      3. 检查 UDS 返回的 timeout 错误
    Expected Result: timeout 通过 UDS 清晰返回
    Failure Indicators: 死锁；无明确错误；错误语义与 inproc 不一致
    Evidence: .sisyphus/evidence/task-t10-uds-timeout.txt
  ```

  **Commit**: YES
  - Message: `feat(sim): add uds control adapter`

- [x] T11. 启动后进入 step 模式与 ready barrier

  **What to do**:
  - 设计并接入“先正常启动，再进入 step 模式”的切换机制，避免启动期被步进屏障卡死。
  - 定义 ready barrier：哪些关键任务/服务必须完成初始化后，外部引擎才允许开始推进 step。
  - 确保 inproc 与 UDS 两种控制通道在进入 step 模式前后语义一致。

  **Must NOT do**:
  - 不让 cFS 在启动期就被外部 step 请求锁死。
  - 不让 ready barrier 依赖静态 app 列表去定义核心语义。

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 涉及系统生命周期切换、协调点和全局时序。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: T14,T15
  - **Blocked By**: T3,T6,T7,T8,T9,T10

  **References**:
  - `sample_defs/cpu1_cfe_es_startup.scr` - 标准启动顺序和 app 装载基线。
  - `cfe/modules/es/fsw/src/cfe_es_task.c` - 运行态进入与任务主循环基线。
  - `cfe/modules/es/fsw/src/cfe_es_apps.c` - 启动同步/延迟相关路径。

  **Acceptance Criteria**:
  - [ ] cFS 能先完整启动到可运行态，再切换到 step 模式。
  - [ ] 只有 ready barrier 满足后，外部 step 请求才会被接受。

  **QA Scenarios**:
  ```text
  Scenario: 启动完成后才接受 step
    Tool: Bash
    Preconditions: step 模式支持已接入
    Steps:
      1. 启动 cFS
      2. 在 ready barrier 之前发送 step 请求
      3. 观察返回结果与日志
      4. 等系统进入 ready 后再次发送 step 请求
    Expected Result: barrier 前拒绝或排队；barrier 后可正常推进 step
    Failure Indicators: 启动期 step 被执行；或 ready 后仍无法推进
    Evidence: .sisyphus/evidence/task-t11-ready-barrier.txt

  Scenario: 两种控制通道切换语义一致
    Tool: Bash
    Preconditions: inproc 与 UDS 两种 adapter 都可用
    Steps:
      1. 分别用两种通道在 ready 前后发送 step 请求
      2. 对比返回状态与日志事件
    Expected Result: 两种通道在 ready barrier 前后行为一致
    Failure Indicators: 一个通道提前放行，另一个阻塞/报错
    Evidence: .sisyphus/evidence/task-t11-dual-adapter-ready.txt
  ```

  **Commit**: YES
  - Message: `feat(sim): add ready barrier and step-mode handoff`

- [x] T12. timeout、诊断与错误策略

  **What to do**:
  - 定义 step 超时、participant 卡死、重复请求、非法状态转换、socket 断链等错误策略。
  - 为 trigger/ack/complete 增加统一诊断输出、计数器与可检索日志。
  - 明确错误时对当前 step 的处理：失败返回、保留现场、禁止静默强推。

  **Must NOT do**:
  - 不让 step timeout 变成无日志的死锁。
  - 不让 inproc 与 UDS 两种通道对同一错误给出不同语义。

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 这是稳定性与可观测性策略层，不只是代码路径接线。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: T14,T15
  - **Blocked By**: T2,T5,T6,T7,T8

  **References**:
  - `models/TrickCFSInterface.cpp` - 完成等待与协调状态语义参考。
  - 本计划中 T6-T10 的状态机/adapter 设计 - 错误策略需统一覆盖。

  **Acceptance Criteria**:
  - [ ] 至少覆盖 step timeout、重复 step、非法 complete、adapter 断链 四类错误。
  - [ ] 所有错误都有统一日志与返回语义。

  **QA Scenarios**:
  ```text
  Scenario: participant 卡死触发 timeout
    Tool: Bash
    Preconditions: 可让一个被触发对象不 complete
    Steps:
      1. 推进一个 step
      2. 阻止其中一个 participant 完成
      3. 检查 timeout 返回、日志和证据
    Expected Result: 在超时窗口后返回失败并留下诊断信息
    Failure Indicators: 死锁无返回；或静默跳过 timeout
    Evidence: .sisyphus/evidence/task-t12-timeout.txt

  Scenario: 重复 step 请求被明确拒绝
    Tool: Bash
    Preconditions: 前一个 step 尚未完成
    Steps:
      1. 发送 begin-step 请求
      2. 在其未完成前再次发送 begin-step
      3. 检查错误语义
    Expected Result: 第二个请求被明确拒绝或按设计排队，并有一致日志
    Failure Indicators: 状态机损坏；双重触发；无错误记录
    Evidence: .sisyphus/evidence/task-t12-reentry.txt
  ```

  **Commit**: YES
  - Message: `feat(sim): add timeout and diagnostics policy`

- [x] T13. `OS_TaskDelay` 型 app 验证（`to_lab` 等）

  **What to do**:
  - 以 `to_lab` 为代表，验证 delay-driven app 在 step 模式下不会脱离外部引擎自行推进。
  - 检查其与 `sch`/核心模块共同存在时，触发与等待语义是否一致。
  - 明确 delay-driven app 在当前 step 中何时进入 wait-set、何时 complete。

  **Must NOT do**:
  - 不只验证 sample_app 这类 SB receive app。
  - 不忽略 delay-driven app 与统一时间语义的差异。

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 以已知代表性 app 做具体验证与回归补洞。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: T14,T15
  - **Blocked By**: T4

  **References**:
  - `apps/to_lab/fsw/src/to_lab_app.c` - delay-driven 代表实现。
  - `osal/src/os/posix/src/os-impl-tasks.c` - delay hook 语义来源。

  **Acceptance Criteria**:
  - [x] `to_lab` 在未 step 时不自行推进。
  - [x] 推进 N 个 step 后，其可观测输出与步数相符。

  **QA Scenarios**:
  ```text
  Scenario: to_lab 与 step 数严格对应
    Tool: Bash
    Preconditions: to_lab 已加载，step 模式工作
    Steps:
      1. 启动 cFS 并进入 ready 状态
      2. 记录初始输出计数
      3. 推进 5 个 step
      4. 比较 to_lab 输出/计数增量
    Expected Result: to_lab 只按 step 推进，不按 wall-clock 漂移
    Failure Indicators: 输出增量与 step 数无关
    Evidence: .sisyphus/evidence/task-t13-tolab-steps.txt

  Scenario: 不推进 step 时 to_lab 保持静止
    Tool: Bash
    Preconditions: to_lab 已启动
    Steps:
      1. 启动系统后静置一段 wall-clock 时间
      2. 检查 to_lab 输出是否变化
    Expected Result: 未 step 时输出保持稳定
    Failure Indicators: 未 step 时仍持续输出
    Evidence: .sisyphus/evidence/task-t13-tolab-idle.txt
  ```

  **Commit**: NO

- [x] T14. tests-after 与 Linux 回归

  **What to do**:
  - 在主路径落地后，补充/更新适合该方案的自动化测试与 Linux-only 回归脚本。
  - 覆盖两种控制通道、核心模块动态 wait-set、delay-driven app、timeout/error 场景。
  - 把构建/运行/测试命令统一到 CI/本地可复现路径。

  **Must NOT do**:
  - 不只依赖人工观察日志。
  - 不只测 happy path。

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 需要把前面所有核心语义压缩为系统性回归测试。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: F1-F4
  - **Blocked By**: T4,T5,T6,T7,T8,T9,T10,T11,T12,T13

  **References**:
  - 现有 `ENABLE_UNIT_TESTS=true` / `make test` 基础设施。
  - 本计划 T1-T13 的 QA Scenarios - 转化为回归套件。

  **Acceptance Criteria**:
  - [ ] 存在可自动执行的 Linux/native 仿真回归命令集。
  - [ ] 回归至少覆盖 inproc、UDS、timeout、核心 HK、to_lab 五类场景。

  **QA Scenarios**:
  ```text
  Scenario: Linux/native 回归脚本一次跑通
    Tool: Bash
    Preconditions: 测试脚本与测试用例已补齐
    Steps:
      1. make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install
      2. make test
      3. 运行 native step 回归脚本
    Expected Result: 所有自动化测试通过
    Failure Indicators: 任一关键场景失败
    Evidence: .sisyphus/evidence/task-t14-regression.txt

  Scenario: error path 自动化存在
    Tool: Bash
    Preconditions: 错误路径测试已接入
    Steps:
      1. 搜索测试目录/脚本中关于 timeout、duplicate、uds error 的用例
    Expected Result: 能检出覆盖主要错误路径的测试
    Failure Indicators: 只有 happy path 回归
    Evidence: .sisyphus/evidence/task-t14-error-tests.txt
  ```

  **Commit**: YES
  - Message: `test(sim): add native stepping regression`

- [x] T15. 运行手册与证据规范

  **What to do**:
  - 编写面向执行者的 Linux/native 仿真步进运行手册，覆盖 build、startup、enter-step-mode、step 驱动、错误排查。
  - 说明两种控制通道的使用方式、证据文件命名、常见故障与日志定位点。
  - 明确哪些对象共享 sim time、哪些对象进入动态 wait-set，以及 ES background 的默认排除规则。

  **Must NOT do**:
  - 不写成抽象概述；必须包含命令、路径、证据命名。
  - 不把“动态 trigger set”写成静态 app allowlist。

  **Recommended Agent Profile**:
  - **Category**: `writing`
    - Reason: 主要产出执行与排障文档。
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: F1-F4
  - **Blocked By**: T9,T10,T11,T12,T13

  **References**:
  - `sample_defs/targets.cmake` / `cpu1_cfe_es_startup.scr` - 运行装配依据。
  - 本计划 T1-T14 - 文档应完整覆盖这些路径。

  **Acceptance Criteria**:
  - [ ] 运行手册包含 build、startup、step drive、error triage 四部分。
  - [ ] 证据规范覆盖 happy path 与 error path。

  **QA Scenarios**:
  ```text
  Scenario: 文档包含可执行命令与路径
    Tool: Bash
    Preconditions: 运行手册已编写
    Steps:
      1. grep -R "SIMULATION=native\|Unix domain socket\|step mode\|Evidence" .sisyphus sample_defs docs
    Expected Result: 文档中能检出构建、运行、控制通道与证据路径说明
    Failure Indicators: 文档只有概念描述，无具体命令/路径
    Evidence: .sisyphus/evidence/task-t15-doc-commands.txt

  Scenario: 文档覆盖错误排查
    Tool: Bash
    Preconditions: 文档已编写
    Steps:
      1. grep -R "timeout\|duplicate step\|ES background\|ready barrier" .sisyphus sample_defs docs
    Expected Result: 文档包含关键错误路径与边界说明
    Failure Indicators: 无错误排查章节
    Evidence: .sisyphus/evidence/task-t15-doc-triage.txt
  ```

  **Commit**: YES
  - Message: `docs(sim): document stepping workflow`

---

## Final Verification Wave

- [x] F1. **Plan Compliance Audit** — `oracle`
  - 核对所有 Must Have / Must NOT Have 是否落实到实际改动与证据。
  - 输出：`Must Have [N/N] | Must NOT Have [N/N] | VERDICT`

- [x] F2. **Code Quality Review** — `unspecified-high`
  - 运行 Linux/native 构建、测试、静态检查；重点搜索仿真宏泄漏、分层反转、重复状态机。
  - 输出：`Build [PASS/FAIL] | Tests [PASS/FAIL] | Leakage [PASS/FAIL] | VERDICT`

- [x] F3. **Runtime QA Replay** — `unspecified-high`
  - 重放所有 happy/error 场景，保存 UDS/inproc 两条通道证据。
  - 输出：`Scenarios [N/N] | Step Sync [PASS/FAIL] | VERDICT`

- [x] F4. **Scope Fidelity Check** — `deep`
  - 比对实际改动与计划边界，确认未触碰 OSAL shared 或无关平台路径。
  - 输出：`Scope [PASS/FAIL] | Forbidden Touches [0/N] | VERDICT`

## Commit Strategy

- Group 1: `build(sim): gate native stepping mode`
- Group 2: `feat(osal): add native stepping shims`
- Group 3: `feat(time): align native sim time source`
- Group 4: `feat(sch): drive dynamic trigger-set stepping`
- Group 5: `feat(sim): add inproc and uds control adapters`
- Group 6: `test(sim): add native stepping regression`
- Group 7: `docs(sim): document stepping workflow`

## Success Criteria

### Verification Commands
```bash
make SIMULATION=native prep && make && make install
make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install && make test
make prep && make
```

### Final Checklist
- [ ] native sim 构建可启用 step mode
- [ ] 默认构建不含仿真步进逻辑
- [ ] step 粒度为 SCH 次帧
- [ ] 当前 step 仅等待实际触发对象
- [ ] cFE 核心模块在被触发时可被等待
- [ ] ES background 默认不进入 wait-set
- [ ] UDS 与同进程两种控制通道均可工作

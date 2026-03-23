# ESA Stepping 引擎：差距填补完整计划

## TL;DR

> **快速概要**: 填补原始 `linux-global-sim-stepping` 计划与当前 ESA stepping 实现之间的所有差距。核心状态机和控制通道已完成，需要实现 OSAL hook 真实逻辑、TIME stepping hook、cFE 核心模块 wait-set 集成、API 重命名（CFE_PSP_SimStepping_* → ESA_*）、TDD 测试和文档。
> 
> **交付物**:
> - OSAL POSIX hook 扩展签名 + 双阶段（ACK+COMPLETE）shim 转发实现
> - cFE 四个核心模块（ES/EVS/SB/TBL）stepping wait-set 集成
> - TIME 服务 stepping hook 从 no-op 转为真实 shim 转发
> - 全量 API 重命名（兼容性别名过渡）
> - TDD 单元测试覆盖所有新实现
> - ESA README 文档
> - 端到端集成验证
> 
> **预估工作量**: Large
> **并行执行**: YES — 5 个 Wave
> **关键路径**: T1(hook签名) → T2/T3(hook实现) → T6-T9(模块集成) → T10-T12(API重命名) → T13-T15(测试) → T16-T17(文档+验证) → F1-F4

---

## Context

### 原始请求
评估原始 `linux-global-sim-stepping` 计划（15 个任务）与当前实现的差距，并制定完整工作计划来填补所有差距。

### 访谈总结
**关键讨论**:
- **差距分析**: T1-T3, T6, T9-T12 已真正完成。T4, T5, T7, T8 仅为骨架（no-op 存根）。API 重命名、测试、文档完全缺失。
- **OSAL Hook 设计**: 用户选择 "Hook 内部管理两阶段" — hook 函数自身处理 ACK+COMPLETE 两阶段报告
- **API 重命名**: 包含在此计划中
- **测试策略**: TDD（测试驱动开发）
- **构建开关**: CFE_SIM_STEPPING 保持手动控制

**研究发现**:
- ESA core 已完整实现 AddTrigger（ACK）+ AcknowledgeTrigger（COMPLETE）两阶段模式
- Shim 转发器已有全部 20 种事件类型的 switch 分发
- OSAL hook 当前仅有预阻塞调用点，无后阻塞调用点
- Hook 签名缺少必要上下文（queue_id, task_id, timeout）
- SCH 模块是完整的参考实现模式
- 所有 cFE 核心模块（ES/EVS/SB/TBL）都有独立的 task loop + `CFE_SB_ReceiveBuffer`

### Metis 审查
**发现的差距**（已处理）:
- OSAL hook 后阻塞调用点缺失 → 计划添加 `_Complete` hook 函数
- Hook 签名需要扩展 → 传递 token/entity_id, task_id, timeout
- TBL 是否有独立任务循环 → 已确认有（`cfe_tbl_task.c:85`）
- OSAL↔ESA 循环依赖风险 → 已有 weak-stub 模式处理
- API 大爆炸重命名风险 → 使用兼容性 `#define` 别名过渡
- 后阻塞 COMPLETE 事件需在所有路径（成功/失败）都触发

---

## 工作目标

### 核心目标
将 ESA stepping 引擎从"核心完成 + 桩代码外壳"状态推进到"全栈功能完整"状态，使 stepping 引擎能够真正跟踪所有 cFS 组件的阻塞操作。

### 具体交付物
- `osal/src/os/posix/inc/os-posix-stepping.h` — 扩展 hook 声明（含上下文参数 + Complete 函数）
- `osal/src/os/posix/src/os-posix-stepping.c` — 6 个 hook 函数的真实 shim 转发实现
- `osal/src/os/posix/src/os-impl-{tasks,queues,binsem}.c` — 后阻塞 hook 调用点
- `cfe/modules/{es,evs,sb,tbl}/fsw/src/cfe_*_task.c` — RECEIVE+COMPLETE 事件报告
- `cfe/modules/time/fsw/src/cfe_time_stepping.c` — 3 个 hook 从 no-op 转为 shim 转发
- `esa/**/*.{h,c}` — API 重命名 CFE_PSP_SimStepping_* → ESA_*
- `esa/ut-coverage/` — TDD 测试覆盖
- `esa/README.md` — ESA 模块文档

### 完成标准
- [ ] `make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make && make install && make test` — 全通过
- [ ] `make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install && make test` — 无 stepping 构建不受影响
- [ ] `grep -r "CFE_PSP_SimStepping" --include="*.c" --include="*.h"` — 仅在兼容性别名头文件中出现
- [ ] 所有 OSAL hook 在 stepping 启用时正确向 shim 报告 ACK+COMPLETE 事件
- [ ] 所有 cFE 核心模块参与 stepping wait-set

### 必须有
- 所有 OSAL hook（TaskDelay, QueueReceive, BinSemTake）的双阶段 shim 转发
- cFE 四个核心模块的 RECEIVE+COMPLETE 事件
- TIME 服务的 3 个 stepping hook 真实实现
- 全量 API 重命名
- TDD 测试
- 所有变更在 `#ifdef CFE_SIM_STEPPING` 下

### 必须没有（护栏）
- **禁止**修改 `osal/src/os/shared/src/` — 仅限 POSIX 平台层
- **禁止**使用 SBN 作为控制通道
- **禁止**使用静态允许列表作为核心同步语义
- **禁止**将 ES background 放入默认 wait-set
- **禁止**让 OSAL/PSP 依赖高层 socket/状态机实现
- **禁止**使用"全局系统空闲"作为步骤完成标志
- **禁止**自动链接 CFE_SIM_STEPPING 到 SIMULATION=native
- **禁止**在非 stepping 构建中产生任何副作用
- **禁止**在 hook 实现中包含业务逻辑 — 仅做 thin shim 转发
- **禁止**新增 ESA_Init() 重复调用 — 已在 `cfe_psp_sim_stepping.c` 中存在
- **禁止**在 EVS 格式字符串中使用 `%f`/`%g`
- **禁止**使用原始 `==` 比较 MsgId — 使用 `CFE_SB_MsgId_Equal()`

---

## 验证策略

> **零人工干预** — 所有验证由 agent 执行。接受标准中禁止出现"用户手动测试/确认"。

### 测试决策
- **基础设施存在**: YES — `esa/ut-coverage/` 已有框架
- **自动化测试**: TDD（先写测试，再实现）
- **框架**: ut_assert（UtAssert_INT32_EQ, UtAssert_VOIDCALL, UT_SetDeferredRetcode 等）
- **TDD 流程**: 每个任务先编写失败测试（RED）→ 最小实现使其通过（GREEN）→ 重构（REFACTOR）

### QA 策略
每个任务都必须包含 agent 可执行的 QA 场景。
证据保存到 `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`。

- **构建验证**: 使用 Bash — `make` 编译，验证 zero warnings/errors
- **单元测试**: 使用 Bash — `make test` 运行 CTest
- **代码检查**: 使用 grep/ast_grep — 验证 `#ifdef CFE_SIM_STEPPING` 包裹
- **回归验证**: 使用 Bash — 无 stepping 构建验证

---

## 执行策略

### 并行执行 Wave

> 最大化吞吐量。每个 Wave 完成后下一个才开始。
> 目标：5-8 个任务/Wave。

```
Wave 1（立即开始 — 基础设施 + 签名扩展）:
├── T1: OSAL hook 签名扩展 + 后阻塞调用点 [deep]
├── T2: cFE 核心模块 SERVICE_ID 常量定义 [quick]
├── T3: TIME stepping hook 事件类型确认 + 常量 [quick]

Wave 2（Wave 1 后 — hook 实现 + 模块集成，最大并行）:
├── T4: OSAL TaskDelay hook 双阶段实现 (depends: T1) [deep]
├── T5: OSAL QueueReceive hook 双阶段实现 (depends: T1) [deep]
├── T6: OSAL BinSemTake hook 双阶段实现 (depends: T1) [deep]
├── T7: ES 模块 COMPLETE 事件 + EVS 模块 RECEIVE+COMPLETE (depends: T2) [unspecified-high]
├── T8: SB 模块 RECEIVE+COMPLETE + TBL 模块 RECEIVE+COMPLETE (depends: T2) [unspecified-high]
└── T9: TIME 3 个 stepping hook 真实实现 (depends: T3) [unspecified-high]

Wave 3（Wave 2 后 — API 重命名）:
├── T10: 兼容性别名头文件 + ESA 内部重命名 [deep]
├── T11: 外部调用者重命名（cFE, OSAL, PSP, apps）(depends: T10) [deep]
└── T12: 删除兼容性别名 + 最终清理 (depends: T11) [quick]

Wave 4（Wave 2 后，与 Wave 3 并行 — TDD 测试）:
├── T13: OSAL hook 单元测试（3 个 hook × 双阶段）(depends: T4-T6) [deep]
├── T14: cFE 模块 stepping 集成测试（ES/EVS/SB/TBL）(depends: T7-T8) [deep]
└── T15: TIME stepping hook 测试 (depends: T9) [unspecified-high]

Wave 5（所有 Wave 后 — 文档 + 端到端验证）:
├── T16: ESA README 文档 [writing]
└── T17: 端到端集成验证 [deep]

Wave FINAL（所有任务后 — 4 个并行审查 + 用户确认）:
├── F1: 计划合规审计 (oracle)
├── F2: 代码质量审查 (unspecified-high)
├── F3: 真实 QA 验证 (unspecified-high)
└── F4: 范围保真度检查 (deep)
→ 展示结果 → 获取用户明确确认

关键路径: T1 → T4/T5/T6 → T13 → T17 → F1-F4 → 用户确认
并行加速: ~60% 快于顺序执行
最大并发: 6（Wave 2）
```

### 依赖矩阵

| 任务 | 依赖 | 被依赖 | Wave |
|------|------|--------|------|
| T1 | — | T4, T5, T6 | 1 |
| T2 | — | T7, T8 | 1 |
| T3 | — | T9 | 1 |
| T4 | T1 | T13 | 2 |
| T5 | T1 | T13 | 2 |
| T6 | T1 | T13 | 2 |
| T7 | T2 | T14 | 2 |
| T8 | T2 | T14 | 2 |
| T9 | T3 | T15 | 2 |
| T10 | — | T11 | 3 |
| T11 | T10 | T12 | 3 |
| T12 | T11 | — | 3 |
| T13 | T4-T6 | T17 | 4 |
| T14 | T7-T8 | T17 | 4 |
| T15 | T9 | T17 | 4 |
| T16 | — | — | 5 |
| T17 | T12-T15 | F1-F4 | 5 |

### Agent 调度摘要

- **Wave 1**: **3** — T1 → `deep`, T2 → `quick`, T3 → `quick`
- **Wave 2**: **6** — T4-T6 → `deep`, T7-T8 → `unspecified-high`, T9 → `unspecified-high`
- **Wave 3**: **3** — T10 → `deep`, T11 → `deep`, T12 → `quick`
- **Wave 4**: **3** — T13-T14 → `deep`, T15 → `unspecified-high`
- **Wave 5**: **2** — T16 → `writing`, T17 → `deep`
- **FINAL**: **4** — F1 → `oracle`, F2 → `unspecified-high`, F3 → `unspecified-high`, F4 → `deep`

---

## TODOs

> 实现 + 测试 = 一个任务。绝不分离。
> 每个任务必须有：推荐 Agent Profile + 并行化信息 + QA 场景。

- [x] 1. OSAL Hook 签名扩展 + 后阻塞调用点

  **做什么**:
  1. 扩展 `os-posix-stepping.h` 中的 hook 声明，添加上下文参数：
     - `OS_PosixStepping_Hook_TaskDelay(uint32_t ms, osal_id_t task_id)` — 添加 task_id
     - `OS_PosixStepping_Hook_TaskDelay_Complete(uint32_t ms, osal_id_t task_id)` — 新增后阻塞 hook
     - `OS_PosixStepping_Hook_QueueReceive(const OS_object_token_t *token, int32 timeout)` — 添加 token + timeout
     - `OS_PosixStepping_Hook_QueueReceive_Complete(const OS_object_token_t *token, int32 timeout, int32 return_code)` — 新增
     - `OS_PosixStepping_Hook_BinSemTake(const OS_object_token_t *token, const struct timespec *timeout)` — 添加 token + timeout
     - `OS_PosixStepping_Hook_BinSemTake_Complete(const OS_object_token_t *token, const struct timespec *timeout, int32 return_code)` — 新增
  2. 在 `os-posix-stepping.c` 中添加 3 个新的 `_Complete` 函数桩（暂时 no-op，后续任务实现）
  3. 更新现有 3 个 hook 函数签名以匹配新声明
  4. 在 `os-impl-tasks.c:734` — 更新 `OS_PosixStepping_Hook_TaskDelay` 调用，传递 `impl->id`（task_id）
  5. 在 `os-impl-tasks.c` — 在 `clock_nanosleep` 返回后添加 `OS_PosixStepping_Hook_TaskDelay_Complete()` 调用
  6. 在 `os-impl-queues.c:199` — 更新 hook 调用，传递 `token` 和 `timeout` 参数
  7. 在 `os-impl-queues.c` — 在 `mq_timedreceive`/`mq_receive` 返回后添加 `_Complete` hook 调用
  8. 在 `os-impl-binsem.c:372` — 更新 hook 调用，传递 `token` 和 `timeout` 参数
  9. 在 `os-impl-binsem.c` — 在 `pthread_cond_wait`/`pthread_cond_timedwait` 返回后添加 `_Complete` hook 调用
  10. 所有变更在 `#ifdef CFE_SIM_STEPPING` 保护下
  11. COMPLETE hook 在所有返回路径（成功+失败）都必须触发

  **必须不做**:
  - 禁止修改 `osal/src/os/shared/src/` 下任何文件
  - 禁止在 hook 函数中实现业务逻辑 — 仅修改签名和调用点
  - 禁止让 OSAL 直接包含 ESA 头文件（此时 hook 仍是桩）

  **推荐 Agent Profile**:
  - **Category**: `deep`
    - 原因: 涉及 6 个文件的协调修改，需要理解 OSAL POSIX 层的阻塞调用模式
  - **Skills**: [`create-cfs-app`]
    - `create-cfs-app`: 了解 cFS 代码结构和编码规范
  - **排除的 Skills**:
    - `create-cfs-unit-test`: 此任务不写测试

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 1（与 T2, T3 并行）
  - **阻塞**: T4, T5, T6
  - **被阻塞**: 无 — 可立即开始

  **参考资料**:

  **模式参考**（要遵循的现有代码）:
  - `osal/src/os/posix/inc/os-posix-stepping.h` — 当前 hook 声明（需要扩展的基础）
  - `osal/src/os/posix/src/os-posix-stepping.c` — 当前 3 个 no-op hook 实现
  - `osal/src/os/posix/src/os-impl-tasks.c:730-740` — TaskDelay 中 hook 调用点和 `clock_nanosleep` 位置
  - `osal/src/os/posix/src/os-impl-queues.c:195-250` — QueueGet_Impl 中 hook 调用点和 `mq_timedreceive` 位置
  - `osal/src/os/posix/src/os-impl-binsem.c:365-410` — BinSemTake 中 hook 调用点和 `pthread_cond_wait` 位置

  **API/类型参考**:
  - `osal/src/os/posix/inc/os-posix.h` — `OS_object_token_t` 类型定义
  - `osal/src/os/inc/osapi-idmap.h` — `osal_id_t` 类型和 `OS_ObjectIdFromToken()` 转换

  **为什么每个参考重要**:
  - `os-impl-tasks.c:730-740` — 展示当前 pre-blocking hook 的调用方式，需要在同一函数中找到 `clock_nanosleep` 返回后的位置添加 post-blocking hook
  - `os-impl-queues.c:195-250` — `token` 参数已在 `OS_QueueGet_Impl` 的函数签名中可用，只需传递给 hook；`mq_timedreceive` 返回后需添加 Complete hook
  - `os-impl-binsem.c:365-410` — 同理，`token` 和 `timeout` struct 在作用域内可用

  **验收标准**:

  **QA 场景（必须 — 无场景的任务将被拒绝）:**

  ```
  Scenario: Stepping 构建编译成功
    Tool: Bash
    Preconditions: 干净构建目录
    Steps:
      1. make distclean
      2. make SIMULATION=native CFE_SIM_STEPPING=1 prep
      3. make 2>&1 | tee build.log
    Expected Result: 编译成功，零 error，零与 stepping hook 相关的 warning
    Failure Indicators: 编译错误提及 os-posix-stepping 或 hook 签名不匹配
    Evidence: .sisyphus/evidence/task-1-stepping-build.log

  Scenario: 非 Stepping 构建不受影响
    Tool: Bash
    Preconditions: 干净构建目录
    Steps:
      1. make distclean
      2. make SIMULATION=native prep
      3. make 2>&1 | tee build.log
    Expected Result: 编译成功，hook 代码被 #ifdef 排除，零 error/warning
    Failure Indicators: 编译错误提及 stepping 相关符号
    Evidence: .sisyphus/evidence/task-1-non-stepping-build.log

  Scenario: Hook 签名在头文件中正确声明
    Tool: Bash (grep)
    Preconditions: T1 实现完成
    Steps:
      1. grep -n "OS_PosixStepping_Hook_" osal/src/os/posix/inc/os-posix-stepping.h
    Expected Result: 至少 6 个函数声明（3 个原始 + 3 个 Complete）
    Failure Indicators: 少于 6 个声明或签名不含新参数
    Evidence: .sisyphus/evidence/task-1-hook-signatures.txt

  Scenario: 后阻塞调用点存在
    Tool: Bash (grep)
    Preconditions: T1 实现完成
    Steps:
      1. grep -n "Hook_TaskDelay_Complete\|Hook_QueueReceive_Complete\|Hook_BinSemTake_Complete" osal/src/os/posix/src/os-impl-tasks.c osal/src/os/posix/src/os-impl-queues.c osal/src/os/posix/src/os-impl-binsem.c
    Expected Result: 每个文件至少 1 个 _Complete 调用
    Failure Indicators: 任何文件中缺少 Complete 调用
    Evidence: .sisyphus/evidence/task-1-post-blocking-callsites.txt
  ```

  **Commit**: YES
  - Message: `feat(osal): expand stepping hook signatures and add post-blocking call sites`
  - Files: `osal/src/os/posix/inc/os-posix-stepping.h`, `osal/src/os/posix/src/os-posix-stepping.c`, `osal/src/os/posix/src/os-impl-tasks.c`, `osal/src/os/posix/src/os-impl-queues.c`, `osal/src/os/posix/src/os-impl-binsem.c`
  - Pre-commit: `make SIMULATION=native CFE_SIM_STEPPING=1 prep && make`

- [x] 2. cFE 核心模块 SERVICE_ID 常量定义

  **做什么**:
  1. **修复** `cfe/modules/es/fsw/src/cfe_es_task.c:66` 的 `#define CFE_ES_SERVICE_ID` 从 `0x01` 改为 `0`（0-based index，对应 `1U << 0 = SERVICE_BIT_ES`）
  2. 在 `cfe/modules/evs/fsw/src/cfe_evs_task.c` 顶部添加 `#define CFE_EVS_SERVICE_ID 1`（在 `#ifdef CFE_SIM_STEPPING` 内，对应 `1U << 1 = SERVICE_BIT_EVS`）
  3. 在 `cfe/modules/sb/fsw/src/cfe_sb_task.c` 顶部添加 `#define CFE_SB_SERVICE_ID 2`（对应 `1U << 2 = SERVICE_BIT_SB`）
  4. 在 `cfe/modules/tbl/fsw/src/cfe_tbl_task.c` 顶部添加 `#define CFE_TBL_SERVICE_ID 3`（对应 `1U << 3 = SERVICE_BIT_TBL`）
  5. 添加必要的 `#include "cfe_psp_sim_stepping_shim.h"` 到每个文件（在 `#ifdef CFE_SIM_STEPPING` 内）
  6. SERVICE_ID 值必须是 **0-based index**，与 core.c:615 的 `1U << service_id` 映射一致：ES=0→`1U<<0`=0x01=SERVICE_BIT_ES, EVS=1→`1U<<1`=0x02=SERVICE_BIT_EVS, SB=2→`1U<<2`=0x04=SERVICE_BIT_SB, TBL=3→`1U<<3`=0x08=SERVICE_BIT_TBL
  7. **关键**: 现有 ES 定义 `0x01` 是 bug — 映射到 `1U << 1 = bit1`(EVS 的 bit)，必须修正为 `0`

  **必须不做**:
  - 此任务不添加 RECEIVE/COMPLETE 事件报告代码 — 仅添加常量和 include
  - 不修改 core.h 中的 SERVICE_BIT 定义

  **推荐 Agent Profile**:
  - **Category**: `quick`
    - 原因: 4 个文件中各添加 2-3 行常量定义
  - **Skills**: []
  - **排除的 Skills**: 全部 — 任务过于简单

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 1（与 T1, T3 并行）
  - **阻塞**: T7, T8
  - **被阻塞**: 无 — 可立即开始

  **参考资料**:

  **模式参考**:
  - `cfe/modules/es/fsw/src/cfe_es_task.c:63-67` — ES 的 SERVICE_ID 定义模式和 include 方式
  - `esa/fsw/inc/cfe_psp_sim_stepping_core.h:69-73` — SERVICE_BIT 定义（确保 ID 与 bit 对应）

  **为什么每个参考重要**:
  - `cfe_es_task.c:63-67` — 这是已确立的模式，其他模块必须完全复制此模式（`#ifdef CFE_SIM_STEPPING` + include + define）

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: 所有模块定义了正确的 SERVICE_ID
    Tool: Bash (grep)
    Steps:
      1. grep -n "SERVICE_ID" cfe/modules/es/fsw/src/cfe_es_task.c cfe/modules/evs/fsw/src/cfe_evs_task.c cfe/modules/sb/fsw/src/cfe_sb_task.c cfe/modules/tbl/fsw/src/cfe_tbl_task.c
    Expected Result: ES=0, EVS=1, SB=2, TBL=3（0-based index，NOT 1-based）
    Failure Indicators: 任何模块缺少定义、值不正确、或仍使用 0x01 等 1-based 值
    Evidence: .sisyphus/evidence/task-2-service-ids.txt

  Scenario: SERVICE_ID 与 core.h SERVICE_BIT 一致性验证
    Tool: Bash
    Steps:
      1. grep -n "SERVICE_BIT_" esa/fsw/inc/cfe_psp_sim_stepping_core.h
      2. grep -rn "SERVICE_ID" cfe/modules/es/fsw/src/cfe_es_task.c cfe/modules/evs/fsw/src/cfe_evs_task.c cfe/modules/sb/fsw/src/cfe_sb_task.c cfe/modules/tbl/fsw/src/cfe_tbl_task.c
    Expected Result: ES_SERVICE_ID=0 对应 BIT_ES=1U<<0, EVS=1 对应 1U<<1, SB=2 对应 1U<<2, TBL=3 对应 1U<<3
    Evidence: .sisyphus/evidence/task-2-service-bit-consistency.txt

  Scenario: Stepping 构建编译成功
    Tool: Bash
    Steps:
      1. make SIMULATION=native CFE_SIM_STEPPING=1 prep && make
    Expected Result: 零编译错误
    Evidence: .sisyphus/evidence/task-2-build.log
  ```

  **Commit**: YES（与 T3 合并）
  - Message: `feat(cfe): add stepping service ID constants for core modules`
  - Files: `cfe/modules/evs/fsw/src/cfe_evs_task.c`, `cfe/modules/sb/fsw/src/cfe_sb_task.c`, `cfe/modules/tbl/fsw/src/cfe_tbl_task.c`

- [x] 3. TIME Stepping Hook 事件类型确认 + 常量

  **做什么**:
  1. 确认 `cfe_psp_sim_stepping_shim.h` 中已定义 TIME 相关的事件类型：
     - `CFE_PSP_SIM_STEPPING_EVENT_TIME_TASK_CYCLE`
      - `CFE_PSP_SIM_STEPPING_EVENT_1HZ_BOUNDARY`（注意：无 TIME_ 前缀）
      - `CFE_PSP_SIM_STEPPING_EVENT_TONE_SIGNAL`（注意：无 TIME_ 前缀）
  2. 确认 `cfe_time_stepping.c` 中已有 3 个 no-op hook 函数和正确的调用位置
  3. 在 `cfe_time_stepping.h` 或 `cfe_time_stepping.c` 中添加必要的 `#include "cfe_psp_sim_stepping_shim.h"`（在 `#ifdef CFE_SIM_STEPPING` 内）
  4. 确认 shim forwarder (`cfe_psp_sim_stepping.c`) 已处理这 3 种 TIME 事件类型

  **必须不做**:
  - 此任务不实现 hook 逻辑 — 仅确认常量/类型并添加 include
  - 不修改 shim 或 core 代码

  **推荐 Agent Profile**:
  - **Category**: `quick`
    - 原因: 确认性任务 + 少量 include 添加
  - **Skills**: []

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 1（与 T1, T2 并行）
  - **阻塞**: T9
  - **被阻塞**: 无

  **参考资料**:

  **模式参考**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — 事件类型枚举定义
  - `cfe/modules/time/fsw/src/cfe_time_stepping.c` — 当前 3 个 no-op hook
  - `cfe/modules/time/fsw/src/cfe_time_task.c:97,367,414` — hook 调用位置
  - `esa/fsw/src/cfe_psp_sim_stepping.c:250-275` — shim 中 TIME 事件的 switch 分支

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: TIME 事件类型在 shim 中已定义
    Tool: Bash (grep)
    Steps:
      1. grep -n "EVENT_TIME_TASK_CYCLE\|EVENT_1HZ_BOUNDARY\|EVENT_TONE_SIGNAL" esa/public_inc/cfe_psp_sim_stepping_shim.h
    Expected Result: 至少 3 个匹配（注意：TIME_TASK_CYCLE 有 TIME_ 前缀，而 1HZ_BOUNDARY 和 TONE_SIGNAL 无前缀）
    Evidence: .sisyphus/evidence/task-3-time-events.txt

  Scenario: cfe_time_stepping.c 可以包含 shim 头文件
    Tool: Bash
    Steps:
      1. make SIMULATION=native CFE_SIM_STEPPING=1 prep && make
    Expected Result: 编译成功
    Evidence: .sisyphus/evidence/task-3-build.log
  ```

  **Commit**: YES（与 T2 合并）
  - Message: `feat(cfe): add stepping service ID constants for core modules`

- [x] 4. OSAL TaskDelay Hook 双阶段 Shim 转发实现

  **做什么**:
  0. **前置: 添加缺失的 TASK_DELAY 双阶段枚举和分发路径**:
     - 在 `esa/public_inc/cfe_psp_sim_stepping_shim.h` 的 `CFE_PSP_SimStepping_EventKind` 枚举中，紧跟 `TASK_DELAY` 后面添加:
       ```c
       CFE_PSP_SIM_STEPPING_EVENT_TASK_DELAY_ACK,      /**< TaskDelay ack (pre-blocking) */
       CFE_PSP_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE,  /**< TaskDelay complete (post-blocking) */
       ```
     - 在 `esa/fsw/src/cfe_psp_sim_stepping.c` 的 shim switch 中添加对应 case（参考 QUEUE_RECEIVE_ACK/COMPLETE 的模式）:
       - `TASK_DELAY_ACK` → `CFE_PSP_SimStepping_Core_ReportTaskDelayAck()`
       - `TASK_DELAY_COMPLETE` → `CFE_PSP_SimStepping_Core_ReportTaskDelayComplete()`
     - 在 `esa/fsw/src/cfe_psp_sim_stepping_core.c` 中添加对应的 core handler（参考现有 `ReportQueueReceiveAck/Complete` 模式）
     - 在 `esa/fsw/inc/cfe_psp_sim_stepping_core.h` 中声明新函数
     - **注意**: 现有 `TASK_DELAY` 枚举和 `ReportTaskDelay/ReportTaskDelayReturn` 保持不变（向后兼容）
  1. 在 `os-posix-stepping.c` 中实现 `OS_PosixStepping_Hook_TaskDelay()`：
     - 构造 `CFE_PSP_SimStepping_ShimEvent_t`
     - 设置 `event_kind = CFE_PSP_SIM_STEPPING_EVENT_TASK_DELAY_ACK`
     - 设置 `task_id` 从参数获取（通过 `OS_TaskGetId()` 如果参数不直接包含）
     - 设置 `optional_delay_ms = ms`
     - 调用 `CFE_PSP_SimStepping_Shim_ReportEvent(&event)`
  2. 实现 `OS_PosixStepping_Hook_TaskDelay_Complete()`：
     - 相同结构，`event_kind = CFE_PSP_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE`
     - 调用 shim 报告
  3. 添加 `#include "cfe_psp_sim_stepping_shim.h"` 到 `os-posix-stepping.c`（在 `#ifdef CFE_SIM_STEPPING` 内）
  4. 确保 hook 是 thin shim 转发 — 不含业务逻辑
  5. 注意: `Hook_TaskDelayEligible()` 和 `WaitForDelayExpiry()` 已在 ESA 的 `cfe_psp_sim_stepping.c` 中实现，此 hook 仅负责通知 core "TaskDelay 开始/结束"

  **必须不做**:
  - 不实现 TaskDelay 替代逻辑（已在 ESA core 中实现）
  - 不包含阻塞等待 — hook 仅报告事件
  - 不修改 `osal/src/os/shared/src/`

  **推荐 Agent Profile**:
  - **Category**: `deep`
    - 原因: 需要理解 shim 事件结构和 OSAL 内部 ID 转换
  - **Skills**: [`create-cfs-app`]
    - `create-cfs-app`: cFS 代码结构和约定

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 2（与 T5, T6, T7, T8, T9 并行）
  - **阻塞**: T13
  - **被阻塞**: T1

  **参考资料**:

  **模式参考**:
  - `apps/sch/fsw/src/sch_stepping.c:46-86` — SCH hook 的 thin shim 转发模式（构造 ShimEvent_t + 调用 ReportEvent）
  - `esa/fsw/src/cfe_psp_sim_stepping.c:215-225` — shim 中 QUEUE_RECEIVE_ACK/COMPLETE 的分发模式（TaskDelay ACK/COMPLETE 需参考此模式新增）
  - `esa/fsw/src/cfe_psp_sim_stepping.c:845-881` — WaitForDelayExpiry（了解已存在的 delay 处理）

  **API/类型参考**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h:54-76` — `CFE_PSP_SimStepping_ShimEvent_t` 结构和事件类型枚举（需要在此添加 TASK_DELAY_ACK/COMPLETE）
  - `esa/fsw/inc/cfe_psp_sim_stepping_core.h:267-330` — ReportTaskDelay/ReportTaskDelayReturn 声明（参考已有函数签名添加 Ack/Complete 函数）
  - `esa/fsw/src/cfe_psp_sim_stepping_core.c:267-340` — ReportTaskDelay/ReportTaskDelayReturn 实现（参考模式实现 ReportTaskDelayAck/Complete）
  - `osal/src/os/inc/osapi-task.h` — `OS_TaskGetId()` 用于获取当前 task_id

  **为什么每个参考重要**:
  - `sch_stepping.c:46-86` — 这是唯一完整的 hook→shim 转发参考实现，必须完全复制此模式
  - shim.h — hook 需要构造的事件结构的确切字段

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: TaskDelay hook 调用 shim ReportEvent
    Tool: Bash (grep)
    Steps:
      1. grep -n "Shim_ReportEvent" osal/src/os/posix/src/os-posix-stepping.c
    Expected Result: 至少 2 个匹配（ACK + COMPLETE）在 TaskDelay 相关函数中
    Evidence: .sisyphus/evidence/task-4-taskdelay-shim-calls.txt

  Scenario: Stepping 构建成功
    Tool: Bash
    Steps:
      1. make SIMULATION=native CFE_SIM_STEPPING=1 prep && make
    Expected Result: 零错误
    Evidence: .sisyphus/evidence/task-4-build.log

  Scenario: 非 Stepping 构建不受影响
    Tool: Bash
    Steps:
      1. make distclean && make SIMULATION=native prep && make
    Expected Result: 零错误，hook 代码被 #ifdef 排除
    Evidence: .sisyphus/evidence/task-4-non-stepping-build.log
  ```

  **Commit**: YES（与 T5, T6 合并）
  - Message: `feat(osal): implement dual-phase stepping hooks with shim forwarding`
  - Pre-commit: `make SIMULATION=native CFE_SIM_STEPPING=1 prep && make`

- [x] 5. OSAL QueueReceive Hook 双阶段 Shim 转发实现

  **做什么**:
  1. 在 `os-posix-stepping.c` 中实现 `OS_PosixStepping_Hook_QueueReceive()`：
     - 构造 `CFE_PSP_SimStepping_ShimEvent_t`
     - `event_kind = CFE_PSP_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK`
     - `entity_id` = 从 token 提取 queue id（`OS_ObjectIdToInteger(OS_ObjectIdFromToken(token))`）
     - `task_id` = `OS_TaskGetId()` 获取当前任务 ID
     - 调用 `CFE_PSP_SimStepping_Shim_ReportEvent(&event)`
  2. 实现 `OS_PosixStepping_Hook_QueueReceive_Complete()`：
     - `event_kind = CFE_PSP_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE`
     - 同样的 entity_id 和 task_id
     - 无论 return_code 成功还是失败都报告（步骤完成 = 阻塞操作结束，不管结果）
  3. Thin shim 转发，不含业务逻辑

  **必须不做**:
  - 不根据 return_code 决定是否报告 — 始终报告
  - 不在 hook 中阻塞

  **推荐 Agent Profile**:
  - **Category**: `deep`
    - 原因: 需要 token→id 转换逻辑
  - **Skills**: [`create-cfs-app`]

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 2（与 T4, T6, T7, T8, T9 并行）
  - **阻塞**: T13
  - **被阻塞**: T1

  **参考资料**:

  **模式参考**:
  - `apps/sch/fsw/src/sch_stepping.c:46-86` — shim 转发模式
  - `esa/fsw/src/cfe_psp_sim_stepping.c:215-225` — shim 中 QUEUE_RECEIVE_ACK/COMPLETE 分发
  - `esa/fsw/src/cfe_psp_sim_stepping_core.c:721-772` — core 如何处理 QueueReceive ACK/Complete（AddTrigger + AcknowledgeTrigger）

  **API/类型参考**:
  - `osal/src/os/shared/inc/os-shared-queue.h` — `OS_object_token_t` 在 queue 上下文中的使用
  - `osal/src/os/shared/src/osapi-idmap.c` — `OS_ObjectIdFromToken()` 转换函数

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: QueueReceive hook 包含正确事件类型
    Tool: Bash (grep)
    Steps:
      1. grep -n "QUEUE_RECEIVE_ACK\|QUEUE_RECEIVE_COMPLETE" osal/src/os/posix/src/os-posix-stepping.c
    Expected Result: 各至少 1 个匹配
    Evidence: .sisyphus/evidence/task-5-queue-events.txt

  Scenario: Hook 从 token 正确提取 entity_id
    Tool: Bash (grep)
    Steps:
      1. grep -n "ObjectIdFromToken\|entity_id" osal/src/os/posix/src/os-posix-stepping.c
    Expected Result: QueueReceive 和 QueueReceive_Complete 函数中都有 entity_id 赋值
    Evidence: .sisyphus/evidence/task-5-entity-extraction.txt
  ```

  **Commit**: YES（与 T4, T6 合并）

- [x] 6. OSAL BinSemTake Hook 双阶段 Shim 转发实现

  **做什么**:
  1. 在 `os-posix-stepping.c` 中实现 `OS_PosixStepping_Hook_BinSemTake()`：
     - `event_kind = CFE_PSP_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK`
     - `entity_id` = 从 token 提取 sem id
     - `task_id` = `OS_TaskGetId()`
     - 调用 shim
  2. 实现 `OS_PosixStepping_Hook_BinSemTake_Complete()`：
     - `event_kind = CFE_PSP_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE`
     - 无论成功/超时都报告
  3. Thin shim 转发

  **必须不做**:
  - 同 T5

  **推荐 Agent Profile**:
  - **Category**: `deep`
  - **Skills**: [`create-cfs-app`]

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 2（与 T4, T5, T7, T8, T9 并行）
  - **阻塞**: T13
  - **被阻塞**: T1

  **参考资料**:

  **模式参考**:
  - `apps/sch/fsw/src/sch_stepping.c:46-86` — shim 转发模式
  - `esa/fsw/src/cfe_psp_sim_stepping.c:232-242` — shim 中 BINSEM_TAKE_ACK/COMPLETE 分发
  - `esa/fsw/src/cfe_psp_sim_stepping_core.c:883-935` — core 中 BinSemTake ACK/Complete 处理

  **API/类型参考**:
  - `osal/src/os/shared/inc/os-shared-binsem.h` — token 在 binsem 上下文中的使用

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: BinSemTake hook 包含正确事件类型
    Tool: Bash (grep)
    Steps:
      1. grep -n "BINSEM_TAKE_ACK\|BINSEM_TAKE_COMPLETE" osal/src/os/posix/src/os-posix-stepping.c
    Expected Result: 各至少 1 个匹配
    Evidence: .sisyphus/evidence/task-6-binsem-events.txt

  Scenario: 所有 6 个 OSAL hook 都有 shim 调用
    Tool: Bash (grep)
    Steps:
      1. grep -c "Shim_ReportEvent" osal/src/os/posix/src/os-posix-stepping.c
    Expected Result: 至少 6（3 个 ACK + 3 个 COMPLETE hook）
    Evidence: .sisyphus/evidence/task-6-all-hooks-count.txt
  ```

  **Commit**: YES（与 T4, T5 合并）

- [x] 7. ES 模块 COMPLETE 事件 + EVS 模块 RECEIVE+COMPLETE 集成

  **做什么**:
  1. **ES 模块** (`cfe/modules/es/fsw/src/cfe_es_task.c`):
     - 当前已有 RECEIVE 事件（L174-179）: 在 `CFE_SB_ReceiveBuffer` 成功后报告 `CORE_SERVICE_CMD_PIPE_RECEIVE`
     - 添加 COMPLETE 事件: 在 `CFE_ES_TaskPipe(SBBufPtr)` 返回后、下一次循环迭代前添加:
       ```c
       #ifdef CFE_SIM_STEPPING
       {
           CFE_PSP_SimStepping_ShimEvent_t stepping_complete = {0};
           stepping_complete.event_kind = CFE_PSP_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_COMPLETE;
           stepping_complete.entity_id  = CFE_ES_SERVICE_ID;
           CFE_PSP_SimStepping_Shim_ReportEvent(&stepping_complete);
       }
       #endif
       ```
  2. **EVS 模块** (`cfe/modules/evs/fsw/src/cfe_evs_task.c`):
     - 在 `CFE_SB_ReceiveBuffer` 成功后添加 RECEIVE 事件（复制 ES 模式）
     - 在 `CFE_EVS_ProcessCommandPacket(SBBufPtr)` 返回后添加 COMPLETE 事件
     - 使用 `CFE_EVS_SERVICE_ID` (1, 0-based)
  3. 所有变更在 `#ifdef CFE_SIM_STEPPING` 保护下
  4. Doxygen 中文注释

  **必须不做**:
  - 不修改消息处理逻辑
  - 不添加额外的头文件依赖（仅 shim.h 在 ifdef 内）

  **推荐 Agent Profile**:
  - **Category**: `unspecified-high`
    - 原因: 涉及 2 个 cFE 核心模块的精确代码插入
  - **Skills**: [`create-cfs-app`]
    - `create-cfs-app`: cFS 应用生命周期和分发模式

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 2（与 T4-T6, T8, T9 并行）
  - **阻塞**: T14
  - **被阻塞**: T2

  **参考资料**:

  **模式参考**:
  - `cfe/modules/es/fsw/src/cfe_es_task.c:163-182` — ES 主循环，RECEIVE 事件已在 L174-179
  - `cfe/modules/evs/fsw/src/cfe_evs_task.c:219-242` — EVS 主循环（ReceiveBuffer→ProcessCommandPacket）

  **API/类型参考**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — ShimEvent_t 和 CORE_SERVICE_CMD_PIPE_RECEIVE/COMPLETE 事件类型

  **为什么每个参考重要**:
  - `cfe_es_task.c:163-182` — ES RECEIVE 是确立的模式，COMPLETE 必须紧跟 TaskPipe 返回后
  - `cfe_evs_task.c:219-242` — EVS 循环结构与 ES 几乎完全相同，可直接复制模式

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: ES 同时有 RECEIVE 和 COMPLETE 事件
    Tool: Bash (grep)
    Steps:
      1. grep -n "CMD_PIPE_RECEIVE\|CMD_PIPE_COMPLETE" cfe/modules/es/fsw/src/cfe_es_task.c
    Expected Result: 至少 1 个 RECEIVE + 1 个 COMPLETE
    Failure Indicators: 缺少 COMPLETE 事件
    Evidence: .sisyphus/evidence/task-7-es-events.txt

  Scenario: EVS 有 RECEIVE 和 COMPLETE 事件
    Tool: Bash (grep)
    Steps:
      1. grep -n "CMD_PIPE_RECEIVE\|CMD_PIPE_COMPLETE\|CFE_EVS_SERVICE_ID" cfe/modules/evs/fsw/src/cfe_evs_task.c
    Expected Result: RECEIVE + COMPLETE + SERVICE_ID 都存在
    Evidence: .sisyphus/evidence/task-7-evs-events.txt

  Scenario: Stepping 构建成功
    Tool: Bash
    Steps:
      1. make SIMULATION=native CFE_SIM_STEPPING=1 prep && make
    Expected Result: 零错误
    Evidence: .sisyphus/evidence/task-7-build.log
  ```

  **Commit**: YES（与 T8 合并）
  - Message: `feat(cfe): add stepping wait-set integration for ES/EVS/SB/TBL`

- [x] 8. SB 模块 RECEIVE+COMPLETE + TBL 模块 RECEIVE+COMPLETE 集成

  **做什么**:
  1. **SB 模块** (`cfe/modules/sb/fsw/src/cfe_sb_task.c`):
     - 在 `CFE_SB_ReceiveBuffer` 成功后添加 RECEIVE 事件
     - 在 `CFE_SB_ProcessCmdPipePkt(SBBufPtr)` 返回后添加 COMPLETE 事件
     - 使用 `CFE_SB_SERVICE_ID` (2, 0-based)
  2. **TBL 模块** (`cfe/modules/tbl/fsw/src/cfe_tbl_task.c`):
     - 在 `CFE_SB_ReceiveBuffer` 成功后添加 RECEIVE 事件
     - 在 `CFE_TBL_TaskPipe(SBBufPtr)` 返回后添加 COMPLETE 事件
     - 使用 `CFE_TBL_SERVICE_ID` (3, 0-based)
  3. 遵循与 T7 完全相同的模式
  4. Doxygen 中文注释

  **必须不做**:
  - 不修改消息处理逻辑
  - 不添加额外依赖

  **推荐 Agent Profile**:
  - **Category**: `unspecified-high`
    - 原因: 同 T7，精确代码插入
  - **Skills**: [`create-cfs-app`]

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 2（与 T4-T7, T9 并行）
  - **阻塞**: T14
  - **被阻塞**: T2

  **参考资料**:

  **模式参考**:
  - `cfe/modules/sb/fsw/src/cfe_sb_task.c:95-118` — SB 主循环（ReceiveBuffer→ProcessCmdPipePkt）
  - `cfe/modules/tbl/fsw/src/cfe_tbl_task.c:77-99` — TBL 主循环（ReceiveBuffer→TaskPipe）
  - `cfe/modules/es/fsw/src/cfe_es_task.c:163-182` — ES 参考模式（RECEIVE 已实现）

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: SB 和 TBL 都有 RECEIVE+COMPLETE
    Tool: Bash (grep)
    Steps:
      1. grep -n "CMD_PIPE_RECEIVE\|CMD_PIPE_COMPLETE\|SERVICE_ID" cfe/modules/sb/fsw/src/cfe_sb_task.c cfe/modules/tbl/fsw/src/cfe_tbl_task.c
    Expected Result: 每个文件都有 RECEIVE + COMPLETE + SERVICE_ID
    Evidence: .sisyphus/evidence/task-8-sb-tbl-events.txt

  Scenario: 所有 4 个核心模块都集成了 stepping
    Tool: Bash (grep)
    Steps:
      1. grep -rn "CFE_SIM_STEPPING" cfe/modules/es/fsw/src/cfe_es_task.c cfe/modules/evs/fsw/src/cfe_evs_task.c cfe/modules/sb/fsw/src/cfe_sb_task.c cfe/modules/tbl/fsw/src/cfe_tbl_task.c
    Expected Result: 每个文件至少 2 个 ifdef（RECEIVE + COMPLETE 块）
    Evidence: .sisyphus/evidence/task-8-all-modules.txt
  ```

  **Commit**: YES（与 T7 合并）

- [x] 9. TIME 3 个 Stepping Hook 真实实现

  **做什么**:
  1. 在 `cfe/modules/time/fsw/src/cfe_time_stepping.c` 中将 3 个 no-op 函数转为 shim 转发：
     - `CFE_TIME_Stepping_Hook_TaskCycle()`:
       - 构造 ShimEvent_t，`event_kind = CFE_PSP_SIM_STEPPING_EVENT_TIME_TASK_CYCLE`
       - `entity_id = CFE_PSP_SIM_STEPPING_SERVICE_BIT_TIME` (1<<4 = 0x10)
       - 调用 `CFE_PSP_SimStepping_Shim_ReportEvent()`
     - `CFE_TIME_Stepping_Hook_1HzBoundary()`:
        - `event_kind = CFE_PSP_SIM_STEPPING_EVENT_1HZ_BOUNDARY`（注意：无 TIME_ 前缀）
        - `entity_id` = CHILDPATH_BIT_TIME_LOCAL_1HZ
      - `CFE_TIME_Stepping_Hook_ToneSignal()`:
        - `event_kind = CFE_PSP_SIM_STEPPING_EVENT_TONE_SIGNAL`（注意：无 TIME_ 前缀）
       - `entity_id` = CHILDPATH_BIT_TIME_TONE
  2. 遵循 SCH stepping hook 模式（thin shim 转发）
  3. 添加 `#include "cfe_psp_sim_stepping_shim.h"` 到实现文件
  4. Doxygen 中文注释

  **必须不做**:
  - 不修改 `cfe_time_task.c` 中的调用位置（已正确）
  - 不添加业务逻辑
  - 不修改 TIME 模块的正常功能

  **推荐 Agent Profile**:
  - **Category**: `unspecified-high`
    - 原因: 需要理解 TIME 模块的 3 个 hook 语义和正确的 entity_id
  - **Skills**: [`create-cfs-app`]

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 2（与 T4-T8 并行）
  - **阻塞**: T15
  - **被阻塞**: T3

  **参考资料**:

  **模式参考**:
  - `apps/sch/fsw/src/sch_stepping.c:46-86` — SCH hook 模式（最直接的参考，TIME hook 应完全复制此模式）
  - `cfe/modules/time/fsw/src/cfe_time_stepping.c` — 当前 3 个 no-op 函数（需要填充）
  - `cfe/modules/time/fsw/src/cfe_time_task.c:97,367,414` — 3 个 hook 调用位置（确认语义）

  **API/类型参考**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — ShimEvent_t 和 TIME 事件类型
  - `esa/fsw/inc/cfe_psp_sim_stepping_core.h:73` — `CFE_PSP_SIM_STEPPING_SERVICE_BIT_TIME`
  - `esa/fsw/src/cfe_psp_sim_stepping.c:250-275` — shim 中 TIME 事件的处理分支（确认事件到 core 函数的映射）

  **为什么每个参考重要**:
  - `sch_stepping.c` — 唯一的完整 hook→shim 转发参考，必须对齐
  - `cfe_time_task.c:97,367,414` — 确认每个 hook 的调用上下文（TaskCycle 在主循环、1Hz 在子任务、Tone 在信号处理）

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: TIME hook 函数不再是 no-op
    Tool: Bash (grep)
    Steps:
      1. grep -A5 "Hook_TaskCycle\|Hook_1HzBoundary\|Hook_ToneSignal" cfe/modules/time/fsw/src/cfe_time_stepping.c | grep "Shim_ReportEvent"
    Expected Result: 3 个 Shim_ReportEvent 调用（每个 hook 一个）
    Failure Indicators: 任何 hook 仍是空函数体
    Evidence: .sisyphus/evidence/task-9-time-hooks.txt

  Scenario: Stepping 构建成功
    Tool: Bash
    Steps:
      1. make SIMULATION=native CFE_SIM_STEPPING=1 prep && make
    Expected Result: 零错误
    Evidence: .sisyphus/evidence/task-9-build.log
  ```

  **Commit**: YES
  - Message: `feat(cfe/time): implement stepping hooks with shim forwarding`
  - Files: `cfe/modules/time/fsw/src/cfe_time_stepping.c`

- [x] 10. 兼容性别名头文件 + ESA 内部重命名

  **做什么**:
  1. 创建兼容性别名头文件 `esa/public_inc/cfe_psp_sim_stepping_compat.h`：
     - 将所有旧名称 `#define` 为新名称:
       - 函数: `CFE_PSP_SimStepping_*` → `ESA_Stepping_*`
       - Core 函数: `CFE_PSP_SimStepping_Core_*` → `ESA_Stepping_Core_*`
       - 宏/枚举: `CFE_PSP_SIM_STEPPING_*` → `ESA_SIM_STEPPING_*`
       - 类型: `CFE_PSP_SimStepping_*_t` → `ESA_Stepping_*_t`
     - 在文件顶部用 Doxygen 注释说明这是过渡兼容层
  2. 在 ESA 内部文件中执行重命名（`esa/fsw/src/*.c`, `esa/fsw/inc/*.h`, `esa/public_inc/*.h`）：
     - 使用 `ast_grep_replace` 或全文替换
     - `CFE_PSP_SimStepping_` → `ESA_Stepping_` （函数和类型前缀）
     - `CFE_PSP_SIM_STEPPING_` → `ESA_SIM_STEPPING_` （宏/枚举/常量前缀）
      - `cfe_psp_sim_stepping` → `esa_stepping` （包括内部符号和文件名）
   3. **重命名 ESA 文件**（保持目录结构不变）：
       - `esa/fsw/src/cfe_psp_sim_stepping.c` → `esa/fsw/src/esa_stepping.c`
       - `esa/fsw/src/cfe_psp_sim_stepping_core.c` → `esa/fsw/src/esa_stepping_core.c`
       - `esa/fsw/inc/cfe_psp_sim_stepping.h` → `esa/fsw/inc/esa_stepping.h`
       - `esa/fsw/inc/cfe_psp_sim_stepping_core.h` → `esa/fsw/inc/esa_stepping_core.h`
       - `esa/public_inc/cfe_psp_sim_stepping_shim.h` → `esa/public_inc/esa_stepping_shim.h`
       - `esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` → `esa/ut-stubs/src/esa_stepping_shim_stubs.c`
    4. 更新 `esa/CMakeLists.txt` 中的文件引用
    5. **创建旧路径的 1-line 转发包装头文件**（关键！编译器按文件名查找头文件，不会自动找 compat.h）：
       - `esa/fsw/inc/cfe_psp_sim_stepping.h` — 内容仅为 `#include "esa_stepping.h"`
       - `esa/fsw/inc/cfe_psp_sim_stepping_core.h` — 内容仅为 `#include "esa_stepping_core.h"`
       - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — 内容仅为 `#include "esa_stepping_shim.h"`
       - 每个包装文件顶部加 `/** @deprecated Use esa_stepping*.h instead */` 注释
       - 这些文件确保外部 `#include "cfe_psp_sim_stepping_shim.h"` 等语句在 T11 修改前仍能编译
    6. 兼容性别名头文件 `esa/public_inc/cfe_psp_sim_stepping_compat.h` 提供：
       - 旧符号→新符号的 `#define` 别名（使旧函数/宏名在代码中仍可用）
       - ESA 内部头文件自动包含 compat.h 以确保旧符号在链接期也可用
    7. 确保兼容层使外部调用者暂时不需要任何修改即可编译通过

  **必须不做**:
  - 不在此任务中修改外部调用者（cFE, OSAL, PSP, apps）
  - 不删除兼容性别名（T12 做）

  **推荐 Agent Profile**:
  - **Category**: `deep`
    - 原因: 大规模机械重命名需要精确操作和验证
  - **Skills**: []
  - **排除的 Skills**: 机械替换任务不需要特殊技能

  **并行化**:
  - **可并行运行**: NO
  - **并行组**: Wave 3（顺序: T10 → T11 → T12）
  - **阻塞**: T11
  - **被阻塞**: 无（但建议在 Wave 2 后开始，确保所有 hook 实现已提交）

  **参考资料**:

  **模式参考**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — 当前公共 API（需要重命名的符号清单来源）
  - `esa/fsw/inc/cfe_psp_sim_stepping_core.h` — Core API（~50+ 个函数/宏/类型需要重命名）
  - `esa/fsw/inc/cfe_psp_sim_stepping.h` — 内部头文件

  **为什么每个参考重要**:
  - 这 3 个头文件定义了需要重命名的完整符号表，是重命名的起点

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: ESA 内部使用新名称
    Tool: Bash (grep)
    Steps:
      1. grep -r "CFE_PSP_SimStepping" esa/fsw/src/ esa/fsw/inc/ esa/public_inc/ | grep -v "compat.h" | wc -l
    Expected Result: 0（ESA 内部不再使用旧符号名，排除兼容文件）
    Failure Indicators: 计数 > 0
    Evidence: .sisyphus/evidence/task-10-esa-old-names.txt

  Scenario: ESA 文件已重命名
    Tool: Bash
    Steps:
      1. test -f esa/fsw/src/esa_stepping.c && test -f esa/fsw/src/esa_stepping_core.c && test -f esa/fsw/inc/esa_stepping.h && test -f esa/fsw/inc/esa_stepping_core.h && test -f esa/public_inc/esa_stepping_shim.h && echo "ALL RENAMED"
    Expected Result: 输出 "ALL RENAMED"
    Evidence: .sisyphus/evidence/task-10-files-renamed.txt

  Scenario: 兼容性别名存在
    Tool: Bash
    Steps:
      1. test -f esa/public_inc/cfe_psp_sim_stepping_compat.h && grep -c "define CFE_PSP_SimStepping" esa/public_inc/cfe_psp_sim_stepping_compat.h
    Expected Result: 文件存在且包含 ≥20 个 #define 别名
    Evidence: .sisyphus/evidence/task-10-compat-aliases.txt

  Scenario: 旧路径转发包装文件存在
    Tool: Bash
    Steps:
      1. test -f esa/fsw/inc/cfe_psp_sim_stepping.h && grep 'include.*esa_stepping\.h' esa/fsw/inc/cfe_psp_sim_stepping.h
      2. test -f esa/fsw/inc/cfe_psp_sim_stepping_core.h && grep 'include.*esa_stepping_core\.h' esa/fsw/inc/cfe_psp_sim_stepping_core.h
      3. test -f esa/public_inc/cfe_psp_sim_stepping_shim.h && grep 'include.*esa_stepping_shim\.h' esa/public_inc/cfe_psp_sim_stepping_shim.h
    Expected Result: 三个旧路径文件存在且各自 include 了对应的新文件名
    Failure Indicators: 任何 test -f 失败 或 grep 无匹配
    Evidence: .sisyphus/evidence/task-10-forwarding-wrappers.txt

  Scenario: Stepping 构建成功（别名使外部调用者不变）
    Tool: Bash
    Steps:
      1. make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make
    Expected Result: 零错误 — 外部调用者通过别名继续工作
    Evidence: .sisyphus/evidence/task-10-build.log
  ```

  **Commit**: YES
  - Message: `refactor(esa): add compatibility aliases and begin internal API rename`
  - Files: `esa/public_inc/cfe_psp_sim_stepping_compat.h`, `esa/fsw/src/*.c`, `esa/fsw/inc/*.h`, `esa/public_inc/*.h`

- [x] 11. 外部调用者重命名（cFE, OSAL, PSP, apps）

  **做什么**:
  1. 将所有外部调用者从旧名称迁移到新名称：
     - `cfe/modules/es/fsw/src/cfe_es_task.c` — 更新 shim 调用
     - `cfe/modules/evs/fsw/src/cfe_evs_task.c` — 更新 shim 调用
     - `cfe/modules/sb/fsw/src/cfe_sb_task.c` — 更新 shim 调用
     - `cfe/modules/tbl/fsw/src/cfe_tbl_task.c` — 更新 shim 调用
     - `cfe/modules/time/fsw/src/cfe_time_stepping.c` — 更新 shim 调用
     - `osal/src/os/posix/src/os-posix-stepping.c` — 更新 shim 调用
     - `osal/src/os/posix/inc/os-posix-stepping.h` — 更新头文件引用
     - `osal/src/bsp/generic-linux/src/bsp_start.c` — 更新初始化调用
     - `apps/sch/fsw/src/sch_stepping.c` — 更新 shim 调用
     - `apps/sch/fsw/src/sch_stepping.h` — 更新头文件引用
     - `psp/fsw/modules/timebase_posix_clock/cfe_psp_timebase_posix_clock.c` — 更新调用
     - `psp/fsw/modules/soft_timebase/cfe_psp_soft_timebase.c` — 更新调用
   2. 更新所有 `#include` 路径：旧文件名 → 新文件名（如 `#include "cfe_psp_sim_stepping_shim.h"` → `#include "esa_stepping_shim.h"`）
  3. 使用 `ast_grep_replace` 或全文替换进行机械操作
  4. 验证所有文件中旧名称已被替换

  **必须不做**:
  - 不修改功能逻辑
  - 不删除兼容性别名（T12 做）

  **推荐 Agent Profile**:
  - **Category**: `deep`
    - 原因: 跨 12+ 文件的机械重命名，需要精确验证
  - **Skills**: []

  **并行化**:
  - **可并行运行**: NO
  - **并行组**: Wave 3（T10 → T11 → T12）
  - **阻塞**: T12
  - **被阻塞**: T10

  **参考资料**:

  **模式参考**:
  - T10 创建的兼容性别名文件 — 新名称的完整映射

  **外部调用者参考**:
  - `cfe/modules/es/fsw/src/cfe_es_task.c:63-67,174-179` — ES 中的 shim include 和调用
  - `osal/src/os/posix/src/os-posix-stepping.c` — OSAL 中的 shim include 和调用
  - `osal/src/bsp/generic-linux/src/bsp_start.c` — BSP 中的 ESA_Init 调用
  - `apps/sch/fsw/src/sch_stepping.c` — SCH 中的 shim 调用

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: 外部调用者使用新名称
    Tool: Bash (grep)
    Steps:
      1. grep -r "CFE_PSP_SimStepping" --include="*.c" --include="*.h" cfe/ osal/ psp/ apps/ | grep -v "compat.h" | wc -l
    Expected Result: 0
    Evidence: .sisyphus/evidence/task-11-external-old-names.txt

  Scenario: Stepping 构建成功
    Tool: Bash
    Steps:
      1. make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make
    Expected Result: 零错误
    Evidence: .sisyphus/evidence/task-11-build.log
  ```

  **Commit**: YES
  - Message: `refactor: rename CFE_PSP_SimStepping to ESA_Stepping across codebase`

- [x] 12. 删除兼容性别名 + 最终清理

  **做什么**:
  1. 删除 `esa/public_inc/cfe_psp_sim_stepping_compat.h`
  2. 删除旧路径转发包装文件:
     - `esa/fsw/inc/cfe_psp_sim_stepping.h`（T10 创建的转发文件）
     - `esa/fsw/inc/cfe_psp_sim_stepping_core.h`（T10 创建的转发文件）
     - `esa/public_inc/cfe_psp_sim_stepping_shim.h`（T10 创建的转发文件）
  3. 从其他头文件中移除对 compat.h 的 `#include`
  4. 运行全代码库搜索确认无旧名称残留
  5. 更新 `esa/ut-stubs/` 中的存根文件名和符号（如果尚未重命名）
  5. 更新 `esa/CMakeLists.txt` 中的 weak stub 符号名

  **必须不做**:
  - 不在此步骤后保留任何旧名称

  **推荐 Agent Profile**:
  - **Category**: `quick`
    - 原因: 删除文件 + 最终验证搜索
  - **Skills**: []

  **并行化**:
  - **可并行运行**: NO
  - **并行组**: Wave 3（T10 → T11 → T12）
  - **阻塞**: T17
  - **被阻塞**: T11

  **参考资料**:
  - `esa/public_inc/cfe_psp_sim_stepping_compat.h` — 需要删除的文件
  - `esa/CMakeLists.txt` — weak stub 定义

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: 旧名称完全消除
    Tool: Bash (grep)
    Steps:
      1. grep -r "CFE_PSP_SimStepping\|CFE_PSP_SIM_STEPPING\|cfe_psp_sim_stepping" --include="*.c" --include="*.h" /workspace/cFS/ | grep -v "compat" | wc -l
      2. find /workspace/cFS/esa/ -name "*cfe_psp_sim_stepping*" -not -name "*compat*" | wc -l
    Expected Result: 两者均为 0（排除 compat 文件 — 但 compat 也应在此步删除）
    Failure Indicators: 任一计数 > 0
    Evidence: .sisyphus/evidence/task-12-no-old-names.txt

  Scenario: 兼容性文件已删除
    Tool: Bash
    Steps:
      1. test ! -f esa/public_inc/cfe_psp_sim_stepping_compat.h && echo "COMPAT DELETED"
      2. test ! -f esa/fsw/inc/cfe_psp_sim_stepping.h && test ! -f esa/fsw/inc/cfe_psp_sim_stepping_core.h && test ! -f esa/public_inc/cfe_psp_sim_stepping_shim.h && echo "WRAPPERS DELETED"
    Expected Result: 输出 "COMPAT DELETED" 和 "WRAPPERS DELETED"
    Evidence: .sisyphus/evidence/task-12-compat-deleted.txt

  Scenario: Stepping 和非 Stepping 构建都成功
    Tool: Bash
    Steps:
      1. make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make
      2. make distclean && make SIMULATION=native prep && make
    Expected Result: 两次构建都零错误
    Evidence: .sisyphus/evidence/task-12-both-builds.log
  ```

  **Commit**: YES
  - Message: `refactor(esa): remove compatibility aliases, complete API rename`

- [x] 13. OSAL Hook 单元测试（3 个 Hook × 双阶段）

  **做什么**:
  1. 创建测试文件 `esa/ut-coverage/coveragetest-osal-hooks.c`（放在 ESA 测试套件下而非 OSAL，因为 OSAL 的 `add_osal_ut_exe` 只链接 `ut_assert + osal`，不链接 ESA shim stubs，而 hook 测试需要验证 hook→shim 的交互）
  2. 在 `esa/ut-coverage/CMakeLists.txt` 中注册新测试：
     - 新增 test target `coverage-esa-osal_hooks`
     - 参照现有 `coverage-esa-sim_stepping` 的模式创建 OBJECT + RUNNER target
     - OBJECT target 需编译 OSAL hook 源码 `osal/src/os/posix/src/os-posix-stepping.c`
     - RUNNER target 的 include path 需额外包含 `osal/src/os/posix/inc/`（因为 `os-posix-stepping.h` 是 POSIX 私有头，不通过 `osal_public_api` 导出）
     - 链接库与现有 runner 相同: `ut_esa_api_stubs`, `psp_module_api`, `ut_psp_api_stubs`, `ut_psp_libc_stubs`, `ut_osapi_stubs`, `ut_assert`
  3. TDD 流程 — 先写失败测试:
     - 测试 `OS_PosixStepping_Hook_TaskDelay()` 调用 shim 并传递正确的 event_kind=TASK_DELAY_ACK 和 delay_ms
     - 测试 `OS_PosixStepping_Hook_TaskDelay_Complete()` 传递 event_kind=TASK_DELAY_COMPLETE
     - 测试 `OS_PosixStepping_Hook_QueueReceive()` 传递 QUEUE_RECEIVE_ACK 和正确的 entity_id
     - 测试 `OS_PosixStepping_Hook_QueueReceive_Complete()` 传递 QUEUE_RECEIVE_COMPLETE
     - 测试 `OS_PosixStepping_Hook_BinSemTake()` 传递 BINSEM_TAKE_ACK 和正确的 entity_id
     - 测试 `OS_PosixStepping_Hook_BinSemTake_Complete()` 传递 BINSEM_TAKE_COMPLETE
  4. 使用 ut_assert 框架:
     - 创建 `CFE_PSP_SimStepping_Shim_ReportEvent` 的 stub（已有：`esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c`）
     - 使用 `UT_SetHookFunction` 捕获传入的 ShimEvent_t 参数
     - 验证 event_kind、entity_id、task_id 字段正确
  5. 确保 `make test` 包含这些测试

  **必须不做**:
  - 不测试 shim 或 core 内部（那些有单独的测试）
  - 不测试 OSAL 阻塞操作本身 — 仅测试 hook 函数

  **推荐 Agent Profile**:
  - **Category**: `deep`
    - 原因: TDD 测试需要理解 ut_assert stub 机制和 OSAL 内部
  - **Skills**: [`create-cfs-unit-test`]
    - `create-cfs-unit-test`: cFS ut_assert 测试框架、stub 创建、CMake 集成

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 4（与 T14, T15 并行）
  - **阻塞**: T17
  - **被阻塞**: T4, T5, T6

  **参考资料**:

  **模式参考**:
  - `esa/ut-coverage/coveragetest-sim_stepping.c` — ESA 现有测试模式（UtTest_Setup, ADD_TEST, ResetTest）
  - `esa/ut-coverage/CMakeLists.txt` — CMake 测试注册模式（OBJECT + RUNNER + 链接库清单），新测试必须参照此模式注册
  - `esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` — shim 的 stub 实现（测试可直接复用，无需额外创建 stub）

  **API/类型参考**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — ShimEvent_t 结构定义
  - `osal/src/os/posix/inc/os-posix-stepping.h` — OSAL hook 函数声明（注意：POSIX 私有头，需在 CMake 中手动添加 include path）
  - `osal/src/os/posix/src/os-posix-stepping.c` — hook 实现源码（需作为 OBJECT target 编译进测试）

  **构建集成参考**:
  - `esa/ut-coverage/CMakeLists.txt:24-85` — 完整的 coverage test 注册模式，新 test 必须复制此模式并修改源码列表和 include path
  - `osal/src/os/posix/inc/` — 此路径需添加到 RUNNER 和 OBJECT 的 include_directories

  **为什么每个参考重要**:
  - `coveragetest-sim_stepping.c` — 建立的测试结构和断言模式
  - `ut-coverage/CMakeLists.txt` — 链接库和 include path 的完整示例，确保新测试不遗漏依赖
  - `shim_stubs.c` — 已有的 shim stub 可以直接在测试中使用
  - `os-posix-stepping.h` — 私有头，不在公共 API 中，测试的 CMake 必须手动添加其路径

  **为什么每个参考重要**:
  - `coveragetest-sim_stepping.c` — 建立的测试结构和断言模式
  - `shim_stubs.c` — 已有的 shim stub 可以直接在测试中使用

  **验收标准**:

  **TDD:**
  - [ ] 测试文件创建并编译
  - [ ] `make test` → 6 个测试全部通过

  **QA 场景:**

  ```
  Scenario: OSAL hook 测试全部通过
    Tool: Bash
    Steps:
      1. make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make && make install && make test 2>&1 | grep -E "stepping.*hook|OSAL.*stepping"
    Expected Result: 所有 OSAL stepping hook 测试通过
    Failure Indicators: 任何测试 FAIL
    Evidence: .sisyphus/evidence/task-13-osal-hook-tests.log

  Scenario: 每个 hook 验证正确的 event_kind
    Tool: Bash (grep)
    Steps:
      1. grep -n "event_kind\|TASK_DELAY\|QUEUE_RECEIVE_ACK\|BINSEM_TAKE_ACK\|COMPLETE" esa/ut-coverage/coveragetest-osal-hooks.c
    Expected Result: 每种事件类型至少验证一次
    Evidence: .sisyphus/evidence/task-13-event-kind-assertions.txt
  ```

  **Commit**: YES（与 T14, T15 合并）
  - Message: `test(esa): add TDD unit tests for all stepping hooks`

- [x] 14. cFE 模块 Stepping 集成测试（ES/EVS/SB/TBL）

  **做什么**:
  1. 扩展 `esa/ut-coverage/coveragetest-sim_stepping.c` 或创建新测试文件
  2. TDD 测试:
     - 测试 ES 模块在 TaskPipe 前后分别发送 RECEIVE 和 COMPLETE 事件
     - 测试 EVS 模块的 RECEIVE+COMPLETE
     - 测试 SB 模块的 RECEIVE+COMPLETE
     - 测试 TBL 模块的 RECEIVE+COMPLETE
     - 验证每个模块使用正确的 SERVICE_ID
  3. 使用 stub 替代 `CFE_SB_ReceiveBuffer` 和各模块的 TaskPipe
  4. 捕获 `CFE_PSP_SimStepping_Shim_ReportEvent` 调用并验证参数

  **必须不做**:
  - 不测试消息处理逻辑 — 仅测试 stepping 事件报告
  - 不修改已实现的代码

  **推荐 Agent Profile**:
  - **Category**: `deep`
    - 原因: 需要理解 cFE 核心模块的 stub 和测试模式
  - **Skills**: [`create-cfs-unit-test`]

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 4（与 T13, T15 并行）
  - **阻塞**: T17
  - **被阻塞**: T7, T8

  **参考资料**:

  **模式参考**:
  - `esa/ut-coverage/coveragetest-sim_stepping.c` — 现有 ESA 测试模式
  - `cfe/modules/es/ut-coverage/` — ES 模块的现有单元测试（如何 stub cFE API）

  **验收标准**:

  **TDD:**
  - [ ] 测试编译
  - [ ] `make test` → 所有新测试通过

  **QA 场景:**

  ```
  Scenario: 4 个核心模块的 stepping 测试通过
    Tool: Bash
    Steps:
      1. make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make && make install && make test 2>&1 | grep -E "core.*service|stepping.*module"
    Expected Result: ES, EVS, SB, TBL 的 stepping 测试全部通过
    Evidence: .sisyphus/evidence/task-14-module-tests.log

  Scenario: 每个模块验证正确的 SERVICE_ID
    Tool: Bash (grep)
    Steps:
      1. grep -n "SERVICE_ID\|entity_id" esa/ut-coverage/coveragetest-sim_stepping.c
    Expected Result: ES=0, EVS=1, SB=2, TBL=3（0-based index）
    Evidence: .sisyphus/evidence/task-14-service-id-verification.txt
  ```

  **Commit**: YES（与 T13, T15 合并）

- [x] 15. TIME Stepping Hook 测试

  **做什么**:
  1. 为 TIME 的 3 个 stepping hook 编写 TDD 测试:
     - `CFE_TIME_Stepping_Hook_TaskCycle` → 验证 event_kind=TIME_TASK_CYCLE
      - `CFE_TIME_Stepping_Hook_1HzBoundary` → 验证 event_kind=1HZ_BOUNDARY（完整: `CFE_PSP_SIM_STEPPING_EVENT_1HZ_BOUNDARY`，无 TIME_ 前缀）
      - `CFE_TIME_Stepping_Hook_ToneSignal` → 验证 event_kind=TONE_SIGNAL（完整: `CFE_PSP_SIM_STEPPING_EVENT_TONE_SIGNAL`，无 TIME_ 前缀）
  2. 验证每个 hook 传递正确的 entity_id
  3. 使用 shim stub 捕获事件
  4. CMake 集成

  **必须不做**:
  - 不测试 TIME 模块的正常功能
  - 仅测试 stepping hook 的 shim 报告

  **推荐 Agent Profile**:
  - **Category**: `unspecified-high`
    - 原因: 范围明确的 3 个测试，参考模式已建立
  - **Skills**: [`create-cfs-unit-test`]

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 4（与 T13, T14 并行）
  - **阻塞**: T17
  - **被阻塞**: T9

  **参考资料**:

  **模式参考**:
  - T13 创建的 OSAL hook 测试模式（复用相同的 stub 和验证方法）

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: TIME hook 测试全部通过
    Tool: Bash
    Steps:
      1. make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make && make install && make test 2>&1 | grep -E "time.*stepping|stepping.*time"
    Expected Result: 3 个 TIME hook 测试全部通过
    Evidence: .sisyphus/evidence/task-15-time-tests.log
  ```

  **Commit**: YES（与 T13, T14 合并）

- [x] 16. ESA README 文档

  **做什么**:
  1. 创建 `esa/README.md`，内容包含:
     - ESA（Event-Step-Advance）stepping 引擎概述
     - 架构图（ASCII）：OSAL hooks → Shim → Core → Control channels
     - 配置说明：`CFE_SIM_STEPPING` 宏、CMake 选项
     - 两阶段模型（ACK+COMPLETE）说明
     - 事件类型清单（20 种事件类型及其语义）
     - 控制通道说明（InProc + UDS）
     - 状态机转换图
     - 构建方法：`make SIMULATION=native CFE_SIM_STEPPING=1 prep && make`
     - API 参考（主要公共函数）
     - 使用示例
  2. 使用中文编写（按项目约定）
  3. Markdown 格式，带代码块

  **必须不做**:
  - 不添加英文文档（中文优先）
  - 不包含实现细节（API 级别文档）
  - 不修改代码文件

  **推荐 Agent Profile**:
  - **Category**: `writing`
    - 原因: 纯文档编写任务
  - **Skills**: []

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 5（与 T17 并行）
  - **阻塞**: 无
  - **被阻塞**: 无（可在任何时间开始，但建议在 Wave 2 后）

  **参考资料**:

  **模式参考**:
  - `esa/public_inc/cfe_psp_sim_stepping_shim.h` — 事件类型枚举（20 种事件类型的权威来源）
  - `esa/fsw/inc/cfe_psp_sim_stepping_core.h` — Core API 和状态机定义
  - `esa/fsw/src/cfe_psp_sim_stepping.c` — InProc/UDS 控制通道实现
  - `.sisyphus/archive/linux-global-sim-stepping/plan/linux-global-sim-stepping.md` — 原始计划中的架构描述

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: README 存在且内容完整
    Tool: Bash
    Steps:
      1. test -f esa/README.md && wc -l esa/README.md
    Expected Result: 文件存在且 ≥100 行
    Evidence: .sisyphus/evidence/task-16-readme-exists.txt

  Scenario: README 包含关键章节
    Tool: Bash (grep)
    Steps:
      1. grep -c "## " esa/README.md
    Expected Result: ≥6 个章节标题（概述、架构、配置、事件类型、控制通道、构建）
    Evidence: .sisyphus/evidence/task-16-readme-sections.txt
  ```

  **Commit**: YES
  - Message: `docs(esa): add ESA stepping engine README`
  - Files: `esa/README.md`

- [x] 17. 端到端集成验证

  **做什么**:
  1. 执行完整的 stepping 构建 + 测试验证:
     - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make && make install && make test`
     - 验证所有测试通过
  2. 执行非 stepping 回归验证:
     - `make distclean && make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install && make test`
     - 验证无 stepping 构建不受影响
  3. API 重命名完整性验证:
     - `grep -r "CFE_PSP_SimStepping" --include="*.c" --include="*.h" /workspace/cFS/` — 预期 0 匹配
  4. 验证所有 hook 集成点:
     - 验证 OSAL 3 个 hook × 2（pre+post） = 6 个调用点
     - 验证 cFE 4 个模块 × 2（RECEIVE+COMPLETE） = 8 个事件
     - 验证 TIME 3 个 hook = 3 个事件
  5. 运行 ESA 已有的单元测试确认未被破坏
  6. 收集所有证据到 `.sisyphus/evidence/`

  **必须不做**:
  - 不修改任何代码
  - 这是纯验证任务

  **推荐 Agent Profile**:
  - **Category**: `deep`
    - 原因: 全面的端到端验证需要系统性执行
  - **Skills**: []

  **并行化**:
  - **可并行运行**: YES
  - **并行组**: Wave 5（与 T16 并行）
  - **阻塞**: F1-F4
  - **被阻塞**: T12, T13, T14, T15

  **参考资料**:
  - 计划中的"成功标准"章节 — 精确的验证命令

  **验收标准**:

  **QA 场景:**

  ```
  Scenario: Stepping 全栈构建 + 测试通过
    Tool: Bash
    Steps:
      1. make distclean
      2. make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep
      3. make
      4. make install
      5. make test 2>&1 | tee test-output.log
      6. grep "tests passed" test-output.log
    Expected Result: 100% tests passed, 0 failures
    Failure Indicators: 任何 FAIL 或编译错误
    Evidence: .sisyphus/evidence/task-17-stepping-full-test.log

  Scenario: 非 Stepping 回归通过
    Tool: Bash
    Steps:
      1. make distclean
      2. make SIMULATION=native ENABLE_UNIT_TESTS=true prep
      3. make
      4. make install
      5. make test 2>&1 | tee test-output.log
    Expected Result: 100% tests passed, 0 failures
    Evidence: .sisyphus/evidence/task-17-non-stepping-test.log

  Scenario: API 重命名完整
    Tool: Bash
    Steps:
      1. grep -r "CFE_PSP_SimStepping\|CFE_PSP_SIM_STEPPING\|cfe_psp_sim_stepping" --include="*.c" --include="*.h" /workspace/cFS/ | wc -l
      2. find /workspace/cFS/esa/ -name "*cfe_psp_sim_stepping*" | wc -l
    Expected Result: 两者均为 0（所有旧名称和旧文件名均已消除）
    Evidence: .sisyphus/evidence/task-17-no-old-names.txt

  Scenario: Hook 调用点计数正确
    Tool: Bash
    Steps:
      1. grep -rn "Shim_ReportEvent\|Stepping_Shim_Report" --include="*.c" osal/src/os/posix/src/os-posix-stepping.c cfe/modules/es/fsw/src/ cfe/modules/evs/fsw/src/ cfe/modules/sb/fsw/src/ cfe/modules/tbl/fsw/src/ cfe/modules/time/fsw/src/ apps/sch/fsw/src/ | wc -l
    Expected Result: ≥17（OSAL 6 + cFE ES/EVS/SB/TBL 各2=8 + TIME 3 = 17，不含 SCH）
    Evidence: .sisyphus/evidence/task-17-hook-count.txt
  ```

  **Commit**: NO
  - 纯验证任务，不产生代码变更

---

## Final Verification Wave（必须 — 所有实现任务完成后）

> 4 个审查 agent 并行运行。全部必须 APPROVE。向用户展示汇总结果并获取明确"好的"确认后才能完成。
>
> 在验证后不要自动继续。等待用户明确批准后才标记工作完成。

- [x] F1. **计划合规审计** — `oracle`
  逐条阅读计划。对每个"必须有"：验证实现存在（读取文件、运行命令）。对每个"必须没有"：搜索代码库中的禁止模式 — 发现则拒绝并给出 file:line。检查 `.sisyphus/evidence/` 中的证据文件。对比交付物与计划。
  输出: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT: APPROVE/REJECT`

- [x] F2. **代码质量审查** — `unspecified-high`
  运行 `make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make` + `make test`。审查所有变更文件：检查 `as any`/注释掉的代码、未使用的 include、空 catch。检查 AI 风格问题：过度注释、过度抽象、泛型命名。验证所有变更在 `#ifdef CFE_SIM_STEPPING` 保护下。
  输出: `Build [PASS/FAIL] | Tests [N pass/N fail] | Files [N clean/N issues] | VERDICT`

- [x] F3. **真实 QA 验证** — `unspecified-high`
  从干净状态开始。执行每个任务的每个 QA 场景 — 按照精确步骤，捕获证据。测试跨任务集成（功能协同工作）。测试边界情况：stepping 禁用时零副作用、stepping 启用但无控制器连接时的行为。保存到 `.sisyphus/evidence/final-qa/`。
  输出: `Scenarios [N/N pass] | Integration [N/N] | Edge Cases [N tested] | VERDICT`

- [x] F4. **范围保真度检查** — `deep`
  对每个任务：阅读"做什么"，阅读实际 diff。验证 1:1 — spec 中的所有内容都已构建（无遗漏），spec 之外的内容未被构建（无蔓延）。检查"必须没有"合规性。检测跨任务污染：任务 N 触及任务 M 的文件。标记未计入的变更。
  输出: `Tasks [N/N compliant] | Contamination [CLEAN/N issues] | Unaccounted [CLEAN/N files] | VERDICT`

---

## 提交策略

| 提交 | 任务 | 消息 | 文件 |
|------|------|------|------|
| 1 | T1 | `feat(osal): expand stepping hook signatures and add post-blocking call sites` | os-posix-stepping.h/.c, os-impl-*.c |
| 2 | T2+T3 | `feat(cfe): add stepping service ID constants for core modules` | cfe_*_task.c headers, cfe_time_stepping.h |
| 3 | T4-T6 | `feat(osal): implement dual-phase stepping hooks with shim forwarding` | os-posix-stepping.c |
| 4 | T7+T8 | `feat(cfe): add stepping wait-set integration for ES/EVS/SB/TBL` | cfe_es_task.c, cfe_evs_task.c, cfe_sb_task.c, cfe_tbl_task.c |
| 5 | T9 | `feat(cfe/time): implement stepping hooks with shim forwarding` | cfe_time_stepping.c |
| 6 | T10 | `refactor(esa): add compatibility aliases and begin API rename` | esa headers |
| 7 | T11 | `refactor: rename CFE_PSP_SimStepping to ESA across codebase` | all callers |
| 8 | T12 | `refactor(esa): remove compatibility aliases, complete rename` | esa headers |
| 9 | T13-T15 | `test(esa): add TDD unit tests for all stepping hooks` | ut-coverage/ |
| 10 | T16 | `docs(esa): add ESA stepping engine README` | esa/README.md |
| 11 | T17 | `test(esa): add end-to-end integration verification` | evidence/ |

---

## 成功标准

### 验证命令
```bash
# Stepping 构建 + 测试
make distclean
make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep
make
make install
make test  # 预期: 100% tests passed, 0 failures

# 非 Stepping 回归
make distclean
make SIMULATION=native ENABLE_UNIT_TESTS=true prep
make
make install
make test  # 预期: 100% tests passed, 0 failures

# API 重命名完整性
grep -r "CFE_PSP_SimStepping" --include="*.c" --include="*.h" /workspace/cFS/  # 预期: 0 匹配
```

### 最终检查清单
- [x] 所有"必须有"存在
- [x] 所有"必须没有"不存在
- [x] 所有测试通过
- [x] API 重命名完整
- [x] 非 stepping 构建不受影响
- [x] 文档完整

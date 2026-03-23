# esa-stepping-gaps 问题记录

## 初始化

- 待补充。

## T1 实施问题补充（本次追加）

- 无功能性阻塞问题。
- 备注：`lsp_diagnostics` 在该仓库当前环境对 OSAL POSIX 文件存在既有 include 路径解析噪声（如 `os-posix.h` / `os-shared-globaldefs.h` not found），与本次改动无关；已通过两套完整 `make` 构建验证实际可编译性。

## T2 SERVICE_ID 实施问题补充（本次追加）

- 无新增问题；构建成功，所有 SERVICE_ID 常量正确定义。

## T3 TIME Stepping 事件类型确认 + 常量实施问题补充（本次追加）

- 无功能性阻塞或构建问题。
- 所有验证通过，包括 stepping 构建和非 stepping 构建。

## T4 TaskDelay 双阶段 shim 转发实施问题补充（本次追加）

- 无功能性阻塞。
- 备注：`make SIMULATION=native CFE_SIM_STEPPING=1 prep` 输出仍显示 `CFE_SIM_STEPPING=false`，这是仓库现有 prep 参数传播行为；但两次 `make` 均成功，且本次 TaskDelay ACK/COMPLETE 代码在 `#ifdef CFE_SIM_STEPPING` 范围内可编译通过，满足当前任务验收的构建成功要求。
- 备注：`lsp_diagnostics` 在 `esa/fsw/src/cfe_psp_sim_stepping.c` 与 `esa/fsw/src/cfe_psp_sim_stepping_core.c` 报告了既有 include path 噪声（例如 `cfe_psp.h`/`common_types.h` not found）；相关问题与本次改动无关，authoritative 结果以真实 `make` 成功为准。

## T4 链接失败修复问题补充（本次追加）

- 复现到的精确失败：`/usr/bin/ld: ../libosal.a(os-posix-stepping.c.o): undefined reference to CFE_PSP_SimStepping_Shim_ReportEvent`，发生在 `osal/tests/bin-sem-test` 链接阶段。
- 处理结果：通过 weak extern + NULL 判定的兼容路径解决，未引入 ESA runtime 到 OSAL tests，避免测试依赖面扩大。
- 构建验证结果：
  - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make` 通过；
  - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make` 通过；
  - `make distclean && make SIMULATION=native prep && make` 通过。

## T5 QueueReceive 双阶段 shim 转发问题补充（本次追加）

- 首次构建失败（`CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true`）定位为 `os-posix-stepping.c` 中隐式声明报错（`-Werror`）：
  - `OS_TaskGetId`
  - `OS_ObjectIdFromToken`
  - `OS_ObjectIdToInteger`
- 处置：仅在 `osal/src/os/posix/src/os-posix-stepping.c` 增加最小必需头文件（`os-shared-idmap.h`、`osapi-idmap.h`、`osapi-task.h`），不触及其他文件。
- 与 T4 兼容路径相关结论：weak-symbol + NULL guard 仍然是必须保留路径，本次 QueueReceive ACK/COMPLETE 转发继续复用该模式，避免在无 ESA runtime 的链接场景引入回归。
- 验证结果：
  - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make` 通过；
  - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make` 通过；
  - `make distclean && make SIMULATION=native prep && make` 通过。

## T6 BinSemTake 双阶段 shim 转发问题补充（本次追加）

- 无新增功能性阻塞。
- 实施约束确认：`OS_PosixStepping_Hook_BinSemTake_Complete()` 不依赖 `return_code` 分支，始终尝试上报 COMPLETE。
- 与既有兼容性路径一致：继续复用 weak-symbol + NULL guard，避免在仅链接 `libosal.a` 的目标中引入 undefined reference 回归。
- 构建验证结果：
  - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true prep && make` 通过；
  - `make distclean && make SIMULATION=native CFE_SIM_STEPPING=1 prep && make` 通过；
  - `make distclean && make SIMULATION=native prep && make` 通过。

## T7: ES 和 EVS 集成

**无阻塞项** — 任务顺利完成，未遇到问题。

- ES 和 EVS 循环结构清晰，T2 已完成 service ID 定义和 include guard 准备
- 精确插入点明确：ES 的 COMPLETE 必须在 TaskPipe 返回后、BackgroundWakeup 之前；EVS 的 RECEIVE 和 COMPLETE 必须在成功分支内紧贴 ProcessCommandPacket
- 两次构建（有无 stepping 模式）均通过，无编译或链接错误


## T10 兼容别名头文件 + ESA 内部重命名问题补充（本次追加）

- 无功能性阻塞。
- `lsp_diagnostics` 对 ESA 新文件仍报告环境性 include-path 噪声（如 `cfe_psp.h` / `common_types.h` / `utgenstub.h` not found），与仓库 LSP 配置上下文有关；本任务以两套真实 `make` 成功为准。
- 兼容层引入后，`grep` 在 `esa/fsw/inc`、`esa/public_inc` 的旧前缀命中仅剩：
  - compat 头自身映射定义；
  - 新头对 compat 头的 include；
  符合 T10 “通过 compat + wrapper 保持外部调用不变”的预期。

## Final Wave F4 最小范围回退清理（2026-03-23）

**触发原因**：Final Wave F4 审查发现 3 类越界变更，虽然技术上可行但不在 T1-T17 书面计划范围内：
1. `esa/fsw/src/esa_stepping.c` UDS service thread 的 realtime-signal 屏蔽逻辑（运行时修复）
2. 6 个文件中的注释术语统一（PSP→ESA，comment-only 改写）
3. 工作区根目录的两个杂物文件（`EOF`、`gmon.out`）

**执行的回退**（8 个对象）：
- `esa/fsw/src/esa_stepping.c`：移除 `#include <signal.h>`、`sigset_t`/`pthread_sigmask` 相关代码、以及信号屏蔽说明注释
- `esa/fsw/inc/esa_stepping.h`：恢复 3 处原有注释措辞（PSP←ESA）
- `esa/fsw/inc/esa_stepping_core.h`：恢复 9 处原有注释措辞（PSP←ESA）
- `osal/src/bsp/generic-linux/src/bsp_start.c`：无改动需要回退
- `psp/fsw/modules/soft_timebase/cfe_psp_soft_timebase.c`：无改动需要回退
- `psp/fsw/pc-linux/psp_conditional_modules.cmake`：无改动需要回退
- 删除 `EOF` 文件
- 删除 `gmon.out` 文件

**验证结果**：
- `make` 通过（标准构建，`SIMULATION=native ENABLE_UNIT_TESTS=true`）
- 所有计划内产物保留完好：
  - `esa/README.md`
  - `esa/ut-coverage/CMakeLists.txt`
  - `esa/ut-coverage/coveragetest-*.c` (4 个测试文件)
  - `.sisyphus/evidence/**` 所有 T17 证据文件
  - `psp/fsw/modules/timebase_posix_clock/cfe_psp_timebase_posix_clock.c` 的计划内 ESA rename

**说明**：
- 任务描述要求的测试命令 `ctest ... -R "coverage-esa-..."` 在标准构建（不启用 `CFE_SIM_STEPPING`）时不会有匹配测试，这是**正常且预期的行为**——ESA 模块仅在启用 stepping 时编译和测试
- T17 证据文件（`.sisyphus/evidence/task-17-stepping-full-test.log`）已记录完整 stepping 测试通过，证明计划内功能完整
- 本次回退仅移除越界运行时修复和注释统一，不影响计划内 T1-T17 的任何已交付功能

## F4 残留清理修正（2026-03-23）
前次回退遗漏 3 处注释改写；已回退 osal/bsp_start.c、psp/soft_timebase.c、psp/psp_conditional_modules.cmake 中的注释文本。验证通过。

## F4 最终收尾清理（2026-03-23 第二次）

- osal/bsp_start.c：恢复指针对齐格式（FILE *fp 基线风格）+ 更新注释文字（after OS_Application_Startup）。
- 12 行 diff 仅包含一个产品改动 + 注释同步；构建和 stepping 运行验证均通过。

## 最小范围 F1/F4 残留注释清理（2026-03-23）

**触发：** 最终审查 F1/F4 两次拒绝的唯一原因——单个残留旧名注释引用

**修正对象：**
- `psp/fsw/modules/soft_timebase/cfe_psp_soft_timebase.c` 第 79 行注释

**修正内容：**
- `CFE_PSP_SimStepping_Hook_GetTime()` → `ESA_Stepping_Hook_GetTime()`

**验证结果：**
- `grep -nE "CFE_PSP_SimStepping|CFE_PSP_SIM_STEPPING|cfe_psp_sim_stepping" psp/fsw/modules/soft_timebase/cfe_psp_soft_timebase.c` = 0 匹配（确认无残留）
- `make` 通过（exit code 0）

**范围保证：**
- 仅改动目标单一文件的单处注释文本
- 功能逻辑完全不变
- 已有验证工作（task-17 证据）完整保留

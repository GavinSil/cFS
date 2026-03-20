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

# CFE_SIM_STEPPING 功能完整变更清单

**生成时间:** 2026-03-23
**分支:** `migrate_trick_cfs`
**基线:** `b4ec679b2fd9d825610515ceb186cbfa80c60cfc` (main)

---

## 概述

`CFE_SIM_STEPPING` 是一个**编译时条件宏**，用于在 cFS 原生仿真模式下启用确定性步进控制。
所有变更通过 `#ifdef CFE_SIM_STEPPING` / `#ifndef CFE_SIM_STEPPING` 守护，未启用时零代码影响。

**启用方式：**
```bash
make SIMULATION=native CFE_SIM_STEPPING=1 prep
```

**传递链：** 环境变量 → `mission_build.cmake` 缓存 → `-DCFE_SIM_STEPPING=1` 传递给所有子项目 → 各 CMakeLists.txt 条件编译

---

## 一、构建系统（CMake 传递链）

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `cfe/cmake/mission_build.cmake` | 修改 | 从环境变量读取 `CFE_SIM_STEPPING`，缓存并传递给所有子项目 |
| `cfe/cmake/mission_defaults.cmake` | 修改 | `CFE_SIM_STEPPING` 启用时将 `esa` 添加到构建列表 |

## 二、ESA 模块（全部新增 🆕）

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `esa/CMakeLists.txt` | 🆕 新增 | 条件编译 stepping 源文件 |
| `esa/README.md` | 🆕 新增 | 模块说明文档（含架构、API、使用指南） |
| `esa/fsw/inc/esa_stepping.h` | 🆕 新增 | stepping 引擎公共 API 头（初始化、步进、时间查询等） |
| `esa/fsw/inc/esa_stepping_core.h` | 🆕 新增 | 核心服务步进挂起/唤醒内部头 |
| `esa/fsw/src/esa_stepping.c` | 🆕 新增 | stepping 引擎主实现（UNIX socket 控制、步进循环、时间管理） |
| `esa/fsw/src/esa_stepping_core.c` | 🆕 新增 | 核心服务 shim 实现（barrier 同步） |
| `esa/public_inc/esa_stepping_shim.h` | 🆕 新增 | 公共 shim 接口（供 cFE 核心服务调用） |
| `esa/ut-stubs/CMakeLists.txt` | 🆕 新增 | stub 库构建 |
| `esa/ut-stubs/src/esa_stepping_shim_stubs.c` | 🆕 新增 | shim 函数 UT 桩 |
| `esa/ut-coverage/CMakeLists.txt` | 🆕 新增 | 覆盖率测试构建 |
| `esa/ut-coverage/coveragetest-sim_stepping.c` | 🆕 新增 | stepping 引擎核心逻辑测试 |
| `esa/ut-coverage/coveragetest-osal-hooks.c` | 🆕 新增 | OSAL stepping hook 测试 |
| `esa/ut-coverage/coveragetest-core-services.c` | 🆕 新增 | 核心服务 shim 覆盖测试 |
| `esa/ut-coverage/coveragetest-time-hooks.c` | 🆕 新增 | TIME stepping hook 测试 |

## 三、OSAL 子模块（posix 层 stepping hooks）

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `osal/src/os/posix/inc/os-posix-stepping.h` | 🆕 新增 | POSIX stepping hook 声明（任务/队列/信号量同步点） |
| `osal/src/os/posix/src/os-posix-stepping.c` | 🆕 新增 | hook 实现：任务调度前、队列收发、信号量操作时暂停 |
| `osal/src/os/posix/src/os-impl-tasks.c` | 修改 | `#ifdef CFE_SIM_STEPPING` 在任务创建/删除时调用 stepping hook |
| `osal/src/os/posix/src/os-impl-queues.c` | 修改 | `#ifdef CFE_SIM_STEPPING` 在消息队列收发时调用 stepping hook |
| `osal/src/os/posix/src/os-impl-binsem.c` | 修改 | `#ifdef CFE_SIM_STEPPING` 在二值信号量操作时调用 stepping hook |
| `osal/src/os/posix/CMakeLists.txt` | 修改 | 条件编译 stepping 源文件 + 添加 esa 头文件搜索路径 |
| `osal/src/bsp/generic-linux/src/bsp_start.c` | 修改 | `#ifdef CFE_SIM_STEPPING` BSP 启动时初始化 stepping 引擎 |
| `osal/src/bsp/generic-linux/CMakeLists.txt` | 修改 | 条件链接 esa 头文件路径 |

## 四、cFE 子模块（核心服务 stepping 集成）

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `cfe/modules/es/fsw/src/cfe_es_task.c` | 修改 | ES 任务主循环：`#ifdef CFE_SIM_STEPPING` 挂起/唤醒调用 |
| `cfe/modules/es/CMakeLists.txt` | 修改 | 条件添加 esa 头文件搜索路径 |
| `cfe/modules/evs/fsw/src/cfe_evs_task.c` | 修改 | EVS 任务主循环：stepping 挂起/唤醒 |
| `cfe/modules/sb/fsw/src/cfe_sb_task.c` | 修改 | SB 任务主循环：stepping 挂起/唤醒 |
| `cfe/modules/tbl/fsw/src/cfe_tbl_task.c` | 修改 | TBL 任务主循环：stepping 挂起/唤醒 |
| `cfe/modules/time/fsw/src/cfe_time_task.c` | 修改 | TIME 任务：stepping 模式下使用仿真时间替代硬件时间 |
| `cfe/modules/time/fsw/src/cfe_time_stepping.h` | 🆕 新增 | TIME stepping 接口声明（1Hz/tone 回调桩） |
| `cfe/modules/time/fsw/src/cfe_time_stepping.c` | 🆕 新增 | TIME stepping 实现（仿真时钟推进） |
| `cfe/modules/time/CMakeLists.txt` | 修改 | 条件编译 stepping 源 + esa 头文件路径 |

## 五、PSP 子模块（时基 stepping 适配）

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `psp/fsw/modules/soft_timebase/cfe_psp_soft_timebase.c` | 修改 | `#ifndef CFE_SIM_STEPPING` 跳过硬件定时器，stepping 模式由 esa 管理时基 |
| `psp/fsw/modules/timebase_posix_clock/cfe_psp_timebase_posix_clock.c` | 修改 | `#ifdef CFE_SIM_STEPPING` 使用仿真时间替代 `clock_gettime` |
| `psp/fsw/modules/timebase_posix_clock/CMakeLists.txt` | 修改 | 条件添加 esa 头文件路径 |

## 六、SCH 应用子模块（调度器 stepping 集成）

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `apps/sch/fsw/src/sch_stepping.h` | 🆕 新增 | SCH stepping hook 声明 |
| `apps/sch/fsw/src/sch_stepping.c` | 🆕 新增 | SCH stepping 实现（步进模式下调度槽同步） |
| `apps/sch/fsw/src/sch_custom.c` | 修改 | `#ifdef CFE_SIM_STEPPING` 调用 stepping hook |
| `apps/sch/fsw/src/sch_app.c` | 修改 | `#ifdef CFE_SIM_STEPPING` 初始化时注册 stepping |
| `apps/sch/CMakeLists.txt` | 修改 | 条件编译 stepping 源 + esa 头文件路径 |

---

## 统计汇总

| 类别 | 新增文件 | 修改文件 | 合计 |
|------|---------|---------|------|
| 构建系统 | 0 | 2 | 2 |
| ESA 模块 | 14 | 0 | 14 |
| OSAL | 2 | 6 | 8 |
| cFE | 2 | 7 | 9 |
| PSP | 0 | 3 | 3 |
| SCH | 2 | 3 | 5 |
| **总计** | **20** | **21** | **41** |

---

## 架构层次

```
┌─────────────────────────────────────────────┐
│  外部控制 (UNIX Socket: /tmp/cfs_stepping)  │  ← REPL/Trick 连接点
├─────────────────────────────────────────────┤
│  ESA Stepping 引擎 (esa/)                   │  ← 步进控制核心
│  ├── esa_stepping.c     (socket + 步进循环) │
│  ├── esa_stepping_core.c (barrier 同步)     │
│  └── esa_stepping_shim.h (公共接口)         │
├─────────────────────────────────────────────┤
│  cFE 核心服务 hooks                          │
│  ├── ES/EVS/SB/TBL: 主循环挂起/唤醒        │
│  └── TIME: 仿真时钟推进                     │
├─────────────────────────────────────────────┤
│  OSAL POSIX hooks                            │
│  ├── 任务: 创建/切换时暂停                   │
│  ├── 队列: 收发时暂停                        │
│  └── 信号量: 操作时暂停                      │
├─────────────────────────────────────────────┤
│  PSP 时基适配                                │
│  ├── soft_timebase: 跳过硬件定时器           │
│  └── posix_clock: 返回仿真时间               │
├─────────────────────────────────────────────┤
│  SCH 调度器 hook                             │
│  └── 步进模式下调度槽同步                    │
└─────────────────────────────────────────────┘
```

## 验证结果

- **Stepping 模式:** `make SIMULATION=native CFE_SIM_STEPPING=1 ENABLE_UNIT_TESTS=true` → 121/121 测试通过 ✅
- **Non-stepping 模式:** `make SIMULATION=native ENABLE_UNIT_TESTS=true` → 117/117 测试通过 ✅
- **零影响确认:** 未启用 `CFE_SIM_STEPPING` 时，所有条件编译代码不参与编译

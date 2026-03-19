# 项目知识库

**生成时间:** 2026-03-05  
**提交版本:** b4ec679  
**分支:** main (Draco / v7.0.0)

## 概述

NASA 核心飞行系统 (cFS) — 用于航天器的可复用飞行软件架构框架。本仓库是 git 子模块的**集成包**（不是飞行发行版）。Lab 应用仅作为示例；实际任务需添加任务特定的应用程序。

## 目录结构

```
cFS/
├── cfe/              # 核心飞行执行层 (ES, EVS, SB, TBL, TIME, FS, MSG)
├── osal/             # 操作系统抽象层 (POSIX, RTEMS, VxWorks, QNX)
├── psp/              # 平台支持包 (pc-linux, pc-rtems, mcp750-vxworks 等)
├── apps/             # 实验室/示例应用 (ci_lab, to_lab, sch_lab, sample_app)
├── libs/             # 共享库 (sample_lib)
├── tools/            # 地面系统 (Python/PyQt5), elf2cfetbl, tblCRCTool
├── sample_defs/      # 任务配置 (targets.cmake, 启动脚本, 工具链)
├── Makefile          # CMake 的 GNU Make 包装器
└── .github/workflows # 17 个 CI 工作流 (构建、测试、覆盖率、静态分析)
```

## 查找指南

| 任务 | 位置 | 说明 |
|------|------|------|
| 添加新应用 | `apps/`, `sample_defs/targets.cmake`, `sample_defs/cpu1_cfe_es_startup.scr` | 添加到 targets.cmake 的 APPLIST 和启动脚本 |
| cFE 公共 API | `cfe/modules/core_api/fsw/inc/` | ES, EVS, SB, TBL, TIME, FS, MSG 头文件 |
| OSAL API | `osal/src/os/inc/` | 任务、队列、信号量、文件、定时器抽象 |
| PSP API | `psp/fsw/inc/` | 内存、看门狗、异常、时间硬件 |
| 任务配置 | `sample_defs/` | 重命名/复制为 `<name>_defs/` 用于您的任务 |
| 构建系统 | `cfe/cmake/`, `Makefile` | mission_build.cmake 协调所有内容 |
| CI 流水线 | `.github/workflows/` | build-cfs.yml 是主流水线 |
| 应用模板 | `apps/sample_app/` | 复制并重命名以创建新应用 |
| 地面站 | `tools/cFS-GroundSystem/` | Python3 + PyQt5 + PyZMQ |
| 表工具 | `tools/elf2cfetbl/`, `tools/tblCRCTool/` | ELF→表镜像, CRC 验证 |

## 架构

```
┌─────────────────────────────────────┐
│  应用程序 (sample_app, ci_lab)       │  ← 您的代码在这里
├─────────────────────────────────────┤
│  cFE 核心 (ES│EVS│SB│TBL│TIME│FS)    │  ← 飞行执行服务
├─────────────────────────────────────┤
│  OSAL (任务│队列│文件│定时器)        │  ← 操作系统无关的 API
├─────────────────────────────────────┤
│  PSP (内存│看门狗│异常)              │  ← 硬件/平台层
├─────────────────────────────────────┤
│  操作系统 (Linux│RTEMS│VxWorks│QNX)  │  ← 目标操作系统
└─────────────────────────────────────┘
```

每层只调用其直接下方的层。应用程序**绝不**直接调用 OSAL/PSP — 始终通过 cFE API。

## 应用生命周期

```
Main() {
    CFE_ES_PerfLogEntry(PERF_ID);
    Init();  // EVS_Register, SB_CreatePipe, SB_Subscribe, TBL_Register
    while (CFE_ES_RunLoop(&RunStatus)) {
        CFE_ES_PerfLogExit(PERF_ID);
        CFE_SB_ReceiveBuffer(&BufPtr, Pipe, PEND_FOREVER);
        CFE_ES_PerfLogEntry(PERF_ID);
        TaskPipe(BufPtr);  // 根据 MsgId 分发 → 命令处理器
    }
    CFE_ES_ExitApp(RunStatus);
}
```

**分发模式:** `CFE_MSG_GetMsgId` → 按 MsgId 路由（使用 `CFE_SB_MsgId_Equal`，**绝不使用 `==`**）→ `CFE_MSG_GetFcnCode` → switch 到命令处理器 → 验证长度 → 执行 → 递增计数器。

## 编码规范

- **C 风格:** Allman 大括号风格, 4 空格缩进, 无制表符, 120 列限制 (`.clang-format`, CI 强制执行)
- **指针对齐:** 右对齐 (`char *ptr` 而非 `char* ptr`)
- **命名:** `MODULE_FunctionName`, `MODULE_Data_t` (结构体), `MODULE_CONSTANT` (宏)
- **返回码:** `CFE_SUCCESS` (0), `CFE_STATUS_*` 负数错误码。始终检查返回值。
- **MsgId 比较:** `CFE_SB_MsgId_Equal()` — **绝不使用原始 `==`**
- **MsgId 转换:** `CFE_SB_ValueToMsgId()` / `CFE_SB_MsgIdToValue()` 用于数值 ↔ 类型转换
- **表访问:** `TBL_GetAddress` → 还要检查 `INFO_UPDATED` → 使用 → `TBL_ReleaseAddress`
- **EVS 格式字符串:** 不使用浮点转换 (`%f`, `%g`) — 不支持
- **文件句柄:** 始终关闭。绝不保留为空/空白。
- **包含排序:** 否 (`SortIncludes: false` 在 clang-format 中)

## 反面模式 (此项目)

- **切勿**使用 `==` 比较 `CFE_SB_MsgId_t` — 使用 `CFE_SB_MsgId_Equal()`
- **切勿**在 EVS 事件格式字符串中使用 `%f`/`%g`
- **切勿**编辑自动生成的文件: `target_config.c`, `cfeconfig_platformdata_tool.c`
- **切勿**直接从应用调用 OSAL/PSP — 始终使用 cFE API 层
- **切勿**使用已弃用的 API（使用 `OMIT_DEPRECATED=true` 构建以强制执行）
- **切勿**在新代码中使用 `cfe_endian.h` 宏（仅向后兼容）
- **切勿**将 UT 专用模式泄漏到飞行代码中（ut-stubs 仅用于测试）
- **避免**原始 `strcpy`/`strcat`/`sprintf` — 使用有界变体 (`snprintf`, `strncpy`)

## 命令

```bash
# 构建 (调试, 模拟)
make SIMULATION=native prep
make
make install
cd build/exe/cpu1 && ./core-cpu1

# 构建 (发布, 无弃用 API)
make BUILDTYPE=release OMIT_DEPRECATED=true prep
make install

# 单元测试
make SIMULATION=native ENABLE_UNIT_TESTS=true prep
make
make install     # 运行测试前必须安装
make test        # 通过 CTest 运行
make lcov        # 覆盖率报告 → build/lcov/

# 清理
make clean       # 移除构建产物
make distclean   # 移除整个 build/ 目录

# 文档
make doc          # 所有文档
make usersguide   # cFE 用户指南
make osalguide    # OSAL 用户指南
```

## 子模块映射

| 子模块 | 远程仓库 | 用途 |
|--------|----------|------|
| `cfe` | nasa/cFE | 核心飞行执行层 |
| `osal` | nasa/osal | 操作系统抽象层 |
| `psp` | nasa/PSP | 平台支持包 |
| `apps/ci_lab` | nasa/ci_lab | 命令输入实验室应用 |
| `apps/to_lab` | nasa/to_lab | 遥测输出实验室应用 |
| `apps/sch_lab` | nasa/sch_lab | 调度器实验室应用 |
| `apps/sample_app` | nasa/sample_app | 示例应用 |
| `libs/sample_lib` | nasa/sample_lib | 示例共享库 |
| `tools/cFS-GroundSystem` | nasa/cFS-GroundSystem | Python 地面站 |
| `tools/elf2cfetbl` | nasa/elf2cfetbl | ELF→表镜像工具 |
| `tools/tblCRCTool` | nasa/tblCRCTool | 表 CRC 计算器 |

## 注意事项

- 这是**集成包仓库** — 每个子模块有自己的 issue/PR/发布
- 启动脚本格式: `ObjType, Filename, EntryPoint, CFE_Name, Priority, StackSize, LoadAddr, ExceptionAction` — `!` 标记 EOF
- 任务配置目录: 任何 `*_defs/` 目录。多个 → 显式设置 `MISSIONCONFIG`
- 交叉编译: 使用 `sample_defs/` 中的工具链文件 (RTEMS, VxWorks, QNX, ARM, PPC, LEON3)
- 地面系统需要: Python3, PyQt5, pyzmq。监听 UDP 2234 用于遥测
- `cmdUtil`（`tools/cFS-GroundSystem/Subsystems/cmdUtil/` 中的 C 工具）用于脚本化命令注入

## 开发约定

### 对话语言
- **所有的对话都使用中文** — 用户与助手之间的所有交互均使用中文进行

### 文档语言
- **生成的文档使用中文** — 所有自动生成的文档、README、注释等应优先使用中文编写

### 代码注释规范
- **每次新增的代码都应该有doxygen风格的中文注释** — 所有新添加的函数、变量、结构体、宏等必须使用Doxygen格式进行中文注释
  ```c
  /**
   * @brief 函数简要描述
   * @param[in] param1 参数1说明
   * @param[out] param2 参数2说明
   * @return 返回值说明
   * @retval CFE_SUCCESS 成功
   * @retval CFE_STATUS_* 错误码
   */
  CFE_Status_t MODULE_FunctionName(type param1, type *param2);
  ```

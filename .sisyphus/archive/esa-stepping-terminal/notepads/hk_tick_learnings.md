# HK Tick Logging 学习记录

## 任务完成摘要

在 cFS sample_app 中为 `SendHkCmd()` 添加了 EVS 定时日志，用于在 `CFE_SIM_STEPPING=1` 步进模式下观测外部控制器的控制节奏。

## 核心实现模式

### 1. 事件 ID 定义
- 位置：`apps/sample_app/fsw/inc/sample_app_eventids.h`
- 新增：`#define SAMPLE_APP_HK_TICK_INF_EID   13`
- 关键：避免与现有 EID 冲突（前面最大为 12）
- 约定：一个 EID 对应一种事件类型

### 2. 全局计数器字段
- 位置：`apps/sample_app/fsw/src/sample_app.h` 中的 `SAMPLE_APP_Data_t` 结构体
- 新增：`uint32 HkTickCounter;`
- 关键：
  - 使用 `uint32` 类型，可以计数 40 亿+ 次 HK tick
  - 位置：紧跟在 `ErrCounter` 之后，保持相关计数器聚集
  - 不需要重置（除非 app 重启）— 步进模式关注的是 tick 频率而非绝对数值

### 3. EVS 日志记录
- 位置：`apps/sample_app/fsw/src/sample_app_cmds.c` 中 `SAMPLE_APP_SendHkCmd()` 函数开头
- 实现：
  ```c
  SAMPLE_APP_Data.HkTickCounter++;
  CFE_EVS_SendEvent(SAMPLE_APP_HK_TICK_INF_EID, CFE_EVS_EventType_INFORMATION,
                    "SAMPLE: HK Tick #%lu CmdCnt=%u ErrCnt=%u",
                    (unsigned long)SAMPLE_APP_Data.HkTickCounter,
                    SAMPLE_APP_Data.CmdCounter, SAMPLE_APP_Data.ErrCounter);
  ```
- 关键规范：
  - **禁止用 `%f`/`%g`** — cFE EVS 不支持浮点格式化
  - `uint32` 必须用 `%lu` + `(unsigned long)` 强制转换
  - 格式字符串简洁清晰（便于日志解析）

## cFS 编码规范应用

### 命名约定
- 所有新增标识符均使用 `SAMPLE_APP_` 前缀（`SendHkCmd`、`HkTickCounter`、`HK_TICK_INF_EID`）
- 遵循既有代码风格：Allman 大括号、4 空格缩进

### 中文注释规范
- 项目要求所有新增代码使用 Doxygen 风格中文注释
- 成员变量：行尾 `/**< @brief ... */`
- 宏定义：行前 `/** @brief ... */`
- 函数注释：块注释 `/* ** ... */`

### 构建验证
- `make` 编译通过，无错误
- `make install` 安装完成
- 构建系统（CMakeLists.txt 等）无需修改 — 设计良好的 cFS 不需要为每次功能添加而修改构建脚本

## 步进模式应用场景

这个实现为外部仿真控制器（如 `esa_stepping_terminal`）提供了**实时反馈通道**：

1. **快速步进**：外部控制器快速发送 `begin`/`wait` 命令 → SCH 频繁触发 HK 请求 → EVS 日志密集出现 → 控制器实时观测到步进速率
2. **暂停/减速**：外部控制器暂停 → HK tick 日志停止 → 控制器确认系统暂停

这避免了等待日志文件 dump 的延迟，实现了**同步节奏观测**。

## 验证检查清单

- ✓ 三个文件修改正确（eventids.h、sample_app.h、sample_app_cmds.c）
- ✓ 构建成功（no errors）
- ✓ 安装完成
- ✓ 新增代码符合 cFS 编码规范（格式字符串、命名、注释）
- ✓ 无依赖项修改（CMakeLists.txt 等）

## 后续扩展建议

1. **性能计数**：如需统计 HK tick 频率变化趋势，可在地面站工具中解析 EVS 日志时间戳
2. **错误追踪**：如 HK tick 突然中断，可由 EVS 日志缺失推断出故障
3. **多应用支持**：同样的模式可复制到其他需要步进节奏反馈的应用

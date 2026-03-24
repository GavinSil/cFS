# cFS 步进模式测试结果

**测试日期：** 2026-03-23  
**测试人员：** 自动化验证  
**cFS 版本：** v7.0.0 (Draco)，commit `git:v7.0.0-42-g3af7f3d-dirty`  
**日志文件：** `/tmp/cfs_run3.log`（第一轮）、`docs/stepping_test/cfs_timeout_test.log`（超时测试）

---

## 1. 测试环境确认

```
EVS Port1 1980-012-14:03:20.55412 66/1/SAMPLE_APP 1: Sample App Initialized.
EVS Port1 1980-012-14:03:20.55592 66/1/SCH 1: SCH Initialized. Version 2.2.1.0
1980-012-14:03:22.50891 CFE_ES_Main: CFE_ES_Main entering OPERATIONAL state
```

cFS 以步进模式正常启动，SCH 和 SAMPLE_APP 均成功加载。

---

## 2. 场景 A：控制器暂停 — 日志停止

**测试时间：** 约 21:40  
**步骤：** 记录 Tick 基准数 → 停止推进 5 秒 → 再次统计

| 时间点 | HK Tick 条数 |
|--------|-------------|
| 暂停前 | 6 |
| 暂停 5 秒后 | 6 |
| **新增条数** | **0** |

**结论：✅ 通过** — 控制器暂停期间，`sample_app` 没有产生任何新的 HK Tick 日志。

---

## 3. 场景 B：快速推进 300 步 — 日志按比例产生

**测试时间：** 约 21:46  
**步骤：** 记录基准 → 执行 `repeat 300 200` → 统计新增

| 指标 | 值 |
|------|-----|
| 推进步数 | 300 步 |
| 耗时 | 6,819 ms（约 6.8 秒） |
| 推进前 Tick 数 | 6 |
| 推进后 Tick 数 | 9 |
| **新增 Tick 数** | **3** |
| 理论预期（300步/100slots） | 3 |

**结论：✅ 通过** — 300 步产生了精确的 3 条 HK Tick，与理论值完全吻合。

---

## 4. 完整日志记录

以下为本次测试中产生的全部 HK Tick 条目：

```
EVS Port1 1980-012-11:34:47.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #1 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:34:48.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #2 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:34:49.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #3 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:34:50.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #4 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:34:51.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #5 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:34:52.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #6 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:34:53.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #7 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:34:54.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #8 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:34:55.07899 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #9 CmdCnt=0 ErrCnt=0
```

**观察：**
- 仿真时间戳相邻两条间隔恰好 1 仿真秒（`11:34:47` → `11:34:48` → ...）
- 计数器 `#1` ~ `#9` 严格单调递增，无跳号
- `CmdCnt=0 ErrCnt=0` 表明测试期间无外部命令干扰

---

## 5. 场景 C：步进超时测试（独立轮次）

**日志文件：** `docs/stepping_test/cfs_timeout_test.log`  
**测试时间：** 约 21:53

### 5.1 测试步骤

| 阶段 | 操作 | 超时设置 | 结果 |
|------|------|---------|------|
| 基准建立 | `repeat 150 2000` | 2000ms | 全部成功，产生 Tick #1~#3 |
| 故意触发超时 | `repeat 200 1` | **1ms** | 第 19 步 `wait 结果: 超时` |
| 恢复挂起 | `wait 5000` | 5000ms | `wait 结果: 成功` |
| 恢复后验证 | `repeat 200 2000` | 2000ms | 全部成功，新增 Tick #4~#5 |

### 5.2 超时触发时的工具侧输出

```
[19/200] begin 结果: 成功
wait 结果: 超时
```

### 5.3 超时触发时的 cFS 侧诊断

cFS 内部记录了对应的超时事件（`detail_a=1` 对应 1ms 超时参数）：

```
CFE_PSP: SIM_STEPPING_DIAG class=timeout status=-4 site=InProc_WaitStepComplete detail_a=1 detail_b=1
```

### 5.4 恢复后的 HK Tick 日志

```
EVS Port1 1980-012-11:20:41.07611 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #1 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:20:42.07611 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #2 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:20:43.07611 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #3 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:20:44.07611 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #4 CmdCnt=0 ErrCnt=0
EVS Port1 1980-012-11:20:45.07611 66/1/SAMPLE_APP 13: SAMPLE: HK Tick #5 CmdCnt=0 ErrCnt=0
```

**结论：✅ 通过** — 超时后通过 `wait` 命令可完全恢复，cFS 无需重启，恢复后正常推进无异常。

### 5.5 超时恢复操作指南

```bash
# 当出现 "wait 结果: 超时" 后执行：
printf 'wait 5000\n' | /workspace/cFS/tools/esa_stepping_terminal/build/esa_stepping_terminal
```

**推荐超时设置：**
- 稳定推进：≥2000ms
- 快速推进：≥500ms
- 强制超时测试：≤50ms（会在负载波动时触发）

---

## 6. 总体结论

| 测试场景 | 预期 | 实际 | 结果 |
|---------|------|------|------|
| 暂停 5 秒，无推进 | 新增 Tick = 0 | 新增 Tick = 0 | ✅ 通过 |
| 快速推进 300 步 | 新增 Tick = 3 | 新增 Tick = 3 | ✅ 通过 |
| Tick 计数器递增 | 严格单调 | #1→#9 无跳号 | ✅ 通过 |
| 步进响应成功率 | 全部成功 | 300/300 成功 | ✅ 通过 |
| 超时触发（1ms） | 第 N 步超时 | 第 19 步超时 | ✅ 通过 |
| 超时后恢复推进 | 无需重启 | wait 后正常恢复 | ✅ 通过 |

**结论：步进模式下外部控制器完全控制 `sample_app` 的输出节奏。控制器暂停则日志停止，控制器推进则日志按步进速率精确产生，节奏耦合关系完全符合设计预期。超时后通过 `wait` 命令可无损恢复，无需重启 cFS。**

# cFS 步进模式测试方法

**测试日期：** 2026-03-23  
**测试环境：** Ubuntu Linux，cFS v7.0.0 (Draco)，仿真步进模式（`CFE_SIM_STEPPING=1`）

---

## 1. 测试目的

验证在 `CFE_SIM_STEPPING=1` 步进模式下，外部控制器的推进节奏能否直接控制 `sample_app` 的 EVS 日志输出，具体包括：

1. 控制器**暂停**时，`sample_app` 输出是否停止
2. 控制器**快速推进**时，`sample_app` 输出是否加速

---

## 2. 测试前提条件

### 2.1 软件配置

| 组件 | 修改内容 |
|------|----------|
| `apps/sample_app/fsw/src/sample_app_cmds.c` | `SAMPLE_APP_SendHkCmd()` 中添加 EVS 日志：`SAMPLE: HK Tick #N CmdCnt=X ErrCnt=Y` |
| `apps/sample_app/fsw/src/sample_app.h` | `SAMPLE_APP_Data_t` 中添加 `uint32 HkTickCounter` 字段 |
| `apps/sample_app/fsw/inc/sample_app_eventids.h` | 添加 `SAMPLE_APP_HK_TICK_INF_EID = 13` |
| `apps/sch/fsw/tables/sch_def_msgtbl.c` | command ID #16 设为 `SAMPLE_APP_SEND_HK_MID (0x1883)` |
| `apps/sch/fsw/tables/sch_def_schtbl.c` | slot #7 设为 `SCH_ENABLED, Frequency=1, Remainder=0, MsgIdx=16` |
| `esa/fsw/src/esa_stepping.c` | 每次步进时调用 `OS_BinSemGive("SCH_TIME_SEM")` 唤醒 SCH |

### 2.2 构建与启动

```bash
# 构建
cd /workspace/cFS
make SIMULATION=native prep
make && make install

# 启动 cFS（步进模式）
cd build/exe/cpu1
./core-cpu1 > /tmp/cfs_run.log 2>&1 &

# 等待进入 OPERATIONAL 状态（约 5 秒）
sleep 6
grep "OPERATIONAL" /tmp/cfs_run.log
```

### 2.3 步进工具

使用 `tools/esa_stepping_terminal`，通过 Unix socket `/tmp/cfe_sim_stepping.sock` 控制 cFS 时间推进。

**常用命令：**

```bash
# 单步推进（timeout_ms 为等待响应超时）
echo 'step <timeout_ms>' | /workspace/cFS/tools/esa_stepping_terminal/build/esa_stepping_terminal

# 重复推进 N 步，每步超时 T 毫秒
echo 'repeat <N> <T>' | /workspace/cFS/tools/esa_stepping_terminal/build/esa_stepping_terminal

# 清除挂起状态（超时后恢复）
printf 'wait 3000\n' | /workspace/cFS/tools/esa_stepping_terminal/build/esa_stepping_terminal
```

---

## 3. 触发机制说明

```
外部控制器
    │
    │ begin（每次步进）
    ▼
esa_stepping.c → OS_BinSemGive("SCH_TIME_SEM")
    │
    ▼
SCH 调度器被唤醒 → 推进 1 个 minor frame slot
    │
    │ 每 100 次步进 = 1 次完整轮转（100 slots）
    │ slot #7：SCH_ENABLED, Frequency=1, Remainder=0
    ▼
SCH 发送 SAMPLE_APP_SEND_HK_MID (0x1883)
    │
    ▼
SAMPLE_APP_SendHkCmd() 执行
    │
    ▼
EVS 输出：SAMPLE: HK Tick #N CmdCnt=X ErrCnt=Y
```

**关键参数：**
- 每 **100 次步进** 精确触发 **1 条** HK Tick 日志
- `Frequency=1, Remainder=0`：每次完整轮转（`TablePassCount % 1 == 0`）都触发

---

## 4. 测试场景设计

### 场景 A：暂停测试

**目标：** 验证不推进时日志停止

**方法：**
1. 记录当前 HK Tick 条数（基准值）
2. 停止所有步进，等待 5 秒
3. 再次统计 HK Tick 条数
4. 对比：新增条数应为 **0**

**命令序列：**
```bash
BEFORE=$(grep "HK Tick" /tmp/cfs_run.log | wc -l)
sleep 5
AFTER=$(grep "HK Tick" /tmp/cfs_run.log | wc -l)
echo "新增 Tick: $((AFTER - BEFORE))"
```

### 场景 B：快速推进测试

**目标：** 验证快速推进时日志密集产生

**方法：**
1. 记录基准 Tick 数
2. 执行 300 步，超时设为 200ms（尽量快）
3. 统计新增 Tick 数
4. 验证：300 步 / 100 slots = **3 个完整轮转** → 新增 **3 条** Tick

**命令序列：**
```bash
BEFORE=$(grep "HK Tick" /tmp/cfs_run.log | wc -l)
START=$(date +%s%N)
echo 'repeat 300 200' | /workspace/cFS/tools/esa_stepping_terminal/build/esa_stepping_terminal
END=$(date +%s%N)
ELAPSED=$(( (END - START) / 1000000 ))
AFTER=$(grep "HK Tick" /tmp/cfs_run.log | wc -l)
echo "耗时: ${ELAPSED}ms，新增 Tick: $((AFTER - BEFORE))"
```

### 场景 C：完整对比测试

**目标：** 在同一次运行中依次验证暂停→快速推进的节奏变化

**方法：**
1. 确认 cFS 处于暂停状态（不推进）
2. 等待 5 秒，确认无新 Tick
3. 立即执行 300 步快速推进
4. 对比两阶段日志密度

---

## 5. 验证指标

| 指标 | 检查方法 | 预期值 |
|------|----------|--------|
| 暂停期间无新 Tick | `grep "HK Tick" log \| wc -l` | 新增 = 0 |
| 300 步产生 Tick 数 | 步进前后 Tick 数之差 | 新增 = 3 |
| Tick 计数器递增 | 日志中 `#N` 编号 | 严格单调递增 |
| 步进响应状态 | 步进工具输出 | 全部 `成功`，无 `超时` |

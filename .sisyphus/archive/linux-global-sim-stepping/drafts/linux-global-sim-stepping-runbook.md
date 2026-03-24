# Linux Native cFS 全局仿真步进运行手册

本文档描述 Linux-only cFS 全局仿真步进模式的使用方法、控制通道接口、证据规范及故障排查指南。

**版本**: T15 (对应实现阶段 T1-T14 已完成)  
**适用平台**: Linux/POSIX native 仿真 (SIMULATION=native)  
**构建开关**: CFE_SIM_STEPPING  

---

## 1. 构建与安装

### 1.1 启用步进模式的构建

```bash
# 清理之前的构建
make distclean

# 配置并构建 (启用步进模式)
make SIMULATION=native CFE_SIM_STEPPING=ON prep
make
make install
```

### 1.2 默认构建 (不包含步进逻辑)

```bash
# 标准 native 构建 (不包含任何步进代码)
make SIMULATION=native prep
make
make install
```

**验证步进符号是否隔离**:

```bash
# 步进构建应包含步进符号
nm build/native/default_cpu1/cpu1/core-cpu1 | grep -E "CFE_SIM_STEPPING|SimStep"

# 默认构建不应包含步进符号
nm build/exe/cpu1/core-cpu1 | grep -E "CFE_SIM_STEPPING|SimStep" && echo "FAIL" || echo "PASS"
```

### 1.3 单元测试与覆盖率构建

```bash
# 启用单元测试和步进模式
make SIMULATION=native ENABLE_UNIT_TESTS=true CFE_SIM_STEPPING=ON prep
make
make install
make test

# 生成覆盖率报告
make lcov
# 报告输出位置: build/lcov/
```

### 1.4 关键输出路径

| 产物 | 路径 | 说明 |
|------|------|------|
| 主可执行文件 | `build/native/default_cpu1/cpu1/core-cpu1` | 步进模式主程序 |
| 运行时目录 | `build/exe/cpu1/` | 启动脚本和共享库位置 |
| 命令工具 | `build/exe/host/cmdUtil` | 命令注入工具 |
| 备用 cmdUtil | `tools/cFS-GroundSystem/Subsystems/cmdUtil/cmdUtil` | 源码编译版本 |

**运行时工作目录要求**: 必须从 `build/exe/cpu1/` 目录启动,否则无法找到启动脚本和共享库。

---

## 2. 启动与进入步进模式

### 2.1 启动 cFS

```bash
cd build/exe/cpu1
./core-cpu1
```

**关键启动日志标记**:

```
CI_LAB listening on UDP port: 1234    # CI_LAB 就绪
TO Lab Initialized                     # TO_LAB 就绪
CFE_ES_Main entering OPERATIONAL state # 系统进入运行态
```

### 2.2 Ready Barrier (就绪屏障)

cFS 启动分为两个阶段:

1. **启动阶段**: 系统正常初始化,各服务按标准顺序加载
2. **步进阶段**: 外部引擎可通过控制通道驱动步进

**ready barrier 条件**:
- 所有核心服务 (ES, EVS, SB, TBL, TIME) 进入主循环
- SCH 调度器等待 minor-frame 触发
- Unix domain socket 控制通道就绪

**验证 socket 就绪**:

```bash
# 默认 socket 路径
ls -la /tmp/cfe_sim_stepping.sock
```

### 2.3 控制通道

系统支持两种控制通道,共享同一状态机:

| 通道类型 | 接口方式 | 适用场景 |
|----------|----------|----------|
| inproc | 同进程函数调用 | 嵌入式驱动、测试框架集成 |
| Unix domain socket | `/tmp/cfe_sim_stepping.sock` | 独立进程外部引擎 |

**UDS 协议 (最小)**:

```
请求格式 (8 bytes):
  byte 0: opcode (1=BEGIN_STEP, 2=QUERY_STATE, 3=WAIT_COMPLETE)
  byte 1-3: reserved padding (xxx)
  byte 4-7: timeout_ms (uint32 LE)
  对应 Python struct.pack('<BxxxI', opcode, timeout_ms)

响应格式 (4 bytes for BEGIN/WAIT, 12 bytes for QUERY):
  BEGIN/WAIT: int32 status (0=SUCCESS, 负值=错误码)
  QUERY: int32 status + uint32 state + uint32 trigger_count
```

---

## 3. Step 驱动操作

### 3.1 Step 粒度

- **基本单位**: SCH minor frame (次帧)
- **默认时长**: 10ms 仿真时间/quantum
- **推进方式**: 显式 step 请求驱动

### 3.2 UDS 驱动示例

```python
import socket
import struct

SOCK_PATH = "/tmp/cfe_sim_stepping.sock"

def step_once(timeout_ms=2000):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(3.0)
    s.connect(SOCK_PATH)
    
    # BEGIN_STEP
    s.sendall(struct.pack('<BxxxI', 1, 0))
    begin_resp = s.recv(4)
    begin_status = struct.unpack('<i', begin_resp)[0]
    
    # WAIT_COMPLETE
    s.sendall(struct.pack('<BxxxI', 3, timeout_ms))
    wait_resp = s.recv(4)
    wait_status = struct.unpack('<i', wait_resp)[0]
    
    # QUERY_STATE
    s.sendall(struct.pack('<BxxxI', 2, 0))
    query_resp = s.recv(12)
    query_status, state, triggers = struct.unpack('<iII', query_resp)
    
    s.close()
    return begin_status, wait_status, state, triggers
```

### 3.3 动态 Wait-Set 语义

**核心原则**: 每个 step 只等待**本 step 实际被触发**的对象完成。

**流程**:
1. SCH minor-frame 触发,标记本次 step 的参与者 (dynamic trigger set)
2. 被触发的任务/服务处理消息
3. 各参与者完成时上报 ack/complete
4. 所有被触发对象完成后,step 返回

**触发来源示例**:
- SCH 发送 HK 请求消息 → 触发 ES/EVS/SB/TBL/TIME 核心服务
- SCH 发送应用消息 → 触发 sample_app 等应用
- 外部命令注入 → 触发对应服务

### 3.4 共享仿真时间的对象

以下对象共享统一的 `sim_time_ns` 时间源:

| 对象类型 | 共享方式 | 备注 |
|----------|----------|------|
| OS_TaskDelay 任务 | 阻塞等待仿真时间推进 | to_lab 等 delay-driven 应用 |
| Queue receive 边界 | 时间戳标记 | SB/ES/EVS/TBL 等 |
| BinSem wait 边界 | 时间戳标记 | SCH TimeSemaphore 等 |
| TIME 服务 | 统一时间源查询 | MET/STCF 等 |

### 3.5 默认排除的对象

**ES background task 默认不进入 wait-set**:

- ES 后台维护任务 (`cfe_es_backgroundtask.c`) 没有稳定的 step 级 request/ack/complete 语义
- 默认不参与 step 完成判定,避免阻塞 step 推进
- 后台任务仍正常运行,但不被 step 机制等待

---

## 4. 错误排查与诊断

### 4.1 错误码速查

| 错误码 | 名称 | 含义 |
|--------|------|------|
| 0 | SUCCESS | 操作成功 |
| -1 | ERROR | 通用错误 |
| -2 | DUPLICATE_BEGIN | step 已开始,重复 begin 请求 |
| -4 | TIMEOUT | wait 超时 |
| -5 | TRANSPORT_ERROR | UDS 传输错误 |
| -6 | ILLEGAL_STATE | 非法状态调用 |
| -7 | ILLEGAL_COMPLETE | 非法 complete 上报 |

### 4.2 常见故障场景

#### 4.2.1 timeout (超时)

**症状**: WAIT_COMPLETE 返回 -4 (TIMEOUT)

**可能原因**:
1. 被触发对象未完成处理 (卡住或死锁)
2. step 期望的 ack 数量与实际收到的不匹配
3. CI_LAB UDP 命令未在超时窗口内送达

**排查步骤**:

```bash
# 1. 启动时重定向日志到文件 (手动运行场景)
cd build/exe/cpu1 && ./core-cpu1 > /tmp/core.log 2>&1

# 2. 检查 core 日志中的 trigger/ack/complete 计数
grep -E "trigger|ack|complete" /tmp/core.log

# 3. 或使用回归脚本日志 (如已运行回归脚本)
grep -E "trigger|ack|complete" build/sim-stepping-regression/{RUN_ID}/scenario1_core.log

# 4. 检查当前 step 状态 (通过 QUERY_STATE)
# state 值: 0=INIT, 1=READY, 2=RUNNING, 3=WAITING, 4=COMPLETE

# 5. 检查是否缺少命令注入 (CI_LAB 需要 wall-clock 时间)
# 在命令发送后添加 ~0.8s 延迟再开始 stepping
```

**关键日志标记**:

```
InProc_WaitStepComplete timeout          # 超时发生
duplicate_begin detected                 # 重复 begin
trigger_count=N acks_expected=M          # 触发与期望不匹配
```

#### 4.2.2 duplicate step (重复 step)

**症状**: BEGIN_STEP 返回 -2 (DUPLICATE_BEGIN)

**可能原因**:
1. 前一个 step 尚未完成就发送了新 begin
2. 前一个 step 超时后未正确处理状态
3. 外部引擎与 cFS 状态不同步

**解决方案**:
- 确保每个 begin 对应一个 wait,等待完成后再发下一个 begin
- 检查 `query_triggers` 返回值,确认 step 真实完成
- 超时后建议查询状态,必要时重启 cFS

#### 4.2.3 ready barrier 未满足

**症状**: 启动后 socket 不存在或 step 立即失败

**排查步骤**:

```bash
# 1. 启动时重定向日志到文件
cd build/exe/cpu1 && ./core-cpu1 > /tmp/core.log 2>&1 &

# 2. 确认核心日志中出现 OPERATIONAL标记
grep "CFE_ES_Main entering OPERATIONAL state" /tmp/core.log

# 3. 确认 socket 文件已创建
ls -la /tmp/cfe_sim_stepping.sock

# 4. 检查 SCH 是否已加载
grep "SCH" /tmp/core.log | head -20
```

#### 4.2.4 ES background 相关问题

**注意**: ES background task 默认不进入 wait-set,这可能导致:
- 文件系统维护操作不受 step 控制
- 清理任务在 step 间隙异步执行

**验证方法**:

```bash
# 启动时重定向日志到文件
cd build/exe/cpu1 && ./core-cpu1 > /tmp/core.log 2>&1 &

# 检查日志中是否出现 background task 相关警告
grep -i "background" /tmp/core.log
```

#### 4.2.5 CI_LAB UDP 注入时序约束

**重要限制**: CI_LAB 的 UDP 命令注入不是纯 step 驱动的。

**原因**:
- CI_LAB 主循环使用 `CFE_SB_ReceiveBuffer(..., 500ms timeout)`
- UDP socket 读取发生在 SB 超时后
- 纯 stepping 推进仿真时间但不加速 wall-clock 超时

**正确用法**:

```bash
# 1. 启动 cFS
cd build/exe/cpu1 && ./core-cpu1 &

# 2. 等待 ready barrier
sleep 2  # 或使用日志检测

# 3. 发送命令后添加 wall-clock 延迟
./cmdUtil --pktid=0x1880 --cmdcode=1
sleep 0.8  # 必须等待 CI_LAB 服务 UDP

# 4. 开始 stepping
./stepping_client.py  # 你的 step 驱动脚本
```

**验证命令送达**:

```bash
# 检查 core 日志中的命令处理标记 (日志路径取决于你的重定向位置)
grep "Reset counters command" /tmp/core.log
grep "TO AddPkt" /tmp/core.log

# 或使用回归脚本日志
grep "Reset counters command" build/sim-stepping-regression/{RUN_ID}/scenario*_core.log
```

---

## 5. 证据规范

### 5.1 证据文件命名

证据文件统一存放于 `.sisyphus/evidence/` 目录:

```
.sisyphus/evidence/task-{N}-{slug}.txt
```

**命名规则**:
- `{N}`: 任务编号 (如 t1, t4, t12, t13)
- `{slug}`: 简短描述 (如 native-gate, timeout, tolab-steps)
- 扩展名: `.txt` 用于文本日志, `.json` 用于结构化数据

### 5.2 必含信息

每个证据文件应包含:

1. **构建信息**:
   ```
   Build: SIMULATION=native CFE_SIM_STEPPING=ON
   Target: build/native/default_cpu1/cpu1/core-cpu1
   Date: 2026-03-13
   ```

2. **验证命令及输出**:
   ```
   Command: ctest --test-dir build/native/default_cpu1 -R coverage-pspmod-sim_stepping
   Output: Test #N: coverage-pspmod-sim_stepping ... Passed
   ```

3. **运行时状态** (如适用):
   ```
   begin_status=0 wait_status=0 query_triggers=0
   state=COMPLETE (4)
   ```

### 5.3 QA 术语检索

以下术语用于自动化 QA 验证,文档中应明确包含:

- `SIMULATION=native` - 构建模式
- `Unix domain socket` - 控制通道类型
- `step mode` - 步进模式
- `Evidence` - 证据文件
- `timeout` - 超时错误
- `duplicate step` - 重复 step 错误
- `ES background` - ES 后台任务
- `ready barrier` - 就绪屏障

---

## 6. 已知限制与边界

### 6.1 当前已验证范围 (T1-T14)

| 功能 | 状态 | 验证方式 |
|------|------|----------|
| 构建门控 (CFE_SIM_STEPPING) | 已验证 | 构建测试 |
| hook/shim ABI | 已验证 | 代码审查 + 构建 |
| 统一仿真时间源 | 已验证 | 代码审查 |
| OS_TaskDelay 接管 | 已验证 | T13 观察 |
| Queue/BinSem 边界 | 已验证 | T5-T7 实现 |
| SCH minor-frame 驱动 | 已验证 | T6 实现 |
| 核心服务 wait-set | 已验证 | T19-T22 实现 |
| inproc 控制通道 | 已验证 | 单元测试 |
| UDS 控制通道 | 已验证 | 运行时测试 |
| ready barrier | 已验证 | T11 实现 |
| timeout/错误策略 | 已验证 | T12 实现 |
| to_lab 步进验证 | 已验证 | T13 观察 |

### 6.2 T14 运行回归脚本状态

**注意**: T14 完整的端到端运行回归脚本已完成。

**当前状态**:
- 脚本路径: `tools/sim_stepping_regression.sh`
- scenario1 (UDS success): 通过
- scenario2 (core HK runtime): 通过
- scenario3 (TO_LAB HK step-driven): 通过
- scenario4 (TO_LAB DataTypes step-driven): 通过

**使用脚本**:

```bash
cd /workspace/cFS
O=build ./tools/sim_stepping_regression.sh
```

**日志输出**:
- 运行日志: `build/sim-stepping-regression/{RUN_ID}/`
- scenario 日志: `scenario{N}_{name}.log`

### 6.3 平台限制

- **仅限 Linux/POSIX**: 不适用于 RTEMS、VxWorks 等平台
- **仅限 native 仿真**: 不适用于真实硬件目标
- **x86-64 验证**: 当前主要在 x86-64 平台验证

### 6.4 默认构建保证

- 非 `SIMULATION=native` 构建**不包含**任何步进代码
- 非 `CFE_SIM_STEPPING=ON` 的 native 构建**不包含**步进代码
- 步进符号**不泄漏**到默认构建中

---

## 7. 快速参考

### 7.1 常用命令

```bash
# 完整步进构建
make distclean
make SIMULATION=native CFE_SIM_STEPPING=ON prep
make && make install

# 运行 cFS
cd build/exe/cpu1 && ./core-cpu1

# 单元测试
make SIMULATION=native ENABLE_UNIT_TESTS=true CFE_SIM_STEPPING=ON prep
make && make install && make test

# 运行特定覆盖率测试
ctest --test-dir build/native/default_cpu1 -R coverage-pspmod-sim_stepping --output-on-failure

# 运行时回归
O=build ./tools/sim_stepping_regression.sh
```

### 7.2 关键路径汇总

| 用途 | 路径 |
|------|------|
| 步进可执行文件 | `build/native/default_cpu1/cpu1/core-cpu1` |
| 运行时工作目录 | `build/exe/cpu1/` |
| cmdUtil 工具 | `build/exe/host/cmdUtil` |
| UDS socket | `/tmp/cfe_sim_stepping.sock` |
| 运行时日志 | `build/sim-stepping-regression/{RUN_ID}/` |
| 证据目录 | `.sisyphus/evidence/` |

### 7.3 联系与反馈

本文档对应实现版本: T15  
计划文件: `.sisyphus/plans/linux-global-sim-stepping.md`  
学习笔记: `.sisyphus/notepads/linux-global-sim-stepping/`  

---

*文档生成时间: 2026-03-13*  
*对应代码版本: T1-T14 已完成*

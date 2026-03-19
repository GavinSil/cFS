# linux-global-sim-stepping 测试方法与运行回归场景说明

**版本**: 2026-03-13  
**对应阶段**: T1-T14 已完成 (2026-03-16 运行时回归通过)  
**关联文件**: `tools/sim_stepping_regression.sh`

---

## 1. 测试方法概述

### 1.1 测试分层结构

本项目采用四层验证体系,各层职责分明:

| 层级 | 验证目标 | 典型命令/手段 | 输出位置 |
|------|----------|---------------|----------|
| **Build** | 步进代码隔离性、编译正确性 | `make SIMULATION=native CFE_SIM_STEPPING=ON prep && make` | `build/native/default_cpu1/` |
| **Unit Test** | 模块级覆盖率、状态机语义 | `make ENABLE_UNIT_TESTS=true prep && make test` | `build/native/default_cpu1/` |
| **Runtime Regression** | 端到端步进行为、场景覆盖 | `./tools/sim_stepping_regression.sh` | `build/sim-stepping-regression/{RUN_ID}/` |
| **Evidence** | 可审计的验证记录 | 日志文件、截图、计数器输出 | `.sisyphus/evidence/` |

### 1.2 针对性运行时探针 vs 完整回归脚本

**针对性探针**适用于开发调试阶段:
- 快速验证单个假设 (如 "TO_LAB 是否在 step 20 后输出")
- 手动构造边界条件 (如 "重复 begin 是否返回 DUPLICATE_BEGIN")
- 使用临时脚本直接调用 UDS 或 inproc 接口

**完整回归脚本** (`sim_stepping_regression.sh`) 适用于:
- CI/CD 流水线集成
- 发布前全面验证
- 确保场景间无副作用 (自动清理进程、重置状态)

**关键区别**: 回归脚本强制串行执行四个场景,每个场景独立启动/停止 cFS 核心,确保状态隔离。

### 1.3 证据与日志文件位置

| 类型 | 路径示例 | 说明 |
|------|----------|------|
| 构建产物 | `build/native/default_cpu1/cpu1/core-cpu1` | 步进模式主程序 |
| 运行时日志 | `build/sim-stepping-regression/20260313-120000/scenario1_core.log` | 回归脚本自动收集 |
| UDP 遥测日志 | `build/sim-stepping-regression/{RUN_ID}/scenario{N}_{name}_udp.log` | Python 监听器输出 |
| 单元测试报告 | `build/lcov/` | `make lcov` 生成 |
| 证据文件 | `.sisyphus/evidence/task-t13-tolab-steps.txt` | 人工归档的关键验证点 |

### 1.4 为什么不能只依赖手动控制台观察

**手动观察的陷阱**:
1. **时序错觉**: 控制台输出有缓冲延迟,肉眼难以判断 packet 是在 step N 还是 step N+1 产生
2. **计数遗漏**: UDP 丢包、日志滚动可能导致关键事件被忽略
3. **状态不可见**: 步进核心内部状态 (READY/RUNNING/WAITING/COMPLETE) 不会直接输出到控制台
4. **环境敏感**: 不同终端宽度、颜色设置可能导致日志格式变化,影响可读性

**自动化证据的优势**:
- 日志文件可被 `grep`、`awk`、`python` 精确解析
- UDP 遥测被完整记录到文件,可事后统计 MID 出现次数
- 回归脚本使用 `wait_for_log` 等函数实现确定性等待,而非人工 sleep

---

## 2. 运行回归场景详解

回归脚本 `tools/sim_stepping_regression.sh` 包含四个顺序执行的场景,覆盖从基础连通性到应用级步进验证的完整范围。

### 2.1 scenario1: UDS 成功路径

**目的**  
验证最基本的 UDS 控制通道连通性和单步往返语义。这是所有后续场景的前置条件。

**触发/控制通道**  
使用 Unix Domain Socket (`/tmp/cfe_sim_stepping.sock`),协议格式:
- 请求: `struct.pack('<BxxxI', opcode, timeout_ms)` (8 bytes)
- 响应: `int32 status` (4 bytes for BEGIN/WAIT, 12 bytes for QUERY)

**执行步骤**  
1. 启动 cFS 核心 (`start_core`)
2. 等待 CI_LAB/TO_LAB/OPERATIONAL 就绪标记
3. 等待 UDS socket 文件出现
4. 发送单步请求 (`uds_step_once`):
   - BEGIN_STEP (opcode=1)
   - WAIT_COMPLETE (opcode=3, timeout=2000ms)
   - QUERY_STATE (opcode=2)
5. 停止核心

**观察内容**  
- UDS 连接是否成功建立
- 三个 opcode 的返回状态是否均为 0 (SUCCESS)
- 无超时、无传输错误

**通过/失败判定**  
- **通过**: `begin_status=0 && wait_status=0 && query_status=0`
- **失败**: 任何非零状态码,或 UDS 连接超时

**当前状态**: **已通过** (verified 2026-03-13)  
此场景自 T11 以来保持稳定,是回归脚本中最可靠的基线验证点。

---

### 2.2 scenario2: 核心 HK 运行时转发

**目的**  
验证步进模式下核心服务 (ES/EVS/SB/TBL/TIME) 的 HK 遥测能否被正确触发并转发到 TO_LAB 输出通道。

**触发/控制通道**  
- **控制**: UDS 步进驱动 (同 scenario1)
- **触发源**: SCH 调度器按默认 schedule table 发送 HK 请求消息
- **观测通道**: TO_LAB UDP 输出 (port 2234)

**执行步骤**  
1. 启动核心
2. 配置 TO_LAB 订阅核心 HK MID (`0x0801` EVS HK)
3. 启动 UDP 遥测监听器
4. 循环步进最多 60 次:
   - 执行 `uds_step_once`
   - 检查 UDP 日志中是否出现目标 MID
   - 出现后提前退出循环

**观察内容**  
- 在指定步数窗口内 (默认 60 steps) 是否观察到至少 1 个核心 HK 包
- 关注的 MID: `0x0800` (ES), `0x0801` (EVS), `0x0803` (SB), `0x0804` (TBL), `0x0805` (TIME)

**通过/失败判定**  
- **通过**: `core_count >= 1` 在步数上限内
- **失败**: 达到 `core_steps` (60) 仍未观察到任何核心 HK 包

**历史问题与修复**  
- **早期问题**: 最初使用错误的 MID (`0x0808` 等请求/事件 MID 而非 HK MID)
- **修复**: 更正为实际的 HK 遥测 MID (`0x0800-0x0805`)
- **步数调整**: 将 scenario2 本地步数上限从 40 提升到 60,以覆盖 SCH 默认 slot 布局 (HK 请求分布在 slot 3,13,23,33,43)

**当前状态**: **脚本级验证通过,非当前阻塞点** (as of 2026-03-13)  
scenario2 的脚本级代表性核心 HK 观察已成功 (historical note: 2026-03-13 时曾是关注点)。

---

### 2.3 scenario3: TO_LAB HK 步进驱动路径

**目的**  
验证 TO_LAB 应用的 HK 输出是否严格受步进控制,即:
- 在指定步数之前无输出 (验证 "不 step 不前进")
- 在预期步数有输出 (验证 "step 后确实前进")

**触发/控制通道**  
- **控制**: UDS 步进驱动
- **命令注入**: 通过 cmdUtil 发送 TO_LAB HK 请求 (MID `0x1881`)
- **观测通道**: TO_LAB UDP 输出 (port 2234),监听 MID `0x0880`

**执行步骤**  
1. 启动核心
2. 配置 TO_LAB 订阅自身 HK MID (`0x0880`)
3. 启动 UDP 监听器
4. 执行 1 个空步 (确保系统进入步进状态)
5. 发送 HK 请求命令 (`cmdUtil --pktid=0x1881`)
6. **关键检查点**: 立即统计 pre-step 包数,必须为 0
7. 循环步进最多 `TO_MAX_STEPS` (24) 次:
   - 执行 `uds_step_once`
   - 检查包数
   - 在 `TO_FIRST_PKT_STEP` (20) 之前包数必须为 0
   - 在 step 20 时包数必须 >= 1

**观察内容**  
- `pre` (步进前包数) 必须为 0
- `TO_FIRST_PKT_STEP` (20) 之前包数保持 0
- step 20 时至少观察到 1 个 `0x0880` 包

**通过/失败判定**  
- **通过**: `pre == 0` &&  step 20 时 `cnt >= 1`
- **失败**: 
  - `pre != 0` (步进前就有输出)
  - step < 20 时出现包 (提前输出)
  - step 20 时 `cnt < 1` (缺失输出)

**历史问题与修复**  
- **早期问题**: "premature pre-step output" - 在显式步进前就观察到 TO_LAB HK 输出
- **根本原因**: SCH minor-frame 回调在 `session_active` 之前就执行了 quantum 推进
- **修复**: 在 `ReportSchMinorFrame()` 中添加 `session_active` 和 `READY` 状态门控

**当前状态**: **已通过** (historical note: 2026-03-13 时曾是阻塞点,在后续修复后于 2026-03-16 通过)

**已知约束**  
CI_LAB UDP 命令注入有 wall-clock 时序依赖:
- CI_LAB 主循环使用 `CFE_SB_ReceiveBuffer(..., 500ms timeout)`
- UDP socket 读取发生在 SB 超时后
- 纯 stepping 推进仿真时间但不加速 wall-clock 超时
- 因此命令发送后需要约 0.8s 的 wall-clock 延迟才能确保送达

---

### 2.4 scenario4: TO_LAB DataTypes 步进驱动路径

**目的**  
验证 TO_LAB 的 DataTypes 遥测输出 (MID `0x0881`) 是否同样严格受步进控制。这是 scenario3 的补充,覆盖不同类型的遥测数据。

**触发/控制通道**  
- **控制**: UDS 步进驱动
- **命令注入**: `cmdUtil --pktid=0x1880 --cmdcode=3` (Send DataTypes)
- **观测通道**: TO_LAB UDP 输出,监听 MID `0x0881`

**执行步骤**  
1. 启动核心
2. 配置 TO_LAB 订阅 DataTypes MID (`0x0881`)
3. 启动 UDP 监听器
4. 发送 SendDataTypes 命令
5. **关键检查点**: 立即统计 pre-step 包数,必须为 0
6. 循环步进最多 24 次,逻辑同 scenario3

**观察内容**  
- `pre` 必须为 0
- step 20 之前包数保持 0
- step 20 时至少观察到 1 个 `0x0881` 包

**通过/失败判定**  
- **通过**: `pre == 0` && step 20 时 `cnt >= 1`
- **失败**: 提前输出或缺失输出

**当前状态**: **已通过** (historical note: 2026-03-13 时待 scenario3 解决后验证,已于 2026-03-16 通过)

---

## 3. 运行回归整体状态总结

### 3.1 各场景当前状态

| 场景 | 目的 | 当前状态 | 备注 |
|------|------|----------|------|
| scenario1 | UDS 成功路径 | **已通过** | 基线稳定 |
| scenario2 | 核心 HK 运行时转发 | **脚本级通过** | 代表性核心 HK 观察成功,非当前阻塞点 |
| scenario3 | TO_LAB HK 步进驱动 | **已通过** | (historical: 曾超时阻塞,已修复) |
| scenario4 | TO_LAB DataTypes 步进驱动 | **已通过** | (historical: 依赖 scenario3 修复) |

### 3.2 T14 运行回归整体进度

**声明**: T14 运行回归工作已于 2026-03-16 全部通过。

- T15 运行手册 (本文档的姊妹文档) 已经完成
- 回归脚本的基础设施 (`start_core`, `uds_step_once`, `count_mid` 等) 已经完善
- scenario1/scenario2/scenario3/scenario4 全部通过验证 (Run ID: 20260316-131359)
- (historical: 2026-03-13 时 scenario3 曾是阻塞点,后续修复)

### 3.3 已知注意事项

**CI_LAB UDP 注入的 wall-clock 时序依赖**  
如上所述,CI_LAB 的 UDP 命令接收不是纯步进驱动的。在 scenario3 和 scenario4 中,命令发送后需要 wall-clock 等待 (约 0.8s) 才能确保被 cFS 处理。这是环境约束,不是步进核心的缺陷。

**TO_LAB 设置命令必须序列化**  
在 `setup_to_lab_channel()` 函数中,每个 cmdUtil 命令后都有 `wait_for_log` 和 `sleep 1`,确保命令被处理后再执行下一步。批量发送命令可能导致溢出或乱序。

**运行时进程清理的重要性**  
回归脚本在 `trap cleanup EXIT` 中注册清理函数,确保 core-cpu1 和 UDP 监听器在脚本退出时被终止。手动调试时如果忘记清理,可能导致端口占用或 socket 文件残留,影响下次运行。

**UDS 请求格式字节数**  
UDS 请求实际是 8 字节,不是 5 字节 (`<BxxxI` = 1 byte opcode + 3 bytes padding + 4 bytes uint32 LE)。旧文档中的 "5 bytes" 描述是错误的。

**日志路径**  
cFS 默认输出到 stdout/stderr,没有内置的固定日志文件路径。手动运行时需要重定向 (`./core-cpu1 > /tmp/core.log 2>&1`),或使用回归脚本的自动日志收集 (`$LOG_DIR/scenario{N}_core.log`)。

---

## 4. 调试指引

### 4.1 遇到 scenario3 超时时

1. 检查核心日志中的 `InProc_WaitStepComplete timeout` 标记
2. 确认 `trigger_count` 和 `acks_expected` 是否匹配
3. 检查是否有 `duplicate_begin` 或 `illegal_complete` 错误
4. 验证 TO_LAB 设置命令是否成功 (查找 `TO AddPkt` 和 `TO telemetry output enabled`)
5. 确认命令注入后有足够的 wall-clock 延迟 (scenario3 中 `sleep 1`)

### 4.2 证据收集检查清单

- [ ] 构建通过: `make SIMULATION=native CFE_SIM_STEPPING=ON prep && make && make install`
- [ ] 单元测试通过: `make ENABLE_UNIT_TESTS=true prep && make && make install && make test`
- [ ] 场景日志存在: `build/sim-stepping-regression/{RUN_ID}/scenario{N}_*.log`
- [ ] 核心日志包含 OPERATIONAL 标记
- [ ] UDP 日志格式正确: `mid=0xXXXX` 格式

---

*文档版本: 2026-03-13*  
*对应计划: `.sisyphus/plans/linux-global-sim-stepping.md`*  
*对应脚本: `tools/sim_stepping_regression.sh`*

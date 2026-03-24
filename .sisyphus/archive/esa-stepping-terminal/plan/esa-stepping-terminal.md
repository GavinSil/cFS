# ESA Stepping Terminal — 交互式 UDS REPL 工具

## TL;DR

> **Quick Summary**: 在 `tools/esa_stepping_terminal/` 创建一个独立的 C 语言 REPL 工具，通过 UDS socket 连接 ESA stepping 状态机，支持 query/begin/wait/step/repeat 等命令，用于模拟外部控制器调试 stepping 行为。
> 
> **Deliverables**:
> - `tools/esa_stepping_terminal/CMakeLists.txt` — 独立构建配置
> - `tools/esa_stepping_terminal/src/protocol.h` — 协议常量、结构体、静态断言
> - `tools/esa_stepping_terminal/src/uds_client.h` + `.c` — UDS 连接/收发/关闭
> - `tools/esa_stepping_terminal/src/repl.h` + `.c` — REPL 循环、命令解析、分发
> - `tools/esa_stepping_terminal/src/main.c` — 入口、CLI 参数、信号处理
> - `tools/esa_stepping_terminal/README.md` — 中文使用文档
> 
> **Estimated Effort**: Medium (8 source files, ~600-900 行 C 代码)
> **Parallel Execution**: YES — 3 waves
> **Critical Path**: Task 1 → Task 3 → Task 5 → Task 6

---

## Context

### Original Request
在 `tools/` 下创建一个独立目录的 C 语言交互式终端 / REPL，用于模拟外部控制器，连接 ESA stepping 的 UDS socket。不接入顶层 `tools/CMakeLists.txt`。设计已由用户批准并锁定。

### Interview Summary
**Key Discussions**:
- 协议设计: 短连接模式 (connect→transact→close per command)
- 命令集: help, connect, query, status, begin, wait, step, repeat, quit, exit
- `step`/`repeat` 是本地组合命令，不新增协议 opcode
- 独立 CMake 构建，不修改任何仓库现有文件

**Research Findings**:
- 服务端代码: `esa/fsw/src/esa_stepping.c:660-889` — 非阻塞 accept, 读固定大小请求, 写响应, 关闭
- Wire 协议: 请求 `{uint8_t opcode, uint32_t timeout_ms}` (8字节含 padding), 本机字节序
- Opcodes: BEGIN_STEP=1, QUERY_STATE=2, WAIT_STEP_COMPLETE=3
- 响应: BEGIN→`int32_t status`; QUERY→`{int32_t status, uint32_t state, uint32_t trigger_count}` (12字节); WAIT→`int32_t status`
- Status: SUCCESS=0, FAILURE=-1, DUPLICATE_BEGIN=-2, NOT_READY=-3, TIMEOUT=-4, ILLEGAL_COMPLETE=-5, TRANSPORT_ERROR=-6, PROTOCOL_ERROR=-7, ILLEGAL_STATE=-8
- State: INIT=0, READY=1, RUNNING=2, WAITING=3, COMPLETE=4
- 现有 tools/ 下的 CMake 模式: snippet 式 (add_executable + install), 但本工具需要独立 cmake_minimum_required + project()

### Metis Review
**Identified Gaps** (addressed):
- 结构体 ABI 一致性 → 添加 `_Static_assert` 编译时检查
- 短读/短写 → 实现 `read_exact()` / `write_exact()` 循环辅助函数
- SIGINT 处理 → 阻塞 `wait` 期间 Ctrl+C 可中断并返回提示符
- `fgets()` 返回 NULL (EOF) → 作为优雅退出处理
- socket path 长度 → 验证 < 108 字节 (`sizeof(sun_path)`)
- `wait 0` 无限阻塞 → 允许但打印警告，SIGINT 可中断
- 空行/未知命令 → 静默跳过空行，未知命令打印提示

---

## Work Objectives

### Core Objective
创建一个完整可用的命令行 REPL 工具，可通过 UDS socket 与 ESA stepping 状态机交互，用于调试和观察 stepping 行为。

### Concrete Deliverables
- 可独立编译的 `esa_stepping_terminal` 可执行文件
- 支持 10 个命令 (help, connect, query, status, begin, wait, step, repeat, quit, exit)
- 中文 README.md 文档

### Definition of Done
- [ ] `cmake . && make` 在工具目录内无错编译
- [ ] `echo "help" | ./esa_stepping_terminal` 输出所有命令帮助
- [ ] 无 socket 时 `query` 输出清晰的连接错误
- [ ] 与运行中的 cFS stepping 配合可完成 query/begin/wait 操作

### Must Have
- 所有 10 个命令按批准设计语义实现
- 短连接模式 (每个命令独立 connect→transact→close)
- `-s/--socket PATH` CLI 参数覆盖默认 socket 路径
- 9 个 status code + 5 个 state 的人类可读字符串映射
- `_Static_assert` 验证协议结构体大小
- `read_exact()` / `write_exact()` 防止短读/短写
- SIGINT 处理：阻塞操作时可中断
- Doxygen 风格中文注释 (所有新函数)
- 中文 README.md

### Must NOT Have (Guardrails)
- **不修改** `tools/CMakeLists.txt` 或仓库内任何其他现有文件
- **不添加** readline/ncurses/任何外部库依赖 — 纯 POSIX
- **不使用** `__attribute__((packed))` — 服务端使用自然对齐
- **不包含** `esa_stepping.h` — 协议常量在本地 `protocol.h` 中独立定义
- **不实现** 长连接/连接池/异步模式
- **不实现** 脚本/批处理模式、ANSI 颜色输出、持久配置文件
- **不超出** `tools/esa_stepping_terminal/` 目录范围

---

## Verification Strategy

> **ZERO HUMAN INTERVENTION** — ALL verification is agent-executed. No exceptions.

### Test Decision
- **Infrastructure exists**: NO (独立工具，无 cFS ut_assert 依赖)
- **Automated tests**: NO (通过管道式 smoke test 验证，不建立单独测试框架)
- **Framework**: None — 使用 `echo | ./binary` 管道测试 + 构建验证

### QA Policy
每个任务包含 agent-executed QA 场景。
Evidence 保存到 `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`。

- **CLI/Terminal**: 使用 Bash — 管道输入、检查输出、验证退出码
- **Build**: 使用 Bash — cmake + make，检查编译错误/警告

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 1 (Start Immediately — foundation):
├── Task 1: Build scaffold + protocol definitions [quick]
└── Task 2: UDS client layer [unspecified-high]

Wave 2 (After Wave 1 — REPL core):
├── Task 3: REPL implementation (all commands) [deep]
└── Task 4: Main entry + CLI + SIGINT [quick]

Wave 3 (After Wave 2 — docs + verification):
└── Task 5: README.md + integration smoke test [quick]

Wave FINAL (After ALL tasks — 4 parallel reviews):
├── F1: Plan compliance audit (oracle)
├── F2: Code quality review (unspecified-high)
├── F3: Real manual QA (unspecified-high)
└── F4: Scope fidelity check (deep)
→ Present results → Get explicit user okay

Critical Path: Task 1 → Task 3 → Task 5 → F1-F4 → user okay
Parallel Speedup: ~40% faster than sequential
Max Concurrent: 2 (Waves 1 & 2)
```

### Dependency Matrix

| Task | Depends On | Blocks |
|------|-----------|--------|
| T1   | —         | T2, T3, T4 |
| T2   | T1        | T3, T4 |
| T3   | T1, T2    | T4, T5 |
| T4   | T1, T2, T3 | T5 |
| T5   | T3, T4    | F1-F4 |

### Agent Dispatch Summary

- **Wave 1**: **2** — T1 → `quick`, T2 → `unspecified-high`
- **Wave 2**: **2** — T3 → `deep`, T4 → `quick`
- **Wave 3**: **1** — T5 → `quick`
- **FINAL**: **4** — F1 → `oracle`, F2 → `unspecified-high`, F3 → `unspecified-high`, F4 → `deep`

---

## TODOs

> Implementation tasks below. EVERY task has: Agent Profile + Parallelization + QA Scenarios.

- [x] 1. Build Scaffold + Protocol Definitions

  **What to do**:
  - 创建目录结构: `tools/esa_stepping_terminal/`, `tools/esa_stepping_terminal/src/`
  - 创建 `CMakeLists.txt`:
    - `cmake_minimum_required(VERSION 3.5)`
    - `project(esa_stepping_terminal C)`
    - `add_executable(esa_stepping_terminal src/main.c src/repl.c src/uds_client.c)`
    - `target_compile_options(... PRIVATE -Wall -Wextra -Wpedantic)`
    - `install(TARGETS esa_stepping_terminal DESTINATION host)` (可选)
  - 创建 `src/protocol.h`:
    - 包含 `<stdint.h>`, `<stddef.h>`
    - 定义默认 socket 路径: `#define EST_DEFAULT_SOCKET_PATH "/tmp/cfe_sim_stepping.sock"`
    - 定义 opcodes: `EST_OPCODE_BEGIN_STEP 1`, `EST_OPCODE_QUERY_STATE 2`, `EST_OPCODE_WAIT_STEP_COMPLETE 3`
    - 定义请求结构体 `EST_Request_t { uint8_t opcode; uint32_t timeout_ms; }` — 不使用 packed，自然对齐
    - 定义查询响应结构体 `EST_QueryResponse_t { int32_t status; uint32_t state; uint32_t trigger_count; }`
    - 添加 `_Static_assert(sizeof(EST_Request_t) == 8, "...")` 和 `_Static_assert(sizeof(EST_QueryResponse_t) == 12, "...")`
    - 定义 status code 宏 (SUCCESS=0 到 ILLEGAL_STATE=-8)，值必须与 `esa/fsw/inc/esa_stepping.h:50-58` 完全一致
    - 定义 state 枚举 (INIT=0 到 COMPLETE=4)，值必须与 `esa/fsw/inc/esa_stepping_core.h:101-105` 完全一致
    - 所有定义使用 Doxygen 中文注释
  - 创建 `src/main.c` 最小桩: `#include "protocol.h"` + `int main() { return 0; }`

  **Must NOT do**:
  - 不使用 `__attribute__((packed))`
  - 不 include ESA 模块头文件
  - 不修改任何仓库现有文件

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 文件创建 + 常量定义，无复杂逻辑
  - **Skills**: [`create-cfs-app`]
    - `create-cfs-app`: 提供 cFS 工具目录结构和 CMake 模式参考（虽然本工具不集成进 cFS 构建，但目录组织模式类似）
  - **Skills Evaluated but Omitted**:
    - `create-cfs-lib`: 本工具是可执行文件而非库

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Task 2)
  - **Blocks**: Tasks 2, 3, 4, 5
  - **Blocked By**: None (can start immediately)

  **References**:

  **Pattern References**:
  - `esa/fsw/src/esa_stepping.c:667-697` — 服务端协议常量和结构体的精确定义，必须 1:1 匹配 wire format
  - `tools/elf2cfetbl/CMakeLists.txt` — 现有工具的 CMake 模式参考（但本工具需要独立 cmake_minimum_required）

  **API/Type References**:
  - `esa/fsw/inc/esa_stepping.h:50-58` — 9 个 status code 宏定义 (ESA_SIM_STEPPING_STATUS_*)，值必须精确复制
  - `esa/fsw/inc/esa_stepping_core.h:101-105` — 5 个 state 枚举值 (ESA_SIM_STEPPING_STATE_*)，值必须精确复制

  **WHY Each Reference Matters**:
  - `esa_stepping.c:667-697`: 这是 wire format 的 **权威来源**。结构体必须逐字节匹配，否则通信失败
  - `esa_stepping.h:50-58`: status code 数值必须一致才能正确解析服务端响应
  - `esa_stepping_core.h:101-105`: state 枚举值必须一致才能正确显示状态

  **Acceptance Criteria**:

  **QA Scenarios**:

  ```
  Scenario: 独立 CMake 配置和编译成功
    Tool: Bash
    Preconditions: tools/esa_stepping_terminal/ 目录已创建
    Steps:
      1. cd tools/esa_stepping_terminal && mkdir -p build && cd build
      2. cmake .. 2>&1
      3. make 2>&1
    Expected Result: cmake 配置成功 (无 "Error"), make 编译成功 (无 "error:"), 生成 esa_stepping_terminal 可执行文件
    Failure Indicators: cmake 输出包含 "Error", make 输出包含 "error:", 可执行文件不存在
    Evidence: .sisyphus/evidence/task-1-cmake-build.txt

  Scenario: protocol.h 结构体大小静态断言通过
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. 检查编译输出无 static_assert 失败信息
      2. grep -c "_Static_assert" tools/esa_stepping_terminal/src/protocol.h
    Expected Result: 编译无 static_assert 错误; grep 输出 >= 2 (至少 Request 和 QueryResponse 两个断言)
    Failure Indicators: 编译输出包含 "static assertion failed"
    Evidence: .sisyphus/evidence/task-1-static-assert.txt
  ```

  **Commit**: YES (C1)
  - Message: `feat(tools): add esa_stepping_terminal scaffold with protocol definitions`
  - Files: `tools/esa_stepping_terminal/CMakeLists.txt`, `tools/esa_stepping_terminal/src/protocol.h`, `tools/esa_stepping_terminal/src/main.c`
  - Pre-commit: `cd tools/esa_stepping_terminal && mkdir -p build && cd build && cmake .. && make`

- [x] 2. UDS Client Layer

  **What to do**:
  - 创建 `src/uds_client.h`:
    - `int est_uds_connect(const char *socket_path)` — 创建 AF_UNIX socket, 连接到路径, 返回 fd 或 -1
    - `int est_uds_transact(const char *socket_path, const void *req, size_t req_size, void *resp, size_t resp_size)` — 连接+发送+接收+关闭 一体化事务函数
    - `void est_uds_close(int fd)` — 关闭 fd
    - `const char *est_status_to_string(int32_t status)` — status code → 人类可读字符串
    - `const char *est_state_to_string(uint32_t state)` — state → 人类可读字符串
  - 创建 `src/uds_client.c`:
    - 实现 `read_exact(fd, buf, n)` — 循环 read 直到收到 n 字节或错误/EOF
    - 实现 `write_exact(fd, buf, n)` — 循环 write 直到写完 n 字节或错误
    - 实现 `est_uds_connect()`: 创建 `SOCK_STREAM` socket, 验证 path 长度 < 108, `connect()`, 错误时 `close(fd)` 并返回 -1
    - 实现 `est_uds_transact()`: connect → write_exact(request) → read_exact(response) → close, 任何步骤失败返回 -1 并打印错误
    - 实现 `est_status_to_string()`: switch 映射 9 个 status code 到描述字符串（中文）
    - 实现 `est_state_to_string()`: switch 映射 5 个 state 到描述字符串（中文）
    - 所有函数 Doxygen 中文注释

  **Must NOT do**:
  - 不保持长连接
  - 不添加重连逻辑
  - 不使用 `fcntl` 设置非阻塞 (客户端使用阻塞模式)

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 涉及 POSIX socket 编程，需要正确处理错误路径和边界情况
  - **Skills**: []
  - **Skills Evaluated but Omitted**:
    - `create-cfs-app`: 本文件是纯 POSIX socket 代码，不涉及 cFS API

  **Parallelization**:
  - **Can Run In Parallel**: YES (与 Task 1 同波次)
  - **Parallel Group**: Wave 1 (with Task 1)
  - **Blocks**: Tasks 3, 4
  - **Blocked By**: Task 1 (需要 protocol.h 中的结构体定义)

  **References**:

  **Pattern References**:
  - `esa/fsw/src/esa_stepping.c:700-780` — 服务端如何 accept + read + write + close，客户端必须匹配这个流程
  - `esa/fsw/src/esa_stepping.c:678-697` — 请求/响应结构体定义，确认 wire format

  **API/Type References**:
  - `tools/esa_stepping_terminal/src/protocol.h` (Task 1 产出) — EST_Request_t, EST_QueryResponse_t, status/state 常量

  **External References**:
  - POSIX `sys/socket.h`, `sys/un.h`, `unistd.h` — AF_UNIX stream socket API

  **WHY Each Reference Matters**:
  - `esa_stepping.c:700-780`: 服务端读取 `UDS_REQUEST_SIZE` 字节 → 客户端必须发送完全相同大小。服务端写 `sizeof(int32_t)` 或 `sizeof(UDS_QueryStateResponse_t)` → 客户端必须读取对应大小
  - `protocol.h`: 共享的结构体定义，确保客户端与服务端 wire format 一致

  **Acceptance Criteria**:

  **QA Scenarios**:

  ```
  Scenario: UDS client 编译成功且符号完整
    Tool: Bash
    Preconditions: Task 1 scaffold 已完成
    Steps:
      1. cd tools/esa_stepping_terminal/build && cmake .. && make 2>&1
      2. nm esa_stepping_terminal | grep -E "est_uds_|est_status_|est_state_"
    Expected Result: 编译成功; nm 输出包含 est_uds_connect, est_uds_transact, est_uds_close, est_status_to_string, est_state_to_string 符号
    Failure Indicators: 编译错误; 符号缺失
    Evidence: .sisyphus/evidence/task-2-uds-symbols.txt

  Scenario: status/state 字符串映射覆盖所有值
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. grep -c "case" tools/esa_stepping_terminal/src/uds_client.c
    Expected Result: 至少 14 个 case (9 status + 5 state)
    Failure Indicators: case 数量 < 14 表示有遗漏的映射
    Evidence: .sisyphus/evidence/task-2-case-count.txt

  Scenario: 连接不存在的 socket 返回错误
    Tool: Bash (需要 Task 3/4 完成后才能端到端测试，此处仅验证代码正确性)
    Preconditions: 编译成功
    Steps:
      1. 检查 est_uds_connect 中包含 connect() 失败的 perror/fprintf 错误处理
      2. grep -n "perror\|fprintf(stderr" tools/esa_stepping_terminal/src/uds_client.c
    Expected Result: 有至少 3 处错误输出 (connect 失败, read 失败, write 失败)
    Failure Indicators: 无错误输出代码，表示错误被静默忽略
    Evidence: .sisyphus/evidence/task-2-error-handling.txt
  ```

  **Commit**: YES (C2)
  - Message: `feat(tools): add UDS client layer for stepping terminal`
  - Files: `tools/esa_stepping_terminal/src/uds_client.h`, `tools/esa_stepping_terminal/src/uds_client.c`
  - Pre-commit: `cd tools/esa_stepping_terminal/build && cmake .. && make`

- [x] 3. REPL Implementation (All Commands)

  **What to do**:
  - 创建 `src/repl.h`:
    - 定义 REPL 上下文结构体 `EST_ReplContext_t { char socket_path[108]; volatile sig_atomic_t interrupted; }`
    - 声明 `int est_repl_run(EST_ReplContext_t *ctx)` — REPL 主循环
    - 声明 `void est_repl_set_interrupted(EST_ReplContext_t *ctx)` — SIGINT 回调设置标志
  - 创建 `src/repl.c`:
    - **REPL 主循环** `est_repl_run()`:
      - 打印欢迎信息（含版本和默认 socket 路径）
      - 循环: 打印 `est> ` 提示符 → `fgets()` 读取 → 去除尾部换行 → 跳过空行 → 解析命令 → 分发
      - `fgets()` 返回 NULL (EOF) → 优雅退出
    - **命令解析**: `strtok()` 分割 command + args, 大小写不敏感比较 (`strcasecmp`)
    - **命令实现** (10 个):
      1. `help` — 打印所有命令用法和简要说明
      2. `connect [path]` — 更新 `ctx->socket_path`，无参数时显示当前路径
      3. `query` — 构造 `EST_Request_t{opcode=QUERY_STATE, timeout_ms=0}`, 调用 `est_uds_transact()`, 读 `EST_QueryResponse_t`, 打印原始字段 (status, state 数值, trigger_count)
      4. `status` — 与 `query` 相同请求，但输出使用 `est_status_to_string()` 和 `est_state_to_string()` 格式化为友好文本
      5. `begin` — 构造 `EST_Request_t{opcode=BEGIN_STEP, timeout_ms=0}`, 调用 `est_uds_transact()`, 读 `int32_t status`, 打印结果
      6. `wait <timeout_ms>` — 参数必填，解析 timeout, 若为 0 打印警告 "timeout_ms=0 将无限阻塞，Ctrl+C 可中断"，构造请求调用 transact, 打印结果。阻塞期间检查 `ctx->interrupted`
      7. `step <timeout_ms>` — 组合命令: 先执行 `begin`，成功后执行 `wait <timeout_ms>`，任一失败立即返回错误
      8. `repeat <count> <timeout_ms> [interval_ms]` — 循环执行 `step`，每次迭代打印 "[N/total] step result"。如果某次 step 失败，打印错误并停止。如果 `interval_ms` > 0，步间 `usleep(interval_ms * 1000)`。检查 `ctx->interrupted` 以支持 Ctrl+C 中断
      9. `quit` — 打印 "再见" 并返回 0
      10. `exit` — 同 `quit`
    - **未知命令**: 打印 "未知命令: '%s'。输入 'help' 查看可用命令。"
    - 所有函数 Doxygen 中文注释

  **Must NOT do**:
  - 不使用 readline/ncurses
  - 不实现命令历史、Tab 补全
  - 不添加颜色输出
  - 不实现脚本/批处理模式
  - `step`/`repeat` 不新增协议 opcode，仅组合现有命令

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: REPL 核心实现，涉及 10 个命令处理器、参数解析、错误处理、组合命令逻辑，需要深度理解和仔细实现
  - **Skills**: [`create-cfs-app`]
    - `create-cfs-app`: 提供 cFS 应用的命令分发模式参考（TaskPipe dispatch pattern 类似 REPL 命令分发）
  - **Skills Evaluated but Omitted**:
    - `create-cfs-table`: REPL 不涉及表管理

  **Parallelization**:
  - **Can Run In Parallel**: NO (依赖 Wave 1 产出)
  - **Parallel Group**: Wave 2 (with Task 4)
  - **Blocks**: Tasks 4, 5
  - **Blocked By**: Tasks 1, 2

  **References**:

  **Pattern References**:
  - `apps/sample_app/fsw/src/sample_app.c` (TaskPipe 函数) — 命令分发 switch 模式参考。REPL 的命令分发与 cFS 的 MsgId→handler 分发在结构上类似
  - `esa/fsw/src/esa_stepping.c:700-780` — 服务端处理各 opcode 的流程，客户端发送的请求必须匹配

  **API/Type References**:
  - `tools/esa_stepping_terminal/src/protocol.h` (Task 1 产出) — EST_Request_t, EST_QueryResponse_t, opcode/status/state 常量
  - `tools/esa_stepping_terminal/src/uds_client.h` (Task 2 产出) — est_uds_transact(), est_status_to_string(), est_state_to_string()

  **WHY Each Reference Matters**:
  - `protocol.h`: 所有命令处理器需要构造正确的请求结构体并解析正确的响应
  - `uds_client.h`: REPL 的每个网络命令都通过 `est_uds_transact()` 执行，不直接操作 socket
  - `esa_stepping.c`: 确认服务端对各 opcode 的预期行为，确保客户端发送的请求被正确处理

  **Acceptance Criteria**:

  **QA Scenarios**:

  ```
  Scenario: help 命令输出包含所有命令
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. echo "help" | ./build/esa_stepping_terminal 2>&1 > /tmp/est-help.txt
      2. 检查输出包含: help, connect, query, status, begin, wait, step, repeat, quit, exit
    Expected Result: 10 个命令名称全部出现在输出中
    Failure Indicators: 任一命令名称缺失
    Evidence: .sisyphus/evidence/task-3-help-output.txt

  Scenario: EOF (空管道) 优雅退出
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. echo "" | ./build/esa_stepping_terminal; echo "EXIT_CODE=$?"
    Expected Result: 程序正常退出，退出码 0，无 segfault 或异常输出
    Failure Indicators: 退出码非 0, 输出包含 "Segmentation fault" 或 "core dumped"
    Evidence: .sisyphus/evidence/task-3-eof-exit.txt

  Scenario: 无 socket 时 query 命令报清晰错误
    Tool: Bash
    Preconditions: 编译成功, /tmp/cfe_sim_stepping.sock 不存在
    Steps:
      1. rm -f /tmp/cfe_sim_stepping.sock
      2. echo -e "query\nquit" | ./build/esa_stepping_terminal 2>&1 > /tmp/est-nosock.txt
      3. 检查输出包含 "connect" 或 "连接" 或 "No such file" 或 "Connection refused" 相关错误信息
    Expected Result: 输出包含明确的连接错误信息，程序不崩溃，正常退出
    Failure Indicators: 无错误提示, 崩溃, 挂起
    Evidence: .sisyphus/evidence/task-3-no-socket-error.txt

  Scenario: connect 命令更新 socket 路径
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. echo -e "connect /tmp/test.sock\nconnect\nquit" | ./build/esa_stepping_terminal 2>&1
    Expected Result: 第二个 connect (无参数) 输出显示路径已变为 "/tmp/test.sock"
    Failure Indicators: 路径未更新, 仍显示默认路径
    Evidence: .sisyphus/evidence/task-3-connect-path.txt

  Scenario: 未知命令提示
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. echo -e "foobar\nquit" | ./build/esa_stepping_terminal 2>&1
    Expected Result: 输出包含 "未知命令" 或 "Unknown command" 和 "foobar"
    Failure Indicators: 无提示, 崩溃, 静默忽略
    Evidence: .sisyphus/evidence/task-3-unknown-cmd.txt

  Scenario: wait 缺少参数时报错
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. echo -e "wait\nquit" | ./build/esa_stepping_terminal 2>&1
    Expected Result: 输出包含参数缺失的错误提示 (如 "用法: wait <timeout_ms>")
    Failure Indicators: 崩溃, 使用默认值而不报错
    Evidence: .sisyphus/evidence/task-3-wait-no-arg.txt
  ```

  **Commit**: YES (C3, 与 Task 4 合并提交)
  - Message: `feat(tools): implement REPL with all stepping commands`
  - Files: `tools/esa_stepping_terminal/src/repl.h`, `tools/esa_stepping_terminal/src/repl.c`
  - Pre-commit: `cd tools/esa_stepping_terminal/build && cmake .. && make && echo "help" | ./esa_stepping_terminal`

- [x] 4. Main Entry + CLI Arguments + SIGINT Handler

  **What to do**:
  - 完善 `src/main.c` (替换 Task 1 的桩):
    - `#include` protocol.h, repl.h, `<signal.h>`, `<stdio.h>`, `<string.h>`, `<getopt.h>`
    - **全局 REPL context 指针** (用于 SIGINT handler): `static EST_ReplContext_t *g_ctx = NULL;`
    - **SIGINT handler**: `sigint_handler(int sig)` → 调用 `est_repl_set_interrupted(g_ctx)`
    - **CLI 参数解析** (使用 `getopt_long`):
      - `-s PATH` / `--socket PATH` — 覆盖默认 socket 路径
      - `-h` / `--help` — 打印用法并退出
    - **main()**:
      1. 解析 CLI 参数
      2. 初始化 `EST_ReplContext_t`（设置 socket_path，清除 interrupted 标志）
      3. 设置 SIGINT handler (`sigaction`)
      4. 调用 `est_repl_run(&ctx)`
      5. 返回结果
    - 所有函数 Doxygen 中文注释

  **Must NOT do**:
  - 不添加 daemon 模式
  - 不添加日志文件输出
  - signal handler 中不调用非 async-signal-safe 函数（只设置 `sig_atomic_t` 标志）

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: main.c 逻辑简单 — CLI 参数解析 + signal 设置 + 调用 repl_run
  - **Skills**: []
  - **Skills Evaluated but Omitted**:
    - `create-cfs-app`: main.c 模式与 cFS 应用入口差异较大（本工具无 CFE_ES_RunLoop）

  **Parallelization**:
  - **Can Run In Parallel**: YES (与 Task 3 同波次)
  - **Parallel Group**: Wave 2 (with Task 3)
  - **Blocks**: Task 5
  - **Blocked By**: Tasks 1, 2, 3 (需要 protocol.h, uds_client.h, repl.h)

  **References**:

  **Pattern References**:
  - `tools/cFS-GroundSystem/Subsystems/cmdUtil/cmdUtil.c` — 工具级 main() 的参数解析模式参考

  **API/Type References**:
  - `tools/esa_stepping_terminal/src/repl.h` (Task 3 产出) — EST_ReplContext_t, est_repl_run(), est_repl_set_interrupted()
  - `tools/esa_stepping_terminal/src/protocol.h` (Task 1 产出) — EST_DEFAULT_SOCKET_PATH

  **WHY Each Reference Matters**:
  - `repl.h`: main.c 的核心功能就是初始化 context 并调用 `est_repl_run()`
  - `protocol.h`: 获取默认 socket 路径常量

  **Acceptance Criteria**:

  **QA Scenarios**:

  ```
  Scenario: --help 参数输出用法信息
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. ./build/esa_stepping_terminal --help 2>&1
    Expected Result: 输出包含 "用法" 或 "Usage"，包含 "--socket" 和 "-s" 选项说明
    Failure Indicators: 无用法输出, 崩溃
    Evidence: .sisyphus/evidence/task-4-cli-help.txt

  Scenario: --socket 参数覆盖默认路径
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. echo -e "connect\nquit" | ./build/esa_stepping_terminal --socket /tmp/custom.sock 2>&1
    Expected Result: connect 无参数输出显示路径为 "/tmp/custom.sock"
    Failure Indicators: 显示默认路径 "/tmp/cfe_sim_stepping.sock"
    Evidence: .sisyphus/evidence/task-4-socket-override.txt

  Scenario: SIGINT 不导致崩溃
    Tool: Bash
    Preconditions: 编译成功
    Steps:
      1. timeout 2 bash -c 'echo -e "help\n" | ./build/esa_stepping_terminal &; PID=$!; sleep 0.5; kill -INT $PID 2>/dev/null; wait $PID 2>/dev/null; echo "EXIT=$?"'
    Expected Result: 程序不崩溃（无 "Segmentation fault"），退出码不为 139 (SIGSEGV)
    Failure Indicators: 输出包含 "Segmentation fault", 退出码 139
    Evidence: .sisyphus/evidence/task-4-sigint-safety.txt
  ```

  **Commit**: YES (C3, 与 Task 3 合并提交)
  - Message: `feat(tools): implement REPL with all stepping commands`
  - Files: `tools/esa_stepping_terminal/src/main.c`
  - Pre-commit: `cd tools/esa_stepping_terminal/build && cmake .. && make && echo "help" | ./esa_stepping_terminal`

- [x] 5. README.md + Integration Smoke Test

  **What to do**:
  - 创建 `README.md` (中文):
    - **标题**: ESA Stepping Terminal — 交互式 UDS 调试终端
    - **概述**: 简述用途（模拟外部控制器，通过 UDS 与 ESA stepping 交互）
    - **构建**:
      ```
      mkdir build && cd build
      cmake ..
      make
      ```
    - **运行**: `./esa_stepping_terminal [-s <socket_path>]`
    - **命令参考表**: 每个命令的语法、说明、示例
    - **典型工作流**: 演示 connect → query → begin → wait → step → repeat 的典型使用顺序
    - **协议说明**: 简述 wire format（opcode + timeout），短连接模式
    - **故障排除**: 常见错误及解决方法（socket 不存在、连接被拒、超时等）
  - 执行完整的集成 smoke test 验证所有本地命令工作正常

  **Must NOT do**:
  - 不添加英文翻译（中文 README 即可）
  - 不包含 cFS 构建/安装的详细步骤（那是 cFS 文档的事）

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: README 撰写 + smoke test 运行
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 3 (sequential after Wave 2)
  - **Blocks**: F1-F4
  - **Blocked By**: Tasks 3, 4

  **References**:

  **Pattern References**:
  - `tools/cFS-GroundSystem/Guide-GroundSystem.md` — 现有工具的文档格式参考

  **API/Type References**:
  - 所有 Task 1-4 产出的 .c/.h 文件 — 确保 README 中的命令说明与实际实现一致

  **WHY Each Reference Matters**:
  - 确保文档准确反映实际实现的命令语法和行为

  **Acceptance Criteria**:

  **QA Scenarios**:

  ```
  Scenario: README.md 存在且包含所有命令
    Tool: Bash
    Preconditions: Task 5 完成
    Steps:
      1. test -f tools/esa_stepping_terminal/README.md
      2. grep -c -E "help|connect|query|status|begin|wait|step|repeat|quit|exit" tools/esa_stepping_terminal/README.md
    Expected Result: 文件存在; grep 匹配数 >= 10
    Failure Indicators: 文件不存在; 命令未在文档中提及
    Evidence: .sisyphus/evidence/task-5-readme-check.txt

  Scenario: 完整集成 smoke test (无服务端)
    Tool: Bash
    Preconditions: 编译成功, 无运行中的 stepping server
    Steps:
      1. cd tools/esa_stepping_terminal && rm -rf build && mkdir build && cd build && cmake .. && make 2>&1
      2. echo "help" | ./esa_stepping_terminal 2>&1
      3. echo -e "connect /tmp/test.sock\nconnect\nquit" | ./esa_stepping_terminal 2>&1
      4. echo -e "query\nquit" | ./esa_stepping_terminal 2>&1
      5. echo -e "status\nquit" | ./esa_stepping_terminal 2>&1
      6. echo -e "begin\nquit" | ./esa_stepping_terminal 2>&1
      7. echo -e "foobar\nquit" | ./esa_stepping_terminal 2>&1
      8. echo "" | ./esa_stepping_terminal 2>&1
      9. ./esa_stepping_terminal --help 2>&1
      10. ./esa_stepping_terminal -s /tmp/custom.sock <<< "connect" 2>&1
    Expected Result:
      - Step 1: 编译成功，无 error 无 warning
      - Step 2: help 输出包含 10 个命令
      - Step 3: connect 路径更新为 /tmp/test.sock
      - Steps 4-6: 连接错误信息（清晰、非崩溃）
      - Step 7: "未知命令" 提示包含 "foobar"
      - Step 8: EOF 优雅退出
      - Step 9: 显示用法信息
      - Step 10: socket 路径为 /tmp/custom.sock
    Failure Indicators: 任一步骤崩溃, 编译有 warning, 输出不符合预期
    Evidence: .sisyphus/evidence/task-5-integration-smoke.txt

  Scenario: 全量 clean build 从零开始
    Tool: Bash
    Preconditions: 无残留 build 目录
    Steps:
      1. rm -rf tools/esa_stepping_terminal/build
      2. cd tools/esa_stepping_terminal && mkdir build && cd build && cmake .. 2>&1 && make 2>&1
      3. file esa_stepping_terminal
    Expected Result: cmake + make 成功; file 输出包含 "ELF" 和 "executable"
    Failure Indicators: cmake 或 make 失败; file 不显示 executable
    Evidence: .sisyphus/evidence/task-5-clean-build.txt
  ```

  **Commit**: YES (C4)
  - Message: `docs(tools): add Chinese README for esa_stepping_terminal`
  - Files: `tools/esa_stepping_terminal/README.md`
  - Pre-commit: `cd tools/esa_stepping_terminal/build && cmake .. && make`

---

## Final Verification Wave

> 4 review agents run in PARALLEL. ALL must APPROVE. Present consolidated results to user.

- [x] F1. **Plan Compliance Audit** — `oracle`
  Read the plan end-to-end. For each "Must Have": verify implementation exists. For each "Must NOT Have": search `tools/esa_stepping_terminal/` for forbidden patterns. Verify no files outside `tools/esa_stepping_terminal/` were modified. Check evidence files exist.
  Output: `Must Have [9/9] | Must NOT Have [5/5] | Tasks [5/5] | VERDICT: APPROVE`

- [x] F2. **Code Quality Review** — `unspecified-high`
  编译检查: 在 `tools/esa_stepping_terminal/` 目录 `cmake . && make` 须无 warning。Review all .c/.h files for: missing includes, unused variables, buffer overflows, unchecked return values, missing `_Static_assert`. Check Doxygen 中文注释覆盖率。
  Output: `Build [PASS] | Warnings [0] | Files [6 clean/0 issues] | VERDICT: APPROVE`

- [x] F3. **Real Manual QA** — `unspecified-high`
  Execute EVERY QA scenario from EVERY task. Start from clean build. Test: `help` output, EOF handling, no-socket error, `connect` path change, argument validation errors. Save evidence to `.sisyphus/evidence/final-qa/`.
  Output: `Scenarios [15/15 pass] | Integration [N/A] | Edge Cases [15 tested] | VERDICT: APPROVE`

- [x] F4. **Scope Fidelity Check** — `deep`
  对比 plan 中每个 task 的 "What to do" 与实际实现。验证：1) 所有设计中的命令都已实现；2) 未添加超出 scope 的功能；3) 未修改 `tools/esa_stepping_terminal/` 以外的文件。用 `git diff --stat` 验证。
  Output: `Tasks [5/5 compliant] | Unaccounted [CLEAN] | VERDICT: APPROVE`

---

## Commit Strategy

| # | Message | Files | Pre-commit |
|---|---------|-------|-----------|
| C1 | `feat(tools): add esa_stepping_terminal scaffold with protocol definitions` | CMakeLists.txt, src/protocol.h, src/main.c(stub) | cmake + make |
| C2 | `feat(tools): add UDS client layer for stepping terminal` | src/uds_client.h, src/uds_client.c | cmake + make |
| C3 | `feat(tools): implement REPL with all stepping commands` | src/repl.h, src/repl.c, src/main.c(full) | cmake + make + echo "help" pipe test |
| C4 | `docs(tools): add Chinese README for esa_stepping_terminal` | README.md | cmake + make (full clean build) |

---

## Success Criteria

### Verification Commands
```bash
# 1. 独立构建成功
cd tools/esa_stepping_terminal && mkdir -p build && cd build && cmake .. && make
# Expected: 无 error, 无 warning, 生成 esa_stepping_terminal 可执行文件

# 2. help 输出正确
echo "help" | ./esa_stepping_terminal
# Expected: 输出包含所有 10 个命令 (help, connect, query, status, begin, wait, step, repeat, quit, exit)

# 3. 无 socket 错误清晰
echo -e "query\nquit" | ./esa_stepping_terminal
# Expected: 包含 "connect" 或 "连接" 相关错误信息，非崩溃，退出码 0

# 4. EOF 优雅退出
echo "" | ./esa_stepping_terminal
# Expected: 无崩溃，退出码 0

# 5. CLI 参数
./esa_stepping_terminal --help
# Expected: 显示用法信息，包含 --socket 参数说明
```

### Final Checklist
- [ ] All "Must Have" present
- [ ] All "Must NOT Have" absent
- [ ] 独立编译无 error/warning
- [ ] help 输出覆盖全部命令
- [ ] 无 socket 时错误提示清晰
- [ ] EOF/空行处理正确

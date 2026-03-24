# ESA Stepping Terminal — 交互式 UDS 调试终端

ESA Stepping Terminal 是一个交互式命令行工具，用于模拟外部控制器通过 Unix Domain Socket (UDS) 与 ESA 步进引擎进行通信调试。

本工具支持发送步进命令（begin、wait、step、repeat）、查询当前状态（query、status）和管理连接路径（connect）。

---

## 概述

ESA Stepping Terminal 为开发人员和测试人员提供了一个轻量级的交互式接口，用于：

- 手动或自动执行 ESA 步进操作
- 实时监控步进引擎状态
- 调试 UDS 通信和协议问题
- 批量执行重复步进任务

通过简洁的命令行界面，用户可以方便地与 ESA 步进引擎进行交互，无需编写复杂的测试代码。

---

## 构建

**环境要求**: CMake (>= 3.5)、GCC

```bash
cd tools/esa_stepping_terminal
mkdir build && cd build
cmake ..
make
```

构建产物: `build/esa_stepping_terminal`

---

## 运行

```bash
./esa_stepping_terminal [选项]
```

### 命令行选项

| 选项 | 说明 |
|------|------|
| `-s PATH`, `--socket PATH` | 指定 Unix Domain Socket 路径（默认: `/tmp/cfe_sim_stepping.sock`） |
| `-h`, `--help` | 显示帮助信息并退出 |

---

## 命令参考

进入 REPL 后，可使用以下命令:

| 命令 | 语法 | 说明 | 示例 |
|------|------|------|------|
| `help` | `help` | 显示可用命令列表 | `est> help` |
| `connect` | `connect [path]` | 显示当前 Socket 路径（无参数）或更新路径 | `est> connect /tmp/test.sock` |
| `query` | `query` | 查询步进引擎状态（显示原始数值） | `est> query` |
| `status` | `status` | 查询步进引擎状态（显示友好文本） | `est> status` |
| `begin` | `begin` | 启动一次步进周期 | `est> begin` |
| `wait` | `wait <timeout_ms>` | 等待步进完成，`timeout_ms=0` 无限等待 | `est> wait 5000` |
| `step` | `step <timeout_ms>` | 执行完整步进（begin + wait 组合） | `est> step 3000` |
| `repeat` | `repeat <count> <timeout_ms> [interval_ms]` | 重复执行步进 count 次，每次间隔 interval_ms 毫秒 | `est> repeat 10 5000 100` |
| `quit` / `exit` | `quit` 或 `exit` | 退出终端 | `est> quit` |

### 命令详细说明

**help**
显示所有可用命令及其简要说明。无任何参数。

**connect [path]**
- 无参数: 显示当前配置的 Socket 路径
- 带参数: 将 Socket 路径更新为指定路径

**query**
发送查询请求并显示原始响应数据：
- 状态码 (int32)
- 步进状态值 (uint32)
- 触发计数 (uint32)

**status**
与 query 功能相同，但将数值转换为可读文本显示。

**begin**
启动一个新的步进周期。如果步进正在进行中，会返回 DUPLICATE_BEGIN 错误。

**wait <timeout_ms>**
阻塞等待当前步进周期完成。
- `timeout_ms > 0`: 最多等待指定毫秒数
- `timeout_ms = 0`: 无限等待（可用 Ctrl+C 中断）

**step <timeout_ms>**
执行一次完整步进，相当于自动调用 `begin` 然后 `wait`。

**repeat <count> <timeout_ms> [interval_ms]**
重复执行多次步进操作：
- `count`: 重复次数
- `timeout_ms`: 每次步进的超时时间
- `interval_ms`: 可选，每次步进之间的间隔（毫秒）

**quit / exit**
优雅地退出 REPL 并返回命令提示符。

---

## 典型工作流

### 1. 确认连接

```
est> connect
当前 Socket 路径: /tmp/cfe_sim_stepping.sock
```

如需更改路径：

```
est> connect /tmp/my_test.sock
Socket 路径已更新为: /tmp/my_test.sock
```

### 2. 查询当前状态

```
est> status
状态: SUCCESS
步进状态: READY
触发计数: 0
```

或使用原始数值查询：

```
est> query
状态码: 0
步进状态: 1
触发计数: 0
```

### 3. 执行单步操作

```
est> step 5000
begin 结果: SUCCESS
wait 结果: SUCCESS
```

### 4. 连续执行多步

```
est> repeat 5 5000 100
[1/5] begin 结果: SUCCESS
wait 结果: SUCCESS
[2/5] begin 结果: SUCCESS
wait 结果: SUCCESS
...
```

### 5. 分步控制（高级）

如需更细粒度的控制，可分开使用 begin 和 wait：

```
est> begin
begin 结果: SUCCESS
est> wait 0
警告: timeout_ms=0 将无限阻塞，Ctrl+C 可中断
wait 结果: SUCCESS
```

---

## 协议说明

### 连接模式

采用**短连接**模式：每个命令独立建立 UDS 连接、发送请求、接收响应、关闭连接。

### 请求格式 (8 字节)

```
+--------+--------+--------+--------+--------+--------+--------+--------+
| opcode | reserved (3 bytes)              | timeout_ms (4 bytes)       |
+--------+--------+--------+--------+--------+--------+--------+--------+
```

- `opcode`: 1 字节，操作码
  - `1` = BEGIN_STEP
  - `2` = QUERY_STATE
  - `3` = WAIT_STEP_COMPLETE
- `timeout_ms`: 4 字节（小端），超时时间（毫秒）

### 响应格式

**BEGIN/WAIT 响应 (4 字节)**:
```
+--------+--------+--------+--------+
| status (int32)                    |
+--------+--------+--------+--------+
```

**QUERY 响应 (12 字节)**:
```
+--------+--------+--------+--------+--------+--------+--------+--------+
| status (int32)                    | state (uint32)                    |
+--------+--------+--------+--------+--------+--------+--------+--------+
| trigger_count (uint32)                                                |
+--------+--------+--------+--------+--------+--------+--------+--------+
```

### 状态码

| 状态码 | 常量名 | 说明 |
|--------|--------|------|
| 0 | SUCCESS | 操作成功 |
| -1 | FAILURE | 通用操作失败 |
| -2 | DUPLICATE_BEGIN | 步进已开始，拒绝重复开始 |
| -3 | NOT_READY | 系统未就绪 |
| -4 | TIMEOUT | 操作超时 |
| -5 | ILLEGAL_COMPLETE | 非法完成状态 |
| -6 | TRANSPORT_ERROR | 传输错误 |
| -7 | PROTOCOL_ERROR | 协议错误 |
| -8 | ILLEGAL_STATE | 非法状态 |

### 步进状态

| 状态值 | 名称 | 说明 |
|--------|------|------|
| 0 | INIT | 未初始化 |
| 1 | READY | 就绪，等待步进命令 |
| 2 | RUNNING | 正在执行步进 |
| 3 | WAITING | 等待确认/完成 |
| 4 | COMPLETE | 步进完成 |

---

## 故障排除

### `connect: No such file or directory`

**原因**: ESA stepping 服务器未运行，或 Socket 路径配置错误。

**解决**:
1. 确认 ESA 模拟器或步进引擎已启动
2. 检查 Socket 路径是否正确：
   ```
   est> connect
   ```
3. 如需更新路径：
   ```
   est> connect /correct/path/to/socket.sock
   ```

### `connect: Connection refused`

**原因**: Socket 文件存在，但服务器未监听该端口。

**解决**:
1. 检查步进引擎是否正常运行
2. 尝试重启步进引擎
3. 检查是否有其他进程占用了该 Socket

### 超时错误 (`TIMEOUT`)

**原因**: 步进操作在指定时间内未完成。

**解决**:
1. 增大 `timeout_ms` 参数值
2. 检查步进引擎状态：
   ```
   est> status
   ```
3. 确认步进引擎没有卡死或阻塞
4. 使用 `timeout_ms=0` 无限等待（需手动中断）

### 协议错误 (`PROTOCOL_ERROR`)

**原因**: 与服务器的协议版本不匹配或数据格式错误。

**解决**:
1. 确认客户端和服务器使用相同版本的协议
2. 检查请求结构体大小是否为 8 字节
3. 检查响应结构体大小是否匹配

### 未知命令错误

**现象**: `未知命令: 'xxx'`

**原因**: 输入了不支持的命令。

**解决**:
```
est> help
```
查看所有可用命令列表。

---

## 安全提示

1. **不要在生产环境使用**: 本工具仅用于开发和测试
2. **注意超时设置**: `timeout_ms=0` 会无限阻塞，确保有中断机制
3. **Socket 权限**: 确保当前用户有权限访问目标 Socket 文件

---

## 许可证

本工具遵循 NASA cFS 项目的开源许可证。

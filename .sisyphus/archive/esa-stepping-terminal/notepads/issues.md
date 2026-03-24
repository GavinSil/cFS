# ESA Stepping Terminal — Anti-Patterns to Avoid

## Anti-Pattern Catalog

These patterns were identified during research and should NOT be used in the ESA stepping terminal implementation.

---

## 1. ❌ Packed Structs (`__attribute__((packed))`)

### Why Avoid
The server uses natural alignment, NOT packed structures. Using packed structs on the client side would cause ABI mismatch and protocol failures.

### Server Reference
The server code in `esa/fsw/src/esa_stepping.c` uses naturally-aligned structures:
```c
typedef struct {
    uint8_t opcode;       /* Offset 0 */
    uint32_t timeout_ms;  /* Offset 4 (aligned) */
} UDS_Request_t;          /* Size = 8 bytes (3 bytes padding after opcode) */
```

### Correct Approach
Use `_Static_assert()` to verify structure sizes match expected wire format:
```c
_Static_assert(sizeof(EST_Request_t) == 8, "Request size mismatch");
_Static_assert(sizeof(EST_QueryResponse_t) == 12, "Response size mismatch");
```

### References
- Plan: "Must NOT Have" section explicitly prohibits `__attribute__((packed))`
- Server source: `esa/fsw/src/esa_stepping.c:667-697`

---

## 2. ❌ Long-Lived Connections / Connection Pooling

### Why Avoid
The plan specifies short-lived connections: connect → transact → close per command. This simplifies error handling and avoids stale connection issues.

### Pattern to Avoid
```c
/* WRONG - Keeping connection open */
int fd = connect_to_server();
while (1) {
    send_command(fd, cmd);  /* Reusing connection */
}
close(fd);
```

### Correct Approach
```c
/* CORRECT - Short-lived connections */
int transact(const char *path, void *req, void *resp) {
    int fd = connect(path);
    write_exact(fd, req, req_size);
    read_exact(fd, resp, resp_size);
    close(fd);
}
```

### References
- Plan: "Must NOT Have" section — "不实现长连接/连接池/异步模式"
- Interview: "短连接模式 (connect→transact→close per command)"

---

## 3. ❌ readline / ncurses Dependencies

### Why Avoid
The plan requires pure POSIX implementation with no external library dependencies.

### Pattern to Avoid
```c
/* WRONG - Requires readline library */
#include <readline/readline.h>
#include <readline/history.h>
char *line = readline("est> ");
```

### Correct Approach
```c
/* CORRECT - Pure POSIX */
char line[256];
printf("est> ");
fflush(stdout);
if (fgets(line, sizeof(line), stdin) == NULL) {
    /* EOF handling */
}
```

### References
- Plan: "Must NOT Have" — "不添加 readline/ncurses/任何外部库依赖 — 纯 POSIX"
- Plan: "Must NOT Have" — "不实现命令历史、Tab 补全"

---

## 4. ❌ ANSI Color Output

### Why Avoid
The plan explicitly excludes colored output to keep implementation simple and portable.

### Pattern to Avoid
```c
/* WRONG - ANSI color codes */
printf("\033[32m成功\033[0m\n");  /* Green text */
printf("\033[1;31m错误\033[0m\n"); /* Bold red text */
```

### Correct Approach
```c
/* CORRECT - Plain text */
printf("成功\n");
printf("错误: %s\n", error_msg);
```

### References
- Plan: "Must NOT Have" — "不实现脚本/批处理模式、ANSI 颜色输出、持久配置文件"

---

## 5. ❌ Using signal() Instead of sigaction()

### Why Avoid
`signal()` behavior varies across platforms and UNIX versions. `sigaction()` is the POSIX-standard way with predictable behavior.

### Pattern to Avoid
```c
/* WRONG - Deprecated, platform-dependent behavior */
void handler(int sig) { /* ... */ }
signal(SIGINT, handler);  /* Avoid this */
```

### Correct Approach
```c
/* CORRECT - POSIX standard */
struct sigaction sa;
memset(&sa, 0, sizeof(sa));
sa.sa_handler = handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;
sigaction(SIGINT, &sa, NULL);
```

### References
- Plan: Task 4 requires SIGINT handler with `sigaction`
- https://pvs-studio.com/en/blog/posts/cpp/0950/ — "Remember that the behavior of signal() can vary widely"

---

## 6. ❌ Non-Signal-Safe Operations in Signal Handler

### Why Avoid
Signal handlers can interrupt normal execution at any point. Calling non-async-signal-safe functions can cause deadlocks or undefined behavior.

### Pattern to Avoid
```c
/* WRONG - Non-safe operations in handler */
void sigint_handler(int sig) {
    printf("Interrupted!\n");  /* printf is NOT async-signal-safe! */
    free(buffer);             /* malloc/free NOT safe in handler! */
    exit(0);                  /* exit() can call atexit handlers - unsafe! */
}
```

### Correct Approach
```c
/* CORRECT - Only set volatile flag */
volatile sig_atomic_t interrupted = 0;

void sigint_handler(int sig) {
    (void)sig;
    interrupted = 1;  /* Only this is safe! */
}
```

### Async-Signal-Safe Functions (POSIX.1-2008)
Safe to call in signal handlers:
- `_exit()` (not `exit()`!)
- `read()`, `write()`
- `close()`
- `sigaction()`, `sigprocmask()`
- Access to `volatile sig_atomic_t` variables

Unsafe functions (common mistakes):
- `printf()`, `fprintf()`
- `malloc()`, `free()`
- `exit()`
- String manipulation functions

### References
- https://www.codequoi.com/en/sending-and-intercepting-a-signal-in-c/
- https://pvs-studio.com/en/blog/posts/cpp/0950/
- Plan: Task 4 — "signal handler 中不调用非 async-signal-safe 函数"

---

## 7. ❌ Raw strcmp() for MsgId/Socket Path Comparison

### Why Avoid
While the plan doesn't involve MsgId, the pattern is worth noting: for protocol-level comparisons, exact matching is required.

### Pattern to Avoid (from cFS context)
```c
/* WRONG in cFS context - MsgId is opaque type */
if (msgid == expected_msgid) { /* Don't do this */ }
```

### Note for ESA Stepping Terminal
For socket path comparison in the tool, `strcmp()` is appropriate since paths are strings:
```c
if (strcmp(socket_path, "/tmp/cfe_sim_stepping.sock") == 0) {
    /* OK - strings can use strcmp */
}
```

### References
- AGENTS.md: "**切勿**使用 `==` 比较 `CFE_SB_MsgId_t` — 使用 `CFE_SB_MsgId_Equal()`"

---

## 8. ❌ Using scanf() for Input Parsing

### Why Avoid
`scanf()` can leave unconsumed input in buffer and has complex behavior with invalid input. `fgets()` + `strtok()` is more predictable for REPL.

### Pattern to Avoid
```c
/* WRONG - scanf issues */
char cmd[64];
scanf("%s", cmd);  /* Buffer overflow risk! No size limit! */
```

### Correct Approach
```c
/* CORRECT - fgets + strtok */
char line[256];
if (fgets(line, sizeof(line), stdin)) {
    char *cmd = strtok(line, " \t");
    /* Parse safely */
}
```

### References
- Plan: REPL implementation uses `fgets()` + `strtok()` pattern

---

## 9. ❌ Blocking on accept() Without Timeout (Client Side)

### Why Avoid
While this is more relevant for servers, clients should also be careful about blocking indefinitely. The `wait` command with timeout=0 should allow SIGINT interruption.

### Pattern to Avoid
```c
/* WRONG - No way to interrupt */
while (1) {
    read(fd, buf, size);  /* Blocks forever if server dies */
}
```

### Correct Approach
```c
/* CORRECT - Check interrupt flag */
while (!interrupted && total < size) {
    ssize_t n = read(fd, buf + total, size - total);
    if (n < 0 && errno == EINTR) continue;
    /* ... */
}
```

### References
- Plan: Task 3 — `wait` command checks `ctx->interrupted` during blocking

---

## 10. ❌ strncat/strncpy Without Proper Null Termination

### Why Avoid
`strncpy` does not guarantee null termination if source is longer than count. `strncat` is safer but still tricky.

### Pattern to Avoid
```c
/* WRONG - Potential non-null-terminated string */
char path[108];
strncpy(path, user_input, sizeof(path));  /* Missing -1! */
/* path may not be null-terminated if input >= 108 chars */
```

### Correct Approach
```c
/* CORRECT - Ensure null termination */
char path[108];
strncpy(path, user_input, sizeof(path) - 1);
path[sizeof(path) - 1] = '\0';
```

### References
- Plan: Task 1 — socket path validation < 108 bytes
- TLPI examples use `strncpy(..., sizeof(...) - 1)`

---

## Summary Table

| Anti-Pattern | Severity | Impact |
|--------------|----------|--------|
| Packed structs | **Critical** | Protocol mismatch, data corruption |
| Long-lived connections | High | Violates design spec |
| readline/ncurses | High | External dependency violation |
| ANSI color | Low | Spec violation |
| signal() vs sigaction() | Medium | Portability issues |
| Non-safe signal handlers | **Critical** | Deadlocks, undefined behavior |
| scanf for input | Medium | Buffer overflows, parsing issues |
| strncpy without null-term | Medium | Buffer overreads |

---

## Additional Notes

### Error Handling Philosophy
Following the cFS conventions from AGENTS.md:
- Check return values of all system calls
- Use `perror()` for system errors
- Close file descriptors before returning errors
- Never leave sockets in half-open state

### Memory Safety
- Use fixed-size buffers on stack (no malloc for small structures)
- Always check bounds before string operations
- Prefer `snprintf()` over `sprintf()`

### Portability
- Stick to POSIX.1-2008 features
- Avoid platform-specific extensions
- Test on target platform early

## 11. 本次任务观测到的问题

- 本地 `lsp_diagnostics` 对 `esa/fsw/src/esa_stepping.c` 报告大量缺少头文件/类型错误，
  主要因为该 TU 依赖 mission 构建上下文（include path 与编译宏）而非独立 clangd 环境。
  该现象不影响 `make`/`make install` 的真实构建结果。
- `esa_stepping_terminal` 的 `step` 命令语法为 `step <timeout_ms>`，不带参数会返回用法错误；
  验证时需使用 `step 5000`。

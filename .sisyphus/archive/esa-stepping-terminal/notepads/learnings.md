# ESA Stepping Terminal — Pattern Research Findings

## 1. CMake Project Structure (Minimal Standalone)

### Recommended Pattern
**Source**: [funvake/cmkc99min](https://github.com/funvake/cmkc99min)

Minimal `CMakeLists.txt` for standalone C tools:
```cmake
cmake_minimum_required(VERSION 3.5)
project(esa_stepping_terminal C)

add_executable(esa_stepping_terminal src/main.c src/repl.c src/uds_client.c)

target_compile_options(esa_stepping_terminal PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS esa_stepping_terminal DESTINATION host)
```

**Key Takeaways**:
- Use `cmake_minimum_required(VERSION 3.5)` for broad compatibility
- Specify `C` in `project()` to restrict to C language
- Use `target_compile_options` with PRIVATE scope for warnings
- Optional install target for deployment

### References
- https://github.com/funvake/cmkc99min

---

## 2. Unix Domain Socket Client (Short Transaction Pattern)

### Recommended Pattern
**Source**: [The Linux Programming Interface — us_xfr_cl.c](https://man7.org/tlpi/code/online/dist/sockets/us_xfr_cl.c.html)

Key implementation details for short-lived UDS connections:

```c
struct sockaddr_un addr;
int sfd;

sfd = socket(AF_UNIX, SOCK_STREAM, 0);
if (sfd == -1) { /* handle error */ }

memset(&addr, 0, sizeof(struct sockaddr_un));
addr.sun_family = AF_UNIX;
strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

if (connect(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
    /* handle error */
}

/* transact... */

close(sfd);  /* Close after each transaction */
```

**Key Takeaways**:
- Always use `struct sockaddr_un` for AF_UNIX sockets (not generic `struct sockaddr`)
- Use `strncpy` with `sizeof(addr.sun_path) - 1` to respect 108-byte limit
- Clear structure with `memset` first for portability
- Close socket after each transaction (short-lived pattern)
- `sizeof(sun_path)` = 108 bytes on Linux (check with `sizeof()`)

### References
- https://man7.org/tlpi/code/online/dist/sockets/us_xfr_cl.c.html
- https://man7.org/tlpi/code/online/dist/sockets/ud_ucase_cl.c.html
- https://systemprogrammingatntu.github.io/mp2/unix_socket.html

---

## 3. Exact Read/Write Loops (Handling Partial I/O)

### Recommended Pattern
**Source**: [Stack Overflow — TCP socket partial reads](https://stackoverflow.com/questions/69574585/c-socket-programming-read-and-write-of-tcp-server-and-client-out-of-sync)

Implementation for reliable fixed-size message transfer:

```c
ssize_t read_exact(int fd, void *buf, size_t n) {
    size_t total = 0;
    char *p = buf;
    
    while (total < n) {
        ssize_t r = read(fd, p + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;  /* Interrupted by signal */
            return -1;  /* Error */
        }
        if (r == 0) return total;  /* EOF */
        total += r;
    }
    return total;
}

ssize_t write_exact(int fd, const void *buf, size_t n) {
    size_t total = 0;
    const char *p = buf;
    
    while (total < n) {
        ssize_t w = write(fd, p + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;  /* Should not happen in blocking mode */
        total += w;
    }
    return total;
}
```

**Key Takeaways**:
- Stream sockets (SOCK_STREAM) have no message boundaries
- `read()` may return fewer bytes than requested (partial read)
- `write()` may write fewer bytes than requested (partial write)
- Always handle `EINTR` (interrupted by signal)
- Use loops to ensure exact byte counts
- Essential for fixed-size protocol messages (8-byte request, 12-byte response)

### References
- https://stackoverflow.com/questions/69574585/c-socket-programming-read-and-write-of-tcp-server-and-client-out-of-sync
- https://www.binarytides.com/receive-full-data-with-recv-socket-function-in-c/
- https://en.ittrip.xyz/c-language/c-nonblocking-socket-handling

---

## 4. Line-Oriented REPL with fgets()

### Recommended Pattern
**Source**: [astrilo-monk/repl](https://github.com/astrilo-monk/repl)

Simple REPL implementation:

```c
#define MAX_LINE 256

int repl_run(void) {
    char line[MAX_LINE];
    
    while (1) {
        printf("est> ");
        fflush(stdout);
        
        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF - graceful exit */
            printf("\n");
            return 0;
        }
        
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        /* Skip empty lines */
        if (line[0] == '\0') continue;
        
        /* Parse and dispatch */
        char *cmd = strtok(line, " ");
        if (cmd == NULL) continue;
        
        /* Command dispatch */
        if (strcasecmp(cmd, "quit") == 0) {
            return 0;
        }
        /* ... other commands ... */
    }
}
```

**Key Takeaways**:
- Use `fgets()` for line input (pure POSIX, no readline dependency)
- Check for NULL return (EOF handling for graceful exit)
- Strip trailing newline for cleaner parsing
- Skip empty lines silently
- Use `strtok()` for simple tokenization
- Use `strcasecmp()` for case-insensitive command matching

### References
- https://github.com/astrilo-monk/repl
- https://github.com/baransu/mini-repl

---

## 5. getopt_long Usage Pattern

### Recommended Pattern
**Source**: [GNU getopt_long examples](https://github.com/hippie68/optparse99)

Standard CLI parsing with getopt_long:

```c
#include <getopt.h>
#include <stdio.h>

static struct option long_options[] = {
    {"socket", required_argument, 0, 's'},
    {"help",   no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Options:\n");
    printf("  -s, --socket PATH    Socket path (default: %s)\n", DEFAULT_SOCKET_PATH);
    printf("  -h, --help           Show this help\n");
}

int main(int argc, char *argv[]) {
    const char *socket_path = DEFAULT_SOCKET_PATH;
    int opt;
    
    while ((opt = getopt_long(argc, argv, "s:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                socket_path = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case '?':
                /* getopt_long already printed error message */
                return 1;
        }
    }
    
    /* Rest of program... */
}
```

**Key Takeaways**:
- Define `struct option` array with {name, has_arg, flag, val}
- Terminate array with {0, 0, 0, 0}
- Use short options string like `"s:h"` (colon = requires argument)
- `getopt_long()` handles error messages automatically
- Use `optarg` for argument values
- Provide friendly usage text

### References
- https://github.com/hippie68/optparse99
- https://github.com/likle/cargs
- https://github.com/NationalSecurityAgency/ghidra/blob/master/GPL/DemanglerGnu/src/demangler_gnu_v2_41/c/getopt1.c

---

## 6. SIGINT Handler with sigaction

### Recommended Pattern
**Source**: [codequoi.com — Signal Handling](https://www.codequoi.com/en/sending-and-intercepting-a-signal-in-c/)

Safe signal handler implementation:

```c
#include <signal.h>
#include <stdbool.h>

/* Global flag shared with main program */
volatile sig_atomic_t interrupted = 0;

void sigint_handler(int sig) {
    (void)sig;
    interrupted = 1;
    /* Do NOT call printf, malloc, or other non-async-signal-safe functions! */
}

int setup_sigint_handler(void) {
    struct sigaction sa;
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

/* In main loop, check interrupted flag */
while (!interrupted) {
    /* ... blocking operations ... */
    if (interrupted) {
        /* Handle interrupt */
        break;
    }
}
```

**Key Takeaways**:
- Use `sigaction()` instead of deprecated `signal()`
- Flag MUST be `volatile sig_atomic_t` type
- Handler should ONLY set flag (no printf, malloc, etc.)
- Check flag after blocking operations to support interruption
- `sig_atomic_t` guarantees atomic read/write
- `volatile` prevents compiler optimization caching

### References
- https://www.codequoi.com/en/sending-and-intercepting-a-signal-in-c/
- https://www.cs.wm.edu/~smherwig/courses/csci415-f2024/topics/signals/sig_atomic/index.html
- https://pvs-studio.com/en/blog/posts/cpp/0950/
- https://stackoverflow.com/questions/18907477/set-flag-in-signal-handler
- https://github.com/coreutils/coreutils/blob/master/src/dd.c (real-world example)

---

## 7. Command Dispatch Pattern

### Recommended Pattern
Simple switch-based dispatch with strtok:

```c
typedef int (*cmd_handler_t)(EST_ReplContext_t *ctx, int argc, char *argv[]);

typedef struct {
    const char *name;
    cmd_handler_t handler;
    const char *usage;
} cmd_entry_t;

static int cmd_help(EST_ReplContext_t *ctx, int argc, char *argv[]);
static int cmd_query(EST_ReplContext_t *ctx, int argc, char *argv[]);
/* ... */

static cmd_entry_t commands[] = {
    {"help",   cmd_help,   "显示帮助信息"},
    {"query",  cmd_query,  "查询当前状态"},
    {"begin",  cmd_begin,  "开始步进"},
    /* ... */
    {NULL, NULL, NULL}
};

int dispatch_command(EST_ReplContext_t *ctx, char *line) {
    char *argv[MAX_ARGS];
    int argc = 0;
    
    /* Tokenize */
    char *token = strtok(line, " \t");
    while (token && argc < MAX_ARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    
    if (argc == 0) return 0;
    
    /* Dispatch */
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcasecmp(argv[0], commands[i].name) == 0) {
            return commands[i].handler(ctx, argc, argv);
        }
    }
    
    fprintf(stderr, "未知命令: '%s'。输入 'help' 查看可用命令。\n", argv[0]);
    return -1;
}
```

**Key Takeaways**:
- Use table-driven dispatch for cleaner code
- `strcasecmp()` for case-insensitive matching
- `strtok()` for simple argument parsing
- Return error codes for command failures

---

## Summary of Reusable Patterns

| Pattern | Implementation | Source |
|---------|---------------|--------|
| CMake standalone | `cmake_minimum_required` + `project()` + `add_executable` | funvake/cmkc99min |
| UDS client | `socket(AF_UNIX)` → `connect()` → transact → `close()` | TLPI us_xfr_cl.c |
| Exact I/O | while loop with `EINTR` handling | Stack Overflow |
| REPL loop | `fgets()` → strip newline → `strtok()` → dispatch | astrilo-monk/repl |
| CLI parsing | `getopt_long()` with struct option array | optparse99 |
| SIGINT handling | `sigaction()` + `volatile sig_atomic_t` flag | codequoi.com |
| Command dispatch | Table-driven with function pointers | Common pattern |


## 8. Task 2 实现记录 (UDS 客户端层)
- 完成时间: 2026-03-23
- uds_client.h/c 实现路径: tools/esa_stepping_terminal/src/
- 函数签名已确认: est_uds_connect, est_uds_transact, est_uds_close, est_status_to_string, est_state_to_string
- read_exact/write_exact 均为静态内部函数
- 中文字符串: status 9 个 + state 5 个
- 构建验证通过: CMake + Make 退出码 0
- 符号表验证通过: 5 个公共符号全部导出
- case 覆盖验证通过: 14 个 case（9 status + 5 state）
- 错误处理验证通过: perror 2 处，fprintf(stderr) 2 处

## 9. Task 3 实现记录 (REPL 层)
- 完成时间: 2026-03-23 19:28:35 CST
- repl.h/c 实现路径: tools/esa_stepping_terminal/src/
- 命令表设计: 表驱动 (dispatch_command + 函数指针数组)
- 退出机制: quit/exit 返回 1，主循环 break
- step 实现: 复用 begin + wait 内部逻辑

## 10. Task 4 实现记录 (main.c)
- 完成时间: 2026-03-23 19:35:00 CST
- 实现: getopt_long ("-s PATH", "-h") + sigaction(SIGINT) + est_repl_run()
- 信号处理器: 只调用 est_repl_set_interrupted()，async-signal-safe 合规
- g_ctx: 静态全局指针，供信号处理器访问 ctx
- getopt_long 长选项数组: {"socket", required_argument, 0, 's'}, {"help", no_argument, 0, 'h'}, {0, 0, 0, 0}
- 编译验证通过: CMake + Make 退出码 0
- --help 验证通过: 输出包含 "-s, --socket PATH" 和默认路径说明
- --socket PATH 验证通过: 自定义 /tmp/custom.sock 被 REPL 输出确认
- 证据文件生成: task-4-cli-help.txt 和 task-4-socket-override.txt

## 11. Task 5 实现记录 (README + smoke test)
- 完成时间: Mon Mar 23 19:42:06 CST 2026
- README 路径: tools/esa_stepping_terminal/README.md
- Smoke test 全部通过 (10/10)

## 12. Wait 超时修复记录（InProc 轮询主动推进）
- 完成时间: 2026-03-23
- 修改文件: `esa/fsw/src/esa_stepping.c`
- 关键模式: 在 `ESA_Stepping_InProc_WaitStepComplete()` 的 `while(1)` 轮询内，
  当 `session_active=true` 且 `completion_ready=false` 时，主动调用
  `ESA_Stepping_Core_ReportSchMinorFrame(&stepping_core)` 推进一个量子。
- 触发条件说明: 步进模式下 wall-clock timer 关闭，TIME→SCH 自动链路不会触发，
  需要 wait 轮询阶段显式推进以置位 `completion_ready`。
- 验证结果: `begin + wait 5000` 成功，`step 5000` 成功，`status/query/begin` 保持可用。

#include "repl.h"
#include "protocol.h"
#include "uds_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <unistd.h>    /* usleep */

/**
 * @brief 命令处理器函数指针类型
 */
typedef int (*cmd_handler_fn)(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief REPL 命令表项
 */
typedef struct
{
    const char     *name;  /**< @brief 命令名称 */
    cmd_handler_fn  fn;    /**< @brief 命令处理函数 */
    const char     *brief; /**< @brief 命令简要说明 */
} EST_CommandEntry_t;

/**
 * @brief 执行 begin 请求
 * @param[in] ctx REPL 运行上下文
 * @return 成功返回 0，失败返回 -1
 */
static int est_do_begin(EST_ReplContext_t *ctx);

/**
 * @brief 执行 wait 请求
 * @param[in] ctx REPL 运行上下文
 * @param[in] timeout_ms 超时时间（毫秒）
 * @return 成功返回 0，失败返回 -1
 */
static int est_do_wait(EST_ReplContext_t *ctx, uint32_t timeout_ms);

/**
 * @brief help 命令处理器
 * @param[in] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 成功返回 0
 */
static int cmd_help(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief connect 命令处理器
 * @param[in,out] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 成功返回 0，失败返回 -1
 */
static int cmd_connect(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief query 命令处理器
 * @param[in] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 成功返回 0，失败返回 -1
 */
static int cmd_query(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief status 命令处理器
 * @param[in] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 成功返回 0，失败返回 -1
 */
static int cmd_status(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief begin 命令处理器
 * @param[in] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 成功返回 0，失败返回 -1
 */
static int cmd_begin(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief wait 命令处理器
 * @param[in] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 成功返回 0，失败返回 -1
 */
static int cmd_wait(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief step 命令处理器
 * @param[in] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 成功返回 0，失败返回 -1
 */
static int cmd_step(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief repeat 命令处理器
 * @param[in] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 成功返回 0，失败返回 -1
 */
static int cmd_repeat(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief quit/exit 命令处理器
 * @param[in] ctx REPL 运行上下文
 * @param[in] argc 参数数量
 * @param[in] argv 参数数组
 * @return 返回 1 通知主循环退出
 */
static int cmd_quit(EST_ReplContext_t *ctx, int argc, char *argv[]);

/**
 * @brief 分发命令行到对应处理器
 * @param[in,out] ctx REPL 运行上下文
 * @param[in,out] line 输入命令行（会被原地分词修改）
 * @return 0 正常，1 表示请求退出，-1 表示命令错误
 */
static int dispatch_command(EST_ReplContext_t *ctx, char *line);

/**
 * @brief REPL 支持的命令表
 */
static EST_CommandEntry_t g_commands[] = {
    {"help",    cmd_help,    "显示命令帮助"},
    {"connect", cmd_connect, "显示或更新 Socket 路径"},
    {"query",   cmd_query,   "查询原始状态字段"},
    {"status",  cmd_status,  "查询并显示友好状态文本"},
    {"begin",   cmd_begin,   "开始一次步进"},
    {"wait",    cmd_wait,    "等待步进完成：wait <timeout_ms>"},
    {"step",    cmd_step,    "执行一步：step <timeout_ms>"},
    {"repeat",  cmd_repeat,  "重复步进：repeat <count> <timeout_ms> [interval_ms]"},
    {"quit",    cmd_quit,    "退出终端"},
    {"exit",    cmd_quit,    "退出终端"},
    {NULL,       NULL,        NULL}
};

void est_repl_set_interrupted(EST_ReplContext_t *ctx)
{
    if (ctx != NULL)
    {
        ctx->interrupted = 1;
    }
}

int est_repl_run(EST_ReplContext_t *ctx)
{
    char line[256];

    if (ctx == NULL)
    {
        fprintf(stderr, "REPL 上下文无效\n");
        return -1;
    }

    printf("ESA Stepping Terminal v1.0\n");
    printf("默认 Socket: %s\n", ctx->socket_path);
    printf("输入 'help' 查看可用命令\n\n");

    while (!ctx->interrupted)
    {
        int ret;

        printf("est> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            printf("\n");
            return 0;
        }

        {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n')
            {
                line[len - 1] = '\0';
            }
        }

        if (line[0] == '\0')
        {
            continue;
        }

        ret = dispatch_command(ctx, line);
        if (ret == 1)
        {
            break;
        }
    }

    return 0;
}

static int cmd_help(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    int i;
    (void)ctx;
    (void)argc;
    (void)argv;

    printf("可用命令:\n");
    for (i = 0; g_commands[i].name != NULL; ++i)
    {
        printf("  %-8s - %s\n", g_commands[i].name, g_commands[i].brief);
    }

    return 0;
}

static int cmd_connect(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("当前 Socket 路径: %s\n", ctx->socket_path);
        return 0;
    }

    strncpy(ctx->socket_path, argv[1], sizeof(ctx->socket_path) - 1);
    ctx->socket_path[sizeof(ctx->socket_path) - 1] = '\0';
    printf("Socket 路径已更新为: %s\n", ctx->socket_path);
    return 0;
}

static int cmd_query(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    EST_Request_t       req  = {0};
    EST_QueryResponse_t resp = {0};
    int                 rc;

    (void)argc;
    (void)argv;

    req.opcode = EST_OPCODE_QUERY_STATE;
    req.timeout_ms = 0;

    rc = est_uds_transact(ctx->socket_path, &req, sizeof(req), &resp, sizeof(resp));
    if (rc < 0)
    {
        fprintf(stderr, "查询失败\n");
        return -1;
    }

    printf("状态码: %d\n", resp.status);
    printf("步进状态: %u\n", resp.state);
    printf("触发计数: %u\n", resp.trigger_count);
    return 0;
}

static int cmd_status(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    EST_Request_t       req  = {0};
    EST_QueryResponse_t resp = {0};
    int                 rc;

    (void)argc;
    (void)argv;

    req.opcode = EST_OPCODE_QUERY_STATE;
    req.timeout_ms = 0;

    rc = est_uds_transact(ctx->socket_path, &req, sizeof(req), &resp, sizeof(resp));
    if (rc < 0)
    {
        fprintf(stderr, "查询失败\n");
        return -1;
    }

    printf("状态: %s\n", est_status_to_string(resp.status));
    printf("步进状态: %s\n", est_state_to_string(resp.state));
    printf("触发计数: %u\n", resp.trigger_count);
    return 0;
}

static int est_do_begin(EST_ReplContext_t *ctx)
{
    EST_Request_t req = {0};
    int32_t       status = 0;
    int           rc;

    req.opcode = EST_OPCODE_BEGIN_STEP;
    req.timeout_ms = 0;

    rc = est_uds_transact(ctx->socket_path, &req, sizeof(req), &status, sizeof(status));
    if (rc < 0)
    {
        fprintf(stderr, "begin 失败\n");
        return -1;
    }

    printf("begin 结果: %s\n", est_status_to_string(status));
    if (status != EST_STATUS_SUCCESS)
    {
        return -1;
    }

    return 0;
}

static int cmd_begin(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return est_do_begin(ctx);
}

static int est_do_wait(EST_ReplContext_t *ctx, uint32_t timeout_ms)
{
    EST_Request_t req = {0};
    int32_t       status = 0;
    int           rc;

    if (ctx->interrupted)
    {
        fprintf(stderr, "已中断\n");
        return -1;
    }

    if (timeout_ms == 0)
    {
        printf("警告: timeout_ms=0 将无限阻塞，Ctrl+C 可中断\n");
    }

    req.opcode = EST_OPCODE_WAIT_STEP_COMPLETE;
    req.timeout_ms = timeout_ms;

    rc = est_uds_transact(ctx->socket_path, &req, sizeof(req), &status, sizeof(status));
    if (ctx->interrupted)
    {
        fprintf(stderr, "已中断\n");
        return -1;
    }

    if (rc < 0)
    {
        fprintf(stderr, "wait 失败\n");
        return -1;
    }

    printf("wait 结果: %s\n", est_status_to_string(status));
    if (status != EST_STATUS_SUCCESS)
    {
        return -1;
    }

    return 0;
}

static int cmd_wait(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    uint32_t timeout_ms;

    if (argc < 2)
    {
        fprintf(stderr, "用法: wait <timeout_ms>\n");
        return -1;
    }

    timeout_ms = (uint32_t)strtoul(argv[1], NULL, 10);
    return est_do_wait(ctx, timeout_ms);
}

static int cmd_step(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    uint32_t timeout_ms;

    if (argc < 2)
    {
        fprintf(stderr, "用法: step <timeout_ms>\n");
        return -1;
    }

    timeout_ms = (uint32_t)strtoul(argv[1], NULL, 10);
    if (est_do_begin(ctx) < 0)
    {
        return -1;
    }

    return est_do_wait(ctx, timeout_ms);
}

static int cmd_repeat(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    uint32_t count;
    uint32_t timeout_ms;
    uint32_t interval_ms = 0;
    uint32_t i;

    if (argc < 3)
    {
        fprintf(stderr, "用法: repeat <count> <timeout_ms> [interval_ms]\n");
        return -1;
    }

    count = (uint32_t)strtoul(argv[1], NULL, 10);
    timeout_ms = (uint32_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4)
    {
        interval_ms = (uint32_t)strtoul(argv[3], NULL, 10);
    }

    if (interval_ms > 4000000U)
    {
        fprintf(stderr, "警告: interval_ms 过大，已限制为 4000000\n");
        interval_ms = 4000000U;
    }

    for (i = 0; i < count; ++i)
    {
        if (ctx->interrupted)
        {
            fprintf(stderr, "已中断\n");
            return -1;
        }

        printf("[%u/%u] ", i + 1U, count);
        if (est_do_begin(ctx) < 0)
        {
            fprintf(stderr, "在第 %u 次步进时失败\n", i + 1U);
            return -1;
        }

        if (est_do_wait(ctx, timeout_ms) < 0)
        {
            fprintf(stderr, "在第 %u 次步进时失败\n", i + 1U);
            return -1;
        }

        if (interval_ms > 0U)
        {
            usleep((useconds_t)(interval_ms * 1000U));
        }
    }

    return 0;
}

static int cmd_quit(EST_ReplContext_t *ctx, int argc, char *argv[])
{
    (void)ctx;
    (void)argc;
    (void)argv;
    printf("再见\n");
    return 1;
}

static int dispatch_command(EST_ReplContext_t *ctx, char *line)
{
    char *argv[16];
    int   argc = 0;
    char *token;
    int   i;

    token = strtok(line, " \t");
    while (token != NULL && argc < (int)(sizeof(argv) / sizeof(argv[0])))
    {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }

    if (argc == 0)
    {
        return 0;
    }

    for (i = 0; g_commands[i].name != NULL; ++i)
    {
        if (strcasecmp(argv[0], g_commands[i].name) == 0)
        {
            return g_commands[i].fn(ctx, argc, argv);
        }
    }

    fprintf(stderr, "未知命令: '%s'。输入 'help' 查看可用命令。\n", argv[0]);
    return -1;
}

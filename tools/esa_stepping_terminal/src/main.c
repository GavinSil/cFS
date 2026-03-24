/**
 * @file main.c
 * @brief ESA Stepping Terminal 主入口
 */

#include "protocol.h"
#include "repl.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

/** @brief 全局 REPL 上下文指针（用于 SIGINT 信号处理器） */
static EST_ReplContext_t *g_ctx = NULL;

/**
 * @brief SIGINT 信号处理器
 * @param[in] sig 信号编号（未使用）
 */
static void sigint_handler(int sig)
{
    (void)sig;
    if (g_ctx != NULL)
    {
        est_repl_set_interrupted(g_ctx);
    }
}

/**
 * @brief 打印用法信息
 * @param[in] prog 程序名称（argv[0]）
 */
static void print_usage(const char *prog)
{
    printf("用法: %s [选项]\n", prog);
    printf("\n");
    printf("选项:\n");
    printf("  -s, --socket PATH    指定 Unix Domain Socket 路径\n");
    printf("                       （默认: %s）\n", EST_DEFAULT_SOCKET_PATH);
    printf("  -h, --help           显示此帮助信息并退出\n");
}

/**
 * @brief 程序主入口
 * @param[in] argc 命令行参数数量
 * @param[in] argv 命令行参数数组
 * @return 0 正常退出，非 0 错误退出
 */
int main(int argc, char *argv[])
{
    EST_ReplContext_t ctx;
    struct sigaction sa;
    const char *socket_path = EST_DEFAULT_SOCKET_PATH;
    int opt;

    static struct option long_options[] = {
        {"socket", required_argument, 0, 's'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    /* 解析命令行参数 */
    while ((opt = getopt_long(argc, argv, "s:h", long_options, NULL)) != -1)
    {
        switch (opt)
        {
            case 's':
                socket_path = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case '?':
                /* getopt_long 已打印错误信息 */
                return 1;
            default:
                break;
        }
    }

    /* 初始化 REPL 上下文 */
    memset(&ctx, 0, sizeof(ctx));
    strncpy(ctx.socket_path, socket_path, sizeof(ctx.socket_path) - 1);
    ctx.socket_path[sizeof(ctx.socket_path) - 1] = '\0';
    ctx.interrupted = 0;

    /* 设置全局 context 指针（供信号处理器使用） */
    g_ctx = &ctx;

    /* 注册 SIGINT 信号处理器 */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        return 1;
    }

    /* 运行 REPL */
    return est_repl_run(&ctx);
}

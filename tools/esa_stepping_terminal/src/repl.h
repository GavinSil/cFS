#ifndef EST_REPL_H
#define EST_REPL_H

#include <stdint.h>
#include <signal.h>

/**
 * @brief REPL 运行上下文
 */
typedef struct {
    char socket_path[108]; /**< @brief Unix Domain Socket 路径 */
    volatile sig_atomic_t interrupted; /**< @brief SIGINT 中断标志（由信号处理器设置） */
} EST_ReplContext_t;

/**
 * @brief 运行 REPL 主循环
 * @param[in,out] ctx REPL 运行上下文（包含 socket_path 和 interrupted 标志）
 * @return 0 表示正常退出，-1 表示初始化失败
 */
int est_repl_run(EST_ReplContext_t *ctx);

/**
 * @brief 设置中断标志（由 SIGINT 信号处理器调用）
 * @param[in,out] ctx REPL 运行上下文
 */
void est_repl_set_interrupted(EST_ReplContext_t *ctx);

#endif /* EST_REPL_H */

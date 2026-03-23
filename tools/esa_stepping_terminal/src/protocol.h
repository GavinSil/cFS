/**
 * @file protocol.h
 * @brief ESA Stepping Terminal 协议定义
 *
 * 定义与 ESA Stepping 核心的通信协议，包括操作码、请求/响应结构体、状态码和状态机。
 */

#ifndef EST_PROTOCOL_H
#define EST_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ==================== 操作码定义 ==================== */

/**
 * @brief 开始步进操作码
 *
 * 用于启动新的步进周期。
 */
#define EST_OPCODE_BEGIN_STEP 1

/**
 * @brief 查询状态操作码
 *
 * 用于查询当前步进状态和触发器计数。
 */
#define EST_OPCODE_QUERY_STATE 2

/**
 * @brief 等待步进完成操作码
 *
 * 用于阻塞等待当前步进周期完成。
 */
#define EST_OPCODE_WAIT_STEP_COMPLETE 3

/* ==================== 请求结构体 ==================== */

/**
 * @brief EST 请求结构体
 *
 * 固定大小的请求结构，用于所有操作码。
 * 自然对齐（无填充约束）：
 *   - opcode: 字节 0, 1 字节
 *   - 3 字节填充（由编译器自动生成）
 *   - timeout_ms: 字节 4-7, 4 字节
 *   - 总大小: 8 字节
 */
typedef struct
{
    uint8_t  opcode;      /**< 操作码 (EST_OPCODE_*) */
    uint32_t timeout_ms;  /**< 超时时间（毫秒）*/
} EST_Request_t;

/**
 * @brief 验证 EST_Request_t 大小与服务器线路格式匹配
 */
_Static_assert(sizeof(EST_Request_t) == 8, "EST_Request_t 大小与服务器线路格式不匹配");

/* ==================== 响应结构体 ==================== */

/**
 * @brief EST 查询响应结构体
 *
 * 固定大小的查询状态响应结构。
 * 自然对齐（无填充约束）：
 *   - status: 字节 0-3, 4 字节
 *   - state: 字节 4-7, 4 字节
 *   - trigger_count: 字节 8-11, 4 字节
 *   - 总大小: 12 字节
 */
typedef struct
{
    int32_t  status;        /**< 操作状态码 */
    uint32_t state;         /**< 当前步进状态 */
    uint32_t trigger_count; /**< 待处理触发器计数 */
} EST_QueryResponse_t;

/**
 * @brief 验证 EST_QueryResponse_t 大小与服务器线路格式匹配
 */
_Static_assert(sizeof(EST_QueryResponse_t) == 12, "EST_QueryResponse_t 大小与服务器线路格式不匹配");

/* ==================== 状态码定义 ==================== */

/**
 * @brief 操作成功
 */
#define EST_STATUS_SUCCESS 0

/**
 * @brief 通用操作失败
 */
#define EST_STATUS_FAILURE -1

/**
 * @brief 步进已开始：先前的会话未解决，拒绝开始新会话
 */
#define EST_STATUS_DUPLICATE_BEGIN -2

/**
 * @brief 系统未就绪：请求的操作前提条件未满足
 */
#define EST_STATUS_NOT_READY -3

/**
 * @brief 操作超时：等待步进完成时超时
 */
#define EST_STATUS_TIMEOUT -4

/**
 * @brief 非法完成：报告完成但无对应触发器
 */
#define EST_STATUS_ILLEGAL_COMPLETE -5

/**
 * @brief 传输错误：Unix Domain Socket I/O 或连接错误
 */
#define EST_STATUS_TRANSPORT_ERROR -6

/**
 * @brief 协议错误：UDS 协议帧错误或操作码错误
 */
#define EST_STATUS_PROTOCOL_ERROR -7

/**
 * @brief 非法状态：操作在当前步进状态下非法
 */
#define EST_STATUS_ILLEGAL_STATE -8

/* ==================== 步进状态枚举 ==================== */

/**
 * @brief 步进核心状态枚举
 *
 * 表示当前步进的状态转移状态。
 */
typedef enum
{
    EST_STATE_INIT     = 0, /**< 核心未初始化 */
    EST_STATE_READY    = 1, /**< 等待步进命令 */
    EST_STATE_RUNNING  = 2, /**< 正在执行步进 */
    EST_STATE_WAITING  = 3, /**< 等待确认/完成 */
    EST_STATE_COMPLETE = 4  /**< 步进完成，准备下一步 */
} EST_State_e;

/* ==================== 配置常量 ==================== */

/**
 * @brief 默认 Unix Domain Socket 路径
 *
 * 用于与远程 ESA Stepping 核心通信的套接字路径。
 */
#define EST_DEFAULT_SOCKET_PATH "/tmp/cfe_sim_stepping.sock"

#endif /* EST_PROTOCOL_H */

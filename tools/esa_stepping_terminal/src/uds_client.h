/**
 * @file uds_client.h
 * @brief ESA Stepping Terminal UDS 客户端层公共 API
 *
 * 提供与 ESA Stepping 核心的 Unix Domain Socket 通信接口，包括连接管理、
 * 事务处理和状态码/状态转换辅助函数。
 */

#ifndef EST_UDS_CLIENT_H
#define EST_UDS_CLIENT_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief 连接到 UDS 服务器
 * @param[in] socket_path Unix Domain Socket 路径（例如: /tmp/cfe_sim_stepping.sock）
 * @return 成功返回文件描述符（>= 0），失败返回 -1
 */
int est_uds_connect(const char *socket_path);

/**
 * @brief 执行完整的 UDS 事务（连接 → 写请求 → 读响应 → 关闭）
 * @param[in] socket_path Unix Domain Socket 路径
 * @param[in] req 请求数据指针
 * @param[in] req_size 请求数据大小（字节）
 * @param[out] resp 响应缓冲区指针
 * @param[in] resp_size 响应缓冲区大小（字节）
 * @return 成功返回 0，失败返回 -1
 */
int est_uds_transact(const char *socket_path,
                     const void *req, size_t req_size,
                     void *resp, size_t resp_size);

/**
 * @brief 关闭 UDS 连接
 * @param[in] fd 文件描述符（由 est_uds_connect 返回）
 */
void est_uds_close(int fd);

/**
 * @brief 将状态码转换为中文字符串
 * @param[in] status 状态码（EST_STATUS_* 常量）
 * @return 对应的中文描述字符串
 */
const char *est_status_to_string(int32_t status);

/**
 * @brief 将步进状态转换为中文字符串
 * @param[in] state 步进状态（EST_STATE_* 枚举值）
 * @return 对应的中文描述字符串
 */
const char *est_state_to_string(uint32_t state);

#endif /* EST_UDS_CLIENT_H */

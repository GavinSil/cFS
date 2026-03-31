/**
 * @file
 * @ingroup esa
 * @brief       ESA 仿真步进公共头文件
 * @author      gaoyuan
 * @date        2026-03-20
 *
 * @details     本文件包含 ESA 步进模块的公共 API 定义，包括初始化、Hook 函数和控制适配器接口。
 */
#ifndef ESA_SIM_STEPPING_H
#define ESA_SIM_STEPPING_H

/****************************************************************************************
                                     INCLUDE FILES
 ***************************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************************
                           SHARED STEPPING STATUS TAXONOMY
 ***************************************************************************************/

/**
 * @brief       仿真步进控制/诊断接口的共享状态码
 *
 * @details     这些常量由步进核心和适配器使用，以便在进程内和 UDS 控制路径中一致地表示失败类别。
 */
#define ESA_SIM_STEPPING_STATUS_SUCCESS          0  /*!< 操作成功 */
#define ESA_SIM_STEPPING_STATUS_FAILURE          -1 /*!< 通用失败 */
#define ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN  -2 /*!< 开始步进被拒绝：前一个会话未解决 */
#define ESA_SIM_STEPPING_STATUS_NOT_READY        -3 /*!< 系统/核心未就绪 */
#define ESA_SIM_STEPPING_STATUS_TIMEOUT          -4 /*!< 操作超时 */
#define ESA_SIM_STEPPING_STATUS_ILLEGAL_COMPLETE -5 /*!< 完成报告无匹配触发器 */
#define ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR  -6 /*!< UDS 传输 I/O/连接错误 */
#define ESA_SIM_STEPPING_STATUS_PROTOCOL_ERROR   -7 /*!< UDS 协议帧/操作码错误 */
#define ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE    -8 /*!< 当前步进状态下操作非法 */

/****************************************************************************************
                              INITIALIZATION API
 ***************************************************************************************/

/**
 * @brief       初始化 ESA 步进模块（核心 + UDS 传输）
 *
 * @details     必须在 BSP main() 中早期调用，在 OS_Application_Startup() 之前。
 *              初始化步进核心状态机和 UDS 控制适配器。
 *              即使未定义 CFE_SIM_STEPPING 也可以安全调用（变为空操作）。
 *
 * @note        此函数应在系统初始化期间调用一次。
 * @note        当未定义 CFE_SIM_STEPPING 时，此函数为空操作存根。
 */
void ESA_Init(void);

/****************************************************************************************
                             STEPPING HOOK DECLARATIONS
 ***************************************************************************************/

/**
 * @brief       步进启用时获取仿真时间的钩子函数
 *
 * @details     当启用 CFE_SIM_STEPPING 时，由 PSP 时间基准模块调用以获取仿真时间
 *              而非墙钟时间。这允许在仿真/测试环境中实现确定性时间推进。
 *
 * @param[out]  sim_time_ns    用于存储自纪元以来的仿真时间（纳秒）的指针
 *
 * @retval      true           成功提供仿真时间
 * @retval      false          钩子未实现或步进已禁用（改用墙钟时间）
 *
 * @note        此函数已声明，但仅在定义 CFE_SIM_STEPPING 时提供实现。
 *              未定义时，此函数为返回 false 的桩函数。
 */
bool ESA_Stepping_Hook_GetTime(uint64_t *sim_time_ns);

/**
 * @brief       查询请求的 TaskDelay 是否可由步进处理的钩子函数
 *
 * @details     由 OSAL TaskDelay 钩子调用，用于判断步进核心是否可以接管延迟
 *              （返回 true），或调用者是否应继续使用正常的墙钟睡眠
 *              （返回 false）。
 *
 *              使用保守的资格判定逻辑：核心必须已初始化，TaskDelay 接管门控
 *              必须为 ON，任务必须明确注册参与，且请求的延迟必须是
 *              step_quantum_ns 的整数倍。当门控为 OFF（默认）或任务未注册参与时，
 *              始终返回 false。
 *
 * @param[in]   task_id       请求延迟的任务运行时 ID
 * @param[in]   delay_ms      请求的延迟时间（毫秒）
 *
 * @retval      true          延迟可由步进处理（跳过墙钟睡眠）
 * @retval      false         延迟无法由步进处理（继续使用墙钟睡眠）
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时提供实现。
 *              未定义时，此函数为返回 false 的桩函数。
 */
bool ESA_Stepping_Hook_TaskDelayEligible(uint32_t task_id, uint32_t delay_ms);

/**
 * @brief       查询步进会话是否处于活动状态的钩子函数
 *
 * @details     由 OSAL 同步原语（BinSem、CondVar 等）调用，用于判断是否应使用
 *              ESA 等待路径（返回 true），或回退到传统 POSIX 等待（返回 false）。
 *
 *              仅当步进核心已初始化且步进会话处于活动状态时返回 true。
 *              单元测试和非步进上下文返回 false，使原语使用传统 POSIX 等待路径。
 *
 * @retval      true          步进会话处于活动状态（使用 ESA 等待）
 * @retval      false         无活动会话（使用传统 POSIX 等待）
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时提供实现。
 *              未定义时，此函数为返回 false 的桩函数。
 */
bool ESA_Stepping_Hook_IsSessionActive(void);

/**
 * @brief       阻塞当前任务直到仿真延迟由显式步进量子满足
 *
 * @details     PSP 拥有的步进控制 TaskDelay 阻塞等待。将调用转发到
 *              ESA_Stepping_Core_WaitForDelayExpiry 的轻量封装，使用共享步进核心实例。
 *              轮询 sim_time_ns 直到足够的显式步进量子推进以满足延迟。
 *              当无步进发出时，防止延迟驱动任务自行推进。
 *
 * @param[in]   task_id       请求延迟的任务运行时 ID
 * @param[in]   delay_ms      请求的延迟时间（毫秒）
 *
 * @retval      0             成功（延迟已满足）
 * @retval      -1            错误（无效核心或参数）
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时可用。
 *              未定义时，此函数为返回 -1 的桩函数。
 */
int32_t ESA_Stepping_WaitForDelayExpiry(uint32_t task_id, uint32_t delay_ms);

/****************************************************************************************
                     IN-PROCESS CONTROL ADAPTER API (PUBLIC)
 ***************************************************************************************/

/**
 * @brief       开始仿真步进（进程内控制适配器）
 *
 * @details     启动新的仿真步进。步进核心从 READY 状态转换为 RUNNING 状态，
 *              并等待步进事件发生。立即返回，不阻塞。
 *              这是一个轻量的进程内控制接口，将调用转发到共享步进核心。
 *
 * @retval      ESA_SIM_STEPPING_STATUS_SUCCESS          成功
 * @retval      ESA_SIM_STEPPING_STATUS_NOT_READY        步进未初始化或未处于就绪状态
 * @retval      ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN  前一个会话未解决
 * @retval      ESA_SIM_STEPPING_STATUS_FAILURE          其他失败
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时可用。
 *              未定义时，此函数为返回 -1 的桩函数。
 */
int32_t ESA_Stepping_InProc_BeginStep(void);

/**
 * @brief       等待当前步进完成（进程内控制适配器）
 *
 * @details     阻塞直到步进核心指示当前步进周期完成（所有预期触发器已报告并确认，
 *              所有事件已处理）。这是一个轻量的进程内控制接口，查询共享步进核心。
 *
 * @param[in]   timeout_ms                          超时时间（毫秒）
 *                                                  （0 = 永久等待，~0U = 非阻塞轮询）
 *
 * @retval      ESA_SIM_STEPPING_STATUS_SUCCESS      步进成功完成
 * @retval      ESA_SIM_STEPPING_STATUS_TIMEOUT      超过有限超时时间
 * @retval      ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE 无活动步进会话
 * @retval      ESA_SIM_STEPPING_STATUS_FAILURE      非阻塞未完成或其他失败
 * @retval      ESA_SIM_STEPPING_STATUS_NOT_READY    核心未初始化
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时可用。
 *              未定义时，此函数为返回 -1 的桩函数。
 * @note        此函数可能阻塞；不适用于中断处理程序。
 */
int32_t ESA_Stepping_InProc_WaitStepComplete(uint32_t timeout_ms);

/**
 * @brief       查询当前步进状态（进程内控制适配器）
 *
 * @details     返回步进核心的当前状态，不阻塞。允许进程内调用者确定步进就绪状态和进度。
 *              这是一个轻量的进程内控制接口，查询共享步进核心。
 *
 * @param[out]  state_out               存储当前状态枚举值的指针（可为 NULL）
 * @param[out]  trigger_count           存储当前待处理触发器数量的指针（可为 NULL）
 *
 * @retval      0                       状态查询成功
 * @retval      -1                      核心未初始化或指针验证失败
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时可用。
 *              未定义时，此函数为返回 -1 的桩函数。
 * @note        所有输出参数均为可选（不需要时可传入 NULL）。
 */
int32_t ESA_Stepping_InProc_QueryState(uint32_t *state_out, uint32_t *trigger_count);

/****************************************************************************************
                     UDS CONTROL ADAPTER API (PUBLIC)
 ***************************************************************************************/

/**
 * @brief       初始化 UDS 控制适配器（Unix 域套接字适配器）
 *
 * @details     创建并绑定 Linux AF_UNIX 套接字端点用于外部步进控制。
 *              必须在核心初始化后调用，在处理任何 UDS 控制请求之前。
 *              建立 UDS 监听套接字生命周期（仅端点；协议处理延后）。
 *              执行完整的 Linux 套接字初始化：创建套接字、准备稳定套接字路径、
 *              清理旧路径（如有需要）、绑定并监听。
 *
 *              这是转发到同一步进核心的轻量适配层，而非第二个状态机。
 *              UDS 适配器仅管理端点生命周期；核心维护所有步进状态和语义。
 *
 * @retval      0                       成功（UDS 端点已初始化并监听）
 * @retval      -1                      步进未初始化、适配器已初始化或套接字初始化失败
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时可用。
 *              未定义时，此函数为返回 -1 的桩函数。
 * @note        必须在 UDS_Service() 能成功之前调用此函数。
 * @note        UDS 适配器与进程内适配器共享同一步进核心。
 * @note        接字路径为 /tmp/cfe_sim_stepping.sock（仅 Linux，此环境稳定路径）。
 */
int32_t ESA_Stepping_UDS_Init(void);

/**
 * @brief       服务一个 UDS 控制请求（Unix 域套接字适配器）
 *
 * @details     在 UDS 监听套接字上执行最小传输级服务（非阻塞）：
 *              - 验证核心和适配器已初始化
 *              - 对最多一个待处理客户端连接执行非阻塞连接接受
 *              - 立即关闭任何已接受的客户端，不解析或分发
 *              - 返回一致的适配器级状态（0 = 空闲/已处理，-1 = 未就绪）
 *
 *              此层严格为传输级连接接受/关闭行为。尚无线协议解析、
 *              无请求分发、无帧语义。真正的协议语义
 *              （BeginStep、WaitStepComplete、QueryState 分发）延后到后续任务。
 *
 *              立即返回，无论是否有请求存在（非阻塞）。
 *
 * @retval      ESA_SIM_STEPPING_STATUS_SUCCESS          空闲或请求处理成功
 * @retval      ESA_SIM_STEPPING_STATUS_NOT_READY        适配器/核心未初始化
 * @retval      ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR  套接字传输失败
 * @retval      ESA_SIM_STEPPING_STATUS_PROTOCOL_ERROR   无效/未知操作码帧
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时可用。
 *              未定义时，此函数为返回 -1 的桩函数。
 * @note        此函数不阻塞，应从步进循环周期性调用。
 * @note        UDS 适配器与进程内适配器共享同一步进核心。
 */
int32_t ESA_Stepping_UDS_Service(void);

/**
 * @brief       关闭 UDS 控制适配器（Unix 域套接字适配器）
 *
 * @details     清洁关闭步进核心的 Unix 域套接字适配器并释放 UDS 特定资源。
 *              关闭监听套接字文件描述符并从文件系统取消链接 Unix 域套接字路径。
 *              共享步进核心本身不会关闭（那是核心所有者的责任）。
 *              此适配层仅管理 UDS 端点生命周期和相关资源。
 *
 * @retval      0                       成功（UDS 端点已关闭、fd 已关闭、路径已取消链接）
 * @retval      -1                      适配器未初始化或关闭失败
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时可用。
 *              未定义时，此函数为返回 -1 的桩函数。
 * @note        关闭后，UDS_Service() 将不接受新请求，直到重新初始化。
 * @note        UDS 适配器与进程内适配器共享同一步进核心；
 *              两个适配器可以共存并使用同一核心。
 */
int32_t ESA_Stepping_UDS_Shutdown(void);

/**
 * @brief       服务 UDS 适配器（非阻塞，每次调用处理单个请求）
 *
 * @details     从周期性钩子（例如步进定时器滴答或 TIME 任务周期边界）
 *              调用 ESA_Stepping_UDS_Service() 的轻量封装。
 *              立即返回，无论是否存在客户端请求。
 *              非阻塞，适合从紧凑事件循环调用。
 *
 * @retval      0                       无客户端待处理或客户端请求处理成功
 * @retval      -1                      适配器未初始化或服务失败
 *
 * @note        仅在定义 CFE_SIM_STEPPING 时可用。
 *              未定义时，此函数为返回 -1 的桩函数。
 * @note        设计为在每个钩子周期从步进钩子调用。
 */
int32_t ESA_Stepping_UDS_RunOnce(void);

#endif /* ESA_SIM_STEPPING_H */

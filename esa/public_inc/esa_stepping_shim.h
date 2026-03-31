/**
 * @file
 * @ingroup esa
 * @brief       ESA 步进 Shim 公共 ABI 头文件
 * @author      gaoyuan
 * @date        2026-03-20
 *
 * @details     本头文件声明轻量的事实报告入口点，供 OSAL/TIME/SCH 模块调用。保持钩子层轻量且语义集中。
 */
#ifndef NATIVE_STEPPING_SHIM_H
#define NATIVE_STEPPING_SHIM_H

/****************************************************************************************
                                         包含文件
 ***************************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************************
                             STEPPING SHIM 事件枚举与类型
 ***************************************************************************************/

/**
 * @brief       步进事件类型枚举
 *
 * @details     每种事件类型代表一个独特的边界条件，可能触发步进暂停或同步点。
 *              事件类型决定调用哪个核心报告函数。
 */
typedef enum ESA_Stepping_EventKind
{
    ESA_SIM_STEPPING_EVENT_TASK_DELAY = 0,      /*!< 任务延迟请求（OSAL）- 旧版单事件 */
    ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK,      /*!< 任务延迟确认（阻塞前，等待候选） */
    ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE, /*!< 任务延迟完成（阻塞后，操作完成） */
    ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE,       /*!< 队列接收阻塞（OSAL）- 旧版单事件 */
    ESA_SIM_STEPPING_EVENT_BINSEM_TAKE,         /*!< 二值信号量获取（OSAL）- 旧版单事件 */
    ESA_SIM_STEPPING_EVENT_TIME_TASK_CYCLE,     /*!< TIME 模块任务周期开始 */
    ESA_SIM_STEPPING_EVENT_1HZ_BOUNDARY,        /*!< 1Hz 时钟检测 */
    ESA_SIM_STEPPING_EVENT_TONE_SIGNAL,         /*!< 音调信号触发（PSP/SCH） */
    ESA_SIM_STEPPING_EVENT_SCH_SEMAPHORE_WAIT,  /*!< SCH 等待信号量 */
    ESA_SIM_STEPPING_EVENT_SCH_MINOR_FRAME,     /*!< SCH 小帧边界 */
    ESA_SIM_STEPPING_EVENT_SCH_MAJOR_FRAME,     /*!< SCH 大帧边界 */
    ESA_SIM_STEPPING_EVENT_SCH_SEND_TRIGGER,        /*!< SCH 发送触发器命令 */
    ESA_SIM_STEPPING_EVENT_SCH_DISPATCH_COMPLETE,   /*!< SCH 任务分发完成 */
    ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_RECEIVE, /*!< 核心服务命令管道接收 */
    ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK,   /*!< 队列接收确认（阻塞前，等待候选） */
    ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE, /*!< 队列接收完成（阻塞后，操作完成） */
    ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK,     /*!< 二值信号量获取确认（等待前，候选） */
    ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE, /*!< 二值信号量获取完成（等待后，完成） */
    ESA_SIM_STEPPING_EVENT_TIME_TONE_SEM_CONSUME,   /*!< TIME 模块消费音调信号量 */
    ESA_SIM_STEPPING_EVENT_TIME_LOCAL_1HZ_SEM_CONSUME, /*!< TIME 模块消费本地 1Hz 信号量 */
    ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_COMPLETE, /*!< 核心服务命令管道处理完成 */
    ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING /*!< 系统生命周期就绪：核心初始化完成，准备进入步进模式 */
} ESA_Stepping_EventKind_t;

/**
 * @brief       Shim 事件载荷（紧凑事实描述符）
 *
 * @details     传递事件特定的事实数据。载荷字段依赖于 event_kind 的上下文。
 *              保持结构紧凑以实现高效转发。
 */
typedef struct ESA_Stepping_ShimEvent
{
    ESA_Stepping_EventKind_t event_kind;     /*!< 事件类型（决定载荷语义） */
    uint32_t entity_id;                             /*!< 实体 ID（等待的队列/信号量等） */
    uint32_t task_id;                               /*!< 运行时任务 ID（当前执行任务） */
    uint32_t optional_delay_ms;                     /*!< 可选：延迟/超时值（毫秒） */
} ESA_Stepping_ShimEvent_t;

/****************************************************************************************
                                SHIM 报告函数声明
 ***************************************************************************************/

/**
 * @brief       向核心报告步进事件事实
 *
 * @details     所有 OSAL/TIME/SCH 模块报告原生步进事件的统一入口点。
 *              函数根据 event_kind 决定调用哪个核心报告函数。
 *
 *              这是一个轻量转发层：验证事件、提取相关事实参数，
 *              并调用适当的核心 Report 函数。所有状态机语义保留在核心中。
 *
 * @param[in]   event           步进事件描述符指针
 *
 * @retval      0               事件报告成功
 * @retval      非零            报告失败的错误码（如核心未初始化、无效 event_kind、核心已满）
 *
 * @note        Shim 是实现无关的。实现可能由构建标志保护；
 *              当禁用时，函数变为返回 0 的空操作。
 */
int32_t ESA_Stepping_Shim_ReportEvent(const ESA_Stepping_ShimEvent_t *event);

#endif /* NATIVE_STEPPING_SHIM_H */

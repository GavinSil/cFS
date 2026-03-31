/**
 * @file
 * @ingroup esa
 * @brief       ESA 步进管理的任务等待状态定义
 * @author      gaoyuan
 * @date        2026-03-30
 *
 * @details     本头文件定义等待结果码、等待类型枚举、任务等待状态结构体，以及 ESA 用于管理确定性等待的配置常量。
 */
#ifndef ESA_WAIT_H
#define ESA_WAIT_H

/****************************************************************************************
                                        INCLUDE FILES
 ***************************************************************************************/

#include <stdbool.h>
#include <pthread.h>

#include "common_types.h"
#include "osapi-idmap.h"
#include "osconfig.h"

/****************************************************************************************
                                    WAIT RESULT CODE ENUMERATION
 ***************************************************************************************/

/**
 * @brief       等待结果码枚举
 *
 * @details     对 ESA 管理的等待操作结果进行分类，指示等待是由资源可用、
 *              超时还是刷新操作满足。
 */
typedef enum ESA_WakeReason
{
    ESA_WOKE_BY_RESOURCE = 0,  /*!< 等待因资源可用而结束 */
    ESA_WOKE_BY_TIMEOUT,       /*!< 等待因超时而结束 */
    ESA_WOKE_BY_FLUSH          /*!< 等待因信号量刷新操作而结束 */
} ESA_WakeReason_t;

/****************************************************************************************
                                      WAIT TYPE ENUMERATION
 ***************************************************************************************/

/**
 * @brief       任务等待类型枚举
 *
 * @details     对任务等待的同步机制类型进行分类。
 *              由 ESA 步进用于确定调用哪个等待处理程序以及如何管理
 *              从步进控制到正常执行的转换。
 */
typedef enum ESA_WaitType
{
    ESA_WAIT_NONE = 0,      /*!< 无活动等待（初始/空闲状态） */
    ESA_WAIT_QUEUE,         /*!< 等待消息队列接收 */
    ESA_WAIT_BINSEM,        /*!< 等待二值信号量 */
    ESA_WAIT_COUNTSEM,      /*!< 等待计数信号量 */
    ESA_WAIT_CONDVAR,       /*!< 等待条件变量 */
    ESA_WAIT_DELAY          /*!< 等待任务延迟 */
} ESA_WaitType_t;

/****************************************************************************************
                             CONFIGURATION CONSTANTS (OSAL-ALIGNED)
 ***************************************************************************************/

/**
 * @brief       最大跟踪任务数
 *
 * @details     大小与 OSAL 配置中的 OS_MAX_TASKS 匹配。
 *              用于 ESA 等待状态的每任务注册表。
 */
#define ESA_MAX_TRACKED_TASKS       OS_MAX_TASKS

/**
 * @brief       最大跟踪队列数
 *
 * @details     大小与 OSAL 配置中的 OS_MAX_QUEUES 匹配。
 *              用于 ESA 等待状态的队列通知注册表。
 */
#define ESA_MAX_TRACKED_QUEUES      OS_MAX_QUEUES

/**
 * @brief       最大跟踪信号量数
 *
 * @details     大小用于容纳二值和计数信号量。
 *              等于 OS_MAX_BIN_SEMAPHORES + OS_MAX_COUNT_SEMAPHORES。
 */
#define ESA_MAX_TRACKED_SEMS        (OS_MAX_BIN_SEMAPHORES + OS_MAX_COUNT_SEMAPHORES)

/**
 * @brief       最大跟踪条件变量数
 *
 * @details     大小与 OSAL 配置中的 OS_MAX_CONDVARS 匹配。
 *              用于 ESA 等待状态的条件变量通知注册表。
 */
#define ESA_MAX_TRACKED_CONDVARS    OS_MAX_CONDVARS

/****************************************************************************************
                                    WAIT STATE STRUCTURE
 ***************************************************************************************/

/**
 * @brief       ESA 步进的任务等待状态
 *
 * @details     封装任务的当前等待状态，包括等待类型、OSAL 资源 ID、
 *              仿真时间截止时间，以及 ESA 用于确定性等待管理的 pthread 同步原语
 *              （互斥锁和条件变量）。
 *
 *              此结构嵌入在每任务状态中或维护在全局任务等待注册表中，
 *              以便 ESA 步进正确管理等待转换。
 */
typedef struct ESA_TaskWaitState
{
    ESA_WaitType_t  wait_type;       /*!< 等待类型（QUEUE、BINSEM、COUNTSEM、CONDVAR、DELAY） */
    osal_id_t       resource_id;     /*!< OSAL 资源 ID（queue_id、sem_id、condvar_id） */
    uint64          sim_deadline_ns; /*!< 仿真时间截止时间（纳秒） */
    pthread_cond_t  cond;            /*!< ESA 控制唤醒的条件变量 */
    pthread_mutex_t mutex;           /*!< 保护等待状态转换的互斥锁 */
    bool            is_active;       /*!< 此等待槽位是否在使用 */
} ESA_TaskWaitState_t;

/****************************************************************************************
                              WEAK-SYMBOL WAIT FUNCTION DECLARATIONS
 ***************************************************************************************/

/**
 * @brief       ESA 管理的消息队列接收等待
 *
 * @details     步进管理的队列接收等待的弱符号函数。
 *              当启用 ESA 步进时，此函数可能被覆盖以提供确定性等待语义。
 *              当禁用 ESA 步进时，此函数是返回成功的空操作。
 *
 * @param[in]   queue_id                等待的 OSAL 队列 ID
 * @param[in]   timeout_ms              超时时间（毫秒）
 *
 * @retval      ESA_WOKE_BY_RESOURCE    成功接收
 * @retval      ESA_WOKE_BY_TIMEOUT     超时
 * @retval      负数                    等待失败的错误码
 */
int32 ESA_WaitForMessage(osal_id_t queue_id, uint32 timeout_ms);

/**
 * @brief       ESA 管理的信号量等待（二值或计数）
 *
 * @details     步进管理的信号量等待的弱符号函数。
 *              当启用 ESA 步进时，此函数可能被覆盖以提供确定性等待语义。
 *              当禁用 ESA 步进时，此函数是返回成功的空操作。
 *
 * @param[in]   sem_id                  等待的 OSAL 信号量 ID（二值或计数）
 * @param[in]   timeout_ms              超时时间（毫秒）
 *
 * @retval      ESA_WOKE_BY_RESOURCE    成功释放
 * @retval      ESA_WOKE_BY_TIMEOUT     超时
 * @retval      ESA_WOKE_BY_FLUSH       刷新
 * @retval      负数                    等待失败的错误码
 */
int32 ESA_WaitForSem(osal_id_t sem_id, uint32 timeout_ms);

/**
 * @brief       ESA 管理的条件变量等待
 *
 * @details     步进管理的条件变量等待的弱符号函数。
 *              语义上等同于 ESA_WaitForSem，但操作于条件变量。
 *              当启用 ESA 步进时，此函数可能被覆盖以提供确定性等待语义。
 *              当禁用 ESA 步进时，此函数是返回成功的空操作。
 *
 * @param[in]   condvar_id              等待的 OSAL 条件变量 ID
 * @param[in]   timeout_ms              超时时间（毫秒）
 *
 * @retval      ESA_WOKE_BY_RESOURCE    信号到达
 * @retval      ESA_WOKE_BY_TIMEOUT     超时
 * @retval      负数                    等待失败的错误码
 */
int32 ESA_WaitForCondVar(osal_id_t condvar_id, uint32 timeout_ms);

/**
 * @brief       ESA 管理的任务延迟等待
 *
 * @details     步进管理的任务延迟的弱符号函数。
 *              当启用 ESA 步进时，此函数可能被覆盖以提供由步进量子控制的确定性延迟语义。
 *              当禁用 ESA 步进时，此函数是返回成功的空操作。
 *
 * @param[in]   timeout_ms              请求的延迟时间（毫秒）
 *
 * @retval      ESA_WOKE_BY_TIMEOUT     延迟成功完成
 * @retval      负数                    延迟失败的错误码
 */
int32 ESA_WaitForDelay(uint32 timeout_ms);

/****************************************************************************************
                            WEAK-SYMBOL NOTIFICATION FUNCTION DECLARATIONS
 ***************************************************************************************/

/**
 * @brief       通知 ESA 等待者队列已放入消息
 *
 * @details     当消息放入队列时调用，唤醒任何阻塞在该队列上的 ESA 管理的等待者。
 *              弱符号实现允许 ESA 覆盖并注册通知。
 *
 * @param[in]   queue_id        收到消息的 OSAL 队列 ID
 */
void ESA_NotifyQueuePut(osal_id_t queue_id);

/**
 * @brief       通知 ESA 等待者信号量已释放
 *
 * @details     当信号量被释放（posted）时调用，唤醒任何阻塞在该信号量上的 ESA 管理的等待者。
 *              弱符号实现允许 ESA 覆盖并注册通知。
 *
 * @param[in]   sem_id          被释放的 OSAL 信号量 ID（二值或计数）
 */
void ESA_NotifySemGive(osal_id_t sem_id);

/**
 * @brief       通知 ESA 等待者信号量已刷新
 *
 * @details     当信号量被刷新（所有等待者被释放）时调用，唤醒任何阻塞在该信号量上的
 *              ESA 管理的等待者并返回刷新结果码。
 *              弱符号实现允许 ESA 覆盖并注册通知。
 *
 * @param[in]   sem_id          被刷新的 OSAL 信号量 ID（二值或计数）
 */
void ESA_NotifySemFlush(osal_id_t sem_id);

/**
 * @brief       通知 ESA 等待者条件变量已信号化
 *
 * @details     当条件变量被信号化或广播时调用，唤醒任何阻塞在该条件变量上的 ESA 管理的等待者。
 *              Signal 和 Broadcast 操作都会调用此通知。
 *
 * @param[in]   condvar_id      被信号化/广播的 OSAL 条件变量 ID
 * @param[in]   broadcast       false=Signal（唤醒一个），true=Broadcast（唤醒所有）
 */
void ESA_NotifyCondVar(osal_id_t condvar_id, bool broadcast);

/****************************************************************************************
                           WEAK-SYMBOL TASK LIFECYCLE DECLARATIONS
 ***************************************************************************************/

/**
 * @brief       注册任务到 ESA 等待状态管理
 *
 * @details     当任务创建时调用，将其注册到 ESA 等待状态注册表。
 *              分配每任务等待状态存储并初始化同步原语。
 *              弱符号实现允许 ESA 覆盖并管理生命周期。
 *
 * @param[in]   task_id         要注册的 OSAL 任务 ID
 */
void ESA_RegisterTask(osal_id_t task_id);

/**
 * @brief       从 ESA 等待状态管理注销任务
 *
 * @details     当任务删除时调用，将其从 ESA 等待状态注册表注销。
 *              释放每任务等待状态存储并清理同步原语。
 *              弱符号实现允许 ESA 覆盖并管理生命周期。
 *
 * @param[in]   task_id         要注销的 OSAL 任务 ID
 */
void ESA_DeregisterTask(osal_id_t task_id);

#endif /* ESA_WAIT_H */

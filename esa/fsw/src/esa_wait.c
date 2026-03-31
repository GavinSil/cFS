/**
 * @file
 * @ingroup esa
 * @brief       ESA 任务等待机制实现
 * @author      gaoyuan
 * @date        2026-03-30
 *
 * @details     本文件实现仿真步进环境下的任务等待和唤醒机制。提供队列、信号量、条件变量和延迟的步进感知等待功能。
 */
#include <string.h>
#include <time.h>
#include <errno.h>

#include "esa_wait.h"
#include "esa_stepping.h"
#include "osapi-constants.h"
#include "osapi-task.h"

#ifdef CFE_SIM_STEPPING

/** 已注册任务的等待状态表 */
static ESA_TaskWaitState_t ESA_TaskWaitTable[ESA_MAX_TRACKED_TASKS];
/** 每个任务槽位最近一次唤醒原因表 */
static ESA_WakeReason_t ESA_TaskWakeReason[ESA_MAX_TRACKED_TASKS];
/** 每个任务槽位的唤醒待处理标志表 */
static bool ESA_TaskWakePending[ESA_MAX_TRACKED_TASKS];
/** 每个任务槽位的等待原语初始化状态表 */
static bool ESA_TaskWaitPrimitivesReady[ESA_MAX_TRACKED_TASKS];

/** 全局等待表保护互斥锁 */
static pthread_mutex_t ESA_WaitGlobalMutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief       pthread 清理回调：解锁互斥锁
 * @details     在线程于等待期间异常退出时，确保传入的互斥锁被安全释放。
 * @param[in]   arg 指向待解锁互斥锁的指针
 */
static void ESA_Wait_UnlockMutex(void *arg)
{
    pthread_mutex_t *mutex;

    mutex = (pthread_mutex_t *)arg;
    if (mutex != NULL)
    {
        pthread_mutex_unlock(mutex);
    }
}

/**
 * @brief       将任务 ID 转换为等待表索引
 * @details     仅当任务 ID 已定义且成功映射到跟踪表范围内时返回 true。
 * @param[in]   task_id   任务对象标识符
 * @param[out]  task_idx  用于接收数组索引的输出指针
 * @retval      true      转换成功且索引有效
 * @retval      false     输入无效或索引越界
 */
static bool ESA_Wait_GetTaskIndex(osal_id_t task_id, osal_index_t *task_idx)
{
    if (!OS_ObjectIdDefined(task_id) || task_idx == NULL)
    {
        return false;
    }

    return (OS_ObjectIdToArrayIndex(OS_OBJECT_TYPE_OS_TASK, task_id, task_idx) == 0 &&
            *task_idx < ESA_MAX_TRACKED_TASKS);
}

/**
 * @brief       计算等待操作的仿真截止时间
 * @details     对有限超时值读取当前仿真时间并换算出到期纳秒时间；永久等待返回 UINT64_MAX。
 * @param[in]   timeout_ms 超时时间（毫秒）
 * @retval      UINT64_MAX 表示永久等待
 * @retval      0          无法获取当前仿真时间
 * @retval      其他       计算得到的截止纳秒时间
 */
static uint64 ESA_Wait_ComputeDeadlineNs(uint32 timeout_ms)
{
    uint64 now_ns;

    if (timeout_ms == (uint32)OS_PEND)
    {
        return UINT64_MAX;
    }

    if (!ESA_Stepping_Hook_GetTime(&now_ns))
    {
        return 0;
    }

    return now_ns + (((uint64)timeout_ms) * 1000000ULL);
}

/**
 * @brief       清理指定任务槽位的等待状态
 * @details     重置等待类型、资源标识、截止时间和待处理唤醒标记，使槽位恢复为空闲态。
 * @param[in]   task_idx 任务槽位索引
 */
static void ESA_Wait_ClearSlot(osal_index_t task_idx)
{
    ESA_TaskWaitTable[task_idx].wait_type       = ESA_WAIT_NONE;
    ESA_TaskWaitTable[task_idx].resource_id     = OS_OBJECT_ID_UNDEFINED;
    ESA_TaskWaitTable[task_idx].sim_deadline_ns = 0;
    ESA_TaskWakePending[task_idx]               = false;
    ESA_TaskWakeReason[task_idx]                = ESA_WOKE_BY_RESOURCE;
}

/**
 * @brief       执行统一的步进感知等待流程
 * @details     为当前任务登记等待对象，基于仿真时间循环等待资源唤醒、刷新或超时到期。
 * @param[in]   resource_id 被等待的资源标识符
 * @param[in]   timeout_ms  超时时间（毫秒）
 * @param[in]   wait_type   等待类型
 * @retval      ESA_WOKE_BY_RESOURCE 因资源可用而结束等待
 * @retval      ESA_WOKE_BY_TIMEOUT  因超时到期而结束等待
 * @retval      ESA_WOKE_BY_FLUSH    因刷新操作而结束等待
 * @retval      -1                   登记或等待过程中发生错误
 */
static int32 ESA_Wait_Common(osal_id_t resource_id, uint32 timeout_ms, ESA_WaitType_t wait_type)
{
    osal_id_t            task_id;
    osal_index_t         task_idx;
    ESA_TaskWaitState_t *state;
    int32                result;
    uint64               now_ns;
    struct timespec      wake_time;
    int                  timedwait_status;

    task_id = OS_TaskGetId();
    if (!OS_ObjectIdDefined(task_id))
    {
        return -1;
    }

    ESA_RegisterTask(task_id);

    pthread_mutex_lock(&ESA_WaitGlobalMutex);

    if (!ESA_Wait_GetTaskIndex(task_id, &task_idx))
    {
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return -1;
    }

    state = &ESA_TaskWaitTable[task_idx];
    if (!state->is_active)
    {
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return -1;
    }

    state->wait_type              = wait_type;
    state->resource_id            = resource_id;
    state->sim_deadline_ns        = ESA_Wait_ComputeDeadlineNs(timeout_ms);
    ESA_TaskWakePending[task_idx] = false;
    ESA_TaskWakeReason[task_idx]  = ESA_WOKE_BY_RESOURCE;

    pthread_mutex_unlock(&ESA_WaitGlobalMutex);

    if (timeout_ms == OS_CHECK)
    {
        pthread_mutex_lock(&ESA_WaitGlobalMutex);
        ESA_Wait_ClearSlot(task_idx);
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return ESA_WOKE_BY_TIMEOUT;
    }

    pthread_mutex_lock(&state->mutex);
    pthread_cleanup_push(ESA_Wait_UnlockMutex, &state->mutex);

    result = -1;
    while (!ESA_TaskWakePending[task_idx])
    {
        if (timeout_ms != (uint32)OS_PEND)
        {
            if (!ESA_Stepping_Hook_GetTime(&now_ns))
            {
                result = -1;
                break;
            }

            if (now_ns >= state->sim_deadline_ns)
            {
                ESA_TaskWakePending[task_idx] = true;
                ESA_TaskWakeReason[task_idx]  = ESA_WOKE_BY_TIMEOUT;
                break;
            }
        }

        if (clock_gettime(CLOCK_MONOTONIC, &wake_time) != 0)
        {
            result = -1;
            break;
        }

        wake_time.tv_sec += 1;
        timedwait_status = pthread_cond_timedwait(&state->cond, &state->mutex, &wake_time);
        if (timedwait_status != 0 && timedwait_status != ETIMEDOUT)
        {
            result = -1;
            break;
        }
    }

    if (ESA_TaskWakePending[task_idx])
    {
        result = ESA_TaskWakeReason[task_idx];
    }

    ESA_Wait_ClearSlot(task_idx);

    pthread_cleanup_pop(true);

    return result;
}

/**
 * @brief       根据 OSAL 信号量对象类型确定等待类别
 * @details     将二值信号量和计数信号量映射到 ESA 内部等待类型枚举。
 * @param[in]   sem_id     信号量对象标识符
 * @param[out]  wait_type  用于接收等待类型的输出指针
 * @retval      true       成功识别并返回等待类型
 * @retval      false      输入无效或对象类型不受支持
 */
static bool ESA_Wait_GetSemWaitType(osal_id_t sem_id, ESA_WaitType_t *wait_type)
{
    osal_objtype_t sem_objtype;

    if (!OS_ObjectIdDefined(sem_id) || wait_type == NULL)
    {
        return false;
    }

    sem_objtype = OS_IdentifyObject(sem_id);
    if (sem_objtype == OS_OBJECT_TYPE_OS_BINSEM)
    {
        *wait_type = ESA_WAIT_BINSEM;
        return true;
    }

    if (sem_objtype == OS_OBJECT_TYPE_OS_COUNTSEM)
    {
        *wait_type = ESA_WAIT_COUNTSEM;
        return true;
    }

    return false;
}

/**
 * @brief       按资源标识唤醒等待任务
 * @details     遍历等待表，向匹配资源与等待类型的任务发出唤醒信号，可选择唤醒一个或全部。
 * @param[in]   resource_id 被通知的资源标识符
 * @param[in]   wait_type   目标等待类型
 * @param[in]   wake_reason 唤醒原因
 * @param[in]   wake_all    true 表示广播唤醒，false 表示仅唤醒一个
 */
static void ESA_Wait_NotifyByResource(osal_id_t resource_id, ESA_WaitType_t wait_type, ESA_WakeReason_t wake_reason,
                                      bool wake_all)
{
    osal_index_t task_idx;

    pthread_mutex_lock(&ESA_WaitGlobalMutex);
    for (task_idx = 0; task_idx < ESA_MAX_TRACKED_TASKS; ++task_idx)
    {
        ESA_TaskWaitState_t *state;

        state = &ESA_TaskWaitTable[task_idx];
        if (!state->is_active || state->wait_type != wait_type)
        {
            continue;
        }

        if (!OS_ObjectIdEqual(state->resource_id, resource_id))
        {
            continue;
        }

        pthread_mutex_lock(&state->mutex);
        ESA_TaskWakePending[task_idx] = true;
        ESA_TaskWakeReason[task_idx]  = wake_reason;
        pthread_cond_signal(&state->cond);
        pthread_mutex_unlock(&state->mutex);

        if (!wake_all)
        {
            break;
        }
    }

    pthread_mutex_unlock(&ESA_WaitGlobalMutex);
}

/**
 * @brief       等待消息队列可读
 * @details     将当前任务登记为队列等待者，并在资源通知或仿真超时到期时返回。
 * @param[in]   queue_id    队列对象标识符
 * @param[in]   timeout_ms  超时时间（毫秒）
 * @retval      ESA_WOKE_BY_RESOURCE 因队列资源可用而返回
 * @retval      ESA_WOKE_BY_TIMEOUT  因超时返回
 * @retval      -1                   输入无效或等待失败
 */
int32 ESA_WaitForMessage(osal_id_t queue_id, uint32 timeout_ms)
{
    if (!OS_ObjectIdDefined(queue_id))
    {
        return -1;
    }

    return ESA_Wait_Common(queue_id, timeout_ms, ESA_WAIT_QUEUE);
}

/**
 * @brief       等待信号量可获取
 * @details     自动识别二值或计数信号量类型，并通过统一等待路径执行资源或超时等待。
 * @param[in]   sem_id      信号量对象标识符
 * @param[in]   timeout_ms  超时时间（毫秒）
 * @retval      ESA_WOKE_BY_RESOURCE 因信号量可用而返回
 * @retval      ESA_WOKE_BY_TIMEOUT  因超时返回
 * @retval      ESA_WOKE_BY_FLUSH    因刷新返回
 * @retval      -1                   输入无效或等待失败
 */
int32 ESA_WaitForSem(osal_id_t sem_id, uint32 timeout_ms)
{
    ESA_WaitType_t sem_wait_type;

    if (!OS_ObjectIdDefined(sem_id))
    {
        return -1;
    }

    if (!ESA_Wait_GetSemWaitType(sem_id, &sem_wait_type))
    {
        return -1;
    }

    return ESA_Wait_Common(sem_id, timeout_ms, sem_wait_type);
}

/**
 * @brief       等待条件变量通知
 * @details     将当前任务登记为条件变量等待者，并在单播/广播通知或超时到期时返回。
 * @param[in]   condvar_id  条件变量对象标识符
 * @param[in]   timeout_ms  超时时间（毫秒）
 * @retval      ESA_WOKE_BY_RESOURCE 因条件变量通知而返回
 * @retval      ESA_WOKE_BY_TIMEOUT  因超时返回
 * @retval      -1                   输入无效或等待失败
 */
int32 ESA_WaitForCondVar(osal_id_t condvar_id, uint32 timeout_ms)
{
    if (!OS_ObjectIdDefined(condvar_id))
    {
        return -1;
    }

    return ESA_Wait_Common(condvar_id, timeout_ms, ESA_WAIT_CONDVAR);
}

/**
 * @brief       执行步进感知的延迟等待
 * @details     不绑定具体资源对象，仅依据仿真时间截止点等待延迟预算满足。
 * @param[in]   timeout_ms 延迟时间（毫秒）
 * @retval      ESA_WOKE_BY_TIMEOUT  延迟预算满足后返回
 * @retval      -1                   等待登记失败
 */
int32 ESA_WaitForDelay(uint32 timeout_ms)
{
    return ESA_Wait_Common(OS_OBJECT_ID_UNDEFINED, timeout_ms, ESA_WAIT_DELAY);
}

/**
 * @brief       通知队列放入事件
 * @details     唤醒一个等待指定队列的任务，并将其唤醒原因标记为资源可用。
 * @param[in]   queue_id 队列对象标识符
 */
void ESA_NotifyQueuePut(osal_id_t queue_id)
{
    ESA_Wait_NotifyByResource(queue_id, ESA_WAIT_QUEUE, ESA_WOKE_BY_RESOURCE, false);
}

/**
 * @brief       通知信号量释放事件
 * @details     根据信号量对象类型唤醒一个等待者；若类型无法识别则忽略该通知。
 * @param[in]   sem_id 信号量对象标识符
 */
void ESA_NotifySemGive(osal_id_t sem_id)
{
    ESA_WaitType_t sem_wait_type;

    if (!ESA_Wait_GetSemWaitType(sem_id, &sem_wait_type))
    {
        return;
    }

    ESA_Wait_NotifyByResource(sem_id, sem_wait_type, ESA_WOKE_BY_RESOURCE, false);
}

/**
 * @brief       通知信号量刷新事件
 * @details     对指定信号量同时广播二值与计数信号量等待队列，使所有等待者以刷新原因返回。
 * @param[in]   sem_id 信号量对象标识符
 */
void ESA_NotifySemFlush(osal_id_t sem_id)
{
    ESA_Wait_NotifyByResource(sem_id, ESA_WAIT_BINSEM, ESA_WOKE_BY_FLUSH, true);
    ESA_Wait_NotifyByResource(sem_id, ESA_WAIT_COUNTSEM, ESA_WOKE_BY_FLUSH, true);
}

/**
 * @brief       通知条件变量事件
 * @details     根据 broadcast 标志选择单播或广播唤醒等待指定条件变量的任务。
 * @param[in]   condvar_id 条件变量对象标识符
 * @param[in]   broadcast  true 表示广播唤醒，false 表示仅唤醒一个
 */
void ESA_NotifyCondVar(osal_id_t condvar_id, bool broadcast)
{
    ESA_Wait_NotifyByResource(condvar_id, ESA_WAIT_CONDVAR, ESA_WOKE_BY_RESOURCE, broadcast);
}

/**
 * @brief       为任务初始化等待槽位
 * @details     按任务 ID 分配并初始化互斥锁、条件变量等原语，使任务能够参与 ESA 等待机制。
 * @param[in]   task_id 任务对象标识符
 */
void ESA_RegisterTask(osal_id_t task_id)
{
    osal_index_t       task_idx;
    pthread_condattr_t cond_attr;

    if (!ESA_Wait_GetTaskIndex(task_id, &task_idx))
    {
        return;
    }

    pthread_mutex_lock(&ESA_WaitGlobalMutex);
    if (ESA_TaskWaitTable[task_idx].is_active)
    {
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return;
    }

    if (!ESA_TaskWaitPrimitivesReady[task_idx])
    {
        if (pthread_mutex_init(&ESA_TaskWaitTable[task_idx].mutex, NULL) != 0)
        {
            pthread_mutex_unlock(&ESA_WaitGlobalMutex);
            return;
        }

        if (pthread_condattr_init(&cond_attr) != 0)
        {
            pthread_mutex_destroy(&ESA_TaskWaitTable[task_idx].mutex);
            pthread_mutex_unlock(&ESA_WaitGlobalMutex);
            return;
        }

        if (pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC) != 0)
        {
            pthread_condattr_destroy(&cond_attr);
            pthread_mutex_destroy(&ESA_TaskWaitTable[task_idx].mutex);
            pthread_mutex_unlock(&ESA_WaitGlobalMutex);
            return;
        }

        if (pthread_cond_init(&ESA_TaskWaitTable[task_idx].cond, &cond_attr) != 0)
        {
            pthread_condattr_destroy(&cond_attr);
            pthread_mutex_destroy(&ESA_TaskWaitTable[task_idx].mutex);
            pthread_mutex_unlock(&ESA_WaitGlobalMutex);
            return;
        }

        pthread_condattr_destroy(&cond_attr);
        ESA_TaskWaitPrimitivesReady[task_idx] = true;
    }

    ESA_TaskWaitTable[task_idx].is_active = true;
    ESA_Wait_ClearSlot(task_idx);
    pthread_mutex_unlock(&ESA_WaitGlobalMutex);
}

/**
 * @brief       注销任务等待槽位
 * @details     唤醒可能阻塞的等待者，销毁该槽位的等待原语，并将任务从等待机制中移除。
 * @param[in]   task_id 任务对象标识符
 */
void ESA_DeregisterTask(osal_id_t task_id)
{
    osal_index_t         task_idx;
    ESA_TaskWaitState_t *state;
    bool                 destroy_needed;

    if (!ESA_Wait_GetTaskIndex(task_id, &task_idx))
    {
        return;
    }

    pthread_mutex_lock(&ESA_WaitGlobalMutex);
    state = &ESA_TaskWaitTable[task_idx];
    if (!state->is_active)
    {
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return;
    }

    destroy_needed = ESA_TaskWaitPrimitivesReady[task_idx];

    if (destroy_needed)
    {
        pthread_mutex_lock(&state->mutex);
    }
    state->wait_type              = ESA_WAIT_NONE;
    state->resource_id            = OS_OBJECT_ID_UNDEFINED;
    state->sim_deadline_ns        = 0;
    ESA_TaskWakePending[task_idx] = true;
    ESA_TaskWakeReason[task_idx]  = ESA_WOKE_BY_RESOURCE;

    if (destroy_needed)
    {
        pthread_cond_signal(&state->cond);
        pthread_mutex_unlock(&state->mutex);
    }

    state->is_active = false;
    ESA_Wait_ClearSlot(task_idx);

    if (destroy_needed)
    {
        pthread_cond_destroy(&state->cond);
        pthread_mutex_destroy(&state->mutex);
        memset(&state->cond, 0, sizeof(state->cond));
        memset(&state->mutex, 0, sizeof(state->mutex));
    }

    ESA_TaskWaitPrimitivesReady[task_idx] = false;

    pthread_mutex_unlock(&ESA_WaitGlobalMutex);
}

#else

/**
 * @brief       队列等待接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，直接返回资源可用结果以保持兼容行为。
 * @param[in]   queue_id   队列对象标识符
 * @param[in]   timeout_ms 超时时间（毫秒）
 * @retval      ESA_WOKE_BY_RESOURCE 始终返回资源可用
 */
int32 ESA_WaitForMessage(osal_id_t queue_id, uint32 timeout_ms)
{
    (void)queue_id;
    (void)timeout_ms;
    return ESA_WOKE_BY_RESOURCE;
}

/**
 * @brief       信号量等待接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，直接返回资源可用结果以保持兼容行为。
 * @param[in]   sem_id     信号量对象标识符
 * @param[in]   timeout_ms 超时时间（毫秒）
 * @retval      ESA_WOKE_BY_RESOURCE 始终返回资源可用
 */
int32 ESA_WaitForSem(osal_id_t sem_id, uint32 timeout_ms)
{
    (void)sem_id;
    (void)timeout_ms;
    return ESA_WOKE_BY_RESOURCE;
}

/**
 * @brief       条件变量等待接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，直接返回资源可用结果以保持兼容行为。
 * @param[in]   condvar_id 条件变量对象标识符
 * @param[in]   timeout_ms 超时时间（毫秒）
 * @retval      ESA_WOKE_BY_RESOURCE 始终返回资源可用
 */
int32 ESA_WaitForCondVar(osal_id_t condvar_id, uint32 timeout_ms)
{
    (void)condvar_id;
    (void)timeout_ms;
    return ESA_WOKE_BY_RESOURCE;
}

/**
 * @brief       延迟等待接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，直接返回资源可用结果以保持兼容行为。
 * @param[in]   timeout_ms 超时时间（毫秒）
 * @retval      ESA_WOKE_BY_RESOURCE 始终返回资源可用
 */
int32 ESA_WaitForDelay(uint32 timeout_ms)
{
    (void)timeout_ms;
    return ESA_WOKE_BY_RESOURCE;
}

/**
 * @brief       队列通知接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，忽略该通知。
 * @param[in]   queue_id 队列对象标识符
 */
void ESA_NotifyQueuePut(osal_id_t queue_id)
{
    (void)queue_id;
}

/**
 * @brief       信号量释放通知接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，忽略该通知。
 * @param[in]   sem_id 信号量对象标识符
 */
void ESA_NotifySemGive(osal_id_t sem_id)
{
    (void)sem_id;
}

/**
 * @brief       信号量刷新通知接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，忽略该通知。
 * @param[in]   sem_id 信号量对象标识符
 */
void ESA_NotifySemFlush(osal_id_t sem_id)
{
    (void)sem_id;
}

/**
 * @brief       条件变量通知接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，忽略该通知。
 * @param[in]   condvar_id 条件变量对象标识符
 * @param[in]   broadcast  是否广播唤醒
 */
void ESA_NotifyCondVar(osal_id_t condvar_id, bool broadcast)
{
    (void)condvar_id;
    (void)broadcast;
}

/**
 * @brief       任务注册接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，忽略该调用。
 * @param[in]   task_id 任务对象标识符
 */
void ESA_RegisterTask(osal_id_t task_id)
{
    (void)task_id;
}

/**
 * @brief       任务注销接口的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，忽略该调用。
 * @param[in]   task_id 任务对象标识符
 */
void ESA_DeregisterTask(osal_id_t task_id)
{
    (void)task_id;
}

#endif

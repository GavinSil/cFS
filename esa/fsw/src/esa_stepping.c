/**
 * @file
 * @ingroup esa
 * @brief       ESA 仿真步进主实现
 * @author      gaoyuan
 * @date        2026-03-20
 *
 * @details     本文件实现 ESA 步进模块的主入口点，包括 Shim 事件报告、InProc/UDS 控制适配器和后台服务线程。
 */
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "cfe_psp.h"
#include "esa_stepping.h"
#include "esa_stepping_core.h"
#include "esa_stepping_shim.h"

/**
 * @brief 本 PSP 模块拥有的静态步进核心实例
 *
 * @details 这是任务的唯一权威步进核心。所有 hook 向该核心报告事实；核心维护状态和语义。
 *          在模块启动时初始化一次，模拟时间设为 0。系统 ready 生命周期事实由 ES 通过 shim event 上报，
 *          此处不在 ESA_Init 中直接置位。
 */
static ESA_Stepping_Core_t stepping_core = {0};

/** 核心是否已成功初始化 */
static bool core_initialized = false;

/**
 * @brief PSP 本地 UDS 服务循环线程状态
 *
 * @details 管理后台 POSIX 线程，周期性服务 UDS 客户端请求。仅在 CFE_SIM_STEPPING 启用时创建。
 *          线程不可取消；由于缺少 PSP 模块关闭 hook，清理功能受限。线程退出时将 should_run 设为 false。
 */
typedef struct
{
    volatile bool is_running; /*!< 服务循环线程是否正在运行 */
    volatile bool should_run; /*!< 线程继续运行信号；设为 false 退出 */
    pthread_t     task_id;    /*!< POSIX 线程 ID */
} ESA_Stepping_UDS_ServiceLoop_t;

/** PSP 本地 UDS 服务循环线程状态实例 */
static ESA_Stepping_UDS_ServiceLoop_t uds_service_loop = {.is_running = false, .should_run = false, .task_id = 0};

/**
 * @brief UDS 服务循环的 PSP 本地后台线程函数
 *
 * @details 在独立线程中运行，周期性调用 ESA_Stepping_UDS_Service() 接受并分发 UDS 客户端请求。
 *          使用简单的睡眠轮询模型，每次服务调用间睡眠 10ms。非阻塞服务意味着没有请求会等待超过
 *          一个睡眠间隔的时间。线程运行直到 should_run 被设为 false（无显式取消机制）。
 *          退出时设置 is_running=false。
 *
 * @param[in] arg 线程参数（指向服务循环状态结构的指针）
 *
 * @retval NULL（POSIX 线程返回值）
 */
static void *ESA_Stepping_UDS_ServiceLoop_Task(void *arg)
{
    struct timespec sleep_time = {0};

    sleep_time.tv_nsec = 10000000;

    ESA_Stepping_UDS_ServiceLoop_t *state = (ESA_Stepping_UDS_ServiceLoop_t *)arg;
    if (state == NULL)
    {
        return NULL;
    }

    state->is_running = true;

    while (state->should_run)
    {
        (void)ESA_Stepping_UDS_Service();

        nanosleep(&sleep_time, NULL);
    }

    state->is_running = false;
    return NULL;
}

/**
 * @brief       初始化 ESA 仿真步进模块
 * @details     初始化共享步进核心，并在启用步进仿真时建立 UDS 控制适配器与后台服务线程。
 *              如果核心初始化失败，则保持模块处于未初始化状态并立即返回。
 */
void ESA_Init(void)
{
    int32_t status;
    int     pthread_status;

    status = ESA_Stepping_Core_Init(&stepping_core, 0, ESA_SIM_STEPPING_MAX_TRIGGERS);
    if (status == 0)
    {
        core_initialized = true;
    }
    else
    {
        core_initialized = false;
        return;
    }

#ifdef CFE_SIM_STEPPING
    status = ESA_Stepping_UDS_Init();
    if (status != 0)
    {
        ;
    }
    else
    {
        uds_service_loop.should_run = true;
        pthread_status =
            pthread_create(&uds_service_loop.task_id, NULL, ESA_Stepping_UDS_ServiceLoop_Task, &uds_service_loop);
        if (pthread_status != 0)
        {
            uds_service_loop.should_run = false;
        }
    }
#endif
}

#ifdef CFE_SIM_STEPPING

/**
 * @brief       获取当前仿真时间
 * @details     当核心已初始化且处于步进模式时，从共享步进核心读取当前仿真时间。
 * @param[out]  sim_time_ns 用于接收仿真时间的输出指针
 * @retval      true        成功获取仿真时间
 * @retval      false       输入无效、核心未初始化或查询失败
 */
bool ESA_Stepping_Hook_GetTime(uint64_t *sim_time_ns)
{
    int32_t status;

    if (sim_time_ns == NULL)
    {
        return false;
    }

    if (!core_initialized)
    {
        return false;
    }

    status = ESA_Stepping_Core_QuerySimTime(&stepping_core, sim_time_ns);

    return (status == 0);
}

/**
 * @brief       查询步进会话是否处于活动状态
 * @details     仅在核心已初始化时返回共享步进核心记录的会话活动标志。
 * @retval      true        当前存在活动步进会话
 * @retval      false       核心未初始化或当前无活动会话
 */
bool ESA_Stepping_Hook_IsSessionActive(void)
{
    if (!core_initialized)
    {
        return false;
    }

    return stepping_core.session_active;
}

#else

/**
 * @brief       获取当前仿真时间的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地报告失败并忽略输出参数。
 * @param[out]  sim_time_ns 用于接收仿真时间的输出指针
 * @retval      false       步进模式未启用
 */
bool ESA_Stepping_Hook_GetTime(uint64_t *sim_time_ns)
{
    (void)sim_time_ns;
    return false;
}

/**
 * @brief       查询步进会话状态的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，始终返回无活动会话。
 * @retval      false       步进模式未启用
 */
bool ESA_Stepping_Hook_IsSessionActive(void)
{
    return false;
}

#endif

/**
 * @brief 统一的步进事件事实报告转发器
 *
 * @details 验证输入，检查核心初始化状态，根据 event_kind 将事件分发到相应的核心报告函数。
 *          所有状态机语义集中在核心中。
 *
 * @param[in]  event  步进事件描述符指针
 *
 * @retval 0 成功；报告失败时返回非零错误码
 */
int32_t ESA_Stepping_Shim_ReportEvent(const ESA_Stepping_ShimEvent_t *event)
{
    int32_t status = ESA_SIM_STEPPING_STATUS_FAILURE;

    if (event == NULL)
    {
        return ESA_SIM_STEPPING_STATUS_FAILURE;
    }

    if (!core_initialized)
    {
        return ESA_SIM_STEPPING_STATUS_NOT_READY;
    }

    switch (event->event_kind)
    {
        case ESA_SIM_STEPPING_EVENT_TASK_DELAY:
            status = ESA_Stepping_Core_ReportTaskDelay(&stepping_core, event->task_id, event->optional_delay_ms);
            break;

        case ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK:
            status = ESA_Stepping_Core_ReportTaskDelayAck(&stepping_core, event->task_id, event->optional_delay_ms);
            break;

        case ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE:
            status =
                ESA_Stepping_Core_ReportTaskDelayComplete(&stepping_core, event->task_id, event->optional_delay_ms);
            break;

        case ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE:
            status = ESA_Stepping_Core_ReportQueueReceive(&stepping_core, event->entity_id, event->optional_delay_ms);
            break;

        case ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK:
            status = ESA_Stepping_Core_ReportQueueReceiveAck(&stepping_core, event->task_id, event->entity_id,
                                                             event->optional_delay_ms);
            break;

        case ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE:
            status = ESA_Stepping_Core_ReportQueueReceiveComplete(&stepping_core, event->task_id, event->entity_id,
                                                                  event->optional_delay_ms);
            break;


        case ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK:
            status = ESA_Stepping_Core_ReportBinSemTakeAck(&stepping_core, event->task_id, event->entity_id,
                                                           event->optional_delay_ms);
            break;

        case ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE:
            status = ESA_Stepping_Core_ReportBinSemTakeComplete(&stepping_core, event->task_id, event->entity_id,
                                                                event->optional_delay_ms);
            break;


        case ESA_SIM_STEPPING_EVENT_1HZ_BOUNDARY:
            status = ESA_Stepping_Core_Report1HzBoundary(&stepping_core);
            break;

        case ESA_SIM_STEPPING_EVENT_TONE_SIGNAL:
            status = ESA_Stepping_Core_ReportToneSignal(&stepping_core);
            break;


        case ESA_SIM_STEPPING_EVENT_SCH_MINOR_FRAME:
            status = ESA_Stepping_Core_ReportSchMinorFrame(&stepping_core);
            break;


        case ESA_SIM_STEPPING_EVENT_SCH_SEND_TRIGGER:
            status = ESA_Stepping_Core_ReportSchSendTrigger(&stepping_core, event->entity_id);
            break;

        case ESA_SIM_STEPPING_EVENT_SCH_DISPATCH_COMPLETE:
            status = ESA_Stepping_Core_ReportSchDispatchComplete(&stepping_core);
            break;

        case ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_RECEIVE:
            status = ESA_Stepping_Core_ReportCoreServiceCmdPipeReceive(&stepping_core, event->entity_id);
            break;

        case ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_COMPLETE:
            status = ESA_Stepping_Core_ReportCoreServiceCmdPipeComplete(&stepping_core, event->entity_id);
            break;

        case ESA_SIM_STEPPING_EVENT_TIME_TONE_SEM_CONSUME:
            status = ESA_Stepping_Core_ReportTimeToneSemConsume(&stepping_core, event->entity_id);
            break;

        case ESA_SIM_STEPPING_EVENT_TIME_LOCAL_1HZ_SEM_CONSUME:
            status = ESA_Stepping_Core_ReportTimeLocal1HzSemConsume(&stepping_core, event->entity_id);
            break;

        case ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING:
            status = ESA_Stepping_Core_MarkSystemReadyForStepping(&stepping_core);
            break;

        default:
            /* 未知事件类型 - 返回错误且不转发 */
            status = -1;
            break;
    }

    return status;
}

#ifdef CFE_SIM_STEPPING

/**
 * @brief 查询请求的 TaskDelay 是否可由 stepping 处理
 *
 * @details 由 OSAL TaskDelay hook 调用以判断步进核心是否可接管延迟（返回 handled=true）
 *          或调用者应继续正常的实时睡眠（返回 handled=false）。
 *
 *          使用保守的合格性逻辑：核心必须已初始化，TaskDelay 接管门控必须开启，
 *          任务必须显式选择加入，且请求的延迟必须是 step_quantum_ns 的精确整数倍。
 *          如果门控关闭（默认）或任务未选择加入，总是返回 false。
 *
 * @param[in]  task_id   请求延迟的运行时任务 ID
 * @param[in]  delay_ms  请求的延迟时间（毫秒）
 *
 * @retval  true  如果延迟可由 stepping 处理（跳过实时睡眠）
 * @retval  false 如果延迟不能处理（继续正常实时睡眠）
 */
bool ESA_Stepping_Hook_TaskDelayEligible(uint32_t task_id, uint32_t delay_ms)
{
    /* 通过核心查询检查合格性 */
    if (core_initialized)
    {
        return ESA_Stepping_Core_QueryTaskDelayEligible(&stepping_core, task_id, delay_ms);
    }

    /* 核心未初始化；延迟不能被处理 */
    return false;
}

/**
 * @brief       等待被步进接管的 TaskDelay 到期
 * @details     在核心已初始化时等待指定任务的步进延迟到期，并在成功后补报延迟返回事实。
 * @param[in]   task_id    请求延迟的任务标识
 * @param[in]   delay_ms   期望等待的延迟时长（毫秒）
 * @retval      0          延迟到期且返回事实已上报
 * @retval      -1         核心未初始化或等待失败
 */
int32_t ESA_Stepping_WaitForDelayExpiry(uint32_t task_id, uint32_t delay_ms)
{
    int32_t status;

    if (core_initialized)
    {
        status = ESA_Stepping_Core_WaitForDelayExpiry(&stepping_core, task_id, delay_ms);
        if (status == 0)
        {
            status = ESA_Stepping_Core_ReportTaskDelayReturn(&stepping_core, task_id);
        }

        return status;
    }

    return -1;
}

#else

/**
 * @brief       查询 TaskDelay 接管资格的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，始终要求调用方回退到实时延迟路径。
 * @param[in]   task_id    请求延迟的任务标识
 * @param[in]   delay_ms   请求的延迟时长（毫秒）
 * @retval      false      步进模式未启用
 */
bool ESA_Stepping_Hook_TaskDelayEligible(uint32_t task_id, uint32_t delay_ms)
{
    (void)task_id;
    (void)delay_ms;
    return false;
}

/**
 * @brief       等待 TaskDelay 到期的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地返回失败，由调用方选择其他等待路径。
 * @param[in]   task_id    请求延迟的任务标识
 * @param[in]   delay_ms   请求的延迟时长（毫秒）
 * @retval      -1         步进模式未启用
 */
int32_t ESA_Stepping_WaitForDelayExpiry(uint32_t task_id, uint32_t delay_ms)
{
    (void)task_id;
    (void)delay_ms;
    return -1;
}

#endif

/***********************************************************************************
                    IN-PROCESS CONTROL ADAPTER IMPLEMENTATIONS
 ***********************************************************************************/

#ifdef CFE_SIM_STEPPING

/**
 * @brief 开始仿真步进（进程内控制适配器薄层）
 *
 * @details 通过调用核心的显式步进会话开始 API 启动新的步进周期。这确保每步簿记正确初始化，
 *          包括递增的会话计数器和活跃标志。立即返回而不阻塞。这是转发到单一共享步进核心的薄适配器，
 *          核心维护所有状态和语义。
 *
 * @retval  0  成功（步进已启动）
 * @retval  -1 如果 stepping 未初始化或核心无效
 */
int32_t ESA_Stepping_InProc_BeginStep(void)
{
    int32_t status;

    /* 门控：核心必须已初始化 */
    if (!core_initialized)
    {
        return -1;
    }

    status = ESA_Stepping_Core_BeginStepSession(&stepping_core);

    /* 步进模式下主动唤醒 SCH（Scheduler）应用的定时信号量
     * SCH 的时基定时器在步进模式被禁用，wall-clock 定时器不触发
     * 主动 Give 信号量等效于一次 Minor Frame 定时器触发，让 SCH 运行一次调度周期
     * 发送所有到期的消息（包括 SAMPLE_APP_SEND_HK_MID）*/
    if (status == 0)
    {
        osal_id_t sch_sem_id;
        if (OS_BinSemGetIdByName(&sch_sem_id, "SCH_TIME_SEM") == OS_SUCCESS)
        {
            OS_BinSemGive(sch_sem_id);
        }
    }

    return status;
}

/**
 * @brief 等待当前步进完成（进程内控制适配器薄层）
 *
 * @details 对步进核心执行有界轮询直到指示步进完成。此适配器本身不实现阻塞；它用可配置的
 *          超时语义重复查询核心。
 *
 *          保守轮询语义：
 *          - 如果核心未初始化 -> 立即失败（返回 -1）
 *          - 如果步进已完成 -> 立即成功（返回 0）
 *          - 如果 timeout_ms 为 0 (PEND_FOREVER) -> 无限轮询直到完成
 *          - 如果 timeout_ms 为 ~0U (非阻塞) -> 返回立即就绪/未就绪结果
 *          - 如果 timeout_ms 为有限值 -> 轮询直到超时；轮询间短暂睡眠
 *
 *          核心维护所有完成语义；此适配器是薄轮询包装器。
 *
 * @param[in]  timeout_ms  超时时间（毫秒）：
 *                         0 = PEND_FOREVER（无限轮询）
 *                         ~0U = 非阻塞轮询（立即结果）
 *                         有限值 > 0 = 超时时间（毫秒）
 *
 * @retval  0 如果步进成功完成
 * @retval  -1 如果超时、核心未初始化或轮询失败
 *
 * @note 轮询间隔在重试间使用保守的 1ms 睡眠。此睡眠在适配器内部且不修改 OSAL/TIME/SCH。
 *       不引入线程、socket 或第二个状态机。
 */
int32_t ESA_Stepping_InProc_WaitStepComplete(uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;
    bool     is_infinite_wait;
    bool     is_nonblocking;
    uint32_t poll_interval_ms = 1; /* 保守策略：轮询间睡眠 1ms */

    /* 门控：核心必须已初始化 */
    if (!core_initialized)
    {
        return -1;
    }

    if (!stepping_core.session_active)
    {
        return ESA_Stepping_Core_RecordDiagnostic(&stepping_core, ESA_SIM_STEPPING_DIAG_ILLEGAL_STATE,
                                                  ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE,
                                                  "InProc_WaitStepComplete_NoSession", timeout_ms, 0);
    }

    /* 标记显式等待/检查：启用延迟空会话完成 */
    stepping_core.completion_requested = true;

    /* 确定超时语义 */
    is_nonblocking   = (timeout_ms == ~0U); /* 非阻塞轮询：立即返回 */
    is_infinite_wait = (timeout_ms == 0);   /* 无限等待：永远轮询 */

    /* 轮询循环：检查完成状态，带超时管理 */
    while (1)
    {
        /* 查询核心检查步进是否完成 */
        if (ESA_Stepping_Core_IsStepComplete(&stepping_core))
        {
            return 0; /* 步进完成 - 成功 */
        }

        /**
         * @brief 步进模式下主动推进一次 SCH minor frame
         *
         * wall-clock 定时器在步进模式会被禁用，TIME->SCH 的自动链路不会触发。
         * 当会话仍活跃且 completion_ready 未置位时，主动调用 ReportSchMinorFrame
         * 以推进一个量子并设置 completion_ready，从而使 wait 能按设计完成。
         */
        if (stepping_core.session_active && !stepping_core.completion_ready)
        {
            ESA_Stepping_Core_ReportSchMinorFrame(&stepping_core);
        }

        /* 处理非阻塞轮询：如果未完成则立即返回 */
        if (is_nonblocking)
        {
            return -1; /* 未完成且请求了非阻塞 */
        }

        /* 处理无限等待：循环且不检查超时 */
        if (is_infinite_wait)
        {
            /* 重试前短暂睡眠（保守轮询） */
            usleep(poll_interval_ms * 1000);
            continue;
        }

        /* 处理有限超时：检查已过时间 */
        if (elapsed_ms >= timeout_ms)
        {
            return ESA_Stepping_Core_RecordDiagnostic(&stepping_core, ESA_SIM_STEPPING_DIAG_TIMEOUT,
                                                      ESA_SIM_STEPPING_STATUS_TIMEOUT, "InProc_WaitStepComplete",
                                                      timeout_ms, elapsed_ms);
        }

        /* 重试前短暂睡眠，直到超时 */
        usleep(poll_interval_ms * 1000);
        elapsed_ms += poll_interval_ms;
    }
}

/**
 * @brief 查询当前步进状态（进程内控制适配器薄层）
 *
 * @details 无阻塞返回步进核心的当前状态。此适配器转发到核心状态机，核心是步进状态的唯一权威。
 *          适配器不缓存、解释或修改状态；它只检索并返回核心拥有的内容。
 *
 * @param[out]  state_out     存储当前状态枚举值的指针（如非 NULL）
 * @param[out]  trigger_count 存储当前待处理触发器计数的指针（如非 NULL）
 *
 * @retval  0 如果状态查询成功
 * @retval  -1 如果核心未初始化或 state_out 指针验证失败
 */
int32_t ESA_Stepping_InProc_QueryState(uint32_t *state_out, uint32_t *trigger_count)
{
    ESA_Stepping_CoreState_t core_state;
    int32_t                  status;

    /* 门控：核心必须已初始化 */
    if (!core_initialized)
    {
        return -1;
    }

    /* 查询核心状态（必需） */
    status = ESA_Stepping_Core_QueryState(&stepping_core, &core_state);
    if (status != 0)
    {
        return -1;
    }

    /* 如果提供了输出指针则存储状态 */
    if (state_out != NULL)
    {
        *state_out = (uint32_t)core_state;
    }

    /* 如果提供了输出指针则存储触发器计数 */
    if (trigger_count != NULL)
    {
        *trigger_count = stepping_core.trigger_count;
    }

    return 0;
}

#else

/**
 * @brief       开始仿真步进的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地返回失败。
 * @retval      -1         步进模式未启用
 */
int32_t ESA_Stepping_InProc_BeginStep(void)
{
    return -1;
}

/**
 * @brief       等待步进完成的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地返回失败。
 * @param[in]   timeout_ms 等待超时（毫秒）
 * @retval      -1         步进模式未启用
 */
int32_t ESA_Stepping_InProc_WaitStepComplete(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return -1;
}

/**
 * @brief       查询步进状态的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地返回失败并忽略输出参数。
 * @param[out]  state_out      状态输出指针
 * @param[out]  trigger_count  触发器计数输出指针
 * @retval      -1             步进模式未启用
 */
int32_t ESA_Stepping_InProc_QueryState(uint32_t *state_out, uint32_t *trigger_count)
{
    (void)state_out;
    (void)trigger_count;
    return -1;
}

#endif

/***********************************************************************************
                     UDS CONTROL ADAPTER IMPLEMENTATIONS
 ***********************************************************************************/

#ifdef CFE_SIM_STEPPING

/**
 * @brief UDS 适配器状态（仅限 Linux 的 Unix domain socket 端点生命周期）
 *
 * 跟踪 UDS 端点初始化状态、监听套接字文件描述符和套接字路径。
 * 在 UDS_Init 中初始化，在 UDS_Shutdown 中清理。
 * 每个 PSP 模块仅有一个 UDS 适配器实例。
 */
static struct
{
    bool initialized;      /*!< 如果 UDS 适配器已成功初始化则为 true */
    int  listener_fd;      /*!< AF_UNIX 监听套接字文件描述符（未打开时为 -1） */
    char socket_path[256]; /*!< Unix domain socket 路径（此环境中稳定不变） */
} uds_adapter = {.initialized = false, .listener_fd = -1, .socket_path = {0}};

/**
 * @brief 初始化 UDS 控制适配器（精简实现）
 *
 * 标记 UDS 适配器为就绪状态。在核心和适配器都初始化之前返回保守的未就绪状态。
 * 所有实际的套接字操作都推迟到后续的 T10 时间片执行。
 *
 * @retval  0 成功（适配器已初始化）
 * @retval  -1 如果 stepping 未初始化或适配器已初始化
 */
int32_t ESA_Stepping_UDS_Init(void)
{
    struct sockaddr_un addr;
    int                sock_fd;
    int                status;

    if (!core_initialized)
    {
        return -1;
    }

    if (uds_adapter.initialized)
    {
        return -1;
    }

    sock_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_fd < 0)
    {
        return -1;
    }

    snprintf(uds_adapter.socket_path, sizeof(uds_adapter.socket_path), "/tmp/cfe_sim_stepping.sock");

    unlink(uds_adapter.socket_path);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, uds_adapter.socket_path, sizeof(addr.sun_path) - 1);

    status = bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (status < 0)
    {
        close(sock_fd);
        return -1;
    }

    status = listen(sock_fd, 5);
    if (status < 0)
    {
        close(sock_fd);
        unlink(uds_adapter.socket_path);
        return -1;
    }

    uds_adapter.listener_fd = sock_fd;
    uds_adapter.initialized = true;

    return 0;
}

/**
 * @brief UDS 命令的线协议常量（文件私有）
 */
#define UDS_BEGIN_STEP_OPCODE         1 /*!< BEGIN_STEP 请求操作码 */
#define UDS_QUERY_STATE_OPCODE        2 /*!< QUERY_STATE 请求操作码 */
#define UDS_WAIT_STEP_COMPLETE_OPCODE 3 /*!< WAIT_STEP_COMPLETE 请求操作码 */

/**
 * @brief WAIT_STEP_COMPLETE 请求的线格式（文件私有）
 *
 * 携带超时参数的固定大小请求。
 * 操作码字段标识命令；timeout_ms 传递给 InProc_WaitStepComplete。
 * 所有字段使用本地字节序（主机序）。
 */
typedef struct
{
    uint8_t  opcode;     /*!< 命令操作码（UDS_WAIT_STEP_COMPLETE_OPCODE） */
    uint32_t timeout_ms; /*!< 等待操作的超时时间（毫秒） */
} UDS_WaitStepCompleteRequest_t;

#define UDS_REQUEST_SIZE sizeof(UDS_WaitStepCompleteRequest_t) /*!< 固定大小请求 */

/**
 * @brief QUERY_STATE 响应的线格式（文件私有）
 *
 * 携带步进状态和诊断信息的固定大小响应。
 * 所有字段使用本地字节序（主机序）。
 */
typedef struct
{
    int32_t  status;        /*!< 查询结果：0=成功，非零=错误 */
    uint32_t state;         /*!< 当前核心状态（枚举值） */
    uint32_t trigger_count; /*!< 当前待处理触发器计数 */
} UDS_QueryStateResponse_t;

/**
 * @brief 服务一个 UDS 控制请求（处理带响应的固定大小命令）
 *
 * 执行非阻塞连接接受，最多处理一个待处理客户端。
 * 如果接受了客户端：
 * - 精确读取 UDS_REQUEST_SIZE 字节（包含操作码和可选超时参数的请求）
 * - 如果操作码为 UDS_BEGIN_STEP_OPCODE：
 *   - 调用 ESA_Stepping_InProc_BeginStep()
 *   - 回写 int32_t 状态响应
 * - 如果操作码为 UDS_QUERY_STATE_OPCODE：
 *   - 调用 ESA_Stepping_InProc_QueryState()
 *   - 回写固定大小的 UDS_QueryStateResponse_t（status, state, trigger_count）
 * - 如果操作码为 UDS_WAIT_STEP_COMPLETE_OPCODE：
 *   - 从请求中提取 timeout_ms
 *   - 调用 ESA_Stepping_InProc_WaitStepComplete(timeout_ms)
 *   - 回写 int32_t 状态响应
 * - 关闭客户端连接
 * - 返回 0（成功处理）
 *
 * 如果读取、未知操作码或写入失败时：
 * - 关闭客户端连接
 * - 返回共享分类法中的传输/协议状态
 *
 * 如果没有待处理客户端（EAGAIN/EWOULDBLOCK）：
 * - 返回 0（适配器空闲，正常）
 *
 * 如果核心/适配器未初始化或套接字错误：
 * - 返回 ESA_SIM_STEPPING_STATUS_NOT_READY
 *
 * 立即返回（非阻塞）。每个客户端一条命令，一个响应。
 *
 * @retval  ESA_SIM_STEPPING_STATUS_SUCCESS 如果空闲或请求已服务
 * @retval  ESA_SIM_STEPPING_STATUS_NOT_READY 如果适配器/核心未初始化
 * @retval  ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR 读取、写入或接受连接失败时
 * @retval  ESA_SIM_STEPPING_STATUS_PROTOCOL_ERROR 未知操作码时
 */
int32_t ESA_Stepping_UDS_Service(void)
{
    int                           client_fd;
    struct sockaddr_un            client_addr;
    socklen_t                     client_addr_len;
    UDS_WaitStepCompleteRequest_t request;
    int32_t                       response_status;
    ssize_t                       bytes_read;
    ssize_t                       bytes_written;

    if (!core_initialized)
    {
        return ESA_SIM_STEPPING_STATUS_NOT_READY;
    }

    if (!uds_adapter.initialized)
    {
        return ESA_SIM_STEPPING_STATUS_NOT_READY;
    }

    if (uds_adapter.listener_fd < 0)
    {
        return ESA_SIM_STEPPING_STATUS_NOT_READY;
    }

    memset(&client_addr, 0, sizeof(client_addr));
    client_addr_len = sizeof(client_addr);

    client_fd = accept(uds_adapter.listener_fd, (struct sockaddr *)&client_addr, &client_addr_len);

    if (client_fd < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }

        return ESA_Stepping_Core_RecordDiagnostic(&stepping_core, ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                  ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR, "UDS_Service_Accept",
                                                  (uint32_t)errno, 0);
    }

    memset(&request, 0, sizeof(request));
    bytes_read = read(client_fd, &request, UDS_REQUEST_SIZE);

    if (bytes_read != UDS_REQUEST_SIZE)
    {
        close(client_fd);
        return ESA_Stepping_Core_RecordDiagnostic(&stepping_core, ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                  ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR, "UDS_Service_Read",
                                                  (uint32_t)UDS_REQUEST_SIZE, (uint32_t)bytes_read);
    }

    if (request.opcode == UDS_BEGIN_STEP_OPCODE)
    {
        response_status = ESA_Stepping_InProc_BeginStep();

        bytes_written = write(client_fd, &response_status, sizeof(response_status));

        if (bytes_written != sizeof(response_status))
        {
            close(client_fd);
            return ESA_Stepping_Core_RecordDiagnostic(&stepping_core, ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                      ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR, "UDS_Service_WriteBegin",
                                                      (uint32_t)sizeof(response_status), (uint32_t)bytes_written);
        }
    }
    else if (request.opcode == UDS_QUERY_STATE_OPCODE)
    {
        uint32_t                 state_value   = 0;
        uint32_t                 trigger_count = 0;
        UDS_QueryStateResponse_t response      = {0};

        response.status        = ESA_Stepping_InProc_QueryState(&state_value, &trigger_count);
        response.state         = state_value;
        response.trigger_count = trigger_count;
        bytes_written          = write(client_fd, &response, sizeof(response));

        if (bytes_written != sizeof(response))
        {
            close(client_fd);
            return ESA_Stepping_Core_RecordDiagnostic(&stepping_core, ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                      ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR, "UDS_Service_WriteQuery",
                                                      (uint32_t)sizeof(response), (uint32_t)bytes_written);
        }
    }
    else if (request.opcode == UDS_WAIT_STEP_COMPLETE_OPCODE)
    {
        response_status = ESA_Stepping_InProc_WaitStepComplete(request.timeout_ms);
        bytes_written   = write(client_fd, &response_status, sizeof(response_status));

        if (bytes_written != sizeof(response_status))
        {
            close(client_fd);
            return ESA_Stepping_Core_RecordDiagnostic(&stepping_core, ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                      ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR, "UDS_Service_WriteWait",
                                                      (uint32_t)sizeof(response_status), (uint32_t)bytes_written);
        }
    }
    else
    {
        close(client_fd);
        return ESA_Stepping_Core_RecordDiagnostic(&stepping_core, ESA_SIM_STEPPING_DIAG_PROTOCOL_ERROR,
                                                  ESA_SIM_STEPPING_STATUS_PROTOCOL_ERROR, "UDS_Service_UnknownOpcode",
                                                  request.opcode, 0);
    }

    close(client_fd);
    return 0;
}

/**
 * @brief 关闭 UDS 控制适配器（精简存根实现）
 *
 * 标记 UDS 适配器为已关闭状态。如果适配器从未初始化则返回保守的错误状态。
 * 共享的 stepping 核心不受影响。
 * 现阶段无实际套接字清理；真正的套接字清理将在后续的 T10 时间片中添加。
 *
 * @retval  0 成功（适配器已关闭）
 * @retval  -1 如果适配器未初始化或关闭失败
 */
int32_t ESA_Stepping_UDS_Shutdown(void)
{
    if (!uds_adapter.initialized)
    {
        return -1;
    }

    if (uds_adapter.listener_fd >= 0)
    {
        close(uds_adapter.listener_fd);
        uds_adapter.listener_fd = -1;
    }

    if (uds_adapter.socket_path[0] != '\0')
    {
        unlink(uds_adapter.socket_path);
        memset(uds_adapter.socket_path, 0, sizeof(uds_adapter.socket_path));
    }

    uds_adapter.initialized = false;

    return 0;
}

/**
 * @brief 服务 UDS 适配器（非阻塞，每次调用单个请求）
 *
 * ESA_Stepping_UDS_Service() 的精简包装器，用于周期性步进钩子中。
 * 无论是否存在请求都立即返回。
 * 适合从定时器滴答、步进钩子或紧密轮询循环中调用。
 *
 * @retval  0 如果无待处理客户端或客户端请求处理成功
 * @retval  -1 如果适配器未初始化或服务失败
 */
int32_t ESA_Stepping_UDS_RunOnce(void)
{
    return ESA_Stepping_UDS_Service();
}

#else

/**
 * @brief       初始化 UDS 控制适配器的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地返回失败。
 * @retval      -1         步进模式未启用
 */
int32_t ESA_Stepping_UDS_Init(void)
{
    return -1;
}

/**
 * @brief       服务 UDS 控制适配器的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地返回失败。
 * @retval      -1         步进模式未启用
 */
int32_t ESA_Stepping_UDS_Service(void)
{
    return -1;
}

/**
 * @brief       关闭 UDS 控制适配器的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地返回失败。
 * @retval      -1         步进模式未启用
 */
int32_t ESA_Stepping_UDS_Shutdown(void)
{
    return -1;
}

/**
 * @brief       单步服务 UDS 适配器的禁用模式存根
 * @details     当未启用 CFE_SIM_STEPPING 时，保守地返回失败。
 * @retval      -1         步进模式未启用
 */
int32_t ESA_Stepping_UDS_RunOnce(void)
{
    return -1;
}

#endif

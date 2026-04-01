/**
 * @file
 * @ingroup esa
 * @brief       仿真步进核心状态机实现
 * @author      gaoyuan
 * @date        2026-03-20
 *
 * @details     本模块负责原生步进核心的所有步进语义。维护状态机、触发器跟踪和仿真时间。所有钩子将事实报告到此核心。
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "common_types.h"
#include "esa_stepping.h"
#include "esa_stepping_core.h"

/****************************************************************************************
                            前向声明与私有状态
 ****************************************************************************************/

static uint32_t    ESA_Stepping_AddTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask, uint32_t entity_id);
static void        ESA_Stepping_ClearTriggers(ESA_Stepping_Core_t *core);
static uint32_t    ESA_Stepping_AcknowledgeTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask, uint32_t entity_id);
static bool        ESA_Stepping_HasTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask, uint32_t entity_id);
static bool        ESA_Stepping_Core_IsStepComplete_ReadOnly(ESA_Stepping_Core_t *core);
static bool        ESA_Stepping_HasTaskDelayDebt(ESA_Stepping_Core_t *core);
static const char *ESA_Stepping_DiagClassToString(ESA_Stepping_DiagnosticClass_t diag_class);

/****************************************************************************************
                               私有辅助实现
 ****************************************************************************************/

/**
 * @brief       向当前步进会话添加触发器
 * @param[in,out] core         步进核心实例
 * @param[in]     source_mask  触发器来源掩码
 * @param[in]     entity_id    触发器关联实体标识
 * @retval        非零         新分配的触发器标识
 * @retval        0            触发器列表已满
 */
static uint32_t ESA_Stepping_AddTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask, uint32_t entity_id)
{
    if (core->trigger_count >= ESA_SIM_STEPPING_MAX_TRIGGERS)
    {
        return 0;
    }

    ESA_Stepping_Trigger_t *trigger = &core->triggers[core->trigger_count];
    trigger->trigger_id             = core->next_trigger_id++;
    trigger->source_mask            = source_mask;
    trigger->entity_id              = entity_id;
    trigger->is_acknowledged        = false;

    core->trigger_count++;
    core->acks_expected++;

    return trigger->trigger_id;
}

/**
 * @brief       清空当前会话的触发器与完成簿记
 * @param[in,out] core 步进核心实例
 */
static void ESA_Stepping_ClearTriggers(ESA_Stepping_Core_t *core)
{
    memset(core->triggers, 0, ESA_SIM_STEPPING_MAX_TRIGGERS * sizeof(ESA_Stepping_Trigger_t));
    core->trigger_count                = 0;
    core->acks_received                = 0;
    core->acks_expected                = 0;
    core->completion_requested         = false;
    core->completion_ready             = false;
    core->core_service_membership_mask = 0;
}

/**
 * @brief       确认匹配的触发器
 * @param[in,out] core         步进核心实例
 * @param[in]     source_mask  触发器来源掩码
 * @param[in]     entity_id    触发器关联实体标识
 * @retval        非零         成功确认的触发器标识
 * @retval        0            未找到匹配触发器或触发器已确认
 */
static uint32_t ESA_Stepping_AcknowledgeTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask, uint32_t entity_id)
{
    uint32_t i;

    if (core == NULL)
    {
        return 0;
    }

    for (i = 0; i < core->trigger_count; i++)
    {
        ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
        if (trigger->source_mask == source_mask && trigger->entity_id == entity_id)
        {
            if (!trigger->is_acknowledged)
            {
                trigger->is_acknowledged = true;
                core->acks_received++;
                return trigger->trigger_id;
            }
        }
    }

    return 0;
}

/**
 * @brief       检查当前会话是否已存在匹配触发器
 * @param[in]   core         步进核心实例
 * @param[in]   source_mask  触发器来源掩码
 * @param[in]   entity_id    触发器关联实体标识
 * @retval      true         已存在匹配触发器
 * @retval      false        不存在匹配触发器或核心无效
 */
static bool ESA_Stepping_HasTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask, uint32_t entity_id)
{
    uint32_t i;

    if (core == NULL)
    {
        return false;
    }

    for (i = 0; i < core->trigger_count; i++)
    {
        ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
        if (trigger->source_mask == source_mask && trigger->entity_id == entity_id)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief       检查是否存在已到期但尚未归还的 TaskDelay
 * @param[in,out] core 步进核心实例
 * @retval        true        存在待处理的 TaskDelay 欠账
 * @retval        false       不存在待处理欠账或核心无效
 */
static bool ESA_Stepping_HasTaskDelayDebt(ESA_Stepping_Core_t *core)
{
    uint32_t i;

    if (core == NULL)
    {
        return false;
    }

    for (i = 0; i < core->taskdelay_optin_count; i++)
    {
        if (core->taskdelay_pending[i] && core->sim_time_ns >= core->taskdelay_expiry_ns[i])
        {
            core->taskdelay_owed[i] = true;
        }

        if (core->taskdelay_owed[i])
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief       将诊断类别转换为日志字符串
 * @param[in]   diag_class 诊断类别
 * @retval      非 NULL    对应的诊断类别字符串
 */
static const char *ESA_Stepping_DiagClassToString(ESA_Stepping_DiagnosticClass_t diag_class)
{
    switch (diag_class)
    {
        case ESA_SIM_STEPPING_DIAG_TIMEOUT:
            return "timeout";
        case ESA_SIM_STEPPING_DIAG_DUPLICATE_BEGIN:
            return "duplicate_begin";
        case ESA_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE:
            return "illegal_complete";
        case ESA_SIM_STEPPING_DIAG_ILLEGAL_STATE:
            return "illegal_state";
        case ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR:
            return "transport_error";
        case ESA_SIM_STEPPING_DIAG_PROTOCOL_ERROR:
            return "protocol_error";
        default:
            return "unknown";
    }
}

/****************************************************************************************
                               公共核心 API
 ****************************************************************************************/

/**
 * @brief       初始化步进核心
 * @details     建立初始状态、默认量子和诊断计数器，为后续步进会话做好准备。
 * @param[out]  core              要初始化的核心结构指针
 * @param[in]   initial_time_ns   初始仿真时间（纳秒）
 * @param[in]   trigger_capacity  保留的容量参数，仅用于 API 兼容
 * @retval      0                 初始化成功
 * @retval      -1                输入核心指针无效
 */
int32_t ESA_Stepping_Core_Init(ESA_Stepping_Core_t *core, uint64_t initial_time_ns, uint32_t trigger_capacity)
{
    (void)trigger_capacity;

    if (core == NULL)
    {
        return -1;
    }

    memset(core, 0, sizeof(ESA_Stepping_Core_t));

    core->current_state                = ESA_SIM_STEPPING_STATE_READY;
    core->sim_time_ns                  = initial_time_ns;
    core->next_sim_time_ns             = initial_time_ns;
    core->step_quantum_ns              = 10000000;
    core->step_timeout_ms              = 5000;
    core->next_trigger_id              = 1;
    core->taskdelay_takeover_enabled   = false;
    core->taskdelay_optin_count        = 0;
    core->core_service_membership_mask = 0;
    core->session_active               = false;
    core->session_counter              = 0;
    core->system_ready_for_stepping    = false;

    return 0;
}

/**
 * @brief       重置步进核心以开始新的步进周期
 * @details     清除触发器、确认计数器和完成簿记，并回到 READY 状态。
 * @param[in,out] core 要重置的核心结构指针
 * @retval        0    重置成功
 * @retval        -1   输入核心指针无效
 */
int32_t ESA_Stepping_Core_Reset(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    ESA_Stepping_ClearTriggers(core);
    core->current_state = ESA_SIM_STEPPING_STATE_READY;

    return 0;
}

/**
 * @brief       开始新的步进会话
 * @details     仅在系统生命周期已允许步进且上一会话已经完成时，才接受新的 BEGIN_STEP 请求。
 * @param[in,out] core 步进核心实例
 * @retval        0    会话开始成功
 * @retval        -1   输入核心指针无效
 * @retval        -3   系统尚未进入允许步进的生命周期阶段
 * @retval        其他 来自重复开始保护的诊断状态码
 */
int32_t ESA_Stepping_Core_BeginStepSession(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    /* 生命周期就绪前拒绝开始步进；必须先由 shim 上报系统已允许步进。 */
    if (!core->system_ready_for_stepping)
    {
        return -3;
    }

    /* 若上一会话仍未解决，则拒绝重复 BEGIN_STEP，并使用只读检查避免副作用。 */
    if (core->session_active && !ESA_Stepping_Core_IsStepComplete_ReadOnly(core))
    {
        return ESA_Stepping_Core_RecordDiagnostic(core, ESA_SIM_STEPPING_DIAG_DUPLICATE_BEGIN,
                                                  ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN, "BeginStepSession",
                                                  (uint32_t)core->session_counter, (uint32_t)core->acks_expected);
    }

    ESA_Stepping_ClearTriggers(core);
    core->session_active = true;
    core->session_counter++;

    /* 新会话始终从 READY 开始以便先积累触发器，空会话完成延后到显式检查阶段。 */
    core->current_state = ESA_SIM_STEPPING_STATE_READY;

    return 0;
}

/**
 * @brief       报告任务延迟边界事件
 * @details     记录任务是否参与 TaskDelay 接管，并在符合资格时登记到期时间。
 * @param[in,out] core      核心结构指针
 * @param[in]     task_id   请求延迟的任务标识符
 * @param[in]     delay_ms  请求的延迟时间（毫秒）
 * @retval        0         报告已处理
 * @retval        -1        输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportTaskDelay(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms)
{
    uint32_t i;
    uint32_t optin_index;
    bool     already_registered;
    bool     tracked;

    if (core == NULL)
    {
        return -1;
    }

    if (core->system_ready_for_stepping)
    {
        already_registered = false;
        tracked            = false;
        optin_index        = 0;
        for (i = 0; i < core->taskdelay_optin_count; i++)
        {
            if (core->taskdelay_optin_set[i] == task_id)
            {
                already_registered = true;
                tracked            = true;
                optin_index        = i;
                break;
            }
        }

        if (!already_registered && core->taskdelay_optin_count < 8)
        {
            core->taskdelay_optin_set[core->taskdelay_optin_count] = task_id;
            optin_index                                            = core->taskdelay_optin_count;
            core->taskdelay_optin_count++;
            tracked = true;
        }

        if (tracked)
        {
            if (ESA_Stepping_Core_QueryTaskDelayEligible(core, task_id, delay_ms))
            {
                core->taskdelay_pending[optin_index]   = true;
                core->taskdelay_owed[optin_index]      = false;
                core->taskdelay_expiry_ns[optin_index] = core->sim_time_ns + (((uint64_t)delay_ms) * 1000000);
            }
            else
            {
                core->taskdelay_pending[optin_index]   = false;
                core->taskdelay_owed[optin_index]      = false;
                core->taskdelay_expiry_ns[optin_index] = 0;
            }
        }
    }

    return 0;
}

/**
 * @brief       报告 TaskDelay 确认边界事件
 * @details     在任务真正进入被步进接管的延迟等待前登记确认事实。
 * @param[in,out] core      核心结构指针
 * @param[in]     task_id   任务标识符
 * @param[in]     delay_ms  请求的延迟时间（毫秒）
 * @retval        0         报告已处理
 * @retval        -1        输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportTaskDelayAck(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms)
{
    (void)delay_ms;

    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        if (ESA_Stepping_HasTrigger(core, 0x100, task_id))
        {
            return 0;
        }

        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x100, task_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       报告 TaskDelay 完成边界事件
 * @details     在任务结束被接管的延迟等待后确认对应触发器。
 * @param[in,out] core      核心结构指针
 * @param[in]     task_id   任务标识符
 * @param[in]     delay_ms  请求的延迟时间（毫秒）
 * @retval        0         报告已处理
 * @retval        -1        输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportTaskDelayComplete(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms)
{
    (void)delay_ms;

    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        uint32_t trigger_id = ESA_Stepping_AcknowledgeTrigger(core, 0x100, task_id);
        if (trigger_id == 0)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       报告任务已从 TaskDelay 返回
 * @details     清除该任务在 TaskDelay 接管表中的待处理状态与到期簿记。
 * @param[in,out] core     核心结构指针
 * @param[in]     task_id  任务标识符
 * @retval        0        报告已处理
 * @retval        -1       输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportTaskDelayReturn(ESA_Stepping_Core_t *core, uint32_t task_id)
{
    uint32_t i;

    if (core == NULL)
    {
        return -1;
    }

    for (i = 0; i < core->taskdelay_optin_count; i++)
    {
        if (core->taskdelay_optin_set[i] == task_id)
        {
            core->taskdelay_pending[i]   = false;
            core->taskdelay_owed[i]      = false;
            core->taskdelay_expiry_ns[i] = 0;
            break;
        }
    }

    return 0;
}

/**
 * @brief       报告队列接收边界事件
 * @details     当任务阻塞于队列接收时调用，向当前步进会话登记对应触发器。
 * @param[in,out] core       核心结构指针
 * @param[in]     queue_id   接收队列标识符
 * @param[in]     timeout_ms 接收超时（毫秒）
 * @retval        0          报告已处理
 * @retval        -1         输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportQueueReceive(ESA_Stepping_Core_t *core, uint32_t queue_id, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        ESA_Stepping_AddTrigger(core, 0x02, queue_id);
    }

    return 0;
}



/**
 * @brief       报告 1Hz 边界事件
 * @details     确认当前会话中所有待处理的 TIME local-1Hz 子路径触发器。
 * @param[in,out] core 核心结构指针
 * @retval        0    报告已处理
 * @retval        -1   输入核心指针无效
 */
int32_t ESA_Stepping_Core_Report1HzBoundary(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        uint32_t trigger_id = 0;
        uint32_t i;
        bool     any_acknowledged = false;

        for (i = 0; i < core->trigger_count; i++)
        {
            ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
            if (trigger->source_mask == 0x20000 && !trigger->is_acknowledged)
            {
                trigger->is_acknowledged = true;
                core->acks_received++;
                any_acknowledged = true;
                trigger_id       = trigger->trigger_id;
            }
        }

        if (!any_acknowledged)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       报告音调信号事件
 * @details     确认当前会话中所有待处理的 TIME tone 子路径触发器。
 * @param[in,out] core 核心结构指针
 * @retval        0    报告已处理
 * @retval        -1   输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportToneSignal(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        uint32_t trigger_id = 0;
        uint32_t i;
        bool     any_acknowledged = false;

        for (i = 0; i < core->trigger_count; i++)
        {
            ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
            if (trigger->source_mask == 0x10000 && !trigger->is_acknowledged)
            {
                trigger->is_acknowledged = true;
                core->acks_received++;
                any_acknowledged = true;
                trigger_id       = trigger->trigger_id;
            }
        }

        if (!any_acknowledged)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}


/**
 * @brief       报告调度器小帧边界事件
 * @details     在步进会话活跃且尚未完成时推进一个仿真量子，并标记完成检查已就绪。
 * @param[in,out] core 核心结构指针
 * @retval        0    推进成功或当前无需推进
 * @retval        -1   输入核心指针无效或推进失败
 */
int32_t ESA_Stepping_Core_ReportSchMinorFrame(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (!core->session_active)
    {
        return 0;
    }

    if (core->completion_ready)
    {
        return 0;
    }

    int32_t adv_status = ESA_Stepping_Core_AdvanceOneQuantum(core);
    if (adv_status != 0)
    {
        return adv_status;
    }

    core->completion_ready = true;

    if (core->current_state == ESA_SIM_STEPPING_STATE_READY)
    {
        core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
    }

    return 0;
}


/**
 * @brief       报告调度器发送触发器事件
 * @details     当调度器向目标任务发送触发信号时，为当前步进会话登记对应触发器。
 * @param[in,out] core      核心结构指针
 * @param[in]     target_id 被触发的目标标识符
 * @retval        0         报告已处理
 * @retval        -1        输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportSchSendTrigger(ESA_Stepping_Core_t *core, uint32_t target_id)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x2000, target_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       报告调度器分发完成事件
 * @details     确认当前会话中所有待处理的调度器发送触发器，并在需要时推进状态机。
 * @param[in,out] core 核心结构指针
 * @retval        0    报告已处理
 * @retval        -1   输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportSchDispatchComplete(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        uint32_t trigger_id = 0;
        uint32_t i;
        bool     any_acknowledged = false;

        for (i = 0; i < core->trigger_count; i++)
        {
            ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
            if (trigger->source_mask == 0x2000 && !trigger->is_acknowledged)
            {
                trigger->is_acknowledged = true;
                core->acks_received++;
                any_acknowledged = true;
                trigger_id       = trigger->trigger_id;
            }
        }

        if (!any_acknowledged)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }

        /* 可观测性标记：调度器完成但系统未完成 */
        if (any_acknowledged && core->current_state != ESA_SIM_STEPPING_STATE_COMPLETE)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_WAITING;
        }
    }

    return 0;
}

/**
 * @brief       报告核心服务命令管道接收事件
 * @details     记录指定核心服务已参与当前步进会话，并为其命令管道接收登记触发器。
 * @param[in,out] core       核心结构指针
 * @param[in]     service_id 核心服务标识符
 * @retval        0          报告已处理
 * @retval        -1         输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportCoreServiceCmdPipeReceive(ESA_Stepping_Core_t *core, uint32_t service_id)
{
    uint32_t service_bit;

    if (core == NULL)
    {
        return -1;
    }

    /* 将服务标识映射为位掩码位，用于成员跟踪 */
    if (service_id < 5)
    {
        service_bit = (1U << service_id);
        core->core_service_membership_mask |= service_bit;
    }

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x8000, service_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       报告核心服务命令管道处理完成事件
 * @details     确认指定核心服务此前登记的命令管道接收触发器，并在非法完成时记录诊断。
 * @param[in,out] core       核心结构指针
 * @param[in]     service_id 核心服务标识符
 * @retval        0          报告已处理
 * @retval        -1         输入核心指针无效
 * @retval        其他       诊断记录返回的步进状态码
 */
int32_t ESA_Stepping_Core_ReportCoreServiceCmdPipeComplete(ESA_Stepping_Core_t *core, uint32_t service_id)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AcknowledgeTrigger(core, 0x8000, service_id);
        if (trigger_id == 0)
        {
            return ESA_Stepping_Core_RecordDiagnostic(core, ESA_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE,
                                                      ESA_SIM_STEPPING_STATUS_ILLEGAL_COMPLETE,
                                                      "CoreServiceCmdPipeComplete", service_id, 0);
        }
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       报告 TIME 音调信号量消费事件
 * @details     记录 TIME tone 子路径已参与当前步进，并为其登记对应触发器。
 * @param[in,out] core   核心结构指针
 * @param[in]     sem_id 信号量标识符
 * @retval        0      报告已处理
 * @retval        -1     输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportTimeToneSemConsume(ESA_Stepping_Core_t *core, uint32_t sem_id)
{
    if (core == NULL)
    {
        return -1;
    }

    core->core_service_membership_mask |= ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_TONE;

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x10000, sem_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       报告 TIME 本地 1Hz 信号量消费事件
 * @details     记录 TIME local-1Hz 子路径已参与当前步进，并为其登记对应触发器。
 * @param[in,out] core   核心结构指针
 * @param[in]     sem_id 信号量标识符
 * @retval        0      报告已处理
 * @retval        -1     输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportTimeLocal1HzSemConsume(ESA_Stepping_Core_t *core, uint32_t sem_id)
{
    if (core == NULL)
    {
        return -1;
    }

    /* 记录时间模块本地 1 赫兹子路径参与当前步进 */
    core->core_service_membership_mask |= ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_LOCAL_1HZ;

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x20000, sem_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       查询当前仿真时间
 * @details     读取核心当前维护的仿真时间值，并写入调用方提供的输出参数。
 * @param[in]   core        核心结构指针
 * @param[out]  sim_time_ns 用于接收仿真时间的输出指针
 * @retval      0           查询成功
 * @retval      -1          输入参数无效
 */
int32_t ESA_Stepping_Core_QuerySimTime(ESA_Stepping_Core_t *core, uint64_t *sim_time_ns)
{
    if (core == NULL || sim_time_ns == NULL)
    {
        return -1;
    }

    *sim_time_ns = core->sim_time_ns;

    return 0;
}

/**
 * @brief       报告队列接收确认边界事件
 * @details     在任务真正进入队列等待前，为该队列登记确认触发器。
 * @param[in,out] core       核心结构指针
 * @param[in]     task_id    任务标识符
 * @param[in]     queue_id   队列标识符
 * @param[in]     timeout_ms 等待超时（毫秒）
 * @retval        0          报告已处理
 * @retval        -1         输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportQueueReceiveAck(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t queue_id,
                                                uint32_t timeout_ms)
{
    (void)task_id;
    (void)timeout_ms;

    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        if (ESA_Stepping_HasTrigger(core, 0x200, queue_id))
        {
            return 0;
        }

        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x200, queue_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       上报队列接收完成事件
 * @details     在步进会话允许完成判定时，确认对应队列触发器已经完成。
 * @param[in,out] core        核心结构指针
 * @param[in]     task_id     任务标识符
 * @param[in]     queue_id    队列标识符
 * @param[in]     timeout_ms  原始等待超时（毫秒）
 * @retval        0           处理成功或当前无需记录
 * @retval        -1          核心指针为空
 */
int32_t ESA_Stepping_Core_ReportQueueReceiveComplete(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t queue_id,
                                                     uint32_t timeout_ms)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        uint32_t trigger_id = ESA_Stepping_AcknowledgeTrigger(core, 0x200, queue_id);
        if (trigger_id == 0)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0; /* 成功：匹配队列完成路径 */
}

/**
 * @brief       查询 TaskDelay 请求是否可被步进接管
 * @details     依据系统就绪状态、任务参与登记和量子整除条件，保守判断延迟是否符合接管要求。
 * @param[in]   core      核心结构指针
 * @param[in]   task_id   任务标识符
 * @param[in]   delay_ms  请求延迟时间（毫秒）
 * @retval      true      可由步进核心接管
 * @retval      false     不满足接管条件
 */
bool ESA_Stepping_Core_QueryTaskDelayEligible(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms)
{
    uint64_t delay_ns;
    uint64_t remainder_ns;
    uint32_t i;
    bool     task_opted_in;

    /* 保守门控 1：核心必须已初始化且有效 */
    if (core == NULL)
    {
        return false;
    }

    /* 保守门控 2：系统必须已准备好进行步进（启动/运行时生命周期） */
    /* 此门控防止在启动同步路径（如 CFE_ES_GetTaskFunction）中接管延迟，
     * 允许这些延迟使用墙钟睡眠以避免死锁。一旦 ES 发信号表示
     * system_ready_for_stepping，运行时延迟即可被步进控制接管。 */
    if (!core->system_ready_for_stepping)
    {
        return false;
    }

    /* 保守门控 3：任务延迟接管必须已启用 */
    if (!core->taskdelay_takeover_enabled)
    {
        return false;
    }

    if (core->session_counter == 0)
    {
        return false;
    }

    /* 保守门控 4：任务必须显式注册参与 */
    task_opted_in = false;
    for (i = 0; i < core->taskdelay_optin_count; i++)
    {
        if (core->taskdelay_optin_set[i] == task_id)
        {
            task_opted_in = true;
            break;
        }
    }
    if (!task_opted_in)
    {
        return false;
    }

    /* 保守门控 5：延迟必须是量子的精确整数倍 */
    /* 将 delay_ms 转换为纳秒（1 ms = 1,000,000 ns） */
    delay_ns = ((uint64_t)delay_ms) * 1000000;

    /* 检查延迟是否为量子的精确倍数 */
    remainder_ns = delay_ns % core->step_quantum_ns;
    if (remainder_ns != 0)
    {
        return false;
    }

    /* 所有保守检查通过 */
    return true;
}

/**
 * @brief       将仿真时间推进一个量子
 * @details     同步推进当前仿真时间与下一仿真时间，保持步进核心内部时间簿记一致。
 * @param[in,out] core 核心结构指针
 * @retval        0    推进成功
 * @retval        -1   输入核心指针无效或量子未配置
 */
int32_t ESA_Stepping_Core_AdvanceOneQuantum(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->step_quantum_ns == 0)
    {
        return -1;
    }

    core->sim_time_ns += core->step_quantum_ns;
    core->next_sim_time_ns += core->step_quantum_ns;

    return 0;
}

/**
 * @brief       等待被步进接管的延迟到期
 * @details     按目标仿真时间轮询共享核心的仿真时钟，直到延迟预算被显式步进量子满足。
 * @param[in]   core      核心结构指针
 * @param[in]   task_id   任务标识符
 * @param[in]   delay_ms  请求延迟时间（毫秒）
 * @retval      0         等待成功完成
 * @retval      -1        输入核心指针无效
 */
int32_t ESA_Stepping_Core_WaitForDelayExpiry(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms)
{
    uint64_t        target_expiry_ns;
    struct timespec poll_interval;

    (void)task_id;

    if (core == NULL)
    {
        return -1;
    }

    target_expiry_ns = core->sim_time_ns + (((uint64_t)delay_ms) * 1000000);

    poll_interval.tv_sec  = 0;
    poll_interval.tv_nsec = 1000000;

    while (core->sim_time_ns < target_expiry_ns)
    {
        nanosleep(&poll_interval, NULL);
    }

    return 0;
}

/**
 * @brief       报告二值信号量获取确认边界事件
 * @details     在任务真正进入二值信号量等待前，为该信号量登记确认触发器。
 * @param[in,out] core       核心结构指针
 * @param[in]     task_id    任务标识符
 * @param[in]     sem_id     信号量标识符
 * @param[in]     timeout_ms 等待超时（毫秒）
 * @retval        0          报告已处理
 * @retval        -1         输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportBinSemTakeAck(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t sem_id,
                                              uint32_t timeout_ms)
{
    (void)task_id;
    (void)timeout_ms;

    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        if (ESA_Stepping_HasTrigger(core, 0x800, sem_id))
        {
            return 0;
        }

        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x800, sem_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       报告二值信号量获取完成边界事件
 * @details     在任务离开二值信号量等待后，确认此前登记的完成触发器。
 * @param[in,out] core       核心结构指针
 * @param[in]     task_id    任务标识符
 * @param[in]     sem_id     信号量标识符
 * @param[in]     timeout_ms 等待超时（毫秒）
 * @retval        0          报告已处理
 * @retval        -1         输入核心指针无效
 */
int32_t ESA_Stepping_Core_ReportBinSemTakeComplete(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t sem_id,
                                                   uint32_t timeout_ms)
{
    (void)task_id;
    (void)timeout_ms;

    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        uint32_t trigger_id = ESA_Stepping_AcknowledgeTrigger(core, 0x800, sem_id);
        if (trigger_id == 0)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * @brief       为进程内适配器查询步进核心状态
 * @details     读取当前状态机状态，并在提供输出参数时写回给调用方。
 * @param[in]   core      核心结构指针
 * @param[out]  state_out 用于接收当前状态的输出指针，可为 NULL
 * @retval      0         查询成功
 * @retval      -1        输入核心指针无效
 */
int32_t ESA_Stepping_Core_QueryState(ESA_Stepping_Core_t *core, ESA_Stepping_CoreState_t *state_out)
{
    if (core == NULL)
    {
        return -1;
    }

    if (state_out != NULL)
    {
        *state_out = core->current_state;
    }

    return 0;
}

/**
 * @brief       无副作用检查步进是否完成
 * @details     供重复 Begin 保护路径调用，只读判断当前会话是否已经完成，不修改任何核心状态。
 * @param[in]   core 核心结构指针
 * @retval      true 已满足完成条件
 * @retval      false 尚未完成或输入无效
 */
static bool ESA_Stepping_Core_IsStepComplete_ReadOnly(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return false;
    }

    return (core->current_state == ESA_SIM_STEPPING_STATE_COMPLETE && core->acks_received >= core->acks_expected);
}

/**
 * @brief       为进程内适配器轮询步进是否完成
 * @details     根据完成请求、预期确认数和空会话规则更新核心状态，并在真正完成时结束活动会话。
 * @param[in,out] core 核心结构指针
 * @retval        true 当前步进会话已完成
 * @retval        false 当前仍未完成或输入无效
 */
bool ESA_Stepping_Core_IsStepComplete(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return false;
    }

    if (core->completion_requested && ESA_Stepping_HasTaskDelayDebt(core))
    {
        return false;
    }

    /* 延迟空会话完成：仅当显式请求完成时才转换。
       此门控确保重复 begin 拒绝不会隐式完成空会话。 */
    if (core->completion_requested && core->completion_ready && core->acks_expected == 0 &&
        core->current_state == ESA_SIM_STEPPING_STATE_RUNNING)
    {
        core->current_state = ESA_SIM_STEPPING_STATE_COMPLETE;
    }

    if (core->completion_requested && core->completion_ready && core->acks_expected > 0 &&
        core->acks_received >= core->acks_expected && core->current_state != ESA_SIM_STEPPING_STATE_COMPLETE)
    {
        core->current_state = ESA_SIM_STEPPING_STATE_COMPLETE;
    }

    /* 当所有预期确认均已接收且核心进入完成态时，本步进结束 */
    if (core->current_state == ESA_SIM_STEPPING_STATE_COMPLETE && core->acks_received >= core->acks_expected)
    {
        core->session_active = false;
        return true;
    }

    return false;
}

/**
 * @brief       标记系统已进入仿真步进就绪状态
 * @details     打开系统就绪门控与 TaskDelay 接管门控，使运行期等待可被步进核心接管。
 * @param[in,out] core 核心结构指针
 * @retval        0    标记成功
 * @retval        -1   输入核心指针无效
 */
int32_t ESA_Stepping_Core_MarkSystemReadyForStepping(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    core->system_ready_for_stepping  = true;
    core->taskdelay_takeover_enabled = true;

    return 0;
}

/**
 * @brief       记录步进诊断信息并返回状态码
 * @details     递增对应诊断计数器，输出带分类与细节的诊断日志，并原样返回传入状态码。
 * @param[in,out] core       核心结构指针
 * @param[in]     diag_class 诊断分类
 * @param[in]     status     需要返回的步进状态码
 * @param[in]     site       诊断发生位置字符串
 * @param[in]     detail_a   诊断细节 A
 * @param[in]     detail_b   诊断细节 B
 * @retval        status     原样返回调用方提供的状态码
 * @retval        ESA_SIM_STEPPING_STATUS_FAILURE 输入参数无效
 */
int32_t ESA_Stepping_Core_RecordDiagnostic(ESA_Stepping_Core_t *core, ESA_Stepping_DiagnosticClass_t diag_class,
                                           int32_t status, const char *site, uint32_t detail_a, uint32_t detail_b)
{
    const char *class_name;

    if (core == NULL || site == NULL)
    {
        return ESA_SIM_STEPPING_STATUS_FAILURE;
    }

    switch (diag_class)
    {
        case ESA_SIM_STEPPING_DIAG_TIMEOUT:
            core->diagnostics.timeout_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_DUPLICATE_BEGIN:
            core->diagnostics.duplicate_begin_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE:
            core->diagnostics.illegal_complete_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_ILLEGAL_STATE:
            core->diagnostics.illegal_state_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR:
            core->diagnostics.transport_error_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_PROTOCOL_ERROR:
            core->diagnostics.protocol_error_count++;
            break;
        default:
            break;
    }

    class_name = ESA_Stepping_DiagClassToString(diag_class);
    printf("CFE_PSP: SIM_STEPPING_DIAG class=%s status=%ld site=%s detail_a=%lu detail_b=%lu\n", class_name,
           (long)status, site, (unsigned long)detail_a, (unsigned long)detail_b);

    return status;
}

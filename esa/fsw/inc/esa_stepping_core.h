/**
 * @file
 * @ingroup esa
 * @brief       仿真步进核心状态机内部头文件
 * @author      gaoyuan
 * @date        2026-03-20
 *
 * @details     本头文件定义步进状态的所有权边界和核心事件报告入口点。包含状态机结构、触发器跟踪和诊断计数器定义。
 */
#ifndef ESA_SIM_STEPPING_CORE_H
#define ESA_SIM_STEPPING_CORE_H

/****************************************************************************************
                                      INCLUDE FILES
 ***************************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/****************************************************************************************
                           PSP-LOCAL CONFIGURATION CONSTANTS
 ***************************************************************************************/

/**
 * @brief       最大并发步进触发器数量
 *
 * @details     骨架步进核心的固定编译时容量。适用于仅原生仿真步进的典型工作负载。
 */
#define ESA_SIM_STEPPING_MAX_TRIGGERS 32

/**
 * @brief       核心服务成员意图位掩码常量
 *
 * @details     五个核心服务（ES、EVS、SB、TBL、TIME）的位位置。
 *              用于跟踪当前步进中哪些服务已报告命令管道接收事实。
 */
#define ESA_SIM_STEPPING_SERVICE_BIT_ES   (1U << 0) /*!< 执行服务 */
#define ESA_SIM_STEPPING_SERVICE_BIT_EVS  (1U << 1) /*!< 事件服务 */
#define ESA_SIM_STEPPING_SERVICE_BIT_SB   (1U << 2) /*!< 软件总线 */
#define ESA_SIM_STEPPING_SERVICE_BIT_TBL  (1U << 3) /*!< 表服务 */
#define ESA_SIM_STEPPING_SERVICE_BIT_TIME (1U << 4) /*!< 时间服务 */

/**
 * @brief       TIME 子路径参与意图位掩码常量
 *
 * @details     TIME 子路径事实的位位置（与主任务命令管道接收不同）。
 *              用于跟踪当前步进中哪些 TIME 子路径已报告参与事实。
 *              这些允许明确 TIME 子系统结构（音调信号量、本地 1Hz 信号量）。
 */
#define ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_TONE      (1U << 5) /*!< TIME 音调信号子路径 */
#define ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_LOCAL_1HZ (1U << 6) /*!< TIME 本地 1Hz 子路径 */

/****************************************************************************************
                              CORE STATE MACHINE DEFINITIONS
 ***************************************************************************************/

/**
 * @brief       步进核心状态机状态
 *
 * @details     表示单个步进周期的生命周期：
 *              - INIT：核心尚未初始化
 *              - READY：等待步进命令或触发器
 *              - RUNNING：当前正在执行步进（推进时间）
 *              - WAITING：步进进行中，等待确认/完成
 *              - COMPLETE：所有触发器已确认，所有事件已报告，准备下一步
 */
typedef enum ESA_Stepping_CoreState
{
    ESA_SIM_STEPPING_STATE_INIT,    /*!< 核心未初始化 */
    ESA_SIM_STEPPING_STATE_READY,   /*!< 等待步进命令 */
    ESA_SIM_STEPPING_STATE_RUNNING, /*!< 正在执行步进 */
    ESA_SIM_STEPPING_STATE_WAITING, /*!< 等待确认/完成 */
    ESA_SIM_STEPPING_STATE_COMPLETE /*!< 步进完成，准备下一步 */
} ESA_Stepping_CoreState_t;

/**
 * @brief       动态触发器描述符
 *
 * @details     表示一个待处理的触发器事件（例如，任务延迟边界到达、队列接收、1Hz 信号等）。
 *              跟踪此触发器是否已被确认。
 */
typedef struct ESA_Stepping_Trigger
{
    uint32_t trigger_id;      /*!< 唯一触发器标识符 */
    uint32_t source_mask;     /*!< 源分类（任务延迟、队列等） */
    uint32_t entity_id;       /*!< 与触发器关联的实体标识符 */
    bool     is_acknowledged; /*!< 是否已被确认 */
} ESA_Stepping_Trigger_t;

/**
 * @brief       诊断失败类别枚举
 * @details     对步进控制面暴露的主要失败类型进行归类，用于统一记录诊断计数。
 */
typedef enum ESA_Stepping_DiagnosticClass
{
    ESA_SIM_STEPPING_DIAG_TIMEOUT = 0,      /*!< 步进等待或完成超时 */
    ESA_SIM_STEPPING_DIAG_DUPLICATE_BEGIN,  /*!< 重复发起开始步进 */
    ESA_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE, /*!< 出现无匹配触发器的完成上报 */
    ESA_SIM_STEPPING_DIAG_ILLEGAL_STATE,    /*!< 当前状态下执行了非法操作 */
    ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR,  /*!< 传输层读写或连接失败 */
    ESA_SIM_STEPPING_DIAG_PROTOCOL_ERROR    /*!< 协议帧或操作码非法 */
} ESA_Stepping_DiagnosticClass_t;

/**
 * @brief       核心拥有的诊断计数器，用于步进失败类别
 */
typedef struct ESA_Stepping_Diagnostics
{
    uint32_t timeout_count;          /*!< 超时类诊断次数 */
    uint32_t duplicate_begin_count;  /*!< 重复开始类诊断次数 */
    uint32_t illegal_complete_count; /*!< 非法完成类诊断次数 */
    uint32_t illegal_state_count;    /*!< 非法状态类诊断次数 */
    uint32_t transport_error_count;  /*!< 传输错误类诊断次数 */
    uint32_t protocol_error_count;   /*!< 协议错误类诊断次数 */
} ESA_Stepping_Diagnostics_t;

/**
 * @brief       步进核心状态结构
 *
 * @details     步进状态的唯一所有者。所有状态转换和触发器/确认跟踪在此发生。
 *              钩子报告事实；核心维护语义。
 *
 *              触发器存储为固定容量的编译时数组（非堆分配）。
 */
typedef struct ESA_Stepping_Core
{
    /* 状态机 */
    ESA_Stepping_CoreState_t current_state; /*!< 当前状态 */

    /* 仿真时间存储 */
    uint64_t sim_time_ns;      /*!< 当前仿真时间（纳秒） */
    uint64_t next_sim_time_ns; /*!< 下一个目标仿真时间 */

    /* 步进量子配置 */
    uint64_t step_quantum_ns; /*!< 一个仿真步进的量子大小（纳秒） */

    /* 触发器跟踪（固定编译时容量） */
    ESA_Stepping_Trigger_t triggers[ESA_SIM_STEPPING_MAX_TRIGGERS]; /*!< 待处理触发器数组 */
    uint32_t               trigger_count;                           /*!< 当前触发器数量 */
    uint32_t               next_trigger_id;                         /*!< 唯一触发器 ID 计数器 */

    /* 确认/完成跟踪 */
    uint32_t acks_received; /*!< 已确认触发器计数 */
    uint32_t acks_expected; /*!< 预期确认触发器计数 */

    bool completion_requested; /*!< 是否已请求完成？ */
    bool completion_ready;     /*!< 所有事件已报告且就绪？ */

    /* 核心拥有的诊断计数器 */
    ESA_Stepping_Diagnostics_t diagnostics; /*!< 累积失败类别诊断 */

    /* 超时配置 */
    uint32_t step_timeout_ms; /*!< 步进完成超时（毫秒） */

    /* 任务延迟接管控制 */
    bool taskdelay_takeover_enabled; /*!< 启用 TaskDelay 接管（默认：关闭） */

    /* 每任务任务延迟注册集合（固定编译时容量） */
    uint32_t taskdelay_optin_set[8]; /*!< 注册参与 TaskDelay 接管的任务 ID */
    uint32_t taskdelay_optin_count;  /*!< 注册集合中的任务数量 */
    uint64_t taskdelay_expiry_ns[8]; /*!< 已登记任务延迟的目标到期时间 */
    bool     taskdelay_pending[8];   /*!< 任务延迟是否处于待完成状态 */
    bool     taskdelay_owed[8];      /*!< 任务延迟是否仍欠缺显式步进推进 */

    /* 当前步进核心服务成员意图集合 */
    uint32_t core_service_membership_mask; /*!< 参与当前步进的核心服务位掩码（ES/EVS/SB/TBL/TIME） */

    /* 显式步进会话记账 */
    bool     session_active;  /*!< 步进会话当前是否活动 */
    uint64_t session_counter; /*!< 单调会话计数器，在步进会话开始时递增 */

    /* 持久生命周期就绪状态（与每步 completion_ready 不同） */
    bool system_ready_for_stepping; /*!< 持久标志：系统已声明生命周期就绪后为 true；在步进重置和会话转换后保留 */

} ESA_Stepping_Core_t;

/****************************************************************************************
                             CORE API FUNCTION DECLARATIONS
 ***************************************************************************************/

/**
 * @brief       初始化步进核心
 *
 * @details     设置状态机并准备运行。触发器存储为预分配的固定编译时数组。
 *
 * @param[in]   core                    要初始化的核心结构指针
 * @param[in]   initial_time_ns         初始仿真时间（纳秒）
 * @param[in]   trigger_capacity        未使用（保留用于 API 兼容性）；最大值为 ESA_SIM_STEPPING_MAX_TRIGGERS
 *
 * @retval      0                       初始化成功
 * @retval      非零                    初始化失败的错误码
 */
int32_t ESA_Stepping_Core_Init(ESA_Stepping_Core_t *core, uint64_t initial_time_ns, uint32_t trigger_capacity);

/**
 * @brief       重置步进核心以开始新的步进周期
 *
 * @details     清除触发器和确认计数器，转换为 READY 状态。
 *
 * @param[in]   core                    要重置的核心结构指针
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_Reset(ESA_Stepping_Core_t *core);

/**
 * @brief       报告任务延迟边界事件
 *
 * @details     当任务通过 OSAL TaskDelay 钩子请求延迟时调用。
 *              报告事实；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 请求延迟的任务标识符
 * @param[in]   delay_ms                请求的延迟时间（毫秒）
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportTaskDelay(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms);

/**
 * @brief       报告 TaskDelay 确认边界事件
 *
 * @details     在任务真正进入 TaskDelay 阻塞前调用，登记等待候选事实。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 任务运行时 ID
 * @param[in]   delay_ms                请求的延迟时间（毫秒）
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportTaskDelayAck(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms);

/**
 * @brief       报告 TaskDelay 完成边界事件
 *
 * @details     在任务从 TaskDelay 返回后调用，确认对应等待事实已完成。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 任务运行时 ID
 * @param[in]   delay_ms                请求的延迟时间（毫秒）
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportTaskDelayComplete(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms);

/**
 * @brief       报告任务已从 TaskDelay 返回
 *
 * @details     清理给定任务在 TaskDelay 接管簿记中的挂起、欠账和到期状态。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 任务运行时 ID
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportTaskDelayReturn(ESA_Stepping_Core_t *core, uint32_t task_id);

/**
 * @brief       报告队列接收边界事件
 *
 * @details     当任务通过 OSAL 钩子阻塞于队列接收时调用。
 *              报告事实；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   queue_id                接收队列的标识符
 * @param[in]   timeout_ms              接收操作超时（PEND_FOREVER = ~0U）
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportQueueReceive(ESA_Stepping_Core_t *core, uint32_t queue_id, uint32_t timeout_ms);

/**
 * @brief       报告二值信号量获取边界事件
 *
 * @details     当任务通过 OSAL 钩子阻塞于二值信号量时调用。
 *              报告事实；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   sem_id                  获取信号量的标识符
 * @param[in]   timeout_ms              获取操作超时
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportBinSemTake(ESA_Stepping_Core_t *core, uint32_t sem_id, uint32_t timeout_ms);

/**
 * @brief       报告时间任务周期边界事件
 *
 * @details     当 TIME 模块开始时间同步/更新周期时调用。
 *              报告事实；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportTimeTaskCycle(ESA_Stepping_Core_t *core);

/**
 * @brief       报告 1Hz 边界事件
 *
 * @details     当检测到或触发 1Hz 滴答时调用。
 *              报告事实；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_Report1HzBoundary(ESA_Stepping_Core_t *core);

/**
 * @brief       报告音调信号事件
 *
 * @details     当音调信号（例如来自 PSP 或调度器）触发时调用。
 *              报告事实；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportToneSignal(ESA_Stepping_Core_t *core);

/**
 * @brief       报告调度器信号量等待边界事件
 *
 * @details     当调度器（SCH）阻塞于信号量等待触发器（例如音调信号、1Hz 滴答或软件触发器）时调用。
 *              将事实作为独立的调度器事件报告；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   sem_id                  等待的信号量标识符
 * @param[in]   timeout_ms              信号量等待操作超时
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportSchSemaphoreWait(ESA_Stepping_Core_t *core, uint32_t sem_id, uint32_t timeout_ms);

/**
 * @brief       报告调度器小帧边界事件
 *
 * @details     当调度器到达小帧边界（步进粒度）时调用。
 *              小帧是基本调度单位并定义步进触发粒度。
 *              将事实作为独立的调度器事件报告；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportSchMinorFrame(ESA_Stepping_Core_t *core);

/**
 * @brief       报告调度器大帧边界事件
 *
 * @details     当调度器到达大帧边界（每个周期的帧 0）时调用。
 *              大帧标记完整调度周期的开始。
 *              将事实作为独立的调度器事件报告；核心决定是否应触发步进。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportSchMajorFrame(ESA_Stepping_Core_t *core);

/**
 * @brief       报告调度器发送触发器事件
 *
 * @details     当 SCH 向目标任务分发触发信号时调用，登记对应的调度器发送事实。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   target_id               被触发目标的标识符
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportSchSendTrigger(ESA_Stepping_Core_t *core, uint32_t target_id);

/**
 * @brief       报告调度器分发完成事件
 *
 * @details     当当前小帧内的调度分发结束时调用，用于确认此前的调度发送触发器。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportSchDispatchComplete(ESA_Stepping_Core_t *core);

/**
 * @brief       报告核心服务命令管道接收事件
 *
 * @details     当 ES、EVS、SB、TBL 或 TIME 进入命令管道接收路径时调用，用于登记服务参与事实。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   service_id              核心服务标识符
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportCoreServiceCmdPipeReceive(ESA_Stepping_Core_t *core, uint32_t service_id);

/**
 * @brief       报告核心服务命令管道处理完成事件
 *
 * @details     当指定核心服务完成命令管道处理后调用，用于确认先前登记的服务接收事实。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   service_id              核心服务标识符
 *
 * @retval      0                       成功
 * @retval      非零                    非法完成等诊断状态码
 */
int32_t ESA_Stepping_Core_ReportCoreServiceCmdPipeComplete(ESA_Stepping_Core_t *core, uint32_t service_id);

/**
 * @brief       报告 TIME 音调信号量消费事件
 *
 * @details     当 TIME 子路径消费 tone 信号量时调用，登记该子路径已参与当前步进。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   sem_id                  音调信号量标识符
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportTimeToneSemConsume(ESA_Stepping_Core_t *core, uint32_t sem_id);

/**
 * @brief       报告 TIME 本地 1Hz 信号量消费事件
 *
 * @details     当 TIME 子路径消费 local-1Hz 信号量时调用，登记该子路径已参与当前步进。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   sem_id                  本地 1Hz 信号量标识符
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportTimeLocal1HzSemConsume(ESA_Stepping_Core_t *core, uint32_t sem_id);

/**
 * @brief       查询当前仿真时间
 *
 * @details     返回存储在核心中的仿真时间。由时间基准模块调用以获取确定性时间。
 *
 * @param[in]   core                    核心结构指针
 * @param[out]  sim_time_ns             存储当前仿真时间（纳秒）的指针
 *
 * @retval      0                       时间成功获取
 * @retval      非零                    查询失败的错误码
 */
int32_t ESA_Stepping_Core_QuerySimTime(ESA_Stepping_Core_t *core, uint64_t *sim_time_ns);

/**
 * @brief       查询请求的 TaskDelay 是否可由步进处理
 *
 * @details     检查 PSP 步进核心是否可以处理（接管）特定任务延迟请求。
 *              使用保守的资格判定逻辑：
 *              - 核心必须已初始化
 *              - TaskDelay 接管门控必须为 ON
 *              - 任务必须明确注册参与 TaskDelay 接管
 *              - 请求的延迟必须是 step_quantum_ns 的整数倍
 *
 *              由 OSAL TaskDelay 钩子使用，用于判断是否返回已处理（跳过墙钟睡眠）
 *              或未处理（继续正常睡眠）。当门控为 OFF（默认）或任务未注册参与时，
 *              始终返回 false（未处理）。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 请求延迟的任务运行时 ID
 * @param[in]   delay_ms                请求的延迟时间（毫秒）
 *
 * @retval      true                    延迟可处理（核心已初始化、门控 ON、任务已注册、延迟为量子整数倍）
 * @retval      false                   延迟无法处理或核心未初始化
 */
bool ESA_Stepping_Core_QueryTaskDelayEligible(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms);

/**
 * @brief       阻塞当前任务直到仿真延迟预算由显式步进量子满足
 *
 * @details     PSP 拥有的步进控制 TaskDelay 阻塞等待机制。计算目标到期时间（仿真纳秒），
 *              然后轮询 sim_time_ns 直到足够的显式步进量子已推进以满足延迟。
 *              使用简短的墙钟 nanosleep 间隔避免自旋等待，但释放仅由 sim_time_ns 推进
 *              （通过 AdvanceOneQuantum）控制。这防止延迟驱动任务在无步进发出时自行推进。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 请求延迟的任务运行时 ID
 * @param[in]   delay_ms                请求的延迟时间（毫秒）
 *
 * @retval      0                       成功（延迟已满足）
 * @retval      负数                    错误
 */
int32_t ESA_Stepping_Core_WaitForDelayExpiry(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t delay_ms);

/**
 * @brief       报告队列接收确认边界事件（阻塞前）
 *
 * @details     在任务阻塞于队列接收前立即调用（mq_receive/mq_timedreceive 前）。
 *              报告确认候选事实：任务即将等待队列。
 *              与完成事实不同：这表示等待意图，而非操作成功。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 任务运行时 ID（当前执行任务）
 * @param[in]   queue_id                接收队列的标识符
 * @param[in]   timeout_ms              接收操作超时（PEND_FOREVER = ~0U）
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportQueueReceiveAck(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t queue_id,
                                                uint32_t timeout_ms);

/**
 * @brief       报告队列接收完成边界事件（阻塞后）
 *
 * @details     在 mq_receive/mq_timedreceive 返回后立即调用（无论成功或失败）。
 *              报告完成候选事实：任务已从队列等待返回。
 *              与确认事实不同：这表示操作完成，而非意图。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 任务运行时 ID（当前执行任务）
 * @param[in]   queue_id                接收队列的标识符
 * @param[in]   timeout_ms              接收操作超时（PEND_FOREVER = ~0U）
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportQueueReceiveComplete(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t queue_id,
                                                     uint32_t timeout_ms);

/**
 * @brief       将仿真时间推进一个量子（私有内部 API）
 *
 * @details     PSP 步进核心的仅内部函数，用于将仿真时间精确推进一个配置的量子。
 *              将当前和下一个仿真时间都增加步进量子值。
 *
 *              这是 sim_time_ns 和 next_sim_time_ns 的唯一写路径；
 *              所有其他读端查询使用 ESA_Stepping_Core_QuerySimTime()。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      0                       时间推进成功
 * @retval      非零                    推进失败的错误码（例如核心为空、量子为零或未配置）
 *
 * @note        此函数是 PSP 步进核心的私有函数，不对外部调用者暴露。
 *              旨在用于未来的控制通道集成。
 */
int32_t ESA_Stepping_Core_AdvanceOneQuantum(ESA_Stepping_Core_t *core);

/**
 * @brief       报告二值信号量获取确认边界事件（等待前）
 *
 * @details     在任务阻塞于二值信号量等待前立即调用（pthread_cond_wait 前）。
 *              报告确认候选事实：任务即将等待信号量。
 *              与完成事实不同：这表示等待意图，而非操作成功。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 任务运行时 ID（当前执行任务）
 * @param[in]   sem_id                  获取信号量的标识符
 * @param[in]   timeout_ms              获取操作超时（0 = PEND_FOREVER）
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportBinSemTakeAck(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t sem_id,
                                              uint32_t timeout_ms);

/**
 * @brief       报告二值信号量获取完成边界事件（等待后）
 *
 * @details     在 pthread_cond_wait/pthread_cond_timedwait 返回后立即调用（无论成功或失败）。
 *              报告完成候选事实：任务已从信号量等待返回。
 *              与确认事实不同：这表示操作完成，而非意图。
 *
 * @param[in]   core                    核心结构指针
 * @param[in]   task_id                 任务运行时 ID（当前执行任务）
 * @param[in]   sem_id                  获取信号量的标识符
 * @param[in]   timeout_ms              获取操作超时（0 = PEND_FOREVER）
 *
 * @retval      0                       成功
 */
int32_t ESA_Stepping_Core_ReportBinSemTakeComplete(ESA_Stepping_Core_t *core, uint32_t task_id, uint32_t sem_id,
                                                   uint32_t timeout_ms);

/**
 * @brief       查询步进核心的当前状态（进程内适配器的内部 API）
 *
 * @details     返回当前状态机状态，可选返回触发器计数。
 *              由进程内控制适配器用于确定步进就绪状态。
 *              这是内部 PSP API，不在模块外暴露。
 *
 * @param[in]   core                    核心结构指针
 * @param[out]  state_out               存储当前状态枚举的指针（可为 NULL）
 *
 * @retval      0                       成功
 * @retval      -1                      核心为空或状态查询失败
 *
 * @note        这是 PSP 步进模块进程内适配器的内部 API。
 *              外部调用者应使用公共适配器 API（ESA_Stepping_InProc_*）。
 */
int32_t ESA_Stepping_Core_QueryState(ESA_Stepping_Core_t *core, ESA_Stepping_CoreState_t *state_out);

/**
 * @brief       检查当前步进周期是否完成（进程内适配器的内部 API）
 *
 * @details     当步进核心转换为 COMPLETE 状态时返回 true，表示所有预期触发器已报告
 *              且所有确认已接收。
 *              由进程内控制适配器用于轮询步进完成。
 *              这是内部 PSP API，不在模块外暴露。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      true                    步进已完成
 * @retval      false                   仍在运行、等待中或核心为空
 *
 * @note        这是 PSP 步进模块进程内适配器的内部 API。
 *              外部调用者应使用公共适配器 API（ESA_Stepping_InProc_*）。
 */
bool ESA_Stepping_Core_IsStepComplete(ESA_Stepping_Core_t *core);

/**
 * @brief       开始带有显式记账的新步进会话
 *
 * @details     通过清除触发器、重置确认计数器、标记会话为活动状态并递增会话计数器
 *              来启动新步进会话。这是开始清洁步进周期的主要入口点。
 *              用显式会话语义替换之前的盲目 Core_Reset() 模式。
 *
 * @param[in]   core                                    核心结构指针
 *
 * @retval      ESA_SIM_STEPPING_STATUS_SUCCESS         成功
 * @retval      ESA_SIM_STEPPING_STATUS_FAILURE         核心为空
 * @retval      ESA_SIM_STEPPING_STATUS_NOT_READY       生命周期就绪未达成
 * @retval      ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN 前一个会话未解决
 */
int32_t ESA_Stepping_Core_BeginStepSession(ESA_Stepping_Core_t *core);

/**
 * @brief       标记系统已准备好进行仿真步进
 *
 * @details     设置持久生命周期就绪标志，表示系统已完成初始化并准备进入步进模式。
 *              此标志在步进会话重置和步进周期完成后保留。
 *
 *              与每步完成语义不同：这表示持久的系统级就绪状态，
 *              而非瞬态的每步完成状态。
 *
 * @param[in]   core                    核心结构指针
 *
 * @retval      0                       成功
 * @retval      -1                      核心为空
 */
int32_t ESA_Stepping_Core_MarkSystemReadyForStepping(ESA_Stepping_Core_t *core);

/**
 * @brief       记录一个带有规范化类别/状态日志的诊断事件
 *
 * @details     递增核心拥有的诊断计数器桶并发出规范化、可搜索的日志行。
 *
 * @param[in]   core                    步进核心指针
 * @param[in]   diag_class              失败类别桶
 * @param[in]   status                  此事件的共享状态分类码
 * @param[in]   site                    静态站点标签（例如 "BeginStepSession"）
 * @param[in]   detail_a                可选数值详情 A
 * @param[in]   detail_b                可选数值详情 B
 *
 * @retval      传入的状态值，或 ESA_SIM_STEPPING_STATUS_FAILURE（如果核心/站点无效）
 */
int32_t ESA_Stepping_Core_RecordDiagnostic(ESA_Stepping_Core_t *core, ESA_Stepping_DiagnosticClass_t diag_class,
                                           int32_t status, const char *site, uint32_t detail_a, uint32_t detail_b);

#endif /* ESA_SIM_STEPPING_CORE_H */

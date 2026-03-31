/**
 * @file
 * @ingroup esa
 * @brief       核心服务步进事件单元测试
 * @author      gaoyuan
 * @date        2026-03-23
 *
 * @details     本文件测试 ESA Shim 对 cFE 核心服务的事件报告功能。
 */
#include <string.h>

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include "common_types.h"
#include "cfe_error.h"
#include "cfe_es.h"
#include "cfe_sb.h"
#include "esa_stepping_shim.h"

#include "cfe_evs.h"
#include "cfe_msg.h"
#include "cfe_psp.h"
#include "cfe_es_resetdata_typedef.h"

void CFE_ES_TaskMain(void);
void CFE_EVS_TaskMain(void);
void CFE_SB_TaskMain(void);
void CFE_TBL_TaskMain(void);

/**
 * @brief       核心服务事件捕获上下文
 * @details     保存测试过程中拦截到的步进 Shim 事件以及处理路径观测结果。
 */
typedef struct
{
    ESA_Stepping_ShimEvent_t Events[2];         /*!< 捕获到的事件序列 */
    uint32                   MatchCount;        /*!< 已匹配到的事件数量 */
    bool                     ProcessingInvoked; /*!< 核心服务处理逻辑是否被触发 */
} ESA_CoreServiceCapture_t;

/** 当前激活的核心服务事件捕获上下文 */
static ESA_CoreServiceCapture_t *ESA_ActiveCapture;
/**
 * @brief 核心服务命令管道接收调用计数，用于控制退出路径。
 */
static uint32 ESA_ReceiveCallCount;
/**
 * @brief ES重置区静态缓冲，供TaskInit真实路径访问。
 */
static CFE_ES_ResetData_t ESA_ResetData;
/**
 * @brief CFE_ES_Global中ResetDataPtr字段偏移（由反汇编确认）。
 */
#define ESA_ES_GLOBAL_RESETDATAPTR_OFFSET 57392U

extern uint8 CFE_ES_Global[];

/**
 * @brief       捕获核心服务步进事件
 * @details     作为 ESA_Stepping_Shim_ReportEvent 的桩钩子，记录最多两个事件供断言使用。
 * @param[in,out] UserObj     指向 ESA_CoreServiceCapture_t 的用户上下文
 * @param[in]     StubRetcode 桩函数返回码
 * @param[in]     CallCount   调用计数（未使用）
 * @param[in]     Context     桩函数调用上下文
 * @retval        StubRetcode 原样返回桩函数返回码
 */
static int32 ESA_CaptureHook(void *UserObj, int32 StubRetcode, uint32 CallCount, const UT_StubContext_t *Context)
{
    const ESA_Stepping_ShimEvent_t *event   = UT_Hook_GetArgValueByName(Context, "event", const void *);
    ESA_CoreServiceCapture_t       *capture = UserObj;

    if (event != NULL && capture->MatchCount < 2)
    {
        capture->Events[capture->MatchCount] = *event;
        ++capture->MatchCount;
    }

    (void)CallCount;
    return StubRetcode;
}

/**
 * @brief       断言核心服务事件序列
 * @details     验证核心服务任务是否报告了命令管道接收与完成两个事件，并检查服务标识符。
 * @param[in]   capture             已捕获的事件上下文
 * @param[in]   expected_service_id 期望的核心服务标识符
 */
static void ESA_AssertCoreEvents(const ESA_CoreServiceCapture_t *capture, uint32 expected_service_id)
{
    UtAssert_UINT32_EQ(capture->MatchCount, 2);
    UtAssert_INT32_EQ(capture->Events[0].event_kind, ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_RECEIVE);
    UtAssert_INT32_EQ(capture->Events[1].event_kind, ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_COMPLETE);
    UtAssert_UINT32_EQ(capture->Events[0].entity_id, expected_service_id);
    UtAssert_UINT32_EQ(capture->Events[1].entity_id, expected_service_id);
    UtAssert_BOOL_TRUE(capture->ProcessingInvoked);
}

/**
 * @brief       包装 ES 任务初始化接口
 * @details     为核心服务主循环测试提供最小成功路径。
 * @retval      CFE_SUCCESS 始终返回成功
 */
int32 __wrap_CFE_ES_TaskInit(void)
{
    return CFE_SUCCESS;
}

/**
 * @brief       包装 EVS 任务初始化接口
 * @details     为核心服务主循环测试提供最小成功路径。
 * @retval      CFE_SUCCESS 始终返回成功
 */
int32 __wrap_CFE_EVS_TaskInit(void)
{
    return CFE_SUCCESS;
}

/**
 * @brief       包装 SB 应用初始化接口
 * @details     为核心服务主循环测试提供最小成功路径。
 * @retval      CFE_SUCCESS 始终返回成功
 */
int32 __wrap_CFE_SB_AppInit(void)
{
    return CFE_SUCCESS;
}

/**
 * @brief       包装 TBL 任务初始化接口
 * @details     为核心服务主循环测试提供最小成功路径。
 * @retval      CFE_SUCCESS 始终返回成功
 */
int32 __wrap_CFE_TBL_TaskInit(void)
{
    return CFE_SUCCESS;
}

/**
 * @brief 包装重置类型查询，避免解引用未初始化ResetDataPtr。
 * @param[out] ResetSubtypePtr 重置子类型输出指针。
 * @retval      CFE_PSP_RST_TYPE_POWERON 始终返回上电重置类型
 */
uint32 __wrap_CFE_ES_GetResetType(uint32 *ResetSubtypePtr)
{
    if (ResetSubtypePtr != NULL)
    {
        *ResetSubtypePtr = 0;
    }

    return CFE_PSP_RST_TYPE_POWERON;
}

/**
 * @brief 包装EVS注册接口，隔离真实EVS内部状态依赖。
 * @param[in] Filters 过滤器表。
 * @param[in] NumEventFilters 过滤器数量。
 * @param[in] FilterScheme 过滤方案。
 * @retval      CFE_SUCCESS 始终返回成功
 */
CFE_Status_t __wrap_CFE_EVS_Register(const void *Filters, uint16 NumEventFilters, uint16 FilterScheme)
{
    (void)Filters;
    (void)NumEventFilters;
    (void)FilterScheme;
    return CFE_SUCCESS;
}

/**
 * @brief 包装消息初始化接口，避免触发真实消息实现依赖。
 * @param[in,out] MsgPtr 消息对象指针。
 * @param[in] MsgId 消息ID。
 * @param[in] Size 消息大小。
 * @retval      CFE_SUCCESS 始终返回成功
 */
CFE_Status_t __wrap_CFE_MSG_Init(CFE_MSG_Message_t *MsgPtr, CFE_SB_MsgId_t MsgId, CFE_MSG_Size_t Size)
{
    (void)MsgPtr;
    (void)MsgId;
    (void)Size;
    return CFE_SUCCESS;
}

/**
 * @brief 包装创建管道接口，仅提供最小成功语义。
 * @param[out] PipeIdPtr 管道ID输出。
 * @param[in] Depth 管道深度。
 * @param[in] PipeName 管道名。
 * @retval      CFE_SUCCESS 始终返回成功
 */
CFE_Status_t __wrap_CFE_SB_CreatePipe(CFE_SB_PipeId_t *PipeIdPtr, uint16 Depth, const char *PipeName)
{
    (void)Depth;
    (void)PipeName;

    if (PipeIdPtr != NULL)
    {
        *PipeIdPtr = CFE_SB_PIPEID_C(1);
    }

    return CFE_SUCCESS;
}

/**
 * @brief 包装订阅接口，避免访问真实SB订阅表。
 * @param[in] MsgId 消息ID。
 * @param[in] PipeId 管道ID。
 * @retval      CFE_SUCCESS 始终返回成功
 */
CFE_Status_t __wrap_CFE_SB_Subscribe(CFE_SB_MsgId_t MsgId, CFE_SB_PipeId_t PipeId)
{
    (void)MsgId;
    (void)PipeId;
    return CFE_SUCCESS;
}

/**
 * @brief 包装获取AppID接口，避免依赖真实应用注册状态。
 * @param[out] AppIdPtr 应用ID输出。
 * @retval      CFE_SUCCESS 始终返回成功
 */
CFE_Status_t __wrap_CFE_ES_GetAppID(CFE_ES_AppId_t *AppIdPtr)
{
    if (AppIdPtr != NULL)
    {
        *AppIdPtr = CFE_ES_APPID_C(1);
    }

    return CFE_SUCCESS;
}

/**
 * @brief 包装发送事件接口，避免依赖真实EVS路径。
 * @retval      CFE_SUCCESS 始终返回成功
 */
CFE_Status_t __wrap_CFE_EVS_SendEvent(uint16 EventID, CFE_EVS_EventType_Enum_t EventType, const char *Spec, ...)
{
    (void)EventID;
    (void)EventType;
    (void)Spec;
    return CFE_SUCCESS;
}

/**
 * @brief 包装内存池取块接口，仅返回成功。
 */
CFE_Status_t __wrap_CFE_ES_GetPoolBuf(CFE_ES_MemPoolBuf_t *BufPtr, CFE_ES_MemHandle_t Handle, size_t Size)
{
    static uint8 DummyMem;

    (void)Handle;
    (void)Size;

    if (BufPtr != NULL)
    {
        *BufPtr = &DummyMem;
    }

    return CFE_SUCCESS;
}

/**
 * @brief 包装内存池还块接口，仅返回成功。
 */
CFE_Status_t __wrap_CFE_ES_PutPoolBuf(CFE_ES_MemHandle_t Handle, CFE_ES_MemPoolBuf_t BufPtr)
{
    (void)Handle;
    (void)BufPtr;
    return CFE_SUCCESS;
}

/**
 * @brief 包装后台任务初始化接口，仅返回成功。
 */
CFE_Status_t __wrap_CFE_ES_BackgroundInit(void)
{
    return CFE_SUCCESS;
}

/**
 * @brief       包装系统状态等待接口
 * @details     在测试中直接返回成功，避免依赖真实系统状态机。
 * @param[in]   MinSystemState      目标最小系统状态
 * @param[in]   TimeOutMilliseconds 超时时间（毫秒）
 * @retval      CFE_SUCCESS 始终返回成功
 */
CFE_Status_t __wrap_CFE_ES_WaitForSystemState(uint32 MinSystemState, uint32 TimeOutMilliseconds)
{
    (void)MinSystemState;
    (void)TimeOutMilliseconds;
    return CFE_SUCCESS;
}

/**
 * @brief       包装应用退出接口
 * @details     在测试中吞掉退出请求，避免真实终止当前执行路径。
 * @param[in]   ExitStatus 退出状态码
 */
void __wrap_CFE_ES_ExitApp(uint32 ExitStatus)
{
    (void)ExitStatus;
}

/**
 * @brief 包装性能日志写入，避免访问未初始化ES性能区。
 * @param[in] Marker 性能标记ID。
 * @param[in] EntryExit 入口/出口标识。
 */
void __wrap_CFE_ES_PerfLogAdd(uint32 Marker, uint32 EntryExit)
{
    (void)Marker;
    (void)EntryExit;
}

/**
 * @brief 包装任务计数器递增，隔离未初始化全局状态。
 */
void __wrap_CFE_ES_IncrementTaskCounter(void) {}

/**
 * @brief 包装ES后台唤醒，避免触发后台任务依赖。
 */
void __wrap_CFE_ES_BackgroundWakeup(void) {}

/**
 * @brief 包装SB接收接口，按顺序返回成功后读错误以退出主循环。
 * @param[out] BufPtr 消息缓冲区指针输出。
 * @param[in] PipeId 管道ID（此测试中不使用）。
 * @param[in] TimeOut 超时参数（此测试中不使用）。
 * @retval      CFE_SUCCESS        第一次调用返回成功
 * @retval      CFE_SB_PIPE_RD_ERR 后续调用返回读错误以结束主循环
 */
CFE_Status_t __wrap_CFE_SB_ReceiveBuffer(CFE_SB_Buffer_t **BufPtr, CFE_SB_PipeId_t PipeId, int32 TimeOut)
{
    static CFE_SB_Buffer_t DummyBuf;

    (void)PipeId;
    (void)TimeOut;
    memset(&DummyBuf, 0, sizeof(DummyBuf));

    if (BufPtr != NULL)
    {
        *BufPtr = &DummyBuf;
    }

    ++ESA_ReceiveCallCount;
    if (ESA_ReceiveCallCount == 1)
    {
        return CFE_SUCCESS;
    }

    return CFE_SB_PIPE_RD_ERR;
}

/**
 * @brief       包装 ES 命令管道处理函数
 * @details     标记处理路径已被调用，用于验证 ES 主循环的处理阶段。
 * @param[in]   SBBufPtr 软件总线缓冲区指针
 */
void __wrap_CFE_ES_TaskPipe(const CFE_SB_Buffer_t *SBBufPtr)
{
    if (ESA_ActiveCapture != NULL)
    {
        ESA_ActiveCapture->ProcessingInvoked = true;
    }
    (void)SBBufPtr;
}

/**
 * @brief       包装 EVS 命令处理函数
 * @details     标记处理路径已被调用，用于验证 EVS 主循环的处理阶段。
 * @param[in]   SBBufPtr 软件总线缓冲区指针
 */
void __wrap_CFE_EVS_ProcessCommandPacket(const CFE_SB_Buffer_t *SBBufPtr)
{
    if (ESA_ActiveCapture != NULL)
    {
        ESA_ActiveCapture->ProcessingInvoked = true;
    }
    (void)SBBufPtr;
}

/**
 * @brief       包装 SB 命令处理函数
 * @details     标记处理路径已被调用，用于验证 SB 主循环的处理阶段。
 * @param[in]   SBBufPtr 软件总线缓冲区指针
 */
void __wrap_CFE_SB_ProcessCmdPipePkt(const CFE_SB_Buffer_t *SBBufPtr)
{
    if (ESA_ActiveCapture != NULL)
    {
        ESA_ActiveCapture->ProcessingInvoked = true;
    }
    (void)SBBufPtr;
}

/**
 * @brief       包装 TBL 命令管道处理函数
 * @details     标记处理路径已被调用，用于验证 TBL 主循环的处理阶段。
 * @param[in]   SBBufPtr 软件总线缓冲区指针
 */
void __wrap_CFE_TBL_TaskPipe(const CFE_SB_Buffer_t *SBBufPtr)
{
    if (ESA_ActiveCapture != NULL)
    {
        ESA_ActiveCapture->ProcessingInvoked = true;
    }
    (void)SBBufPtr;
}

/**
 * @brief       验证 ES 核心服务事件报告
 * @details     执行 ES 主循环并断言命令管道接收与完成事件被正确上报。
 */
void Test_core_services_ES(void)
{
    ESA_CoreServiceCapture_t capture = {{{0}}, 0, false};
    ESA_ReceiveCallCount             = 0;
    ESA_ActiveCapture                = &capture;
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), ESA_CaptureHook, &capture);
    CFE_ES_TaskMain();
    ESA_ActiveCapture = NULL;
    ESA_AssertCoreEvents(&capture, 0);
}

/**
 * @brief       验证 EVS 核心服务事件报告
 * @details     执行 EVS 主循环并断言命令管道接收与完成事件被正确上报。
 */
void Test_core_services_EVS(void)
{
    ESA_CoreServiceCapture_t capture = {{{0}}, 0, false};
    ESA_ReceiveCallCount             = 0;
    ESA_ActiveCapture                = &capture;
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), ESA_CaptureHook, &capture);
    CFE_EVS_TaskMain();
    ESA_ActiveCapture = NULL;
    ESA_AssertCoreEvents(&capture, 1);
}

/**
 * @brief       验证 SB 核心服务事件报告
 * @details     执行 SB 主循环并断言命令管道接收与完成事件被正确上报。
 */
void Test_core_services_SB(void)
{
    ESA_CoreServiceCapture_t capture = {{{0}}, 0, false};
    ESA_ReceiveCallCount             = 0;
    ESA_ActiveCapture                = &capture;
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), ESA_CaptureHook, &capture);
    CFE_SB_TaskMain();
    ESA_ActiveCapture = NULL;
    ESA_AssertCoreEvents(&capture, 2);
}

/**
 * @brief       验证 TBL 核心服务事件报告
 * @details     执行 TBL 主循环并断言命令管道接收与完成事件被正确上报。
 */
void Test_core_services_TBL(void)
{
    ESA_CoreServiceCapture_t capture = {{{0}}, 0, false};
    ESA_ReceiveCallCount             = 0;
    ESA_ActiveCapture                = &capture;
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), ESA_CaptureHook, &capture);
    CFE_TBL_TaskMain();
    ESA_ActiveCapture = NULL;
    ESA_AssertCoreEvents(&capture, 3);
}

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

/**
 * @brief       重置核心服务测试环境
 * @details     复位 UT 状态、捕获上下文和 ES 重置区缓冲，使每个测试从一致初态开始。
 */
void ResetTest(void)
{
    CFE_ES_ResetData_t **ResetDataPtrRef;

    UT_ResetState(0);
    ESA_ActiveCapture    = NULL;
    ESA_ReceiveCallCount = 0;
    memset(&ESA_ResetData, 0, sizeof(ESA_ResetData));

    ResetDataPtrRef  = (CFE_ES_ResetData_t **)(void *)(CFE_ES_Global + ESA_ES_GLOBAL_RESETDATAPTR_OFFSET);
    *ResetDataPtrRef = &ESA_ResetData;
}

/**
 * @brief       注册核心服务测试用例
 * @details     将 ES、EVS、SB 与 TBL 的核心服务事件测试加入 ut_assert 框架。
 */
void UtTest_Setup(void)
{
    ADD_TEST(Test_core_services_ES);
    ADD_TEST(Test_core_services_EVS);
    ADD_TEST(Test_core_services_SB);
    ADD_TEST(Test_core_services_TBL);
}

/************************************************************************
 * NASA Docket No. GSC-19,200-1, and identified as "cFS Draco"
 *
 * Copyright (c) 2023 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

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

typedef struct
{
    ESA_Stepping_ShimEvent_t Events[3];
    uint32                   MatchCount;
    bool                     ProcessingInvoked;
} ESA_CoreServiceCapture_t;

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

static int32 ESA_CaptureHook(void *UserObj, int32 StubRetcode, uint32 CallCount, const UT_StubContext_t *Context)
{
    const ESA_Stepping_ShimEvent_t *event   = UT_Hook_GetArgValueByName(Context, "event", const void *);
    ESA_CoreServiceCapture_t *       capture = UserObj;

    if (event != NULL && capture->MatchCount < 2)
    {
        capture->Events[capture->MatchCount] = *event;
        ++capture->MatchCount;
    }

    (void)CallCount;
    return StubRetcode;
}

/**
 * @brief ES 专用捕获 hook：支持捕获 3 个事件
 * 
 * 与通用 hook 的区别：容量上限为 3（而非 2），用于 ES 测试捕获
 * SYSTEM_READY + RECEIVE + COMPLETE 三个事件。
 */
static int32 ESA_CaptureHook_ES(void *UserObj, int32 StubRetcode, uint32 CallCount, const UT_StubContext_t *Context)
{
    const ESA_Stepping_ShimEvent_t *event   = UT_Hook_GetArgValueByName(Context, "event", const void *);
    ESA_CoreServiceCapture_t *       capture = UserObj;

    if (event != NULL && capture->MatchCount < 3)
    {
        capture->Events[capture->MatchCount] = *event;
        ++capture->MatchCount;
    }

    (void)CallCount;
    return StubRetcode;
}

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
 * @brief ES 专用事件断言：验证 SYSTEM_READY + RECEIVE + COMPLETE 三个事件
 * 
 * ES 在系统生命周期达到 CORE_READY 后，先上报 SYSTEM_READY_FOR_STEPPING 事件，
 * 然后才开始命令循环的 RECEIVE + COMPLETE，因此事件序列为 3 个。
 * 其他核心服务无此额外事件，仍为 2 个。
 */
static void ESA_AssertES_Events(const ESA_CoreServiceCapture_t *capture, uint32 expected_service_id)
{
    UtAssert_UINT32_EQ(capture->MatchCount, 3);
    UtAssert_INT32_EQ(capture->Events[0].event_kind, ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING);
    UtAssert_INT32_EQ(capture->Events[1].event_kind, ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_RECEIVE);
    UtAssert_INT32_EQ(capture->Events[2].event_kind, ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_COMPLETE);
    UtAssert_UINT32_EQ(capture->Events[1].entity_id, expected_service_id);
    UtAssert_UINT32_EQ(capture->Events[2].entity_id, expected_service_id);
    UtAssert_BOOL_TRUE(capture->ProcessingInvoked);
}

int32 __wrap_CFE_ES_TaskInit(void)
{
    return CFE_SUCCESS;
}

int32 __wrap_CFE_EVS_TaskInit(void)
{
    return CFE_SUCCESS;
}

int32 __wrap_CFE_SB_AppInit(void)
{
    return CFE_SUCCESS;
}

int32 __wrap_CFE_TBL_TaskInit(void)
{
    return CFE_SUCCESS;
}

/**
 * @brief 包装重置类型查询，避免解引用未初始化ResetDataPtr。
 * @param[out] ResetSubtypePtr 重置子类型输出指针。
 * @return uint32 始终返回上电重置类型。
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
 * @return CFE_Status_t 始终返回成功。
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
 * @return CFE_Status_t 始终返回成功。
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
 * @return CFE_Status_t 始终返回成功。
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
 * @return CFE_Status_t 始终返回成功。
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
 * @return CFE_Status_t 始终返回成功。
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
 * @return CFE_Status_t 始终返回成功。
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

CFE_Status_t __wrap_CFE_ES_WaitForSystemState(uint32 MinSystemState, uint32 TimeOutMilliseconds)
{
    (void)MinSystemState;
    (void)TimeOutMilliseconds;
    return CFE_SUCCESS;
}

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
void __wrap_CFE_ES_IncrementTaskCounter(void)
{
}

/**
 * @brief 包装ES后台唤醒，避免触发后台任务依赖。
 */
void __wrap_CFE_ES_BackgroundWakeup(void)
{
}

/**
 * @brief 包装SB接收接口，按顺序返回成功后读错误以退出主循环。
 * @param[out] BufPtr 消息缓冲区指针输出。
 * @param[in] PipeId 管道ID（此测试中不使用）。
 * @param[in] TimeOut 超时参数（此测试中不使用）。
 * @return CFE_Status_t 首次返回CFE_SUCCESS，随后返回CFE_SB_PIPE_RD_ERR。
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

void __wrap_CFE_ES_TaskPipe(const CFE_SB_Buffer_t *SBBufPtr)
{
    if (ESA_ActiveCapture != NULL)
    {
        ESA_ActiveCapture->ProcessingInvoked = true;
    }
    (void)SBBufPtr;
}

void __wrap_CFE_EVS_ProcessCommandPacket(const CFE_SB_Buffer_t *SBBufPtr)
{
    if (ESA_ActiveCapture != NULL)
    {
        ESA_ActiveCapture->ProcessingInvoked = true;
    }
    (void)SBBufPtr;
}

void __wrap_CFE_SB_ProcessCmdPipePkt(const CFE_SB_Buffer_t *SBBufPtr)
{
    if (ESA_ActiveCapture != NULL)
    {
        ESA_ActiveCapture->ProcessingInvoked = true;
    }
    (void)SBBufPtr;
}

void __wrap_CFE_TBL_TaskPipe(const CFE_SB_Buffer_t *SBBufPtr)
{
    if (ESA_ActiveCapture != NULL)
    {
        ESA_ActiveCapture->ProcessingInvoked = true;
    }
    (void)SBBufPtr;
}

void Test_core_services_ES(void)
{
    ESA_CoreServiceCapture_t capture = {{{0}}, 0, false};
    ESA_ReceiveCallCount = 0;
    ESA_ActiveCapture = &capture;
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), ESA_CaptureHook_ES, &capture);
    CFE_ES_TaskMain();
    ESA_ActiveCapture = NULL;
    ESA_AssertES_Events(&capture, 0);
}

void Test_core_services_EVS(void)
{
    ESA_CoreServiceCapture_t capture = {{{0}}, 0, false};
    ESA_ReceiveCallCount = 0;
    ESA_ActiveCapture = &capture;
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), ESA_CaptureHook, &capture);
    CFE_EVS_TaskMain();
    ESA_ActiveCapture = NULL;
    ESA_AssertCoreEvents(&capture, 1);
}

void Test_core_services_SB(void)
{
    ESA_CoreServiceCapture_t capture = {{{0}}, 0, false};
    ESA_ReceiveCallCount = 0;
    ESA_ActiveCapture = &capture;
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), ESA_CaptureHook, &capture);
    CFE_SB_TaskMain();
    ESA_ActiveCapture = NULL;
    ESA_AssertCoreEvents(&capture, 2);
}

void Test_core_services_TBL(void)
{
    ESA_CoreServiceCapture_t capture = {{{0}}, 0, false};
    ESA_ReceiveCallCount = 0;
    ESA_ActiveCapture = &capture;
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), ESA_CaptureHook, &capture);
    CFE_TBL_TaskMain();
    ESA_ActiveCapture = NULL;
    ESA_AssertCoreEvents(&capture, 3);
}

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

void ResetTest(void)
{
    CFE_ES_ResetData_t **ResetDataPtrRef;

    UT_ResetState(0);
    ESA_ActiveCapture = NULL;
    ESA_ReceiveCallCount = 0;
    memset(&ESA_ResetData, 0, sizeof(ESA_ResetData));

    ResetDataPtrRef  = (CFE_ES_ResetData_t **)(void *)(CFE_ES_Global + ESA_ES_GLOBAL_RESETDATAPTR_OFFSET);
    *ResetDataPtrRef = &ESA_ResetData;
}

void UtTest_Setup(void)
{
    ADD_TEST(Test_core_services_ES);
    ADD_TEST(Test_core_services_EVS);
    ADD_TEST(Test_core_services_SB);
    ADD_TEST(Test_core_services_TBL);
}

/**
 * @file
 * @ingroup esa
 * @brief       OSAL 步进钩子单元测试
 * @author      gaoyuan
 * @date        2026-03-20
 *
 * @details     本测试套件验证所有 OSAL 步进钩子是否正确报告事件。
 */
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include "common_types.h"
#include "esa_stepping_shim.h"
#include "os-posix-stepping.h"
#include "os-shared-idmap.h"
#include "osapi-idmap.h"
#include "osapi-task.h"

/**
 * @brief 钩子上下文:捕获传递给 ESA_Stepping_Shim_ReportEvent 的事件
 */
typedef struct
{
    ESA_Stepping_ShimEvent_t CapturedEvent; /*!< 捕获的事件 */
    uint32                   EventCount;    /*!< 事件计数 */
} TestHookContext_t;

/** OSAL 钩子测试共享上下文 */
static TestHookContext_t GlobalHookContext;

/**
 * @brief 捕获 ESA_Stepping_Shim_ReportEvent 调用的钩子函数
 *
 * @details 此钩子拦截对 ESA_Stepping_Shim_ReportEvent 的调用并捕获事件结构以供验证。
 *
 * @param[in,out] UserObj 用户对象（指向 TestHookContext_t 的指针）
 * @param[in] StubRetcode 桩函数返回码
 * @param[in] CallCount 调用计数（未使用）
 * @param[in] Context 桩函数上下文
 * @retval      StubRetcode 返回桩函数返回码
 */
static int32 CaptureShimEvent_Hook(void *UserObj, int32 StubRetcode, uint32 CallCount, const UT_StubContext_t *Context)
{
    TestHookContext_t              *HookCtx = (TestHookContext_t *)UserObj;
    const ESA_Stepping_ShimEvent_t *Event =
        UT_Hook_GetArgValueByName(Context, "event", const ESA_Stepping_ShimEvent_t *);

    if (Event != NULL)
    {
        HookCtx->CapturedEvent = *Event;
        HookCtx->EventCount++;
    }

    return StubRetcode;
}

/**
 * @brief 设置:为每个测试重置上下文和钩子
 */
void ResetTest(void)
{
    UT_ResetState(0);
    memset(&GlobalHookContext, 0, sizeof(GlobalHookContext));
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), CaptureShimEvent_Hook, &GlobalHookContext);
}

/* ============================================================================
   TASKDELAY 钩子测试
   ============================================================================ */

/**
 * @brief 测试:OS_PosixStepping_Hook_TaskDelay 报告 TASK_DELAY_ACK 事件
 *
 * @details 验证阻塞前 TaskDelay 钩子:
 * - 调用 ESA_Stepping_Shim_ReportEvent
 * - 将 event_kind 设置为 TASK_DELAY_ACK
 * - 正确传递 task_id
 * - 正确传递 delay_ms
 */
void Test_OSAL_Hook_TaskDelay_ACK(void)
{
    uint32_t  TestDelayMs = 500;
    osal_id_t TestTaskId  = OS_ObjectIdFromInteger(0x12345678);

    OS_PosixStepping_Hook_TaskDelay(TestDelayMs, TestTaskId);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, 0x12345678);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, TestDelayMs);
}

/**
 * @brief 测试:OS_PosixStepping_Hook_TaskDelay_Complete 报告 TASK_DELAY_COMPLETE 事件
 *
 * @details 验证阻塞后 TaskDelay 钩子:
 * - 调用 ESA_Stepping_Shim_ReportEvent
 * - 将 event_kind 设置为 TASK_DELAY_COMPLETE
 * - 正确传递 task_id
 * - 正确传递 delay_ms
 */
void Test_OSAL_Hook_TaskDelay_Complete(void)
{
    uint32_t  TestDelayMs = 250;
    osal_id_t TestTaskId  = OS_ObjectIdFromInteger(0xABCDEF00);

    OS_PosixStepping_Hook_TaskDelay_Complete(TestDelayMs, TestTaskId);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, 0xABCDEF00);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, TestDelayMs);
}

/**
 * @brief 测试:TaskDelay ACK 和 COMPLETE 是连续的
 *
 * @details 验证阻塞前和阻塞后钩子可以被调用并产生预期事件
 */
void Test_OSAL_Hook_TaskDelay_Sequence(void)
{
    uint32_t  TestDelayMs = 1000;
    osal_id_t TestTaskId  = OS_ObjectIdFromInteger(0x11223344);

    /* 第一次调用:阻塞前 */
    OS_PosixStepping_Hook_TaskDelay(TestDelayMs, TestTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK);

    /* 第二次调用:阻塞后 */
    OS_PosixStepping_Hook_TaskDelay_Complete(TestDelayMs, TestTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 2);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE);
}

/* ============================================================================
   QUEUERECEIVE 钩子测试
   ============================================================================ */

/**
 * @brief 测试:OS_PosixStepping_Hook_QueueReceive 报告 QUEUE_RECEIVE_ACK 事件
 *
 * @details 验证阻塞前 QueueReceive 钩子:
 * - 调用 ESA_Stepping_Shim_ReportEvent
 * - 将 event_kind 设置为 QUEUE_RECEIVE_ACK
 * - 从 token 中提取 entity_id
 * - 从当前任务中提取 task_id
 * - 正确传递超时值
 */
void Test_OSAL_Hook_QueueReceive_ACK(void)
{
    OS_object_token_t TestToken;
    osal_id_t         TestQueueId = OS_ObjectIdFromInteger(0x00020033UL);
    int32             TestTimeout = 1000;
    uint32            ExpectedTaskId;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_id = TestQueueId;

    UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), 0x100AA);
    ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId());

    OS_PosixStepping_Hook_QueueReceive(&TestToken, TestTimeout);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestQueueId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, (uint32_t)TestTimeout);
}

/**
 * @brief 测试:OS_PosixStepping_Hook_QueueReceive_Complete 报告 QUEUE_RECEIVE_COMPLETE 事件
 *
 * @details 验证阻塞后 QueueReceive 钩子:
 * - 调用 ESA_Stepping_Shim_ReportEvent
 * - 将 event_kind 设置为 QUEUE_RECEIVE_COMPLETE
 * - 传递与 ACK 相同的 entity_id、task_id 和超时值
 * - 不依赖 return_code 的值（总是报告）
 */
void Test_OSAL_Hook_QueueReceive_Complete(void)
{
    OS_object_token_t TestToken;
    osal_id_t         TestQueueId = OS_ObjectIdFromInteger(0x00020044UL);
    int32             TestTimeout = 500;
    uint32            ExpectedTaskId;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_id = TestQueueId;

    UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), 0x100AB);
    ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId());

    OS_PosixStepping_Hook_QueueReceive_Complete(&TestToken, TestTimeout, OS_SUCCESS);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestQueueId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, (uint32_t)TestTimeout);
}

/**
 * @brief 测试:即使返回错误,QueueReceive Complete 也会报告
 *
 * @details 验证 COMPLETE 钩子无论 return_code 值如何都会报告
 */
void Test_OSAL_Hook_QueueReceive_Complete_OnError(void)
{
    OS_object_token_t TestToken;
    osal_id_t         TestQueueId    = OS_ObjectIdFromInteger(0x00020055UL);
    int32             TestTimeout    = 100;
    int32             TestReturnCode = -1; /* 错误码 */
    uint32            ExpectedTaskId;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_id = TestQueueId;

    UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), 0x100AC);
    ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId());

    OS_PosixStepping_Hook_QueueReceive_Complete(&TestToken, TestTimeout, TestReturnCode);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestQueueId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, (uint32_t)TestTimeout);
}

/**
 * @brief 测试:QueueReceive ACK 和 COMPLETE 序列
 */
void Test_OSAL_Hook_QueueReceive_Sequence(void)
{
    OS_object_token_t TestToken;
    osal_id_t         TestQueueId = OS_ObjectIdFromInteger(0x00020066UL);
    int32             TestTimeout = 2000;
    uint32            ExpectedTaskId;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_id = TestQueueId;

    UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), 0x100AD);
    ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId());

    OS_PosixStepping_Hook_QueueReceive(&TestToken, TestTimeout);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestQueueId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, (uint32_t)TestTimeout);

    OS_PosixStepping_Hook_QueueReceive_Complete(&TestToken, TestTimeout, 0);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 2);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestQueueId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, (uint32_t)TestTimeout);
}

/* ============================================================================
   BINSEMTAKE 钩子测试
   ============================================================================ */

/**
 * @brief 测试:OS_PosixStepping_Hook_BinSemTake 报告 BINSEM_TAKE_ACK 事件
 *
 * @details 验证阻塞前 BinSemTake 钩子:
 * - 调用 ESA_Stepping_Shim_ReportEvent
 * - 将 event_kind 设置为 BINSEM_TAKE_ACK
 * - 从 token 中提取 entity_id
 * - 从当前任务中提取 task_id
 */
void Test_OSAL_Hook_BinSemTake_ACK(void)
{
    OS_object_token_t TestToken;
    osal_id_t         TestBinSemId = OS_ObjectIdFromInteger(0x00040044UL);
    struct timespec   TestTimeout;
    uint32            ExpectedTaskId;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_id    = TestBinSemId;
    TestTimeout.tv_sec  = 1;
    TestTimeout.tv_nsec = 500000000;

    UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), 0x100BA);
    ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId());

    OS_PosixStepping_Hook_BinSemTake(&TestToken, &TestTimeout);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestBinSemId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
}

/**
 * @brief 测试:OS_PosixStepping_Hook_BinSemTake_Complete 报告 BINSEM_TAKE_COMPLETE 事件
 *
 * @details 验证阻塞后 BinSemTake 钩子:
 * - 调用 ESA_Stepping_Shim_ReportEvent
 * - 将 event_kind 设置为 BINSEM_TAKE_COMPLETE
 * - 传递与 ACK 相同的 entity_id 和 task_id
 * - 不依赖 return_code 的值（总是报告）
 */
void Test_OSAL_Hook_BinSemTake_Complete(void)
{
    OS_object_token_t TestToken;
    osal_id_t         TestBinSemId = OS_ObjectIdFromInteger(0x00040055UL);
    struct timespec   TestTimeout;
    uint32            ExpectedTaskId;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_id    = TestBinSemId;
    TestTimeout.tv_sec  = 0;
    TestTimeout.tv_nsec = 0;

    UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), 0x100BB);
    ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId());

    OS_PosixStepping_Hook_BinSemTake_Complete(&TestToken, &TestTimeout, OS_SUCCESS);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestBinSemId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
}

/**
 * @brief 测试:即使超时,BinSemTake Complete 也会报告
 *
 * @details 验证 COMPLETE 钩子无论 return_code 值如何都会报告（例如 OS_SEM_TIMEOUT）
 */
void Test_OSAL_Hook_BinSemTake_Complete_OnTimeout(void)
{
    OS_object_token_t TestToken;
    osal_id_t         TestBinSemId = OS_ObjectIdFromInteger(0x00040066UL);
    struct timespec   TestTimeout;
    int32             TestReturnCode = -3; /* OS_SEM_TIMEOUT 类似值 */
    uint32            ExpectedTaskId;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_id = TestBinSemId;
    memset(&TestTimeout, 0, sizeof(TestTimeout));

    UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), 0x100BC);
    ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId());

    OS_PosixStepping_Hook_BinSemTake_Complete(&TestToken, &TestTimeout, TestReturnCode);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestBinSemId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
}

/**
 * @brief 测试:BinSemTake ACK 和 COMPLETE 序列
 */
void Test_OSAL_Hook_BinSemTake_Sequence(void)
{
    OS_object_token_t TestToken;
    osal_id_t         TestBinSemId = OS_ObjectIdFromInteger(0x00040077UL);
    struct timespec   TestTimeout;
    uint32            ExpectedTaskId;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_id = TestBinSemId;
    memset(&TestTimeout, 0, sizeof(TestTimeout));
    TestTimeout.tv_sec  = 0;
    TestTimeout.tv_nsec = 100000000;

    UT_SetDefaultReturnValue(UT_KEY(OS_TaskGetId), 0x100BD);
    ExpectedTaskId = (uint32)OS_ObjectIdToInteger(OS_TaskGetId());

    OS_PosixStepping_Hook_BinSemTake(&TestToken, &TestTimeout);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestBinSemId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);

    OS_PosixStepping_Hook_BinSemTake_Complete(&TestToken, &TestTimeout, 0);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 2);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, (uint32)OS_ObjectIdToInteger(TestBinSemId));
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, ExpectedTaskId);
}

/* ============================================================================
   测试注册
   ============================================================================ */

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

/**
 * @brief 测试套件设置函数
 */
void UtTest_Setup(void)
{
    /* TaskDelay 测试 */
    ADD_TEST(Test_OSAL_Hook_TaskDelay_ACK);
    ADD_TEST(Test_OSAL_Hook_TaskDelay_Complete);
    ADD_TEST(Test_OSAL_Hook_TaskDelay_Sequence);

    /* QueueReceive 测试 */
    ADD_TEST(Test_OSAL_Hook_QueueReceive_ACK);
    ADD_TEST(Test_OSAL_Hook_QueueReceive_Complete);
    ADD_TEST(Test_OSAL_Hook_QueueReceive_Complete_OnError);
    ADD_TEST(Test_OSAL_Hook_QueueReceive_Sequence);

    /* BinSemTake 测试 */
    ADD_TEST(Test_OSAL_Hook_BinSemTake_ACK);
    ADD_TEST(Test_OSAL_Hook_BinSemTake_Complete);
    ADD_TEST(Test_OSAL_Hook_BinSemTake_Complete_OnTimeout);
    ADD_TEST(Test_OSAL_Hook_BinSemTake_Sequence);
}

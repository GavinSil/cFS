/**
 * @file
 * @ingroup esa
 * @brief       时间服务步进钩子单元测试
 * @author      gaoyuan
 * @date        2026-03-23
 *
 * @details     本文件测试 ESA 时间服务的步进钩子函数。
 */
#include <string.h>

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include "common_types.h"
#include "esa_stepping_core.h"
#include "esa_stepping_shim.h"

/**
 * @brief 钩子上下文:捕获传递给 ESA_Stepping_Shim_ReportEvent 的事件
 */
typedef struct
{
    ESA_Stepping_ShimEvent_t CapturedEvent; /*!< 捕获的事件 */
    uint32                   EventCount;    /*!< 事件计数 */
} TestHookContext_t;

/** 时间服务钩子测试共享上下文 */
static TestHookContext_t GlobalHookContext;

void CFE_TIME_Stepping_Hook_1HzBoundary(void);
void CFE_TIME_Stepping_Hook_ToneSignal(void);

/**
 * @brief 捕获 Shim 事件的钩子函数
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
    const void                     *EventPtr;
    const ESA_Stepping_ShimEvent_t *Event;

    (void)CallCount;

    EventPtr = UT_Hook_GetArgValueByName(Context, "event", const void *);
    Event    = (const ESA_Stepping_ShimEvent_t *)EventPtr;

    if (Event != NULL)
    {
        HookCtx->CapturedEvent = *Event;
        HookCtx->EventCount++;
    }

    return StubRetcode;
}

/**
 * @brief 重置测试环境
 */
void ResetTest(void)
{
    UT_ResetState(0);
    memset(&GlobalHookContext, 0, sizeof(GlobalHookContext));
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), CaptureShimEvent_Hook, &GlobalHookContext);
}

/**
 * @brief 测试 TIME 1Hz 边界钩子
 *
 * @details 验证 CFE_TIME_Stepping_Hook_1HzBoundary 是否正确报告 1HZ_BOUNDARY 事件
 */
void Test_TIME_Hook_1HzBoundary(void)
{
    CFE_TIME_Stepping_Hook_1HzBoundary();

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_1HZ_BOUNDARY);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_LOCAL_1HZ);
}

/**
 * @brief 测试 TIME Tone 信号钩子
 *
 * @details 验证 CFE_TIME_Stepping_Hook_ToneSignal 是否正确报告 TONE_SIGNAL 事件
 */
void Test_TIME_Hook_ToneSignal(void)
{
    CFE_TIME_Stepping_Hook_ToneSignal();

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TONE_SIGNAL);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_TONE);
}

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

/**
 * @brief 测试套件设置函数
 */
void UtTest_Setup(void)
{
    ADD_TEST(Test_TIME_Hook_1HzBoundary);
    ADD_TEST(Test_TIME_Hook_ToneSignal);
}

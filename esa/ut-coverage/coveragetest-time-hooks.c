#include <string.h>

#include "../../osal/src/os/inc/common_types.h"
#include "../../osal/ut_assert/inc/utassert.h"
#include "../../osal/ut_assert/inc/utstubs.h"
#include "../../osal/ut_assert/inc/uttest.h"

#include "../public_inc/esa_stepping_shim.h"
#include "../fsw/inc/esa_stepping_core.h"
#include "../../cfe/modules/time/fsw/src/cfe_time_stepping.h"

typedef struct
{
    ESA_Stepping_ShimEvent_t CapturedEvent;
    uint32                   EventCount;
} TestHookContext_t;

static TestHookContext_t GlobalHookContext;

static int32 CaptureShimEvent_Hook(void *UserObj, int32 StubRetcode, uint32 CallCount,
                                    const UT_StubContext_t *Context)
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

void ResetTest(void)
{
    UT_ResetState(0);
    memset(&GlobalHookContext, 0, sizeof(GlobalHookContext));
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), CaptureShimEvent_Hook, &GlobalHookContext);
}

void Test_TIME_Hook_TaskCycle(void)
{
    CFE_TIME_Stepping_Hook_TaskCycle();

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TIME_TASK_CYCLE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, ESA_SIM_STEPPING_SERVICE_BIT_TIME);
}

void Test_TIME_Hook_1HzBoundary(void)
{
    CFE_TIME_Stepping_Hook_1HzBoundary();

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_1HZ_BOUNDARY);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_LOCAL_1HZ);
}

void Test_TIME_Hook_ToneSignal(void)
{
    CFE_TIME_Stepping_Hook_ToneSignal();

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TONE_SIGNAL);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.entity_id, ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_TONE);
}

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

void UtTest_Setup(void)
{
    ADD_TEST(Test_TIME_Hook_TaskCycle);
    ADD_TEST(Test_TIME_Hook_1HzBoundary);
    ADD_TEST(Test_TIME_Hook_ToneSignal);
}

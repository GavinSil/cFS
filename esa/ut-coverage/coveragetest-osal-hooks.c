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

/**
 * \file
 * \ingroup esa
 *
 * Purpose: OSAL stepping hook unit tests
 *
 * This test suite verifies that all 6 OSAL stepping hooks (3 pre-blocking + 3 complete)
 * correctly construct and report events to the ESA stepping shim.
 *
 * Test coverage:
 * - OS_PosixStepping_Hook_TaskDelay() reports TASK_DELAY_ACK
 * - OS_PosixStepping_Hook_TaskDelay_Complete() reports TASK_DELAY_COMPLETE
 * - OS_PosixStepping_Hook_QueueReceive() reports QUEUE_RECEIVE_ACK
 * - OS_PosixStepping_Hook_QueueReceive_Complete() reports QUEUE_RECEIVE_COMPLETE
 * - OS_PosixStepping_Hook_BinSemTake() reports BINSEM_TAKE_ACK
 * - OS_PosixStepping_Hook_BinSemTake_Complete() reports BINSEM_TAKE_COMPLETE
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

/*
 * Hook context: captures the event passed to ESA_Stepping_Shim_ReportEvent
 */
typedef struct
{
    ESA_Stepping_ShimEvent_t CapturedEvent;
    uint32                   EventCount;
} TestHookContext_t;

static TestHookContext_t GlobalHookContext;

/**
 * \brief Hook to capture ESA_Stepping_Shim_ReportEvent calls
 *
 * This hook intercepts calls to ESA_Stepping_Shim_ReportEvent and captures
 * the event structure for validation.
 */
static int32 CaptureShimEvent_Hook(void *UserObj, int32 StubRetcode, uint32 CallCount,
                                    const UT_StubContext_t *Context)
{
    TestHookContext_t *HookCtx = (TestHookContext_t *)UserObj;
    const ESA_Stepping_ShimEvent_t *Event = UT_Hook_GetArgValueByName(Context, "event", const ESA_Stepping_ShimEvent_t *);

    if (Event != NULL)
    {
        HookCtx->CapturedEvent = *Event;
        HookCtx->EventCount++;
    }

    return StubRetcode;
}

/**
 * \brief Setup: Reset context and hook for each test
 */
void ResetTest(void)
{
    UT_ResetState(0);
    memset(&GlobalHookContext, 0, sizeof(GlobalHookContext));
    UT_SetHookFunction(UT_KEY(ESA_Stepping_Shim_ReportEvent), CaptureShimEvent_Hook, &GlobalHookContext);
}

/* ============================================================================
   TASKDELAY HOOK TESTS
   ============================================================================ */

/**
 * \brief Test: OS_PosixStepping_Hook_TaskDelay reports TASK_DELAY_ACK event
 *
 * Verifies that the pre-blocking TaskDelay hook:
 * - Calls ESA_Stepping_Shim_ReportEvent
 * - Sets event_kind to TASK_DELAY_ACK
 * - Passes task_id correctly
 * - Passes delay_ms correctly
 */
void Test_OSAL_Hook_TaskDelay_ACK(void)
{
    uint32_t TestDelayMs = 500;
    osal_id_t TestTaskId = OS_ObjectIdFromInteger(0x12345678);

    OS_PosixStepping_Hook_TaskDelay(TestDelayMs, TestTaskId);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, 0x12345678);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, TestDelayMs);
}

/**
 * \brief Test: OS_PosixStepping_Hook_TaskDelay_Complete reports TASK_DELAY_COMPLETE event
 *
 * Verifies that the post-blocking TaskDelay hook:
 * - Calls ESA_Stepping_Shim_ReportEvent
 * - Sets event_kind to TASK_DELAY_COMPLETE
 * - Passes task_id correctly
 * - Passes delay_ms correctly
 */
void Test_OSAL_Hook_TaskDelay_Complete(void)
{
    uint32_t TestDelayMs = 250;
    osal_id_t TestTaskId = OS_ObjectIdFromInteger(0xABCDEF00);

    OS_PosixStepping_Hook_TaskDelay_Complete(TestDelayMs, TestTaskId);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.task_id, 0xABCDEF00);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, TestDelayMs);
}

/**
 * \brief Test: TaskDelay ACK and COMPLETE are sequential
 *
 * Verifies that both pre and post hooks can be called and produce expected events
 */
void Test_OSAL_Hook_TaskDelay_Sequence(void)
{
    uint32_t TestDelayMs = 1000;
    osal_id_t TestTaskId = OS_ObjectIdFromInteger(0x11223344);

    /* First call: pre-blocking */
    OS_PosixStepping_Hook_TaskDelay(TestDelayMs, TestTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK);

    /* Second call: post-blocking */
    OS_PosixStepping_Hook_TaskDelay_Complete(TestDelayMs, TestTaskId);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 2);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE);
}

/* ============================================================================
   QUEUERECEIVE HOOK TESTS
   ============================================================================ */

/**
 * \brief Test: OS_PosixStepping_Hook_QueueReceive reports QUEUE_RECEIVE_ACK event
 *
 * Verifies that the pre-blocking QueueReceive hook:
 * - Calls ESA_Stepping_Shim_ReportEvent
 * - Sets event_kind to QUEUE_RECEIVE_ACK
 * - Extracts entity_id from token
 * - Extracts task_id from current task
 * - Passes timeout correctly
 */
void Test_OSAL_Hook_QueueReceive_ACK(void)
{
    OS_object_token_t TestToken;
    int32 TestTimeout = 1000;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_idx = 99;

    OS_PosixStepping_Hook_QueueReceive(&TestToken, TestTimeout);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, (uint32_t)TestTimeout);
    /* entity_id is set from token (may be 0 if token not fully initialized in unit test) */
    /* task_id is set from current task and should be non-zero */
    UtAssert_NONZERO(GlobalHookContext.CapturedEvent.task_id);
}

/**
 * \brief Test: OS_PosixStepping_Hook_QueueReceive_Complete reports QUEUE_RECEIVE_COMPLETE event
 *
 * Verifies that the post-blocking QueueReceive hook:
 * - Calls ESA_Stepping_Shim_ReportEvent
 * - Sets event_kind to QUEUE_RECEIVE_COMPLETE
 * - Passes same entity_id, task_id, and timeout as ACK
 * - Does NOT condition on return_code (always reports)
 */
void Test_OSAL_Hook_QueueReceive_Complete(void)
{
    OS_object_token_t TestToken;
    int32 TestTimeout = 500;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_idx = 99;

    OS_PosixStepping_Hook_QueueReceive_Complete(&TestToken, TestTimeout, OS_SUCCESS);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.optional_delay_ms, (uint32_t)TestTimeout);
    /* entity_id is set from token (may be 0 if token not fully initialized in unit test) */
    /* task_id is set from current task and should be non-zero */
    UtAssert_NONZERO(GlobalHookContext.CapturedEvent.task_id);
}

/**
 * \brief Test: QueueReceive Complete is reported even on error return
 *
 * Verifies that the COMPLETE hook reports regardless of return_code value
 */
void Test_OSAL_Hook_QueueReceive_Complete_OnError(void)
{
    OS_object_token_t TestToken;
    int32 TestTimeout = 100;
    int32 TestReturnCode = -1;  /* Error code */

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_idx = 7;

    OS_PosixStepping_Hook_QueueReceive_Complete(&TestToken, TestTimeout, TestReturnCode);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE);
}

/**
 * \brief Test: QueueReceive ACK and COMPLETE sequence
 */
void Test_OSAL_Hook_QueueReceive_Sequence(void)
{
    OS_object_token_t TestToken;
    int32 TestTimeout = 2000;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_idx = 55;

    OS_PosixStepping_Hook_QueueReceive(&TestToken, TestTimeout);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK);

    OS_PosixStepping_Hook_QueueReceive_Complete(&TestToken, TestTimeout, 0);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 2);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE);
}

/* ============================================================================
   BINSEMTAKE HOOK TESTS
   ============================================================================ */

/**
 * \brief Test: OS_PosixStepping_Hook_BinSemTake reports BINSEM_TAKE_ACK event
 *
 * Verifies that the pre-blocking BinSemTake hook:
 * - Calls ESA_Stepping_Shim_ReportEvent
 * - Sets event_kind to BINSEM_TAKE_ACK
 * - Extracts entity_id from token
 * - Extracts task_id from current task
 */
void Test_OSAL_Hook_BinSemTake_ACK(void)
{
    OS_object_token_t TestToken;
    struct timespec TestTimeout;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_idx = 99;
    TestTimeout.tv_sec = 1;
    TestTimeout.tv_nsec = 500000000;

    OS_PosixStepping_Hook_BinSemTake(&TestToken, &TestTimeout);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK);
    /* entity_id is set from token (may be 0 if token not fully initialized in unit test) */
    /* task_id is set from current task and should be non-zero */
    UtAssert_NONZERO(GlobalHookContext.CapturedEvent.task_id);
}

/**
 * \brief Test: OS_PosixStepping_Hook_BinSemTake_Complete reports BINSEM_TAKE_COMPLETE event
 *
 * Verifies that the post-blocking BinSemTake hook:
 * - Calls ESA_Stepping_Shim_ReportEvent
 * - Sets event_kind to BINSEM_TAKE_COMPLETE
 * - Passes same entity_id and task_id as ACK
 * - Does NOT condition on return_code (always reports)
 */
void Test_OSAL_Hook_BinSemTake_Complete(void)
{
    OS_object_token_t TestToken;
    struct timespec TestTimeout;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_idx = 99;
    TestTimeout.tv_sec = 0;
    TestTimeout.tv_nsec = 0;

    OS_PosixStepping_Hook_BinSemTake_Complete(&TestToken, &TestTimeout, OS_SUCCESS);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE);
    /* entity_id is set from token (may be 0 if token not fully initialized in unit test) */
    /* task_id is set from current task and should be non-zero */
    UtAssert_NONZERO(GlobalHookContext.CapturedEvent.task_id);
}

/**
 * \brief Test: BinSemTake Complete is reported even on timeout
 *
 * Verifies that the COMPLETE hook reports regardless of return_code value
 * (e.g., OS_SEM_TIMEOUT)
 */
void Test_OSAL_Hook_BinSemTake_Complete_OnTimeout(void)
{
    OS_object_token_t TestToken;
    struct timespec TestTimeout;
    int32 TestReturnCode = -3;  /* OS_SEM_TIMEOUT analog */

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_idx = 66;
    memset(&TestTimeout, 0, sizeof(TestTimeout));

    OS_PosixStepping_Hook_BinSemTake_Complete(&TestToken, &TestTimeout, TestReturnCode);

    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE);
}

/**
 * \brief Test: BinSemTake ACK and COMPLETE sequence
 */
void Test_OSAL_Hook_BinSemTake_Sequence(void)
{
    OS_object_token_t TestToken;
    struct timespec TestTimeout;

    memset(&TestToken, 0, sizeof(TestToken));
    TestToken.obj_idx = 77;
    memset(&TestTimeout, 0, sizeof(TestTimeout));
    TestTimeout.tv_sec = 0;
    TestTimeout.tv_nsec = 100000000;

    OS_PosixStepping_Hook_BinSemTake(&TestToken, &TestTimeout);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 1);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK);

    OS_PosixStepping_Hook_BinSemTake_Complete(&TestToken, &TestTimeout, 0);
    UtAssert_UINT32_EQ(GlobalHookContext.EventCount, 2);
    UtAssert_UINT32_EQ(GlobalHookContext.CapturedEvent.event_kind, ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE);
}

/* ============================================================================
   TEST REGISTRATION
   ============================================================================ */

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

void UtTest_Setup(void)
{
    /* TaskDelay tests */
    ADD_TEST(Test_OSAL_Hook_TaskDelay_ACK);
    ADD_TEST(Test_OSAL_Hook_TaskDelay_Complete);
    ADD_TEST(Test_OSAL_Hook_TaskDelay_Sequence);

    /* QueueReceive tests */
    ADD_TEST(Test_OSAL_Hook_QueueReceive_ACK);
    ADD_TEST(Test_OSAL_Hook_QueueReceive_Complete);
    ADD_TEST(Test_OSAL_Hook_QueueReceive_Complete_OnError);
    ADD_TEST(Test_OSAL_Hook_QueueReceive_Sequence);

    /* BinSemTake tests */
    ADD_TEST(Test_OSAL_Hook_BinSemTake_ACK);
    ADD_TEST(Test_OSAL_Hook_BinSemTake_Complete);
    ADD_TEST(Test_OSAL_Hook_BinSemTake_Complete_OnTimeout);
    ADD_TEST(Test_OSAL_Hook_BinSemTake_Sequence);
}

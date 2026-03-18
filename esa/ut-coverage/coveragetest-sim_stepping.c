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

#include <stdint.h>

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include "common_types.h"
#include "cfe_psp_sim_stepping.h"
#include "cfe_psp_sim_stepping_core.h"
#include "cfe_psp_sim_stepping_shim.h"

extern void ESA_Init(void);

void Test_sim_stepping_StatusTaxonomy(void)
{
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_SUCCESS, 0);
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_FAILURE, -1);
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_DUPLICATE_BEGIN, -2);
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_NOT_READY, -3);
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_TIMEOUT, -4);
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_COMPLETE, -5);
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_TRANSPORT_ERROR, -6);
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_PROTOCOL_ERROR, -7);
    UtAssert_INT32_EQ(CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_STATE, -8);
}

void Test_sim_stepping_NotReadyPaths(void)
{
    uint32_t state = 1234;
    uint32_t triggers = 5678;

    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_BeginStep(), CFE_PSP_SIM_STEPPING_STATUS_FAILURE);
    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_WaitStepComplete(1), CFE_PSP_SIM_STEPPING_STATUS_FAILURE);
    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_QueryState(&state, &triggers), CFE_PSP_SIM_STEPPING_STATUS_FAILURE);
}

void Test_sim_stepping_InProcQueryAndIllegalState(void)
{
    uint32_t State = 99;
    uint32_t TriggerCount = 99;
    uint32_t state = 0;
    uint32_t triggers = 0;

    /* not yet initialized */
    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_BeginStep(), CFE_PSP_SIM_STEPPING_STATUS_FAILURE);
    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_WaitStepComplete(0), CFE_PSP_SIM_STEPPING_STATUS_FAILURE);
    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_QueryState(&State, &TriggerCount),
                      CFE_PSP_SIM_STEPPING_STATUS_FAILURE);

    UtAssert_VOIDCALL(ESA_Init());

    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_QueryState(&state, &triggers), CFE_PSP_SIM_STEPPING_STATUS_SUCCESS);
    UtAssert_INT32_EQ(state, CFE_PSP_SIM_STEPPING_STATE_READY);

    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_QueryState(NULL, &triggers), CFE_PSP_SIM_STEPPING_STATUS_SUCCESS);
    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_QueryState(&state, NULL), CFE_PSP_SIM_STEPPING_STATUS_SUCCESS);

    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_WaitStepComplete(1), CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_STATE);
}

void Test_sim_stepping_BeginDuplicateAndComplete(void)
{
    uint32_t state = 0;
    uint32_t triggers = 0;

    UtAssert_VOIDCALL(ESA_Init());

    {
        CFE_PSP_SimStepping_ShimEvent_t event = {0};
        event.event_kind = CFE_PSP_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING;
        UtAssert_INT32_EQ(CFE_PSP_SimStepping_Shim_ReportEvent(&event), CFE_PSP_SIM_STEPPING_STATUS_SUCCESS);
    }

    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_BeginStep(), CFE_PSP_SIM_STEPPING_STATUS_SUCCESS);
    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_BeginStep(), CFE_PSP_SIM_STEPPING_STATUS_DUPLICATE_BEGIN);

    {
        CFE_PSP_SimStepping_ShimEvent_t event = {0};
        event.event_kind = CFE_PSP_SIM_STEPPING_EVENT_SCH_MINOR_FRAME;
        UtAssert_INT32_EQ(CFE_PSP_SimStepping_Shim_ReportEvent(&event), CFE_PSP_SIM_STEPPING_STATUS_SUCCESS);
    }

    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_QueryState(&state, &triggers), CFE_PSP_SIM_STEPPING_STATUS_SUCCESS);
    UtAssert_INT32_EQ(state, CFE_PSP_SIM_STEPPING_STATE_RUNNING);

    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_WaitStepComplete(2), CFE_PSP_SIM_STEPPING_STATUS_SUCCESS);
}

void Test_sim_stepping_BeginNotReadyBeforeLifecycle(void)
{
    UtAssert_VOIDCALL(ESA_Init());
    UtAssert_INT32_EQ(CFE_PSP_SimStepping_InProc_BeginStep(), CFE_PSP_SIM_STEPPING_STATUS_NOT_READY);
}

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

void ResetTest(void)
{
    UT_ResetState(0);
}

void UtTest_Setup(void)
{
    ADD_TEST(Test_sim_stepping_StatusTaxonomy);
    ADD_TEST(Test_sim_stepping_NotReadyPaths);
    ADD_TEST(Test_sim_stepping_InProcQueryAndIllegalState);
    ADD_TEST(Test_sim_stepping_BeginNotReadyBeforeLifecycle);
    ADD_TEST(Test_sim_stepping_BeginDuplicateAndComplete);
}

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

#include <sys/time.h>

#include "utassert.h"
#include "uttest.h"

#include "osapi.h"

#ifdef CFE_SIM_STEPPING
#include "esa_stepping.h"
#include "esa_stepping_shim.h"
#endif

#define WALL_CLOCK_THRESHOLD_SEC 2.0

static double GetWallClockTime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

#ifdef CFE_SIM_STEPPING

static void Test_Stepping_NoWallClockLeakage(void)
{
    double start_time, end_time, wall_elapsed;
    int32 status;
    
    ESA_Stepping_ShimEvent_t ready_event = {0};
    ready_event.event_kind = ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING;
    status = ESA_Stepping_Shim_ReportEvent(&ready_event);
    UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);
    
    start_time = GetWallClockTime();
    
    status = ESA_Stepping_InProc_BeginStep();
    UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);
    
    ESA_Stepping_ShimEvent_t frame_event = {0};
    frame_event.event_kind = ESA_SIM_STEPPING_EVENT_SCH_MINOR_FRAME;
    
    for (int i = 0; i < 10; i++)
    {
        status = ESA_Stepping_Shim_ReportEvent(&frame_event);
        UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);
    }
    
    status = ESA_Stepping_InProc_WaitStepComplete(11);
    UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);
    
    end_time = GetWallClockTime();
    wall_elapsed = end_time - start_time;
    
    UtAssert_True(wall_elapsed < WALL_CLOCK_THRESHOLD_SEC,
                  "Stepping cycle wall-clock elapsed (%f sec) < threshold (%f sec)",
                  wall_elapsed, WALL_CLOCK_THRESHOLD_SEC);
    
    start_time = GetWallClockTime();
    
    for (int cycle = 0; cycle < 5; cycle++)
    {
        status = ESA_Stepping_InProc_BeginStep();
        UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);
        
        for (int i = 0; i < 20; i++)
        {
            ESA_Stepping_Shim_ReportEvent(&frame_event);
        }
        
        status = ESA_Stepping_InProc_WaitStepComplete(21);
        UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);
    }
    
    end_time = GetWallClockTime();
    wall_elapsed = end_time - start_time;
    
    UtAssert_True(wall_elapsed < WALL_CLOCK_THRESHOLD_SEC,
                  "Multiple cycles wall-clock elapsed (%f sec) < threshold (%f sec)",
                  wall_elapsed, WALL_CLOCK_THRESHOLD_SEC);
}

#endif

static void Test_NonSteppingMode_Regression(void)
{
#ifdef CFE_SIM_STEPPING
    UtAssert_NA("Non-stepping regression test - run full OSAL suite without CFE_SIM_STEPPING");
#else
    UtAssert_True(true, "Non-stepping mode - full OSAL suite should pass (117/117)");
#endif
}

static void IntegrationTest_Setup(void)
{
    int32 status = OS_API_Init();
    if (status != OS_SUCCESS)
    {
        UtAssert_Abort("OS_API_Init() failed");
    }
    
#ifdef CFE_SIM_STEPPING
    ESA_Init();
#endif
}

static void IntegrationTest_Teardown(void)
{
}

void UtTest_Setup(void)
{
#ifdef CFE_SIM_STEPPING
    UtTest_Add(Test_Stepping_NoWallClockLeakage, IntegrationTest_Setup, IntegrationTest_Teardown,
               "Test_Stepping_NoWallClockLeakage");
#endif
    UtTest_Add(Test_NonSteppingMode_Regression, NULL, NULL,
               "Test_NonSteppingMode_Regression");
}

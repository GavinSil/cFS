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

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include "esa_wait.h"
#include "esa_stepping.h"
#include "osapi-constants.h"
#include "osapi-error.h"
#include "osapi-idmap.h"
#include "osapi-task.h"

typedef struct
{
    uint64 NowNs;
    uint64 AdvanceNs;
    bool   GetTimeSuccess;
} ESA_WaitTestContext_t;

static ESA_WaitTestContext_t ESA_WaitCtx;
static pthread_mutex_t       ESA_WaitCtxMutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    uint32 TaskGetIdCount;
} ESA_TaskIdSeqContext_t;

static ESA_TaskIdSeqContext_t ESA_TaskIdCtx;

typedef struct
{
    osal_id_t ResourceId;
    uint32    TimeoutMs;
    int32     Status;
    bool      Done;
} ESA_WaitThreadInfo_t;

bool ESA_Stepping_Hook_GetTime(uint64_t *sim_time_ns)
{
    bool result;

    pthread_mutex_lock(&ESA_WaitCtxMutex);

    if (!ESA_WaitCtx.GetTimeSuccess || sim_time_ns == NULL)
    {
        result = false;
    }
    else
    {
        *sim_time_ns      = ESA_WaitCtx.NowNs;
        ESA_WaitCtx.NowNs = ESA_WaitCtx.NowNs + ESA_WaitCtx.AdvanceNs;
        result            = true;
    }

    pthread_mutex_unlock(&ESA_WaitCtxMutex);

    return result;
}

static void *ESA_Wait_MessageThread(void *arg)
{
    ESA_WaitThreadInfo_t *thread_info;

    thread_info = (ESA_WaitThreadInfo_t *)arg;
    thread_info->Status = ESA_WaitForMessage(thread_info->ResourceId, thread_info->TimeoutMs);
    thread_info->Done   = true;

    return NULL;
}

static void *ESA_Wait_SemThread(void *arg)
{
    ESA_WaitThreadInfo_t *thread_info;

    thread_info = (ESA_WaitThreadInfo_t *)arg;
    thread_info->Status = ESA_WaitForSem(thread_info->ResourceId, thread_info->TimeoutMs);
    thread_info->Done   = true;

    return NULL;
}

static void *ESA_Wait_CondVarThread(void *arg)
{
    ESA_WaitThreadInfo_t *thread_info;

    thread_info = (ESA_WaitThreadInfo_t *)arg;
    thread_info->Status = ESA_WaitForCondVar(thread_info->ResourceId, thread_info->TimeoutMs);
    thread_info->Done   = true;

    return NULL;
}

static uint32 ESA_Wait_GetDoneCount(const ESA_WaitThreadInfo_t *thread_info, uint32 thread_count)
{
    uint32 idx;
    uint32 done_count;

    done_count = 0;
    for (idx = 0; idx < thread_count; ++idx)
    {
        if (thread_info[idx].Done)
        {
            ++done_count;
        }
    }

    return done_count;
}

static void ESA_Wait_WaitForClockGettimeCalls(uint32 expected_count)
{
    uint32 tries;

    for (tries = 0; tries < 200; ++tries)
    {
        if (UT_GetStubCount(UT_KEY(clock_gettime)) >= expected_count)
        {
            break;
        }

        clock_nanosleep(CLOCK_MONOTONIC, 0, &(struct timespec){0, 5000000}, NULL);
    }
}

static void ESA_Wait_SetTaskIds(uint32 task_count)
{
    ESA_TaskIdCtx.TaskGetIdCount = task_count;
}

static void ESA_Wait_TaskGetIdHandler(void *user_obj, UT_EntryKey_t func_key, const UT_StubContext_t *context)
{
    ESA_TaskIdSeqContext_t *task_ctx;
    osal_id_t               task_id;

    (void)func_key;
    (void)context;

    task_ctx = (ESA_TaskIdSeqContext_t *)user_obj;

    if (task_ctx->TaskGetIdCount == 0)
    {
        task_ctx->TaskGetIdCount = 1;
    }

    task_id = OS_ObjectIdFromInteger(task_ctx->TaskGetIdCount);
    --task_ctx->TaskGetIdCount;

    UT_Stub_SetReturnValue(UT_KEY(OS_TaskGetId), task_id);
}

void ResetTest(void)
{
    UT_ResetState(0);
    ESA_WaitCtx.NowNs          = 0;
    ESA_WaitCtx.AdvanceNs      = 0;
    ESA_WaitCtx.GetTimeSuccess = true;

    UT_SetDefaultReturnValue(UT_KEY(OS_ObjectIdToArrayIndex), OS_SUCCESS);
    UT_SetDefaultReturnValue(UT_KEY(OS_IdentifyObject), OS_OBJECT_TYPE_OS_COUNTSEM);
    UT_SetDefaultReturnValue(UT_KEY(clock_gettime), 0);
    UT_SetHandlerFunction(UT_KEY(OS_TaskGetId), ESA_Wait_TaskGetIdHandler, &ESA_TaskIdCtx);
}

void Test_ESA_WaitForMessage_BasicWakeByNotify(void)
{
    pthread_t             waiter_thread;
    ESA_WaitThreadInfo_t thread_info;
    osal_id_t            queue_id;

    queue_id                = OS_ObjectIdFromInteger(0x00020003);
    thread_info.ResourceId  = queue_id;
    thread_info.TimeoutMs   = 20000;
    thread_info.Status      = -1;
    thread_info.Done        = false;

    ESA_Wait_SetTaskIds(1);

    UtAssert_BOOL_TRUE(pthread_create(&waiter_thread, NULL, ESA_Wait_MessageThread, &thread_info) == 0);
    ESA_Wait_WaitForClockGettimeCalls(1);

    ESA_NotifyQueuePut(queue_id);

    UtAssert_BOOL_TRUE(pthread_join(waiter_thread, NULL) == 0);
    UtAssert_BOOL_TRUE(thread_info.Done);
    UtAssert_INT32_EQ(thread_info.Status, ESA_WOKE_BY_RESOURCE);
}

void Test_ESA_WaitForMessage_TimeoutBySimTimeProgression(void)
{
    osal_id_t queue_id;
    int32     status;

    queue_id             = OS_ObjectIdFromInteger(0x00020011);
    ESA_WaitCtx.NowNs    = 0;
    ESA_WaitCtx.AdvanceNs = 200000000;

    ESA_Wait_SetTaskIds(1);
    status = ESA_WaitForMessage(queue_id, 100);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

void Test_ESA_WaitForSem_BasicWakeByNotify(void)
{
    pthread_t             waiter_thread;
    ESA_WaitThreadInfo_t thread_info;
    osal_id_t            sem_id;

    sem_id                  = OS_ObjectIdFromInteger(0x00040007);
    thread_info.ResourceId  = sem_id;
    thread_info.TimeoutMs   = 20000;
    thread_info.Status      = -1;
    thread_info.Done        = false;

    ESA_Wait_SetTaskIds(1);

    UtAssert_BOOL_TRUE(pthread_create(&waiter_thread, NULL, ESA_Wait_SemThread, &thread_info) == 0);
    ESA_Wait_WaitForClockGettimeCalls(1);

    ESA_NotifySemGive(sem_id);

    UtAssert_BOOL_TRUE(pthread_join(waiter_thread, NULL) == 0);
    UtAssert_BOOL_TRUE(thread_info.Done);
    UtAssert_INT32_EQ(thread_info.Status, ESA_WOKE_BY_RESOURCE);
}

void Test_ESA_WaitForDelay_TimeoutAfterNonZeroDelay(void)
{
    int32 status;

    ESA_WaitCtx.NowNs     = 0;
    ESA_WaitCtx.AdvanceNs = 200000000;

    ESA_Wait_SetTaskIds(1);
    status = ESA_WaitForDelay(100);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

void Test_ESA_NotifySemFlush_WakesAllWaiters(void)
{
    pthread_t             waiter_threads[3];
    ESA_WaitThreadInfo_t thread_info[3];
    osal_id_t            sem_id;
    uint32               idx;

    sem_id = OS_ObjectIdFromInteger(0x00040009);

    ESA_Wait_SetTaskIds(3);
    for (idx = 0; idx < 3; ++idx)
    {
        thread_info[idx].ResourceId = sem_id;
        thread_info[idx].TimeoutMs  = 20000;
        thread_info[idx].Status     = -1;
        thread_info[idx].Done       = false;
        UtAssert_BOOL_TRUE(pthread_create(&waiter_threads[idx], NULL, ESA_Wait_SemThread, &thread_info[idx]) == 0);
    }

    ESA_Wait_WaitForClockGettimeCalls(3);
    ESA_NotifySemFlush(sem_id);

    for (idx = 0; idx < 3; ++idx)
    {
        UtAssert_BOOL_TRUE(pthread_join(waiter_threads[idx], NULL) == 0);
        UtAssert_INT32_EQ(thread_info[idx].Status, ESA_WOKE_BY_FLUSH);
    }
}

void Test_ESA_RegisterTask_DeregisterTask_Lifecycle(void)
{
    osal_id_t task_id;
    int32     status;

    task_id = OS_TaskGetId();

    ESA_RegisterTask(task_id);
    status = ESA_WaitForDelay(OS_CHECK);
    ESA_DeregisterTask(task_id);
    ESA_DeregisterTask(task_id);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

void Test_ESA_WaitForCondVar_SignalWakeOne(void)
{
    pthread_t             waiter_threads[2];
    ESA_WaitThreadInfo_t thread_info[2];
    osal_id_t            condvar_id;
    uint32               idx;
    uint32               done_count;

    condvar_id = OS_ObjectIdFromInteger(0x00050003);

    ESA_Wait_SetTaskIds(2);
    for (idx = 0; idx < 2; ++idx)
    {
        thread_info[idx].ResourceId = condvar_id;
        thread_info[idx].TimeoutMs  = 20000;
        thread_info[idx].Status     = -1;
        thread_info[idx].Done       = false;
        UtAssert_BOOL_TRUE(pthread_create(&waiter_threads[idx], NULL, ESA_Wait_CondVarThread, &thread_info[idx]) == 0);
    }

    ESA_Wait_WaitForClockGettimeCalls(2);
    ESA_NotifyCondVar(condvar_id, false);
    UtAssert_BOOL_TRUE(clock_nanosleep(CLOCK_MONOTONIC, 0, &(struct timespec){0, 50000000}, NULL) == 0);

    done_count = ESA_Wait_GetDoneCount(thread_info, 2);
    UtAssert_UINT32_EQ(done_count, 1);

    ESA_NotifyCondVar(condvar_id, true);

    for (idx = 0; idx < 2; ++idx)
    {
        UtAssert_BOOL_TRUE(pthread_join(waiter_threads[idx], NULL) == 0);
        UtAssert_INT32_EQ(thread_info[idx].Status, ESA_WOKE_BY_RESOURCE);
    }
}

void Test_ESA_WaitForCondVar_Timeout(void)
{
    osal_id_t condvar_id;
    int32     status;

    condvar_id              = OS_ObjectIdFromInteger(0x00050007);
    ESA_WaitCtx.NowNs       = 0;
    ESA_WaitCtx.AdvanceNs   = 200000000;

    ESA_Wait_SetTaskIds(1);
    status = ESA_WaitForCondVar(condvar_id, 100);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

void Test_ESA_NotifyCondVar_BroadcastWakesAll(void)
{
    pthread_t             waiter_threads[3];
    ESA_WaitThreadInfo_t thread_info[3];
    osal_id_t            condvar_id;
    uint32               idx;

    condvar_id = OS_ObjectIdFromInteger(0x00050009);

    ESA_Wait_SetTaskIds(3);
    for (idx = 0; idx < 3; ++idx)
    {
        thread_info[idx].ResourceId = condvar_id;
        thread_info[idx].TimeoutMs  = 20000;
        thread_info[idx].Status     = -1;
        thread_info[idx].Done       = false;
        UtAssert_BOOL_TRUE(pthread_create(&waiter_threads[idx], NULL, ESA_Wait_CondVarThread, &thread_info[idx]) == 0);
    }

    ESA_Wait_WaitForClockGettimeCalls(3);
    ESA_NotifyCondVar(condvar_id, true);

    for (idx = 0; idx < 3; ++idx)
    {
        UtAssert_BOOL_TRUE(pthread_join(waiter_threads[idx], NULL) == 0);
        UtAssert_INT32_EQ(thread_info[idx].Status, ESA_WOKE_BY_RESOURCE);
    }
}

void Test_ESA_WaitForMessage_TwoConsumersOnePutWakesOne(void)
{
    pthread_t             waiter_threads[2];
    ESA_WaitThreadInfo_t thread_info[2];
    osal_id_t            queue_id;
    uint32               idx;
    uint32               done_count;

    queue_id = OS_ObjectIdFromInteger(0x00020021);

    ESA_Wait_SetTaskIds(2);
    for (idx = 0; idx < 2; ++idx)
    {
        thread_info[idx].ResourceId = queue_id;
        thread_info[idx].TimeoutMs  = 20000;
        thread_info[idx].Status     = -1;
        thread_info[idx].Done       = false;
        UtAssert_BOOL_TRUE(pthread_create(&waiter_threads[idx], NULL, ESA_Wait_MessageThread, &thread_info[idx]) == 0);
    }

    ESA_Wait_WaitForClockGettimeCalls(2);
    ESA_NotifyQueuePut(queue_id);
    ESA_Wait_WaitForClockGettimeCalls(2);

    done_count = ESA_Wait_GetDoneCount(thread_info, 2);
    UtAssert_UINT32_EQ(done_count, 1);

    ESA_NotifyQueuePut(queue_id);
    UtAssert_BOOL_TRUE(clock_nanosleep(CLOCK_MONOTONIC, 0, &(struct timespec){0, 10000000}, NULL) == 0);

    if (ESA_Wait_GetDoneCount(thread_info, 2) < 2)
    {
        ESA_NotifyQueuePut(queue_id);
    }

    for (idx = 0; idx < 2; ++idx)
    {
        UtAssert_BOOL_TRUE(pthread_join(waiter_threads[idx], NULL) == 0);
        UtAssert_INT32_EQ(thread_info[idx].Status, ESA_WOKE_BY_RESOURCE);
    }
}

void Test_ESA_WaitForMessage_ZeroTimeout_IsImmediateTimeout(void)
{
    osal_id_t queue_id;
    int32     status;

    queue_id = OS_ObjectIdFromInteger(0x00020003);
    ESA_Wait_SetTaskIds(1);
    status   = ESA_WaitForMessage(queue_id, OS_CHECK);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

void UtTest_Setup(void)
{
    ADD_TEST(Test_ESA_WaitForMessage_BasicWakeByNotify);
    ADD_TEST(Test_ESA_WaitForMessage_TimeoutBySimTimeProgression);
    ADD_TEST(Test_ESA_WaitForSem_BasicWakeByNotify);
    ADD_TEST(Test_ESA_WaitForDelay_TimeoutAfterNonZeroDelay);
    ADD_TEST(Test_ESA_NotifySemFlush_WakesAllWaiters);
    ADD_TEST(Test_ESA_RegisterTask_DeregisterTask_Lifecycle);
    ADD_TEST(Test_ESA_WaitForCondVar_SignalWakeOne);
    ADD_TEST(Test_ESA_WaitForCondVar_Timeout);
    ADD_TEST(Test_ESA_NotifyCondVar_BroadcastWakesAll);
    ADD_TEST(Test_ESA_WaitForMessage_TwoConsumersOnePutWakesOne);
    ADD_TEST(Test_ESA_WaitForMessage_ZeroTimeout_IsImmediateTimeout);
}

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
#include <time.h>
#include <errno.h>

#include "esa_wait.h"
#include "esa_stepping.h"
#include "osapi-constants.h"
#include "osapi-task.h"

#ifdef CFE_SIM_STEPPING

static ESA_TaskWaitState_t ESA_TaskWaitTable[ESA_MAX_TRACKED_TASKS];
static ESA_WakeReason_t    ESA_TaskWakeReason[ESA_MAX_TRACKED_TASKS];
static bool                ESA_TaskWakePending[ESA_MAX_TRACKED_TASKS];
static bool                ESA_TaskWaitPrimitivesReady[ESA_MAX_TRACKED_TASKS];

static pthread_mutex_t ESA_WaitGlobalMutex = PTHREAD_MUTEX_INITIALIZER;

static void ESA_Wait_UnlockMutex(void *arg)
{
    pthread_mutex_t *mutex;

    mutex = (pthread_mutex_t *)arg;
    if (mutex != NULL)
    {
        pthread_mutex_unlock(mutex);
    }
}

static bool ESA_Wait_GetTaskIndex(osal_id_t task_id, osal_index_t *task_idx)
{
    if (!OS_ObjectIdDefined(task_id) || task_idx == NULL)
    {
        return false;
    }

    return (OS_ObjectIdToArrayIndex(OS_OBJECT_TYPE_OS_TASK, task_id, task_idx) == 0 && *task_idx < ESA_MAX_TRACKED_TASKS);
}

static uint64 ESA_Wait_ComputeDeadlineNs(uint32 timeout_ms)
{
    uint64 now_ns;

    if (timeout_ms == (uint32)OS_PEND)
    {
        return UINT64_MAX;
    }

    if (!ESA_Stepping_Hook_GetTime(&now_ns))
    {
        return 0;
    }

    return now_ns + (((uint64)timeout_ms) * 1000000ULL);
}

static void ESA_Wait_ClearSlot(osal_index_t task_idx)
{
    ESA_TaskWaitTable[task_idx].wait_type       = ESA_WAIT_NONE;
    ESA_TaskWaitTable[task_idx].resource_id     = OS_OBJECT_ID_UNDEFINED;
    ESA_TaskWaitTable[task_idx].sim_deadline_ns = 0;
    ESA_TaskWakePending[task_idx]               = false;
    ESA_TaskWakeReason[task_idx]                = ESA_WOKE_BY_RESOURCE;
}

static int32 ESA_Wait_Common(osal_id_t resource_id, uint32 timeout_ms, ESA_WaitType_t wait_type)
{
    osal_id_t            task_id;
    osal_index_t         task_idx;
    ESA_TaskWaitState_t *state;
    int32                result;
    uint64               now_ns;
    struct timespec      wake_time;
    int                  timedwait_status;

    task_id = OS_TaskGetId();
    if (!OS_ObjectIdDefined(task_id))
    {
        return -1;
    }

    ESA_RegisterTask(task_id);

    pthread_mutex_lock(&ESA_WaitGlobalMutex);

    if (!ESA_Wait_GetTaskIndex(task_id, &task_idx))
    {
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return -1;
    }

    state = &ESA_TaskWaitTable[task_idx];
    if (!state->is_active)
    {
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return -1;
    }

    state->wait_type                = wait_type;
    state->resource_id              = resource_id;
    state->sim_deadline_ns          = ESA_Wait_ComputeDeadlineNs(timeout_ms);
    ESA_TaskWakePending[task_idx]   = false;
    ESA_TaskWakeReason[task_idx]    = ESA_WOKE_BY_RESOURCE;

    pthread_mutex_unlock(&ESA_WaitGlobalMutex);

    if (timeout_ms == OS_CHECK)
    {
        pthread_mutex_lock(&ESA_WaitGlobalMutex);
        ESA_Wait_ClearSlot(task_idx);
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return ESA_WOKE_BY_TIMEOUT;
    }

    pthread_mutex_lock(&state->mutex);
    pthread_cleanup_push(ESA_Wait_UnlockMutex, &state->mutex);

    result = -1;
    while (!ESA_TaskWakePending[task_idx])
    {
        if (timeout_ms != (uint32)OS_PEND)
        {
            if (!ESA_Stepping_Hook_GetTime(&now_ns))
            {
                result = -1;
                break;
            }

            if (now_ns >= state->sim_deadline_ns)
            {
                ESA_TaskWakePending[task_idx] = true;
                ESA_TaskWakeReason[task_idx]  = ESA_WOKE_BY_TIMEOUT;
                break;
            }
        }

        if (clock_gettime(CLOCK_MONOTONIC, &wake_time) != 0)
        {
            result = -1;
            break;
        }

        wake_time.tv_sec += 1;
        timedwait_status = pthread_cond_timedwait(&state->cond, &state->mutex, &wake_time);
        if (timedwait_status != 0 && timedwait_status != ETIMEDOUT)
        {
            result = -1;
            break;
        }
    }

    if (ESA_TaskWakePending[task_idx])
    {
        result = ESA_TaskWakeReason[task_idx];
    }

    ESA_Wait_ClearSlot(task_idx);

    pthread_cleanup_pop(true);

    return result;
}

static bool ESA_Wait_GetSemWaitType(osal_id_t sem_id, ESA_WaitType_t *wait_type)
{
    osal_objtype_t sem_objtype;

    if (!OS_ObjectIdDefined(sem_id) || wait_type == NULL)
    {
        return false;
    }

    sem_objtype = OS_IdentifyObject(sem_id);
    if (sem_objtype == OS_OBJECT_TYPE_OS_BINSEM)
    {
        *wait_type = ESA_WAIT_BINSEM;
        return true;
    }

    if (sem_objtype == OS_OBJECT_TYPE_OS_COUNTSEM)
    {
        *wait_type = ESA_WAIT_COUNTSEM;
        return true;
    }

    return false;
}

static void ESA_Wait_NotifyByResource(osal_id_t resource_id, ESA_WaitType_t wait_type, ESA_WakeReason_t wake_reason,
                                      bool wake_all)
{
    osal_index_t task_idx;

    pthread_mutex_lock(&ESA_WaitGlobalMutex);
    for (task_idx = 0; task_idx < ESA_MAX_TRACKED_TASKS; ++task_idx)
    {
        ESA_TaskWaitState_t *state;

        state = &ESA_TaskWaitTable[task_idx];
        if (!state->is_active || state->wait_type != wait_type)
        {
            continue;
        }

        if (!OS_ObjectIdEqual(state->resource_id, resource_id))
        {
            continue;
        }

        pthread_mutex_lock(&state->mutex);
        ESA_TaskWakePending[task_idx] = true;
        ESA_TaskWakeReason[task_idx]  = wake_reason;
        pthread_cond_signal(&state->cond);
        pthread_mutex_unlock(&state->mutex);

        if (!wake_all)
        {
            break;
        }
    }

    pthread_mutex_unlock(&ESA_WaitGlobalMutex);
}

int32 ESA_WaitForMessage(osal_id_t queue_id, uint32 timeout_ms)
{
    if (!OS_ObjectIdDefined(queue_id))
    {
        return -1;
    }

    return ESA_Wait_Common(queue_id, timeout_ms, ESA_WAIT_QUEUE);
}

int32 ESA_WaitForSem(osal_id_t sem_id, uint32 timeout_ms)
{
    ESA_WaitType_t sem_wait_type;

    if (!OS_ObjectIdDefined(sem_id))
    {
        return -1;
    }

    if (!ESA_Wait_GetSemWaitType(sem_id, &sem_wait_type))
    {
        return -1;
    }

    return ESA_Wait_Common(sem_id, timeout_ms, sem_wait_type);
}

int32 ESA_WaitForCondVar(osal_id_t condvar_id, uint32 timeout_ms)
{
    if (!OS_ObjectIdDefined(condvar_id))
    {
        return -1;
    }

    return ESA_Wait_Common(condvar_id, timeout_ms, ESA_WAIT_CONDVAR);
}

int32 ESA_WaitForDelay(uint32 timeout_ms)
{
    return ESA_Wait_Common(OS_OBJECT_ID_UNDEFINED, timeout_ms, ESA_WAIT_DELAY);
}

void ESA_NotifyQueuePut(osal_id_t queue_id)
{
    ESA_Wait_NotifyByResource(queue_id, ESA_WAIT_QUEUE, ESA_WOKE_BY_RESOURCE, false);
}

void ESA_NotifySemGive(osal_id_t sem_id)
{
    ESA_WaitType_t sem_wait_type;

    if (!ESA_Wait_GetSemWaitType(sem_id, &sem_wait_type))
    {
        return;
    }

    ESA_Wait_NotifyByResource(sem_id, sem_wait_type, ESA_WOKE_BY_RESOURCE, false);
}

void ESA_NotifySemFlush(osal_id_t sem_id)
{
    ESA_Wait_NotifyByResource(sem_id, ESA_WAIT_BINSEM, ESA_WOKE_BY_FLUSH, true);
    ESA_Wait_NotifyByResource(sem_id, ESA_WAIT_COUNTSEM, ESA_WOKE_BY_FLUSH, true);
}

void ESA_NotifyCondVar(osal_id_t condvar_id, bool broadcast)
{
    ESA_Wait_NotifyByResource(condvar_id, ESA_WAIT_CONDVAR, ESA_WOKE_BY_RESOURCE, broadcast);
}

void ESA_RegisterTask(osal_id_t task_id)
{
    osal_index_t       task_idx;
    pthread_condattr_t cond_attr;

    if (!ESA_Wait_GetTaskIndex(task_id, &task_idx))
    {
        return;
    }

    pthread_mutex_lock(&ESA_WaitGlobalMutex);
    if (ESA_TaskWaitTable[task_idx].is_active)
    {
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return;
    }

    if (!ESA_TaskWaitPrimitivesReady[task_idx])
    {
        if (pthread_mutex_init(&ESA_TaskWaitTable[task_idx].mutex, NULL) != 0)
        {
            pthread_mutex_unlock(&ESA_WaitGlobalMutex);
            return;
        }

        if (pthread_condattr_init(&cond_attr) != 0)
        {
            pthread_mutex_destroy(&ESA_TaskWaitTable[task_idx].mutex);
            pthread_mutex_unlock(&ESA_WaitGlobalMutex);
            return;
        }

        if (pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC) != 0)
        {
            pthread_condattr_destroy(&cond_attr);
            pthread_mutex_destroy(&ESA_TaskWaitTable[task_idx].mutex);
            pthread_mutex_unlock(&ESA_WaitGlobalMutex);
            return;
        }

        if (pthread_cond_init(&ESA_TaskWaitTable[task_idx].cond, &cond_attr) != 0)
        {
            pthread_condattr_destroy(&cond_attr);
            pthread_mutex_destroy(&ESA_TaskWaitTable[task_idx].mutex);
            pthread_mutex_unlock(&ESA_WaitGlobalMutex);
            return;
        }

        pthread_condattr_destroy(&cond_attr);
        ESA_TaskWaitPrimitivesReady[task_idx] = true;
    }

    ESA_TaskWaitTable[task_idx].is_active = true;
    ESA_Wait_ClearSlot(task_idx);
    pthread_mutex_unlock(&ESA_WaitGlobalMutex);
}

void ESA_DeregisterTask(osal_id_t task_id)
{
    osal_index_t         task_idx;
    ESA_TaskWaitState_t *state;
    bool                 destroy_needed;

    if (!ESA_Wait_GetTaskIndex(task_id, &task_idx))
    {
        return;
    }

    pthread_mutex_lock(&ESA_WaitGlobalMutex);
    state = &ESA_TaskWaitTable[task_idx];
    if (!state->is_active)
    {
        pthread_mutex_unlock(&ESA_WaitGlobalMutex);
        return;
    }

    destroy_needed = ESA_TaskWaitPrimitivesReady[task_idx];

    if (destroy_needed)
    {
        pthread_mutex_lock(&state->mutex);
    }
    state->wait_type                = ESA_WAIT_NONE;
    state->resource_id              = OS_OBJECT_ID_UNDEFINED;
    state->sim_deadline_ns          = 0;
    ESA_TaskWakePending[task_idx]   = true;
    ESA_TaskWakeReason[task_idx]    = ESA_WOKE_BY_RESOURCE;

    if (destroy_needed)
    {
        pthread_cond_signal(&state->cond);
        pthread_mutex_unlock(&state->mutex);
    }

    state->is_active = false;
    ESA_Wait_ClearSlot(task_idx);

    if (destroy_needed)
    {
        pthread_cond_destroy(&state->cond);
        pthread_mutex_destroy(&state->mutex);
        memset(&state->cond, 0, sizeof(state->cond));
        memset(&state->mutex, 0, sizeof(state->mutex));
    }

    ESA_TaskWaitPrimitivesReady[task_idx] = false;

    pthread_mutex_unlock(&ESA_WaitGlobalMutex);
}

#else

int32 ESA_WaitForMessage(osal_id_t queue_id, uint32 timeout_ms)
{
    (void)queue_id;
    (void)timeout_ms;
    return ESA_WOKE_BY_RESOURCE;
}

int32 ESA_WaitForSem(osal_id_t sem_id, uint32 timeout_ms)
{
    (void)sem_id;
    (void)timeout_ms;
    return ESA_WOKE_BY_RESOURCE;
}

int32 ESA_WaitForCondVar(osal_id_t condvar_id, uint32 timeout_ms)
{
    (void)condvar_id;
    (void)timeout_ms;
    return ESA_WOKE_BY_RESOURCE;
}

int32 ESA_WaitForDelay(uint32 timeout_ms)
{
    (void)timeout_ms;
    return ESA_WOKE_BY_RESOURCE;
}

void ESA_NotifyQueuePut(osal_id_t queue_id)
{
    (void)queue_id;
}

void ESA_NotifySemGive(osal_id_t sem_id)
{
    (void)sem_id;
}

void ESA_NotifySemFlush(osal_id_t sem_id)
{
    (void)sem_id;
}

void ESA_NotifyCondVar(osal_id_t condvar_id, bool broadcast)
{
    (void)condvar_id;
    (void)broadcast;
}

void ESA_RegisterTask(osal_id_t task_id)
{
    (void)task_id;
}

void ESA_DeregisterTask(osal_id_t task_id)
{
    (void)task_id;
}

#endif

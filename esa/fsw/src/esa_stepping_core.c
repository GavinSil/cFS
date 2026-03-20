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
 * \ingroup psp
 *
 * Purpose: Implementation of the simulation stepping core state machine.
 *
 * This module owns all stepping semantics for the native-only stepping core.
 * It maintains the state machine, trigger tracking, and simulated time.
 * All hooks report facts into this core; the core maintains state transitions
 * and determines when stepping should trigger.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "common_types.h"
#include "esa_stepping.h"
#include "esa_stepping_core.h"

/****************************************************************************************
                            FORWARD DECLARATIONS & PRIVATE STATE
 ****************************************************************************************/

static uint32_t ESA_Stepping_AddTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask,
                                                uint32_t entity_id);
static void ESA_Stepping_ClearTriggers(ESA_Stepping_Core_t *core);
static uint32_t ESA_Stepping_AcknowledgeTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask,
                                                       uint32_t entity_id);
static bool ESA_Stepping_HasTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask,
                                           uint32_t entity_id);
static bool ESA_Stepping_Core_IsStepComplete_ReadOnly(ESA_Stepping_Core_t *core);
static bool ESA_Stepping_HasTaskDelayDebt(ESA_Stepping_Core_t *core);
static const char *ESA_Stepping_DiagClassToString(ESA_Stepping_DiagnosticClass_t diag_class);

/****************************************************************************************
                               PRIVATE HELPER IMPLEMENTATIONS
 ****************************************************************************************/

static uint32_t ESA_Stepping_AddTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask,
                                               uint32_t entity_id)
{
    if (core->trigger_count >= ESA_SIM_STEPPING_MAX_TRIGGERS)
    {
        return 0;
    }

    ESA_Stepping_Trigger_t *trigger = &core->triggers[core->trigger_count];
    trigger->trigger_id      = core->next_trigger_id++;
    trigger->source_mask     = source_mask;
    trigger->entity_id       = entity_id;
    trigger->is_acknowledged = false;

    core->trigger_count++;
    core->acks_expected++;

    return trigger->trigger_id;
}

static void ESA_Stepping_ClearTriggers(ESA_Stepping_Core_t *core)
{
    memset(core->triggers, 0, ESA_SIM_STEPPING_MAX_TRIGGERS * sizeof(ESA_Stepping_Trigger_t));
    core->trigger_count        = 0;
    core->acks_received        = 0;
    core->acks_expected        = 0;
    core->completion_requested = false;
    core->completion_ready     = false;
    core->core_service_membership_mask = 0;
}

static uint32_t ESA_Stepping_AcknowledgeTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask,
                                                       uint32_t entity_id)
{
    uint32_t i;

    if (core == NULL)
    {
        return 0;
    }

    for (i = 0; i < core->trigger_count; i++)
    {
        ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
        if (trigger->source_mask == source_mask && trigger->entity_id == entity_id)
        {
            if (!trigger->is_acknowledged)
            {
                /* Mark as acknowledged and increment acks_received count */
                trigger->is_acknowledged = true;
                core->acks_received++;
                return trigger->trigger_id;
            }
        }
    }

    /* No matching trigger found */
    return 0;
}

static bool ESA_Stepping_HasTrigger(ESA_Stepping_Core_t *core, uint32_t source_mask,
                                           uint32_t entity_id)
{
    uint32_t i;

    if (core == NULL)
    {
        return false;
    }

    for (i = 0; i < core->trigger_count; i++)
    {
        ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
        if (trigger->source_mask == source_mask && trigger->entity_id == entity_id)
        {
            return true;
        }
    }

    return false;
}

static bool ESA_Stepping_HasTaskDelayDebt(ESA_Stepping_Core_t *core)
{
    uint32_t i;

    if (core == NULL)
    {
        return false;
    }

    for (i = 0; i < core->taskdelay_optin_count; i++)
    {
        if (core->taskdelay_pending[i] && core->sim_time_ns >= core->taskdelay_expiry_ns[i])
        {
            core->taskdelay_owed[i] = true;
        }

        if (core->taskdelay_owed[i])
        {
            return true;
        }
    }

    return false;
}

static const char *ESA_Stepping_DiagClassToString(ESA_Stepping_DiagnosticClass_t diag_class)
{
    switch (diag_class)
    {
        case ESA_SIM_STEPPING_DIAG_TIMEOUT:
            return "timeout";
        case ESA_SIM_STEPPING_DIAG_DUPLICATE_BEGIN:
            return "duplicate_begin";
        case ESA_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE:
            return "illegal_complete";
        case ESA_SIM_STEPPING_DIAG_ILLEGAL_STATE:
            return "illegal_state";
        case ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR:
            return "transport_error";
        case ESA_SIM_STEPPING_DIAG_PROTOCOL_ERROR:
            return "protocol_error";
        default:
            return "unknown";
    }
}

/****************************************************************************************
                               PUBLIC CORE API FUNCTIONS
 ****************************************************************************************/

int32_t ESA_Stepping_Core_Init(ESA_Stepping_Core_t *core,
                                       uint64_t                   initial_time_ns,
                                       uint32_t                   trigger_capacity)
{
    if (core == NULL)
    {
        return -1;
    }

    memset(core, 0, sizeof(ESA_Stepping_Core_t));

    core->current_state    = ESA_SIM_STEPPING_STATE_READY;
    core->sim_time_ns      = initial_time_ns;
    core->next_sim_time_ns = initial_time_ns;
    core->step_quantum_ns  = 10000000;  /* Default: 10 ms quantum (scheduler minor frame cadence) */
    core->step_timeout_ms  = 5000;
    core->next_trigger_id  = 1;
    core->taskdelay_takeover_enabled = false;  /* Default: TaskDelay takeover disabled */
    core->taskdelay_optin_count = 0;           /* Default: no tasks opted-in */
    core->core_service_membership_mask = 0;    /* Default: no services in current-step membership */
    core->session_active   = false;            /* Default: no session active until BeginStepSession called */
    core->session_counter  = 0;                /* Default: session counter starts at 0 */
    core->system_ready_for_stepping = false;   /* Default: system not ready until ES signals lifecycle readiness (T11c) */

    return 0;
}

int32_t ESA_Stepping_Core_Reset(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    ESA_Stepping_ClearTriggers(core);
    core->current_state = ESA_SIM_STEPPING_STATE_READY;

    return 0;
}

int32_t ESA_Stepping_Core_BeginStepSession(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    /* Pre-ready guard: reject begin-step requests until system has signaled lifecycle readiness (T11c).
       System must emit lifecycle-ready event (via shim) and call MarkSystemReadyForStepping before
       any begin-step requests will be accepted. */
    if (!core->system_ready_for_stepping)
    {
        return -3;
    }

    /* Reject duplicate BEGIN_STEP if prior session is still unresolved.
       Use ReadOnly check to avoid accidental empty-session completion via mutation. */
    if (core->session_active && !ESA_Stepping_Core_IsStepComplete_ReadOnly(core))
     {
         return ESA_Stepping_Core_RecordDiagnostic(core, ESA_SIM_STEPPING_DIAG_DUPLICATE_BEGIN,
                                                          ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN,
                                                          "BeginStepSession",
                                                          (uint32_t)core->session_counter,
                                                          (uint32_t)core->acks_expected);
     }

    ESA_Stepping_ClearTriggers(core);
    core->session_active = true;
    core->session_counter++;
    
    /* Fresh session always begins in READY state to allow trigger accumulation.
       Empty-session completion is deferred until the controller explicitly waits/checks
       (via IsStepComplete); see deferred-completion rule in IsStepComplete. */
    core->current_state = ESA_SIM_STEPPING_STATE_READY;

    return 0;
}

int32_t ESA_Stepping_Core_ReportTaskDelay(ESA_Stepping_Core_t *core,
                                                   uint32_t                   task_id,
                                                   uint32_t                   delay_ms)
{
    uint32_t i;
    uint32_t optin_index;
    bool     already_registered;
    bool     tracked;

    if (core == NULL)
    {
        return -1;
    }

    if (core->system_ready_for_stepping)
    {
        already_registered = false;
        tracked = false;
        optin_index = 0;
        for (i = 0; i < core->taskdelay_optin_count; i++)
        {
            if (core->taskdelay_optin_set[i] == task_id)
            {
                already_registered = true;
                tracked = true;
                optin_index = i;
                break;
            }
        }

        if (!already_registered && core->taskdelay_optin_count < 8)
        {
            core->taskdelay_optin_set[core->taskdelay_optin_count] = task_id;
            optin_index = core->taskdelay_optin_count;
            core->taskdelay_optin_count++;
            tracked = true;
        }

        if (tracked)
        {
            if (ESA_Stepping_Core_QueryTaskDelayEligible(core, task_id, delay_ms))
            {
                core->taskdelay_pending[optin_index] = true;
                core->taskdelay_owed[optin_index] = false;
                core->taskdelay_expiry_ns[optin_index] = core->sim_time_ns + (((uint64_t)delay_ms) * 1000000);
            }
            else
            {
                core->taskdelay_pending[optin_index] = false;
                core->taskdelay_owed[optin_index] = false;
                core->taskdelay_expiry_ns[optin_index] = 0;
            }
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportTaskDelayAck(ESA_Stepping_Core_t *core,
                                                     uint32_t                   task_id,
                                                     uint32_t                   delay_ms)
{
    (void)delay_ms;

    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        if (ESA_Stepping_HasTrigger(core, 0x100, task_id))
        {
            return 0;
        }

        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x100, task_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportTaskDelayComplete(ESA_Stepping_Core_t *core,
                                                          uint32_t                   task_id,
                                                          uint32_t                   delay_ms)
{
    (void)delay_ms;

    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        uint32_t trigger_id = ESA_Stepping_AcknowledgeTrigger(core, 0x100, task_id);
        if (trigger_id == 0)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportTaskDelayReturn(ESA_Stepping_Core_t *core,
                                                        uint32_t                   task_id)
{
    uint32_t i;

    if (core == NULL)
    {
        return -1;
    }

    for (i = 0; i < core->taskdelay_optin_count; i++)
    {
        if (core->taskdelay_optin_set[i] == task_id)
        {
            core->taskdelay_pending[i] = false;
            core->taskdelay_owed[i] = false;
            core->taskdelay_expiry_ns[i] = 0;
            break;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportQueueReceive(ESA_Stepping_Core_t *core,
                                                    uint32_t                   queue_id,
                                                    uint32_t                   timeout_ms)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        ESA_Stepping_AddTrigger(core, 0x02, queue_id);
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportBinSemTake(ESA_Stepping_Core_t *core,
                                                  uint32_t                   sem_id,
                                                  uint32_t                   timeout_ms)
{
    if (core == NULL)
    {
        return -1;
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportTimeTaskCycle(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    /* Non-blocking fact hook: TIME main-task cycle is reported but does not create a blocking trigger.
     * The TIME main task is already represented in the wait-set via paired core-service
     * receive/completion facts (service id 0x02), so task-cycle reporting is informational only.
     */

    return 0;
}

int32_t ESA_Stepping_Core_Report1HzBoundary(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        /* Completion-style reporter: acknowledge all outstanding TIME local-1Hz child-path triggers (0x20000) */
        uint32_t trigger_id = 0;
        uint32_t i;
        bool any_acknowledged = false;

        for (i = 0; i < core->trigger_count; i++)
        {
            ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
            if (trigger->source_mask == 0x20000 && !trigger->is_acknowledged)
            {
                trigger->is_acknowledged = true;
                core->acks_received++;
                any_acknowledged = true;
                trigger_id = trigger->trigger_id;
            }
        }

        /* If no local-1Hz child-path triggers were acknowledged, add 1Hz-boundary hook trigger (fallback) */
        if (!any_acknowledged)
        {
            return 0;
        }

        /* Conditional READY→RUNNING transition */
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportToneSignal(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        /* Acknowledge all outstanding TIME tone child-path triggers (0x10000) */
        uint32_t trigger_id = 0;
        uint32_t i;
        bool any_acknowledged = false;

        for (i = 0; i < core->trigger_count; i++)
        {
            ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
            if (trigger->source_mask == 0x10000 && !trigger->is_acknowledged)
            {
                /* Mark as acknowledged and increment acks_received count */
                trigger->is_acknowledged = true;
                core->acks_received++;
                any_acknowledged = true;
                trigger_id = trigger->trigger_id;  /* Track last acknowledged for state transition */
            }
        }

        /* If no tone child-path triggers were acknowledged, add tone-signal hook trigger (fallback) */
        if (!any_acknowledged)
        {
            return 0;
        }

        /* Conditional READY→RUNNING transition */
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportSchSemaphoreWait(ESA_Stepping_Core_t *core,
                                                        uint32_t                   sem_id,
                                                        uint32_t                   timeout_ms)
{
    if (core == NULL)
    {
        return -1;
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportSchMinorFrame(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (!core->session_active)
    {
        return 0;
    }

    if (core->completion_ready)
    {
        return 0;
    }

    int32_t adv_status = ESA_Stepping_Core_AdvanceOneQuantum(core);
    if (adv_status != 0)
    {
        return adv_status;
    }

    core->completion_ready = true;

    if (core->current_state == ESA_SIM_STEPPING_STATE_READY)
    {
        core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportSchMajorFrame(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportSchSendTrigger(ESA_Stepping_Core_t *core,
                                                       uint32_t                   target_id)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x2000, target_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportSchDispatchComplete(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active)
    {
        /* Completion-style reporter: acknowledge all outstanding SCH send triggers (0x2000) */
        uint32_t trigger_id = 0;
        uint32_t i;
        bool any_acknowledged = false;

        for (i = 0; i < core->trigger_count; i++)
        {
            ESA_Stepping_Trigger_t *trigger = &core->triggers[i];
            if (trigger->source_mask == 0x2000 && !trigger->is_acknowledged)
            {
                /* Mark as acknowledged and increment acks_received count */
                trigger->is_acknowledged = true;
                core->acks_received++;
                any_acknowledged = true;
                trigger_id = trigger->trigger_id;  /* Track last acknowledged for state transition */
            }
        }

        /* If no SCH send triggers were acknowledged, create new completion trigger (backward compat) */
        if (!any_acknowledged)
        {
            return 0;
        }

        /* Conditional READY→RUNNING transition */
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }

        /* Observability marker: scheduler complete without system complete */
        if (any_acknowledged && core->current_state != ESA_SIM_STEPPING_STATE_COMPLETE)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_WAITING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportCoreServiceCmdPipeReceive(ESA_Stepping_Core_t *core,
                                                                   uint32_t                   service_id)
{
    uint32_t service_bit;

    if (core == NULL)
    {
        return -1;
    }

    /* Map service_id to bitmask bit for membership tracking */
    if (service_id < 5)
    {
        service_bit = (1U << service_id);
        core->core_service_membership_mask |= service_bit;
    }

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x8000, service_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

     return 0;
 }

  int32_t ESA_Stepping_Core_ReportCoreServiceCmdPipeComplete(ESA_Stepping_Core_t *core,
                                                                     uint32_t                   service_id)
  {
      if (core == NULL)
      {
          return -1;
      }

       if (core->session_active)
       {
           /* Completion-style reporter: try to acknowledge existing core-service receive trigger first */
            uint32_t trigger_id = ESA_Stepping_AcknowledgeTrigger(core, 0x8000, service_id);
            if (trigger_id == 0)
             {
                return ESA_Stepping_Core_RecordDiagnostic(core,
                                                                 ESA_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE,
                                                                 ESA_SIM_STEPPING_STATUS_ILLEGAL_COMPLETE,
                                                                 "CoreServiceCmdPipeComplete",
                                                                 service_id,
                                                                 0);
             }
          /* Conditional READY→RUNNING transition */
          if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
          {
              core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
          }
      }

      return 0;
  }

 int32_t ESA_Stepping_Core_ReportTimeToneSemConsume(ESA_Stepping_Core_t *core,
                                                             uint32_t                   sem_id)
{
    if (core == NULL)
    {
        return -1;
    }

    /* Record TIME tone child-path participation in current step */
    core->core_service_membership_mask |= ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_TONE;

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x10000, sem_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportTimeLocal1HzSemConsume(ESA_Stepping_Core_t *core,
                                                                uint32_t                   sem_id)
{
    if (core == NULL)
    {
        return -1;
    }

    /* Record TIME local-1Hz child-path participation in current step */
    core->core_service_membership_mask |= ESA_SIM_STEPPING_CHILDPATH_BIT_TIME_LOCAL_1HZ;

    if (core->session_active)
    {
        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x20000, sem_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_QuerySimTime(ESA_Stepping_Core_t *core,
                                                  uint64_t                   *sim_time_ns)
{
    if (core == NULL || sim_time_ns == NULL)
    {
        return -1;
    }

    *sim_time_ns = core->sim_time_ns;

    return 0;
}

int32_t ESA_Stepping_Core_ReportQueueReceiveAck(ESA_Stepping_Core_t *core,
                                                        uint32_t                   task_id,
                                                        uint32_t                   queue_id,
                                                        uint32_t                   timeout_ms)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        if (ESA_Stepping_HasTrigger(core, 0x200, queue_id))
        {
            return 0;
        }

        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x200, queue_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportQueueReceiveComplete(ESA_Stepping_Core_t *core,
                                                             uint32_t                   task_id,
                                                             uint32_t                   queue_id,
                                                             uint32_t                   timeout_ms)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        uint32_t trigger_id = ESA_Stepping_AcknowledgeTrigger(core, 0x200, queue_id);
        if (trigger_id == 0)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;  /* Success: matched queue-complete path */
}

bool ESA_Stepping_Core_QueryTaskDelayEligible(ESA_Stepping_Core_t *core,
                                                     uint32_t                   task_id,
                                                     uint32_t                   delay_ms)
{
    uint64_t delay_ns;
    uint64_t remainder_ns;
    uint32_t i;
    bool     task_opted_in;

    /* Conservative gate 1: core must be initialized and valid */
    if (core == NULL)
    {
        return false;
    }

    /* Conservative gate 2: system must be ready for stepping (startup/runtime lifecycle) */
    /* This gate prevents takeover during startup synchronization paths (e.g., CFE_ES_GetTaskFunction),
     * allowing those delays to use wall-clock sleep to avoid deadlock. Once ES signals
     * system_ready_for_stepping, runtime delays become eligible for step-control takeover. */
    if (!core->system_ready_for_stepping)
    {
        return false;
    }

    /* Conservative gate 3: TaskDelay takeover must be enabled */
    if (!core->taskdelay_takeover_enabled)
    {
        return false;
    }

    if (core->session_counter == 0)
    {
        return false;
    }

    /* Conservative gate 4: task must be explicitly opted-in */
    task_opted_in = false;
    for (i = 0; i < core->taskdelay_optin_count; i++)
    {
        if (core->taskdelay_optin_set[i] == task_id)
        {
            task_opted_in = true;
            break;
        }
    }
    if (!task_opted_in)
    {
        return false;
    }

    /* Conservative gate 5: delay must be exact whole-number multiple of quantum */
    /* Convert delay_ms to nanoseconds (1 ms = 1,000,000 ns) */
    delay_ns = ((uint64_t)delay_ms) * 1000000;

    /* Check if delay is exact multiple of quantum */
    remainder_ns = delay_ns % core->step_quantum_ns;
    if (remainder_ns != 0)
    {
        return false;
    }

    /* All conservative checks passed */
    return true;
}

int32_t ESA_Stepping_Core_AdvanceOneQuantum(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->step_quantum_ns == 0)
    {
        return -1;
    }

    core->sim_time_ns += core->step_quantum_ns;
    core->next_sim_time_ns += core->step_quantum_ns;

    return 0;
}

int32_t ESA_Stepping_Core_WaitForDelayExpiry(ESA_Stepping_Core_t *core,
                                                     uint32_t                   task_id,
                                                     uint32_t                   delay_ms)
{
    uint64_t        target_expiry_ns;
    struct timespec poll_interval;

    if (core == NULL)
    {
        return -1;
    }

    target_expiry_ns = core->sim_time_ns + (((uint64_t)delay_ms) * 1000000);

    poll_interval.tv_sec  = 0;
    poll_interval.tv_nsec = 1000000;

    while (core->sim_time_ns < target_expiry_ns)
    {
        nanosleep(&poll_interval, NULL);
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportBinSemTakeAck(ESA_Stepping_Core_t *core,
                                                      uint32_t                   task_id,
                                                      uint32_t                   sem_id,
                                                      uint32_t                   timeout_ms)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        if (ESA_Stepping_HasTrigger(core, 0x800, sem_id))
        {
            return 0;
        }

        uint32_t trigger_id = ESA_Stepping_AddTrigger(core, 0x800, sem_id);
        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

int32_t ESA_Stepping_Core_ReportBinSemTakeComplete(ESA_Stepping_Core_t *core,
                                                           uint32_t                   task_id,
                                                           uint32_t                   sem_id,
                                                           uint32_t                   timeout_ms)
{
    if (core == NULL)
    {
        return -1;
    }

    if (core->session_active && core->completion_ready)
    {
        uint32_t trigger_id = ESA_Stepping_AcknowledgeTrigger(core, 0x800, sem_id);
        if (trigger_id == 0)
        {
            return 0;
        }

        if (trigger_id > 0 && core->current_state == ESA_SIM_STEPPING_STATE_READY)
        {
            core->current_state = ESA_SIM_STEPPING_STATE_RUNNING;
        }
    }

    return 0;
}

/**
 * \brief Internal helper: Query stepping core state for in-process adapter
 */
int32_t ESA_Stepping_Core_QueryState(ESA_Stepping_Core_t *core,
                                            ESA_Stepping_CoreState_t *state_out)
{
    if (core == NULL)
    {
        return -1;
    }

    if (state_out != NULL)
    {
        *state_out = core->current_state;
    }

    return 0;
}

/**
 * \brief Internal helper: Non-mutating completion check (for duplicate-begin guard)
 *
 * Checks if step is complete without side effects. Used by BeginStepSession() to
 * detect unresolved prior sessions, preventing accidental completion via duplicate-begin detection.
 */
static bool ESA_Stepping_Core_IsStepComplete_ReadOnly(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return false;
    }

    return (core->current_state == ESA_SIM_STEPPING_STATE_COMPLETE &&
            core->acks_received >= core->acks_expected);
}

/**
 * \brief Internal helper: Check if step is complete for in-process adapter polling
 *
 * Deferred empty-session completion: if completion has been explicitly requested
 * (via explicit wait/check path) AND acks_expected == 0 (empty session) and
 * current_state is READY, transition to COMPLETE to enable immediate wait success.
 */
bool ESA_Stepping_Core_IsStepComplete(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return false;
    }

    if (core->completion_requested && ESA_Stepping_HasTaskDelayDebt(core))
    {
        return false;
    }

    /* Deferred empty-session completion: only transition if completion was explicitly requested.
       This gate ensures duplicate-begin rejection does not implicitly complete empty sessions. */
    if (core->completion_requested && core->completion_ready && core->acks_expected == 0 &&
        core->current_state == ESA_SIM_STEPPING_STATE_RUNNING)
    {
        core->current_state = ESA_SIM_STEPPING_STATE_COMPLETE;
    }

    if (core->completion_requested && core->completion_ready && core->acks_expected > 0 &&
        core->acks_received >= core->acks_expected &&
        core->current_state != ESA_SIM_STEPPING_STATE_COMPLETE)
    {
        core->current_state = ESA_SIM_STEPPING_STATE_COMPLETE;
    }

    /* Step is complete when all expected acks received and core transitioned to COMPLETE */
    if (core->current_state == ESA_SIM_STEPPING_STATE_COMPLETE &&
        core->acks_received >= core->acks_expected)
    {
        core->session_active = false;
        return true;
    }

    return false;
}

/**
 * \brief Mark the system as ready for simulation stepping
 */
int32_t ESA_Stepping_Core_MarkSystemReadyForStepping(ESA_Stepping_Core_t *core)
{
    if (core == NULL)
    {
        return -1;
    }

    core->system_ready_for_stepping = true;
    core->taskdelay_takeover_enabled = true;

    return 0;
}

int32_t ESA_Stepping_Core_RecordDiagnostic(ESA_Stepping_Core_t *core,
                                                   ESA_Stepping_DiagnosticClass_t diag_class,
                                                   int32_t status,
                                                   const char *site,
                                                   uint32_t detail_a,
                                                   uint32_t detail_b)
{
    const char *class_name;

    if (core == NULL || site == NULL)
    {
        return ESA_SIM_STEPPING_STATUS_FAILURE;
    }

    switch (diag_class)
    {
        case ESA_SIM_STEPPING_DIAG_TIMEOUT:
            core->diagnostics.timeout_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_DUPLICATE_BEGIN:
            core->diagnostics.duplicate_begin_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE:
            core->diagnostics.illegal_complete_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_ILLEGAL_STATE:
            core->diagnostics.illegal_state_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_TRANSPORT_ERROR:
            core->diagnostics.transport_error_count++;
            break;
        case ESA_SIM_STEPPING_DIAG_PROTOCOL_ERROR:
            core->diagnostics.protocol_error_count++;
            break;
        default:
            break;
    }

    class_name = ESA_Stepping_DiagClassToString(diag_class);
    printf("CFE_PSP: SIM_STEPPING_DIAG class=%s status=%ld site=%s detail_a=%lu detail_b=%lu\n",
           class_name,
           (long)status,
           site,
           (unsigned long)detail_a,
           (unsigned long)detail_b);

    return status;
}

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
 *
 * Purpose: Mission-owned neutral native stepping fact-reporting ABI.
 *
 * This header declares a single thin fact-reporting entry point that OSAL/TIME/SCH
 * modules can call into. The shim forwards all event reports to the stepping core
 * state machine via a unified function, keeping hook layers thin and semantics
 * centralized.
 *
 * This is a mission-owned neutral location, separate from any PSP/OSAL/CFE-specific
 * dependencies. It declares only standard C types and plain ABI shapes.
 */

#ifndef NATIVE_STEPPING_SHIM_H
#define NATIVE_STEPPING_SHIM_H

/****************************************************************************************
                                        INCLUDE FILES
 ***************************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************************
                            STEPPING SHIM EVENT ENUM & TYPES
 ***************************************************************************************/

/**
 * \brief Enumeration of stepping event kinds
 *
 * Each event type represents a distinct boundary condition that may trigger
 * a stepping pause or synchronization point. Event kind determines which core
 * report function is invoked.
 */
typedef enum ESA_Stepping_EventKind
{
    ESA_SIM_STEPPING_EVENT_TASK_DELAY = 0,      /**< Task delay requested (OSAL) */
    ESA_SIM_STEPPING_EVENT_TASK_DELAY_ACK,
    ESA_SIM_STEPPING_EVENT_TASK_DELAY_COMPLETE,
    ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE,       /**< Queue receive blocking (OSAL) - legacy single-event */
    ESA_SIM_STEPPING_EVENT_BINSEM_TAKE,         /**< Binary semaphore take (OSAL) - legacy single-event */
    ESA_SIM_STEPPING_EVENT_TIME_TASK_CYCLE,     /**< TIME module task cycle start */
    ESA_SIM_STEPPING_EVENT_1HZ_BOUNDARY,        /**< 1Hz tick detected */
    ESA_SIM_STEPPING_EVENT_TONE_SIGNAL,         /**< Tone signal raised (PSP/SCH) */
    ESA_SIM_STEPPING_EVENT_SCH_SEMAPHORE_WAIT,  /**< SCH waiting on semaphore */
    ESA_SIM_STEPPING_EVENT_SCH_MINOR_FRAME,     /**< SCH minor frame boundary */
    ESA_SIM_STEPPING_EVENT_SCH_MAJOR_FRAME,     /**< SCH major frame boundary */
    ESA_SIM_STEPPING_EVENT_SCH_SEND_TRIGGER,
    ESA_SIM_STEPPING_EVENT_SCH_DISPATCH_COMPLETE,
    ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_RECEIVE,
    ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK,   /**< Queue receive ack (pre-blocking, wait candidate) */
    ESA_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE, /**< Queue receive complete (post-blocking, operation done) */
    ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK,     /**< Binary semaphore take ack (pre-wait, candidate) */
    ESA_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE, /**< Binary semaphore take complete (post-wait, done) */
    ESA_SIM_STEPPING_EVENT_TIME_TONE_SEM_CONSUME,
    ESA_SIM_STEPPING_EVENT_TIME_LOCAL_1HZ_SEM_CONSUME,
    ESA_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_COMPLETE,
    ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING /**< System lifecycle readiness: core init complete and ready to enter stepping mode */
} ESA_Stepping_EventKind_t;

/**
 * \brief Shim event payload (compact fact descriptor)
 *
 * Carries event-specific fact data. Payload fields are context-dependent on event_kind.
 * Keep structure compact for efficient forwarding.
 */
typedef struct ESA_Stepping_ShimEvent
{
    ESA_Stepping_EventKind_t event_kind;     /**< Event type (determines payload semantics) */
    uint32_t entity_id;                             /**< Entity ID (waited-on queue/semaphore/etc.) */
    uint32_t task_id;                               /**< Runtime task ID (current executing task) */
    uint32_t optional_delay_ms;                     /**< Optional: delay/timeout value in ms */
} ESA_Stepping_ShimEvent_t;

/****************************************************************************************
                               SHIM REPORT FUNCTION DECLARATION
 ***************************************************************************************/

/**
 * \brief Report a stepping event fact to the core
 *
 * Unified entry point for all OSAL/TIME/SCH modules to report native stepping events.
 * Function determines which core report function to invoke based on event_kind.
 * 
 * This is a thin forwarding layer: it validates the event, extracts relevant
 * fact parameters, and calls the appropriate core Report function.
 * All state machine semantics remain in the core.
 *
 * \param[in]  event  Pointer to stepping event descriptor
 *
 * \return 0 if event reported successfully; non-zero error code if report failed
 *         (e.g., core not initialized, invalid event_kind, core full)
 *
 * \note
 * Shim is implementation-independent. Implementation may be guarded by build flag;
 * when disabled, function becomes a no-op that returns 0.
 */
int32_t ESA_Stepping_Shim_ReportEvent(const ESA_Stepping_ShimEvent_t *event);

#endif /* NATIVE_STEPPING_SHIM_H */

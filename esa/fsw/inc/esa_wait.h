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
 * \ingroup  esa
 *
 * Purpose: ESA stepping-managed task wait state definitions.
 *
 * This header defines wait result codes, wait type enumeration, task wait state
 * structure, and configuration constants used by ESA stepping to manage deterministic
 * waits on queues, semaphores, condition variables, and delays. These structures
 * decouple wait mechanisms from wall-clock time.
 */

#ifndef ESA_WAIT_H
#define ESA_WAIT_H

/****************************************************************************************
                                        INCLUDE FILES
 ***************************************************************************************/

#include <stdbool.h>
#include <pthread.h>

#include "common_types.h"
#include "osapi-idmap.h"
#include "osconfig.h"

/****************************************************************************************
                                    WAIT RESULT CODE ENUMERATION
 ***************************************************************************************/

/**
 * \brief Enumeration of wait result codes
 *
 * Classifies the outcome of an ESA-managed wait operation, indicating whether the wait
 * was satisfied by resource availability, timeout, or flush operation.
 */
typedef enum ESA_WakeReason
{
    ESA_WOKE_BY_RESOURCE = 0,  /**< Wait ended by resource becoming available */
    ESA_WOKE_BY_TIMEOUT,       /**< Wait ended by timeout expiration */
    ESA_WOKE_BY_FLUSH          /**< Wait ended by semaphore flush operation */
} ESA_WakeReason_t;

/****************************************************************************************
                                      WAIT TYPE ENUMERATION
 ***************************************************************************************/

/**
 * \brief Enumeration of task wait types
 *
 * Classifies the type of synchronization mechanism that a task is waiting on.
 * Used by ESA stepping to determine which wait handler to invoke and how to
 * manage the transition from stepping control to normal execution.
 */
typedef enum ESA_WaitType
{
    ESA_WAIT_NONE = 0,      /**< No active wait (initial/idle state) */
    ESA_WAIT_QUEUE,         /**< Waiting on message queue receive */
    ESA_WAIT_BINSEM,        /**< Waiting on binary semaphore */
    ESA_WAIT_COUNTSEM,      /**< Waiting on counting semaphore */
    ESA_WAIT_CONDVAR,       /**< Waiting on condition variable */
    ESA_WAIT_DELAY          /**< Waiting on task delay */
} ESA_WaitType_t;

/****************************************************************************************
                             CONFIGURATION CONSTANTS (OSAL-ALIGNED)
 ***************************************************************************************/

/**
 * \brief Maximum number of tracked tasks
 *
 * Sized to match OS_MAX_TASKS from OSAL configuration.
 * Used for ESA wait state per-task registry.
 */
#define ESA_MAX_TRACKED_TASKS       OS_MAX_TASKS

/**
 * \brief Maximum number of tracked queues
 *
 * Sized to match OS_MAX_QUEUES from OSAL configuration.
 * Used for ESA wait state queue notification registry.
 */
#define ESA_MAX_TRACKED_QUEUES      OS_MAX_QUEUES

/**
 * \brief Maximum number of tracked semaphores
 *
 * Sized to accommodate both binary and counting semaphores.
 * Equals OS_MAX_BIN_SEMAPHORES + OS_MAX_COUNT_SEMAPHORES.
 */
#define ESA_MAX_TRACKED_SEMS        (OS_MAX_BIN_SEMAPHORES + OS_MAX_COUNT_SEMAPHORES)

/**
 * \brief Maximum number of tracked condition variables
 *
 * Sized to match OS_MAX_CONDVARS from OSAL configuration.
 * Used for ESA wait state condition variable notification registry.
 */
#define ESA_MAX_TRACKED_CONDVARS    OS_MAX_CONDVARS

/****************************************************************************************
                                    WAIT STATE STRUCTURE
 ***************************************************************************************/

/**
 * \brief Task wait state for ESA stepping
 *
 * Encapsulates the current wait state of a task, including the wait type,
 * OSAL resource ID, simulated time deadline, and pthread synchronization primitives
 * (mutex and condition variable) used by ESA for deterministic wait management.
 *
 * This structure is embedded in per-task state or maintained in a global
 * task wait registry to enable ESA stepping to correctly manage wait transitions.
 */
typedef struct ESA_TaskWaitState
{
    ESA_WaitType_t  wait_type;       /**< Type of wait (QUEUE, BINSEM, COUNTSEM, CONDVAR, DELAY) */
    osal_id_t       resource_id;     /**< OSAL resource ID (queue_id, sem_id, condvar_id) */
    uint64          sim_deadline_ns; /**< Simulated time deadline in nanoseconds */
    pthread_cond_t  cond;            /**< Condition variable for ESA-controlled wakeup */
    pthread_mutex_t mutex;           /**< Mutex protecting wait state transitions */
    bool            is_active;       /**< True if this wait slot is in use */
} ESA_TaskWaitState_t;

/****************************************************************************************
                              WEAK-SYMBOL WAIT FUNCTION DECLARATIONS
 ***************************************************************************************/

/**
 * \brief ESA-managed wait on message queue receive
 *
 * Weak-symbol function for stepping-managed queue receive waits.
 * When ESA stepping is enabled, this function may be overridden to provide
 * deterministic wait semantics. When ESA stepping is disabled, this function
 * is a no-op that returns success.
 *
 * \param[in]  queue_id   OSAL queue ID being waited on
 * \param[in]  timeout_ms Timeout in milliseconds
 *
 * \return ESA_WOKE_BY_RESOURCE on successful receive; ESA_WOKE_BY_TIMEOUT on timeout;
 *         negative error code if wait failed
 */
int32 ESA_WaitForMessage(osal_id_t queue_id, uint32 timeout_ms);

/**
 * \brief ESA-managed wait on semaphore (binary or counting)
 *
 * Weak-symbol function for stepping-managed semaphore waits.
 * When ESA stepping is enabled, this function may be overridden to provide
 * deterministic wait semantics. When ESA stepping is disabled, this function
 * is a no-op that returns success.
 *
 * \param[in]  sem_id     OSAL semaphore ID (binary or counting) being waited on
 * \param[in]  timeout_ms Timeout in milliseconds
 *
 * \return ESA_WOKE_BY_RESOURCE on successful give; ESA_WOKE_BY_TIMEOUT on timeout;
 *         ESA_WOKE_BY_FLUSH on flush; negative error code if wait failed
 */
int32 ESA_WaitForSem(osal_id_t sem_id, uint32 timeout_ms);

/**
 * \brief ESA-managed wait on condition variable
 *
 * Weak-symbol function for stepping-managed condition variable waits.
 * Semantically equivalent to ESA_WaitForSem but operates on condition variables.
 * When ESA stepping is enabled, this function may be overridden to provide
 * deterministic wait semantics. When ESA stepping is disabled, this function
 * is a no-op that returns success.
 *
 * \param[in]  condvar_id OSAL condition variable ID being waited on
 * \param[in]  timeout_ms Timeout in milliseconds
 *
 * \return ESA_WOKE_BY_RESOURCE on signal; ESA_WOKE_BY_TIMEOUT on timeout;
 *         negative error code if wait failed
 */
int32 ESA_WaitForCondVar(osal_id_t condvar_id, uint32 timeout_ms);

/**
 * \brief ESA-managed task delay wait
 *
 * Weak-symbol function for stepping-managed task delays.
 * When ESA stepping is enabled, this function may be overridden to provide
 * deterministic delay semantics controlled by step quantums. When ESA stepping
 * is disabled, this function is a no-op that returns success.
 *
 * \param[in]  timeout_ms Requested delay in milliseconds
 *
 * \return ESA_WOKE_BY_TIMEOUT on successful delay completion; negative error code if delay failed
 */
int32 ESA_WaitForDelay(uint32 timeout_ms);

/****************************************************************************************
                            WEAK-SYMBOL NOTIFICATION FUNCTION DECLARATIONS
 ***************************************************************************************/

/**
 * \brief Notify ESA waiters that a message was put on a queue
 *
 * Called when a message is placed on a queue to wake any ESA-managed waiters
 * blocked on that queue. Weak-symbol implementation allows ESA to override
 * and register notifications.
 *
 * \param[in]  queue_id  OSAL queue ID that received a message
 */
void ESA_NotifyQueuePut(osal_id_t queue_id);

/**
 * \brief Notify ESA waiters that a semaphore was given
 *
 * Called when a semaphore is given (posted) to wake any ESA-managed waiters
 * blocked on that semaphore. Weak-symbol implementation allows ESA to override
 * and register notifications.
 *
 * \param[in]  sem_id  OSAL semaphore ID (binary or counting) that was given
 */
void ESA_NotifySemGive(osal_id_t sem_id);

/**
 * \brief Notify ESA waiters that a semaphore was flushed
 *
 * Called when a semaphore is flushed (all pending waiters released) to wake any
 * ESA-managed waiters blocked on that semaphore with flush result code.
 * Weak-symbol implementation allows ESA to override and register notifications.
 *
 * \param[in]  sem_id  OSAL semaphore ID (binary or counting) that was flushed
 */
void ESA_NotifySemFlush(osal_id_t sem_id);

/**
 * \brief Notify ESA waiters waiting on a condition variable
 *
 * Called when a condition variable is signaled or broadcast to wake any
 * ESA-managed waiters blocked on that condition variable. Both Signal and
 * Broadcast operations invoke this notification.
 *
 * \param[in]  condvar_id  OSAL condition variable ID that was signaled/broadcast
 * \param[in]  broadcast   false=Signal (wake one), true=Broadcast (wake all)
 */
void ESA_NotifyCondVar(osal_id_t condvar_id, bool broadcast);

/****************************************************************************************
                           WEAK-SYMBOL TASK LIFECYCLE DECLARATIONS
 ***************************************************************************************/

/**
 * \brief Register a task for ESA wait state management
 *
 * Called when a task is created to register it with ESA wait state registry.
 * Allocates per-task wait state storage and initializes synchronization primitives.
 * Weak-symbol implementation allows ESA to override and manage lifecycle.
 *
 * \param[in]  task_id  OSAL task ID to register
 */
void ESA_RegisterTask(osal_id_t task_id);

/**
 * \brief Deregister a task from ESA wait state management
 *
 * Called when a task is deleted to deregister it from ESA wait state registry.
 * Releases per-task wait state storage and cleans up synchronization primitives.
 * Weak-symbol implementation allows ESA to override and manage lifecycle.
 *
 * \param[in]  task_id  OSAL task ID to deregister
 */
void ESA_DeregisterTask(osal_id_t task_id);

#endif /* ESA_WAIT_H */

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
 * \ingroup  psp
 *
 * Purpose: Private internal header for the simulation stepping core state machine.
 *
 * This header defines the ownership boundary for stepping state and core event-reporting
 * entry points. It is NOT exposed outside the PSP sim_stepping module.
 *
 * The core maintains:
 * - Single state machine for one native-only stepping core
 * - Trigger tracking (dynamic set of pending triggers)
 * - Ack tracking (which triggers have been acknowledged)
 * - Completion tracking (when stepping cycle is complete)
 * - Simulated time storage
 *
 * All stepping hooks (OSAL/PSP task delay, queue receive, etc.) report facts into the core;
 * the core maintains state and semantics. This separation keeps hook/shim layers thin.
 */

#ifndef CFE_PSP_SIM_STEPPING_CORE_H
#define CFE_PSP_SIM_STEPPING_CORE_H

/****************************************************************************************
                                      INCLUDE FILES
 ***************************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/****************************************************************************************
                           PSP-LOCAL CONFIGURATION CONSTANTS
 ***************************************************************************************/

/**
 * \brief Maximum number of concurrent stepping triggers
 *
 * Fixed compile-time capacity for the skeleton stepping core.
 * Suitable for native-only simulation stepping with typical workloads.
 */
#define CFE_PSP_SIM_STEPPING_MAX_TRIGGERS 32

/**
 * \brief Core-service membership-intent bitmask constants
 *
 * Bit positions for the five core services (ES, EVS, SB, TBL, TIME).
 * Used to track which services have reported command-pipe receive facts
 * in the current step.
 */
#define CFE_PSP_SIM_STEPPING_SERVICE_BIT_ES   (1U << 0)   /**< Executive Services */
#define CFE_PSP_SIM_STEPPING_SERVICE_BIT_EVS  (1U << 1)   /**< Event Services */
#define CFE_PSP_SIM_STEPPING_SERVICE_BIT_SB   (1U << 2)   /**< Software Bus */
#define CFE_PSP_SIM_STEPPING_SERVICE_BIT_TBL  (1U << 3)   /**< Table Services */
#define CFE_PSP_SIM_STEPPING_SERVICE_BIT_TIME (1U << 4)   /**< Time Services */

/**
 * \brief TIME child-path participation-intent bitmask constants
 *
 * Bit positions for TIME child-path facts (distinct from main-task command-pipe receive).
 * Used to track which TIME child paths have reported participation facts in the current step.
 * These allow explicitness about TIME subsystem structure (tone semaphore, local 1Hz semaphore).
 */
#define CFE_PSP_SIM_STEPPING_CHILDPATH_BIT_TIME_TONE      (1U << 5)   /**< TIME tone child path */
#define CFE_PSP_SIM_STEPPING_CHILDPATH_BIT_TIME_LOCAL_1HZ (1U << 6)   /**< TIME local-1Hz child path */

/****************************************************************************************
                              CORE STATE MACHINE DEFINITIONS
 ***************************************************************************************/

/**
 * \brief Stepping core state machine states
 *
 * Represents the lifecycle of a single stepping cycle:
 * - INIT: Core not yet initialized
 * - READY: Waiting for step command or trigger
 * - RUNNING: Currently executing a step (advancing time)
 * - WAITING: Step in progress, waiting for acknowledgments/completions
 * - COMPLETE: All triggers acknowledged, all events reported, ready for next step
 */
typedef enum CFE_PSP_SimStepping_CoreState
{
    CFE_PSP_SIM_STEPPING_STATE_INIT,      /**< Core not initialized */
    CFE_PSP_SIM_STEPPING_STATE_READY,     /**< Waiting for step command */
    CFE_PSP_SIM_STEPPING_STATE_RUNNING,   /**< Actively executing step */
    CFE_PSP_SIM_STEPPING_STATE_WAITING,   /**< Waiting for acks/completions */
    CFE_PSP_SIM_STEPPING_STATE_COMPLETE   /**< Step complete, ready for next */
} CFE_PSP_SimStepping_CoreState_t;

/**
 * \brief Dynamic trigger descriptor
 *
 * Represents one pending trigger event (e.g., a task delay boundary hit, queue receive,
 * 1Hz signal, etc.). Tracks whether this trigger has been acknowledged.
 */
typedef struct CFE_PSP_SimStepping_Trigger
{
    uint32_t trigger_id;          /**< Unique trigger identifier */
    uint32_t source_mask;         /**< Source classification (task delay, queue, etc.) */
    uint32_t entity_id;
    bool     is_acknowledged;     /**< Has this trigger been acknowledged? */
} CFE_PSP_SimStepping_Trigger_t;

/**
 * \brief Diagnostic failure classes for normalized sim-stepping observability
 */
typedef enum CFE_PSP_SimStepping_DiagnosticClass
{
    CFE_PSP_SIM_STEPPING_DIAG_TIMEOUT = 0,
    CFE_PSP_SIM_STEPPING_DIAG_DUPLICATE_BEGIN,
    CFE_PSP_SIM_STEPPING_DIAG_ILLEGAL_COMPLETE,
    CFE_PSP_SIM_STEPPING_DIAG_ILLEGAL_STATE,
    CFE_PSP_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
    CFE_PSP_SIM_STEPPING_DIAG_PROTOCOL_ERROR
} CFE_PSP_SimStepping_DiagnosticClass_t;

/**
 * \brief Core-owned diagnostic counters for stepping failure classes
 */
typedef struct CFE_PSP_SimStepping_Diagnostics
{
    uint32_t timeout_count;
    uint32_t duplicate_begin_count;
    uint32_t illegal_complete_count;
    uint32_t illegal_state_count;
    uint32_t transport_error_count;
    uint32_t protocol_error_count;
} CFE_PSP_SimStepping_Diagnostics_t;

/**
 * \brief Stepping core state structure
 *
 * Single owner of stepping state. All state transitions and trigger/ack tracking
 * happen here. Hooks report facts; core maintains semantics.
 * 
 * Trigger storage is fixed-capacity compile-time array (not heap-backed).
 */
typedef struct CFE_PSP_SimStepping_Core
{
    /* State machine */
    CFE_PSP_SimStepping_CoreState_t current_state;  /**< Current state */

    /* Simulated time storage */
    uint64_t sim_time_ns;                          /**< Current simulated time in nanoseconds */
    uint64_t next_sim_time_ns;                     /**< Next target simulated time */

    /* Stepping quantum configuration */
    uint64_t step_quantum_ns;                      /**< Quantum size for one simulation step in nanoseconds */

    /* Trigger tracking (fixed compile-time capacity) */
    CFE_PSP_SimStepping_Trigger_t triggers[CFE_PSP_SIM_STEPPING_MAX_TRIGGERS];  /**< Array of pending triggers */
    uint32_t trigger_count;                        /**< Current number of triggers */
    uint32_t next_trigger_id;                      /**< Counter for unique trigger IDs */

    /* Ack/completion tracking */
    uint32_t acks_received;                        /**< Count of acknowledged triggers */
    uint32_t acks_expected;                        /**< Count of triggers expecting ack */

    bool completion_requested;                     /**< Has completion been requested? */
    bool completion_ready;                         /**< All events reported and ready? */

    /* Core-owned diagnostic counters */
    CFE_PSP_SimStepping_Diagnostics_t diagnostics; /**< Accumulated failure-class diagnostics */

    /* Timeout configuration */
    uint32_t step_timeout_ms;                      /**< Timeout for step completion (ms) */

    /* TaskDelay takeover control */
    bool taskdelay_takeover_enabled;               /**< Enable TaskDelay takeover (default: OFF) */

    /* Per-task TaskDelay opt-in set (fixed compile-time capacity) */
    uint32_t taskdelay_optin_set[8];               /**< Task IDs opted-in for TaskDelay takeover */
    uint32_t taskdelay_optin_count;                /**< Number of tasks in opt-in set */
    uint64_t taskdelay_expiry_ns[8];
    bool     taskdelay_pending[8];
    bool     taskdelay_owed[8];

    /* Current-step core-service membership-intent set */
    uint32_t core_service_membership_mask;         /**< Bitmask of core services (ES/EVS/SB/TBL/TIME) that participated in current step */

    /* Explicit step-session bookkeeping */
    bool     session_active;                       /**< True if a step session is currently active */
    uint64_t session_counter;                      /**< Monotonic session counter, incremented at step session begin */

    /* Persistent lifecycle readiness state (distinct from per-step completion_ready) */
    bool     system_ready_for_stepping;            /**< Persistent flag: true after system has signaled lifecycle readiness; survives step resets and session transitions */

} CFE_PSP_SimStepping_Core_t;

/****************************************************************************************
                             CORE API FUNCTION DECLARATIONS
 ***************************************************************************************/

/**
 * \brief Initialize the stepping core
 *
 * Sets up state machine and prepares for operation.
 * Trigger storage is pre-allocated as a fixed compile-time array.
 *
 * \param[in]  core              Pointer to core structure to initialize
 * \param[in]  initial_time_ns   Initial simulated time in nanoseconds
 * \param[in]  trigger_capacity  Unused (kept for API compatibility); max is CFE_PSP_SIM_STEPPING_MAX_TRIGGERS
 *
 * \return 0 if initialization successful; non-zero error code if initialization failed
 */
int32_t CFE_PSP_SimStepping_Core_Init(CFE_PSP_SimStepping_Core_t *core,
                                      uint64_t                   initial_time_ns,
                                      uint32_t                   trigger_capacity);

/**
 * \brief Reset the stepping core for a new step cycle
 *
 * Clears triggers and ack counters, transitions to READY state.
 *
 * \param[in]  core  Pointer to core structure to reset
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_Reset(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Report a task delay boundary event
 *
 * Called when a task requests delay via OSAL TaskDelay hook.
 * Reports the fact; core decides if stepping should trigger.
 *
 * \param[in]  core           Pointer to core structure
 * \param[in]  task_id        Identifier of task requesting delay
 * \param[in]  delay_ms       Requested delay in milliseconds
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportTaskDelay(CFE_PSP_SimStepping_Core_t *core,
                                                  uint32_t                   task_id,
                                                  uint32_t                   delay_ms);

int32_t CFE_PSP_SimStepping_Core_ReportTaskDelayReturn(CFE_PSP_SimStepping_Core_t *core,
                                                        uint32_t                   task_id);

/**
 * \brief Report a queue receive boundary event
 *
 * Called when a task blocks on queue receive via OSAL hook.
 * Reports the fact; core decides if stepping should trigger.
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  queue_id   Identifier of queue being received from
 * \param[in]  timeout_ms Timeout for receive operation (PEND_FOREVER = ~0U)
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportQueueReceive(CFE_PSP_SimStepping_Core_t *core,
                                                    uint32_t                   queue_id,
                                                    uint32_t                   timeout_ms);

/**
 * \brief Report a binary semaphore take boundary event
 *
 * Called when a task blocks on binary semaphore via OSAL hook.
 * Reports the fact; core decides if stepping should trigger.
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  sem_id     Identifier of semaphore being taken
 * \param[in]  timeout_ms Timeout for take operation
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportBinSemTake(CFE_PSP_SimStepping_Core_t *core,
                                                  uint32_t                   sem_id,
                                                  uint32_t                   timeout_ms);

/**
 * \brief Report a time task cycle boundary event
 *
 * Called by TIME module when starting a time synchronization/update cycle.
 * Reports the fact; core decides if stepping should trigger.
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportTimeTaskCycle(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Report a 1Hz boundary event
 *
 * Called when a 1Hz tick is detected or raised.
 * Reports the fact; core decides if stepping should trigger.
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_Report1HzBoundary(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Report a tone signal event
 *
 * Called when a tone signal (e.g., from PSP or scheduler) is raised.
 * Reports the fact; core decides if stepping should trigger.
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportToneSignal(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Report a scheduler semaphore wait boundary event
 *
 * Called when the scheduler (SCH) blocks on a semaphore waiting for a trigger
 * (e.g., tone signal, 1Hz tick, or software trigger).
 * Reports the fact as a distinct scheduler event; core decides if stepping should trigger.
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  sem_id     Identifier of semaphore being waited on
 * \param[in]  timeout_ms Timeout for semaphore wait operation
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportSchSemaphoreWait(CFE_PSP_SimStepping_Core_t *core,
                                                        uint32_t                   sem_id,
                                                        uint32_t                   timeout_ms);

/**
 * \brief Report a scheduler minor frame boundary event
 *
 * Called when the scheduler reaches a minor frame boundary (step granularity).
 * Minor frames are the fundamental scheduling unit and define stepping trigger granularity.
 * Reports the fact as a distinct scheduler event; core decides if stepping should trigger.
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportSchMinorFrame(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Report a scheduler major frame boundary event
 *
 * Called when the scheduler reaches a major frame boundary (frame 0 of each cycle).
 * Major frames mark the beginning of a complete scheduling cycle.
 * Reports the fact as a distinct scheduler event; core decides if stepping should trigger.
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportSchMajorFrame(CFE_PSP_SimStepping_Core_t *core);

int32_t CFE_PSP_SimStepping_Core_ReportSchSendTrigger(CFE_PSP_SimStepping_Core_t *core,
                                                         uint32_t                   target_id);

int32_t CFE_PSP_SimStepping_Core_ReportSchDispatchComplete(CFE_PSP_SimStepping_Core_t *core);

 int32_t CFE_PSP_SimStepping_Core_ReportCoreServiceCmdPipeReceive(CFE_PSP_SimStepping_Core_t *core,
                                                                   uint32_t                   service_id);

 int32_t CFE_PSP_SimStepping_Core_ReportCoreServiceCmdPipeComplete(CFE_PSP_SimStepping_Core_t *core,
                                                                    uint32_t                   service_id);

 int32_t CFE_PSP_SimStepping_Core_ReportTimeToneSemConsume(CFE_PSP_SimStepping_Core_t *core,
                                                            uint32_t                   sem_id);

int32_t CFE_PSP_SimStepping_Core_ReportTimeLocal1HzSemConsume(CFE_PSP_SimStepping_Core_t *core,
                                                               uint32_t                   sem_id);

/**
 * \brief Query the current simulated time
 *
 * Returns the simulated time as stored in the core.
 * Called by timebase modules to get deterministic time.
 *
 * \param[in]  core           Pointer to core structure
 * \param[out] sim_time_ns    Pointer to store current simulated time in nanoseconds
 *
 * \return 0 if time retrieved successfully; non-zero error code if query failed
 */
int32_t CFE_PSP_SimStepping_Core_QuerySimTime(CFE_PSP_SimStepping_Core_t *core,
                                                uint64_t                   *sim_time_ns);

/**
 * \brief Query if a requested TaskDelay can be handled by stepping
 *
 * Checks whether the PSP stepping core can handle (take over) a specific task delay
 * request. Uses conservative eligibility logic:
 * - Core must be initialized
 * - TaskDelay takeover gate must be ON
 * - Task must be explicitly opted-in to TaskDelay takeover
 * - Requested delay must be an exact whole-number multiple of step_quantum_ns
 *
 * Used by OSAL TaskDelay hooks to determine whether to return handled (skip wall-clock
 * sleep) or not-handled (proceed with normal sleep). With gate OFF (default) or task not
 * opted-in, always returns false (not-handled).
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  task_id    Runtime task ID requesting delay
 * \param[in]  delay_ms   Requested delay in milliseconds
 *
 * \return true if delay can be handled (core initialized, gate ON, task opted-in, delay is exact quantum multiple);
 *         false if delay cannot be handled or core not initialized
 */
bool CFE_PSP_SimStepping_Core_QueryTaskDelayEligible(CFE_PSP_SimStepping_Core_t *core,
                                                      uint32_t                   task_id,
                                                      uint32_t                   delay_ms);

/**
 * \brief Block current task until simulated delay budget satisfied by explicit step quantums
 *
 * PSP-owned blocking wait mechanism for step-controlled TaskDelay. Calculates target expiry
 * time in simulated nanoseconds, then polls sim_time_ns until enough explicit step quantums
 * have been advanced to satisfy the delay. Uses brief wall-clock nanosleep intervals to avoid
 * spin-wait, but release is controlled solely by sim_time_ns advancement via AdvanceOneQuantum.
 * This prevents delay-driven tasks from self-advancing when no steps are issued.
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  task_id    Runtime task ID requesting delay
 * \param[in]  delay_ms   Requested delay in milliseconds
 *
 * \return 0 on success (delay satisfied); negative on error
 */
int32_t CFE_PSP_SimStepping_Core_WaitForDelayExpiry(CFE_PSP_SimStepping_Core_t *core,
                                                     uint32_t                   task_id,
                                                     uint32_t                   delay_ms);

/**
 * \brief Report a queue receive ack boundary event (pre-blocking)
 *
 * Called immediately before a task blocks on queue receive (pre-mq_receive/mq_timedreceive).
 * Reports the ack-candidate fact: task is about to wait on queue.
 * Distinct from completion fact: this signals intent to wait, not operation success.
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  task_id    Runtime task ID (current executing task)
 * \param[in]  queue_id   Identifier of queue being received from
 * \param[in]  timeout_ms Timeout for receive operation (PEND_FOREVER = ~0U)
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportQueueReceiveAck(CFE_PSP_SimStepping_Core_t *core,
                                                        uint32_t                   task_id,
                                                        uint32_t                   queue_id,
                                                        uint32_t                   timeout_ms);

/**
 * \brief Report a queue receive complete boundary event (post-blocking)
 *
 * Called immediately after mq_receive/mq_timedreceive returns (regardless of success/failure).
 * Reports the complete-candidate fact: task has returned from queue wait.
 * Distinct from ack fact: this signals operation completion, not intent.
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  task_id    Runtime task ID (current executing task)
 * \param[in]  queue_id   Identifier of queue being received from
 * \param[in]  timeout_ms Timeout for receive operation (PEND_FOREVER = ~0U)
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportQueueReceiveComplete(CFE_PSP_SimStepping_Core_t *core,
                                                             uint32_t                   task_id,
                                                             uint32_t                   queue_id,
                                                             uint32_t                   timeout_ms);

/**
 * \brief Advance simulated time by one quantum (private internal API)
 *
 * Internal-only function for the PSP stepping core to advance simulated time
 * by exactly one configured quantum. Increments both current and next simulated
 * time by the step quantum value.
 *
 * This is the sole write-path for sim_time_ns and next_sim_time_ns; all other
 * read-side queries use CFE_PSP_SimStepping_Core_QuerySimTime().
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return 0 on successful time advance; non-zero error code if advance failed
 *         (e.g., core null, quantum zero or not configured)
 *
 * \note This function is PRIVATE to the PSP stepping core and is not exposed
 *       to external callers. It is intended for future control-channel integration.
 */
int32_t CFE_PSP_SimStepping_Core_AdvanceOneQuantum(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Report a binary semaphore take ack boundary event (pre-wait)
 *
 * Called immediately before a task blocks on binary semaphore wait (pre-pthread_cond_wait).
 * Reports the ack-candidate fact: task is about to wait on semaphore.
 * Distinct from completion fact: this signals intent to wait, not operation success.
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  task_id    Runtime task ID (current executing task)
 * \param[in]  sem_id     Identifier of semaphore being taken
 * \param[in]  timeout_ms Timeout for take operation (0 = PEND_FOREVER)
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportBinSemTakeAck(CFE_PSP_SimStepping_Core_t *core,
                                                     uint32_t                   task_id,
                                                     uint32_t                   sem_id,
                                                     uint32_t                   timeout_ms);

/**
 * \brief Report a binary semaphore take complete boundary event (post-wait)
 *
 * Called immediately after pthread_cond_wait/pthread_cond_timedwait returns (regardless of success/failure).
 * Reports the complete-candidate fact: task has returned from semaphore wait.
 * Distinct from ack fact: this signals operation completion, not intent.
 *
 * \param[in]  core       Pointer to core structure
 * \param[in]  task_id    Runtime task ID (current executing task)
 * \param[in]  sem_id     Identifier of semaphore being taken
 * \param[in]  timeout_ms Timeout for take operation (0 = PEND_FOREVER)
 *
 * \return 0 on success
 */
int32_t CFE_PSP_SimStepping_Core_ReportBinSemTakeComplete(CFE_PSP_SimStepping_Core_t *core,
                                                           uint32_t                   task_id,
                                                           uint32_t                   sem_id,
                                                           uint32_t                   timeout_ms);

/**
 * \brief Query the current state of the stepping core (internal API for in-process adapter)
 *
 * Returns the current state machine state and optionally the trigger count.
 * Used by in-process control adapters to determine stepping readiness.
 * This is an internal PSP API, not exposed outside the module.
 *
 * \param[in]  core       Pointer to core structure
 * \param[out] state_out  Pointer to store current state enum (if not NULL)
 *
 * \return 0 on success; -1 if core null or state query failed
 *
 * \note This is an internal API for the PSP stepping module's in-process adapter.
 *       External callers should use the public adapter API (CFE_PSP_SimStepping_InProc_*).
 */
int32_t CFE_PSP_SimStepping_Core_QueryState(CFE_PSP_SimStepping_Core_t *core,
                                            CFE_PSP_SimStepping_CoreState_t *state_out);

/**
 * \brief Check if the current step cycle is complete (internal API for in-process adapter)
 *
 * Returns true if the stepping core has transitioned to COMPLETE state, indicating
 * that all expected triggers have been reported and all acks received.
 * Used by in-process control adapters to poll for step completion.
 * This is an internal PSP API, not exposed outside the module.
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return true if step is complete; false if still running, waiting, or core null
 *
 * \note This is an internal API for the PSP stepping module's in-process adapter.
 *       External callers should use the public adapter API (CFE_PSP_SimStepping_InProc_*).
 */
bool CFE_PSP_SimStepping_Core_IsStepComplete(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Begin a new step session with explicit bookkeeping
 *
 * Initiates a new step session by clearing triggers, resetting ack counters,
 * marking the session as active, and incrementing the session counter.
 * This is the primary entry point for beginning a clean stepping cycle.
 * Replaces the previous blind Core_Reset() pattern with explicit session semantics.
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return CFE_PSP_SIM_STEPPING_STATUS_SUCCESS on success
 * \return CFE_PSP_SIM_STEPPING_STATUS_FAILURE if core is null
 * \return CFE_PSP_SIM_STEPPING_STATUS_NOT_READY if lifecycle readiness not reached
 * \return CFE_PSP_SIM_STEPPING_STATUS_DUPLICATE_BEGIN if prior session is unresolved
 */
int32_t CFE_PSP_SimStepping_Core_BeginStepSession(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Mark the system as ready for simulation stepping
 *
 * Sets the persistent lifecycle readiness flag, indicating that the system has
 * completed initialization and is prepared to enter stepping mode.
 * This flag survives step session resets and step cycle completions.
 *
 * Distinct from per-step completion semantics: this represents persistent
 * system-level readiness, not transient per-step completion state.
 *
 * \param[in]  core  Pointer to core structure
 *
 * \return 0 on success; -1 if core null
 */
int32_t CFE_PSP_SimStepping_Core_MarkSystemReadyForStepping(CFE_PSP_SimStepping_Core_t *core);

/**
 * \brief Record one diagnostic event with normalized class/status logging
 *
 * Increments a core-owned diagnostic counter bucket and emits a normalized,
 * searchable log line for the supplied class and site.
 *
 * \param[in] core        Pointer to stepping core
 * \param[in] diag_class  Failure class bucket to increment
 * \param[in] status      Shared status taxonomy code for this event
 * \param[in] site        Static site tag (e.g. "BeginStepSession")
 * \param[in] detail_a    Optional numeric detail A
 * \param[in] detail_b    Optional numeric detail B
 *
 * \return status value passed in, or CFE_PSP_SIM_STEPPING_STATUS_FAILURE if core/site invalid
 */
int32_t CFE_PSP_SimStepping_Core_RecordDiagnostic(CFE_PSP_SimStepping_Core_t *core,
                                                   CFE_PSP_SimStepping_DiagnosticClass_t diag_class,
                                                   int32_t status,
                                                   const char *site,
                                                   uint32_t detail_a,
                                                   uint32_t detail_b);

#endif /* CFE_PSP_SIM_STEPPING_CORE_H */

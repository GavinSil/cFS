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
 * Purpose: This file contains PSP-level simulation stepping hooks
 *          that are called to retrieve unified simulation time when CFE_SIM_STEPPING is enabled.
 *
 * These hooks allow simulation stepping implementations to provide deterministic time
 * progression at the PSP level, particularly for timebase module time retrieval operations.
 */

#ifndef ESA_SIM_STEPPING_H
#define ESA_SIM_STEPPING_H

/****************************************************************************************
                                     INCLUDE FILES
 ***************************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************************
                           SHARED STEPPING STATUS TAXONOMY
 ***************************************************************************************/

/**
 * \brief Shared status codes for sim-stepping control/diagnostic surface
 *
 * These constants are used by the stepping core and adapters so failure classes are
 * represented consistently across inproc and UDS control paths.
 */
#define ESA_SIM_STEPPING_STATUS_SUCCESS           0   /**< Operation successful */
#define ESA_SIM_STEPPING_STATUS_FAILURE          -1   /**< Generic failure */
#define ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN  -2   /**< Begin-step rejected: prior session unresolved */
#define ESA_SIM_STEPPING_STATUS_NOT_READY        -3   /**< System/core not ready for requested operation */
#define ESA_SIM_STEPPING_STATUS_TIMEOUT          -4   /**< Operation timed out */
#define ESA_SIM_STEPPING_STATUS_ILLEGAL_COMPLETE -5   /**< Completion reported without matching trigger */
#define ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR  -6   /**< UDS transport I/O/connect error */
#define ESA_SIM_STEPPING_STATUS_PROTOCOL_ERROR   -7   /**< UDS protocol framing/opcode error */
#define ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE    -8   /**< Operation illegal in current stepping state */

/****************************************************************************************
                              INITIALIZATION API
 ***************************************************************************************/

/**
 * \brief Initialize ESA stepping module (core + UDS transport)
 *
 * Must be called early in BSP main(), before OS_Application_Startup().
 * Initializes the stepping core state machine and UDS control adapter.
 * Safe to call even when CFE_SIM_STEPPING is not defined (becomes no-op).
 *
 * \note This function should be called exactly once during system initialization.
 * \note When CFE_SIM_STEPPING is not defined, this is a no-op stub.
 */
void ESA_Init(void);

/****************************************************************************************
                             STEPPING HOOK DECLARATIONS
 ***************************************************************************************/

/**
 * \brief Hook to retrieve simulation time when stepping is enabled
 *
 * Called by PSP timebase modules to obtain simulation time instead of wall-clock time
 * when CFE_SIM_STEPPING is enabled. This allows deterministic time progression in
 * simulation/test environments.
 *
 * \param[out]  sim_time_ns    Pointer to store simulation time in nanoseconds since epoch
 *
 * \return  true if simulation time was successfully provided
 * \return  false if hook not implemented or stepping disabled (use wall-clock instead)
 *
 * \note This function is declared but implementations are provided only when
 *       CFE_SIM_STEPPING is defined. When not defined, this becomes a no-op stub
 *       that returns false.
 */
bool ESA_Stepping_Hook_GetTime(uint64_t *sim_time_ns);

/**
 * \brief Hook to query if a requested TaskDelay can be handled by stepping
 *
 * Called by OSAL TaskDelay hooks to determine if the stepping core can take over
 * the delay (return true) or if the caller should proceed with normal wall-clock
 * sleep (return false).
 *
 * Uses conservative eligibility logic: core must be initialized, TaskDelay takeover
 * gate must be ON, task must be explicitly opted-in, and requested delay must be an
 * exact whole-number multiple of step_quantum_ns. With gate OFF (default) or task not
 * opted-in, always returns false.
 *
 * \param[in]  task_id   Runtime task ID requesting delay
 * \param[in]  delay_ms  Requested delay in milliseconds
 *
 * \return  true if delay can be handled by stepping (skip wall-clock sleep)
 * \return  false if delay cannot be handled (proceed with normal wall-clock sleep)
 *
 * \note Implementations are provided only when CFE_SIM_STEPPING is defined.
 *       When not defined, this becomes a no-op stub that returns false.
 */
bool ESA_Stepping_Hook_TaskDelayEligible(uint32_t task_id, uint32_t delay_ms);

/**
 * \brief Hook to query if a stepping session is currently active
 *
 * Called by OSAL synchronization primitives (BinSem, CondVar, etc.) to determine
 * if they should use ESA wait paths (return true) or fall back to legacy POSIX
 * waits (return false).
 *
 * Returns true only when the stepping core is initialized and a stepping session
 * is active. Unit tests and non-stepping contexts return false, causing primitives
 * to use legacy POSIX wait paths.
 *
 * \return  true if stepping session is active (use ESA waits)
 * \return  false if no session active (use legacy POSIX waits)
 *
 * \note Implementations are provided only when CFE_SIM_STEPPING is defined.
 *       When not defined, this becomes a no-op stub that returns false.
 */
bool ESA_Stepping_Hook_IsSessionActive(void);

/**
 * \brief Block current task until simulated delay satisfied by explicit step quantums
 *
 * PSP-owned blocking wait for step-controlled TaskDelay. Thin wrapper forwarding to
 * ESA_Stepping_Core_WaitForDelayExpiry with shared stepping core instance.
 * Polls sim_time_ns until enough explicit step quantums advanced to satisfy delay.
 * Prevents delay-driven tasks from self-advancing when no steps issued.
 *
 * \param[in]  task_id   Runtime task ID requesting delay
 * \param[in]  delay_ms  Requested delay in milliseconds
 *
 * \return  0 on success (delay satisfied)
 * \return  -1 on error (invalid core or parameters)
 *
 * \note Available only when CFE_SIM_STEPPING is defined.
 *       When not defined, this becomes a no-op stub that returns -1.
 */
int32_t ESA_Stepping_WaitForDelayExpiry(uint32_t task_id, uint32_t delay_ms);

/****************************************************************************************
                     IN-PROCESS CONTROL ADAPTER API (PUBLIC)
 ***************************************************************************************/

/**
 * \brief Begin a simulation step (in-process control adapter)
 *
 * Initiates a new simulation step. The stepping core transitions from READY to RUNNING
 * and waits for step events to occur. Returns immediately without blocking.
 * This is a thin in-process control surface that forwards to the shared stepping core.
 *
 * \return  ESA_SIM_STEPPING_STATUS_SUCCESS on success
 * \return  ESA_SIM_STEPPING_STATUS_NOT_READY if stepping not initialized or pre-ready
 * \return  ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN if prior session is unresolved
 * \return  ESA_SIM_STEPPING_STATUS_FAILURE on other failures
 *
 * \note Available only when CFE_SIM_STEPPING is defined.
 *       When not defined, this becomes a no-op stub that returns -1.
 */
int32_t ESA_Stepping_InProc_BeginStep(void);

/**
 * \brief Wait for current step to complete (in-process control adapter)
 *
 * Blocks until the stepping core indicates that the current step cycle is complete
 * (all expected triggers have been reported and acknowledged, all events processed).
 * This is a thin in-process control surface that queries the shared stepping core.
 *
 * \param[in]  timeout_ms  Timeout in milliseconds (0 = PEND_FOREVER, ~0U = non-blocking poll)
 *
 * \return  ESA_SIM_STEPPING_STATUS_SUCCESS if step completed successfully
 * \return  ESA_SIM_STEPPING_STATUS_TIMEOUT if finite timeout is exceeded
 * \return  ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE if no active step session exists
 * \return  ESA_SIM_STEPPING_STATUS_FAILURE for non-blocking not-complete or other failures
 * \return  ESA_SIM_STEPPING_STATUS_NOT_READY if core is not initialized
 *
 * \note Available only when CFE_SIM_STEPPING is defined.
 *       When not defined, this becomes a no-op stub that returns -1.
 * \note This function may block; not suitable for interrupt handlers.
 */
int32_t ESA_Stepping_InProc_WaitStepComplete(uint32_t timeout_ms);

/**
  * \brief Query current stepping state (in-process control adapter)
  *
  * Returns the current state of the stepping core without blocking.
  * Allows in-process callers to determine stepping readiness and progress.
  * This is a thin in-process control surface that queries the shared stepping core.
  *
  * \param[out]  state_out     Pointer to store current state enum value (if not NULL)
  * \param[out]  trigger_count Pointer to store current pending trigger count (if not NULL)
  *
  * \return  0 if state query successful
  * \return  -1 if core not initialized or pointer validation failed
  *
  * \note Available only when CFE_SIM_STEPPING is defined.
  *       When not defined, this becomes a no-op stub that returns -1.
  * \note All output parameters are optional (may be NULL if not needed).
  */
int32_t ESA_Stepping_InProc_QueryState(uint32_t *state_out, uint32_t *trigger_count);

/****************************************************************************************
                     UDS CONTROL ADAPTER API (PUBLIC)
 ***************************************************************************************/

/**
  * \brief Initialize UDS control adapter (Unix domain socket adapter)
  *
  * Creates and binds a Linux AF_UNIX socket endpoint for external stepping control.
  * Must be called after the core is initialized and before any UDS control requests
  * are processed. This establishes the UDS listener socket lifecycle (endpoint only;
  * protocol handling remains deferred). Performs full Linux socket init: creates socket,
  * prepares stable socket path, cleans stale path if needed, binds, and listens.
  *
  * This is a thin adapter layer that forwards to the same single stepping core,
  * not a second state machine. UDS adapter manages only endpoint lifecycle; core
  * maintains all stepping state and semantics.
  *
  * \return  0 on success (UDS endpoint initialized and listening)
  * \return  -1 if stepping not initialized, adapter already initialized, or socket init failed
  *
  * \note Available only when CFE_SIM_STEPPING is defined.
  *       When not defined, this becomes a no-op stub that returns -1.
  * \note This function must be called before UDS_Service() can succeed.
  * \note The UDS adapter explicitly shares the same stepping core as the inproc adapter.
  * \note Socket path is /tmp/cfe_sim_stepping.sock (Linux-only, stable for this environment).
  */
int32_t ESA_Stepping_UDS_Init(void);

/**
   * \brief Service one UDS control request (Unix domain socket adapter)
   *
   * Performs minimal transport-level servicing on the UDS listener socket (non-blocking):
   * - Validates that core and adapter are initialized
   * - Performs non-blocking accept on at most one pending client connection
   * - Immediately closes any accepted client without parsing or dispatching
   * - Returns consistent adapter-level status (0 = idle/handled, -1 = not ready)
   *
   * This layer is strictly transport-level accept/close behavior. No wire protocol
   * parsing, no request dispatch, no framing semantics yet. Real protocol semantics
   * (BeginStep, WaitStepComplete, QueryState dispatch) deferred to later tasks.
   *
   * Returns immediately whether or not a request was present (non-blocking).
   *
   * \return  ESA_SIM_STEPPING_STATUS_SUCCESS if idle or request handled successfully
   * \return  ESA_SIM_STEPPING_STATUS_NOT_READY if adapter/core not initialized
   * \return  ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR on socket transport failures
   * \return  ESA_SIM_STEPPING_STATUS_PROTOCOL_ERROR on invalid/unknown opcode framing
   *
   * \note Available only when CFE_SIM_STEPPING is defined.
   *       When not defined, this becomes a no-op stub that returns -1.
   * \note This function does not block and should be called periodically from stepping loop.
   * \note The UDS adapter explicitly shares the same stepping core as the inproc adapter.
   */
int32_t ESA_Stepping_UDS_Service(void);

/**
   * \brief Shutdown UDS control adapter (Unix domain socket adapter)
   *
   * Cleanly shuts down the stepping core's Unix domain socket adapter and releases
   * UDS-specific resources. Closes the listener socket file descriptor and unlinks
   * the Unix domain socket path from the filesystem. The shared stepping core itself
   * is NOT shut down (that remains the responsibility of the core owner). This adapter
   * layer only manages UDS endpoint lifecycle and associated resources.
   *
   * \return  0 on success (UDS endpoint shut down, fd closed, path unlinked)
   * \return  -1 if adapter not initialized or shutdown failed
   *
   * \note Available only when CFE_SIM_STEPPING is defined.
   *       When not defined, this becomes a no-op stub that returns -1.
   * \note After shutdown, UDS_Service() will not accept new requests until re-initialized.
   * \note The UDS adapter explicitly shares the same stepping core as the inproc adapter;
   *       both adapters can coexist and use the same core.
   */
int32_t ESA_Stepping_UDS_Shutdown(void);

/**
 * \brief Service UDS adapter (non-blocking, single request per call)
 *
 * Thin wrapper for calling ESA_Stepping_UDS_Service() from periodic
 * hooks (e.g., stepping timer ticks or TIME task cycle boundaries).
 * Returns immediately whether a client request was present or not.
 * Non-blocking and suitable for calling from tight event loops.
 *
 * \return  0 if no client pending or client request processed successfully
 * \return  -1 if adapter not initialized or service failed
 *
 * \note Available only when CFE_SIM_STEPPING is defined.
 *       When not defined, this becomes a no-op stub that returns -1.
 * \note Designed to be called from stepping hooks on each hook cycle.
 */
int32_t ESA_Stepping_UDS_RunOnce(void);

#endif /* ESA_SIM_STEPPING_H */

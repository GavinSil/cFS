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

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "cfe_psp.h"
#include "cfe_psp_sim_stepping.h"
#include "cfe_psp_sim_stepping_core.h"
#include "cfe_psp_sim_stepping_shim.h" /* Mission-owned neutral shim ABI header from sample_defs */

/**
 * \brief Static stepping core instance owned by this PSP module
 *
 * This is the single authoritative stepping core for the mission.
 * All hooks report facts into this core; the core maintains state and semantics.
 * Initialized once at module startup with simulated time = 0.
 */
static CFE_PSP_SimStepping_Core_t stepping_core = {0};
static bool core_initialized = false;

/**
 * \brief PSP-local UDS service loop thread state (Linux-only, stepping-only)
 *
 * Manages the background POSIX thread that periodically services UDS client requests.
 * Only created when CFE_SIM_STEPPING is enabled. Thread is non-cancellable; cleanup
 * limited by lack of PSP module shutdown hook. Thread sets should_run=false on exit.
 */
typedef struct
{
    volatile bool is_running;  /* True if service loop thread is actively running */
    volatile bool should_run;  /* Signal to thread to continue; set false to exit */
    pthread_t task_id;         /* POSIX thread ID */
} CFE_PSP_SimStepping_UDS_ServiceLoop_t;

static CFE_PSP_SimStepping_UDS_ServiceLoop_t uds_service_loop = {
    .is_running = false,
    .should_run = false,
    .task_id = 0
};

/**
 * \brief PSP-local background thread function for UDS service loop
 *
 * Runs in a separate thread and periodically calls CFE_PSP_SimStepping_UDS_Service()
 * to accept and dispatch UDS client requests. Uses simple sleep-poll model with
 * 10ms sleep between service calls. Non-blocking service means no request waits
 * longer than one sleep interval for processing.
 *
 * Thread runs until should_run is set to false (no explicit cancellation).
 * Sets is_running=false when exiting.
 */
static void *CFE_PSP_SimStepping_UDS_ServiceLoop_Task(void *arg)
{
    struct timespec sleep_time = {0};

    sleep_time.tv_nsec = 10000000; /* 10 milliseconds */

    CFE_PSP_SimStepping_UDS_ServiceLoop_t *state = (CFE_PSP_SimStepping_UDS_ServiceLoop_t *)arg;
    if (state == NULL)
    {
        return NULL;
    }

    state->is_running = true;

    while (state->should_run)
    {
        /* Call non-blocking UDS service to accept one pending client */
        (void)CFE_PSP_SimStepping_UDS_Service();

        /* Periodically sleep to avoid busy-waiting; nanosleep tolerates EINTR */
        nanosleep(&sleep_time, NULL);
    }

    state->is_running = false;
    return NULL;
}

void ESA_Init(void)
{
    int32_t status;
    int pthread_status;

    printf("CFE_PSP: Simulation stepping module initialized\n");

    status = CFE_PSP_SimStepping_Core_Init(&stepping_core, 0, CFE_PSP_SIM_STEPPING_MAX_TRIGGERS);
    if (status == 0)
    {
        core_initialized = true;
    }
    else
    {
        printf("CFE_PSP: Failed to initialize stepping core (status=%ld)\n", (long)status);
        core_initialized = false;
        return;
    }

#ifdef CFE_SIM_STEPPING
    status = CFE_PSP_SimStepping_UDS_Init();
    if (status != 0)
    {
        printf("CFE_PSP: Failed to initialize UDS adapter (status=%ld)\n", (long)status);
    }
    else
    {
        /* Start PSP-local UDS service loop thread only if UDS init succeeded (non-fatal if thread creation fails) */
        uds_service_loop.should_run = true;
        pthread_status = pthread_create(&uds_service_loop.task_id, NULL, 
                                         CFE_PSP_SimStepping_UDS_ServiceLoop_Task, 
                                         &uds_service_loop);
        if (pthread_status != 0)
        {
            printf("CFE_PSP: Failed to create UDS service loop thread (status=%d)\n", pthread_status);
            uds_service_loop.should_run = false;
        }
    }
#endif
}

#ifdef CFE_SIM_STEPPING

bool CFE_PSP_SimStepping_Hook_GetTime(uint64_t *sim_time_ns)
{
    int32_t status;

    /* Validate input pointer */
    if (sim_time_ns == NULL)
    {
        return false;
    }

    /* Only query if core is initialized */
    if (!core_initialized)
    {
        return false;
    }

    /* Query the core for simulated time */
    status = CFE_PSP_SimStepping_Core_QuerySimTime(&stepping_core, sim_time_ns);

    /* Return true only if query succeeded */
    return (status == 0);
}

#else

bool CFE_PSP_SimStepping_Hook_GetTime(uint64_t *sim_time_ns)
{
    return false;
}

#endif

/**
 * \brief Unified fact-reporting forwarder for stepping events
 *
 * Validates input, checks core initialization, and dispatches event to the
 * appropriate core report function based on event_kind.
 * All state machine semantics remain centralized in the core.
 *
 * \param[in]  event  Pointer to stepping event descriptor
 *
 * \return 0 on success; non-zero error code if report failed
 */
int32_t CFE_PSP_SimStepping_Shim_ReportEvent(const CFE_PSP_SimStepping_ShimEvent_t *event)
{
    int32_t status = CFE_PSP_SIM_STEPPING_STATUS_FAILURE;

    /* Input validation: event pointer must not be null */
    if (event == NULL)
    {
        return CFE_PSP_SIM_STEPPING_STATUS_FAILURE;
    }

    /* Gate: core must be initialized before reporting events */
    if (!core_initialized)
    {
        return CFE_PSP_SIM_STEPPING_STATUS_NOT_READY;
    }

    /* Dispatch on event_kind and forward to appropriate core report function */
    switch (event->event_kind)
    {
        case CFE_PSP_SIM_STEPPING_EVENT_TASK_DELAY:
            status = CFE_PSP_SimStepping_Core_ReportTaskDelay(&stepping_core, event->task_id,
                                                              event->optional_delay_ms);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_QUEUE_RECEIVE:
            status = CFE_PSP_SimStepping_Core_ReportQueueReceive(&stepping_core, event->entity_id,
                                                                 event->optional_delay_ms);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_QUEUE_RECEIVE_ACK:
            status = CFE_PSP_SimStepping_Core_ReportQueueReceiveAck(&stepping_core, event->task_id,
                                                                    event->entity_id,
                                                                    event->optional_delay_ms);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_QUEUE_RECEIVE_COMPLETE:
            status = CFE_PSP_SimStepping_Core_ReportQueueReceiveComplete(&stepping_core, event->task_id,
                                                                         event->entity_id,
                                                                         event->optional_delay_ms);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_BINSEM_TAKE:
            status = CFE_PSP_SimStepping_Core_ReportBinSemTake(&stepping_core, event->entity_id,
                                                               event->optional_delay_ms);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_BINSEM_TAKE_ACK:
            status = CFE_PSP_SimStepping_Core_ReportBinSemTakeAck(&stepping_core, event->task_id,
                                                                  event->entity_id,
                                                                  event->optional_delay_ms);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_BINSEM_TAKE_COMPLETE:
            status = CFE_PSP_SimStepping_Core_ReportBinSemTakeComplete(&stepping_core, event->task_id,
                                                                       event->entity_id,
                                                                       event->optional_delay_ms);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_TIME_TASK_CYCLE:
            status = CFE_PSP_SimStepping_Core_ReportTimeTaskCycle(&stepping_core);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_1HZ_BOUNDARY:
            status = CFE_PSP_SimStepping_Core_Report1HzBoundary(&stepping_core);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_TONE_SIGNAL:
            status = CFE_PSP_SimStepping_Core_ReportToneSignal(&stepping_core);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_SCH_SEMAPHORE_WAIT:
            status = CFE_PSP_SimStepping_Core_ReportSchSemaphoreWait(&stepping_core, event->entity_id,
                                                                     event->optional_delay_ms);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_SCH_MINOR_FRAME:
            status = CFE_PSP_SimStepping_Core_ReportSchMinorFrame(&stepping_core);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_SCH_MAJOR_FRAME:
            status = CFE_PSP_SimStepping_Core_ReportSchMajorFrame(&stepping_core);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_SCH_SEND_TRIGGER:
            status = CFE_PSP_SimStepping_Core_ReportSchSendTrigger(&stepping_core, event->entity_id);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_SCH_DISPATCH_COMPLETE:
            status = CFE_PSP_SimStepping_Core_ReportSchDispatchComplete(&stepping_core);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_RECEIVE:
            status = CFE_PSP_SimStepping_Core_ReportCoreServiceCmdPipeReceive(&stepping_core,
                                                                               event->entity_id);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_CORE_SERVICE_CMD_PIPE_COMPLETE:
            status = CFE_PSP_SimStepping_Core_ReportCoreServiceCmdPipeComplete(&stepping_core,
                                                                                event->entity_id);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_TIME_TONE_SEM_CONSUME:
            status = CFE_PSP_SimStepping_Core_ReportTimeToneSemConsume(&stepping_core, event->entity_id);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_TIME_LOCAL_1HZ_SEM_CONSUME:
            status = CFE_PSP_SimStepping_Core_ReportTimeLocal1HzSemConsume(&stepping_core, event->entity_id);
            break;

        case CFE_PSP_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING:
            status = CFE_PSP_SimStepping_Core_MarkSystemReadyForStepping(&stepping_core);
            break;

        default:
            /* Unknown event kind - return error without forwarding */
            status = -1;
            break;
    }

    return status;
}

#ifdef CFE_SIM_STEPPING

/**
 * \brief Query if a requested TaskDelay can be handled by stepping
 *
 * Called by OSAL TaskDelay hooks to determine if the stepping core can take over
 * the delay (return handled=true) or if the caller should proceed with normal
 * wall-clock sleep (return handled=false).
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
 */
bool CFE_PSP_SimStepping_Hook_TaskDelayEligible(uint32_t task_id, uint32_t delay_ms)
{
    /* Check eligibility via core query */
    if (core_initialized)
    {
        return CFE_PSP_SimStepping_Core_QueryTaskDelayEligible(&stepping_core, task_id, delay_ms);
    }

    /* Core not initialized; delay cannot be handled */
    return false;
}

int32_t CFE_PSP_SimStepping_WaitForDelayExpiry(uint32_t task_id, uint32_t delay_ms)
{
    int32_t status;

    if (core_initialized)
    {
        status = CFE_PSP_SimStepping_Core_WaitForDelayExpiry(&stepping_core, task_id, delay_ms);
        if (status == 0)
        {
            status = CFE_PSP_SimStepping_Core_ReportTaskDelayReturn(&stepping_core, task_id);
        }

        return status;
    }

    return -1;
}

#else

bool CFE_PSP_SimStepping_Hook_TaskDelayEligible(uint32_t task_id, uint32_t delay_ms)
{
    return false;
}

int32_t CFE_PSP_SimStepping_WaitForDelayExpiry(uint32_t task_id, uint32_t delay_ms)
{
    return -1;
}

#endif

/***********************************************************************************
                    IN-PROCESS CONTROL ADAPTER IMPLEMENTATIONS
 ***********************************************************************************/

#ifdef CFE_SIM_STEPPING

/**
 * \brief Begin a simulation step (in-process control adapter thin layer)
 *
 * Initiates a new stepping cycle by calling the core's explicit step session
 * begin API. This ensures per-step bookkeeping is properly initialized with
 * an incremented session counter and active flag.
 * Returns immediately without blocking. This is a thin adapter that forwards to
 * the single shared stepping core, which maintains all state and semantics.
 *
 * \return  0 on success (step initiated)
 * \return  -1 if stepping not initialized or core invalid
 */
int32_t CFE_PSP_SimStepping_InProc_BeginStep(void)
{
    int32_t status;

    /* Gate: core must be initialized */
    if (!core_initialized)
    {
        return -1;
    }

    status = CFE_PSP_SimStepping_Core_BeginStepSession(&stepping_core);

    return status;
}

/**
 * \brief Wait for current step to complete (in-process control adapter thin layer)
 *
 * Performs bounded polling against the stepping core until it indicates step
 * completion. This adapter does not implement blocking itself; it queries the core
 * repeatedly with configurable timeout semantics.
 *
 * Conservative polling semantics:
 * - if core not initialized -> fail immediately (return -1)
 * - if step already complete -> succeed immediately (return 0)
 * - if timeout_ms is 0 (PEND_FOREVER) -> poll indefinitely until complete
 * - if timeout_ms is ~0U (non-blocking) -> return immediate ready/not-ready result
 * - if timeout_ms is finite -> poll up to timeout; sleep briefly between polls
 *
 * The core maintains all completion semantics; this adapter is a thin polling wrapper.
 *
 * \param[in]  timeout_ms  Timeout in milliseconds:
 *                         0 = PEND_FOREVER (poll indefinitely)
 *                         ~0U = non-blocking poll (immediate result)
 *                         finite > 0 = timeout in milliseconds
 *
 * \return  0 if step completed successfully
 * \return  -1 if timeout exceeded, core not initialized, or polling failed
 *
 * \note Polling interval uses a conservative 1ms sleep between retries.
 *       This sleep is inside the adapter and does not modify OSAL/TIME/SCH.
 *       No threads, sockets, or second state machine are introduced.
 */
int32_t CFE_PSP_SimStepping_InProc_WaitStepComplete(uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;
    bool is_infinite_wait;
    bool is_nonblocking;
    uint32_t poll_interval_ms = 1;  /* Conservative: 1ms sleep between polls */

    /* Gate: core must be initialized */
    if (!core_initialized)
    {
        return -1;
    }

    if (!stepping_core.session_active)
    {
        return CFE_PSP_SimStepping_Core_RecordDiagnostic(&stepping_core,
                                                         CFE_PSP_SIM_STEPPING_DIAG_ILLEGAL_STATE,
                                                         CFE_PSP_SIM_STEPPING_STATUS_ILLEGAL_STATE,
                                                         "InProc_WaitStepComplete_NoSession",
                                                         timeout_ms,
                                                         0);
    }

    /* Mark explicit wait/check: enables deferred empty-session completion */
    stepping_core.completion_requested = true;

    /* Determine timeout semantics */
    is_nonblocking = (timeout_ms == ~0U);  /* Non-blocking poll: return immediately */
    is_infinite_wait = (timeout_ms == 0);  /* Infinite wait: poll forever */

    /* Poll loop: check completion, with timeout management */
    while (1)
    {
        /* Query core to check if step is complete */
        if (CFE_PSP_SimStepping_Core_IsStepComplete(&stepping_core))
        {
            return 0;  /* Step complete - success */
        }

        /* Handle non-blocking poll: return immediately if not complete */
        if (is_nonblocking)
        {
            return -1;  /* Not complete and non-blocking requested */
        }

        /* Handle infinite wait: loop without timeout check */
        if (is_infinite_wait)
        {
            /* Sleep briefly before retrying (conservative polling) */
            usleep(poll_interval_ms * 1000);
            continue;
        }

        /* Handle finite timeout: check elapsed time */
         if (elapsed_ms >= timeout_ms)
         {
             return CFE_PSP_SimStepping_Core_RecordDiagnostic(&stepping_core,
                                                              CFE_PSP_SIM_STEPPING_DIAG_TIMEOUT,
                                                              CFE_PSP_SIM_STEPPING_STATUS_TIMEOUT,
                                                              "InProc_WaitStepComplete",
                                                              timeout_ms,
                                                              elapsed_ms);
        }

        /* Sleep briefly before retrying, up to timeout */
        usleep(poll_interval_ms * 1000);
        elapsed_ms += poll_interval_ms;
    }
}

/**
 * \brief Query current stepping state (in-process control adapter thin layer)
 *
 * Returns the current state of the stepping core without blocking.
 * This adapter forwards to the core state machine, which is the sole authority
 * on stepping state. The adapter does not cache, interpret, or modify state;
 * it only retrieves and returns what the core owns.
 *
 * \param[out]  state_out     Pointer to store current state enum value (if not NULL)
 * \param[out]  trigger_count Pointer to store current pending trigger count (if not NULL)
 *
 * \return  0 if state query successful
 * \return  -1 if core not initialized or state_out pointer validation failed
 */
int32_t CFE_PSP_SimStepping_InProc_QueryState(uint32_t *state_out, uint32_t *trigger_count)
{
    CFE_PSP_SimStepping_CoreState_t core_state;
    int32_t status;

    /* Gate: core must be initialized */
    if (!core_initialized)
    {
        return -1;
    }

    /* Query core state (required) */
    status = CFE_PSP_SimStepping_Core_QueryState(&stepping_core, &core_state);
    if (status != 0)
    {
        return -1;
    }

    /* Store state if output pointer provided */
    if (state_out != NULL)
    {
        *state_out = (uint32_t)core_state;
    }

    /* Store trigger count if output pointer provided */
    if (trigger_count != NULL)
    {
        *trigger_count = stepping_core.trigger_count;
    }

    return 0;
}

#else

int32_t CFE_PSP_SimStepping_InProc_BeginStep(void)
{
    return -1;
}

int32_t CFE_PSP_SimStepping_InProc_WaitStepComplete(uint32_t timeout_ms)
{
    return -1;
}

int32_t CFE_PSP_SimStepping_InProc_QueryState(uint32_t *state_out, uint32_t *trigger_count)
{
    return -1;
}

#endif

/***********************************************************************************
                     UDS CONTROL ADAPTER IMPLEMENTATIONS
 ***********************************************************************************/

#ifdef CFE_SIM_STEPPING

/**
 * \brief UDS adapter state (Linux-only Unix domain socket endpoint lifecycle)
 *
 * Tracks UDS endpoint initialization status, listener socket fd, and socket path.
 * Initialized in UDS_Init, cleaned up in UDS_Shutdown.
 * Only one UDS adapter instance per PSP module.
 */
static struct {
    bool initialized;         /* True if UDS adapter has been initialized successfully */
    int  listener_fd;         /* AF_UNIX listener socket file descriptor (-1 if not open) */
    char socket_path[256];    /* Unix domain socket path (stable for this environment) */
} uds_adapter = {
    .initialized = false,
    .listener_fd = -1,
    .socket_path = {0}
};

/**
 * \brief Initialize UDS control adapter (thin implementation)
 *
 * Marks the UDS adapter as ready. Returns conservative not-ready status until
 * both core and adapter are initialized. All actual socket operations are
 * deferred to later T10 slices.
 *
 * \return  0 on success (adapter initialized)
 * \return  -1 if stepping not initialized or adapter already initialized
 */
int32_t CFE_PSP_SimStepping_UDS_Init(void)
{
    struct sockaddr_un addr;
    int sock_fd;
    int status;

    if (!core_initialized)
    {
        return -1;
    }

    if (uds_adapter.initialized)
    {
        return -1;
    }

    sock_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_fd < 0)
    {
        return -1;
    }

    snprintf(uds_adapter.socket_path, sizeof(uds_adapter.socket_path),
             "/tmp/cfe_sim_stepping.sock");

    unlink(uds_adapter.socket_path);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, uds_adapter.socket_path, sizeof(addr.sun_path) - 1);

    status = bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (status < 0)
    {
        close(sock_fd);
        return -1;
    }

    status = listen(sock_fd, 5);
    if (status < 0)
    {
        close(sock_fd);
        unlink(uds_adapter.socket_path);
        return -1;
    }

    uds_adapter.listener_fd = sock_fd;
    uds_adapter.initialized = true;

    return 0;
}

/**
 * \brief Wire protocol constants for UDS commands (private to this file)
 */
#define UDS_BEGIN_STEP_OPCODE       1  /* BEGIN_STEP request opcode */
#define UDS_QUERY_STATE_OPCODE      2  /* QUERY_STATE request opcode */
#define UDS_WAIT_STEP_COMPLETE_OPCODE 3 /* WAIT_STEP_COMPLETE request opcode */

/**
 * \brief Wire format for WAIT_STEP_COMPLETE request (private to this file)
 *
 * Fixed-size request carrying the timeout parameter.
 * Opcode field identifies the command; timeout_ms is passed to InProc_WaitStepComplete.
 * All fields in native byte order (host order).
 */
typedef struct
{
    uint8_t  opcode;      /* Command opcode (UDS_WAIT_STEP_COMPLETE_OPCODE) */
    uint32_t timeout_ms;  /* Timeout in milliseconds for wait operation */
} UDS_WaitStepCompleteRequest_t;

#define UDS_REQUEST_SIZE sizeof(UDS_WaitStepCompleteRequest_t) /* Fixed-size request */

/**
 * \brief Wire format for QUERY_STATE response (private to this file)
 *
 * Fixed-size response carrying stepping state and diagnostics.
 * All fields in native byte order (host order).
 */
typedef struct
{
    int32_t status;           /* Result of query: 0=success, non-zero=error */
    uint32_t state;           /* Current core state (enum value) */
    uint32_t trigger_count;   /* Current pending trigger count */
} UDS_QueryStateResponse_t;

/**
  * \brief Service one UDS control request (process fixed-size commands with response)
  *
  * Performs non-blocking accept of at most one pending client connection.
  * If a client is accepted:
  * - Reads exactly UDS_REQUEST_SIZE bytes (request with opcode and optional timeout)
  * - If opcode is UDS_BEGIN_STEP_OPCODE:
  *   - Calls CFE_PSP_SimStepping_InProc_BeginStep()
  *   - Writes back int32_t status response
  * - If opcode is UDS_QUERY_STATE_OPCODE:
  *   - Calls CFE_PSP_SimStepping_InProc_QueryState()
  *   - Writes back fixed-size UDS_QueryStateResponse_t (status, state, trigger_count)
  * - If opcode is UDS_WAIT_STEP_COMPLETE_OPCODE:
  *   - Extracts timeout_ms from request
  *   - Calls CFE_PSP_SimStepping_InProc_WaitStepComplete(timeout_ms)
  *   - Writes back int32_t status response
  * - Closes client connection
  * - Returns 0 (handled successfully)
  *
 * If any error during read, unknown opcode, or write fails:
 * - Closes client connection
 * - Returns transport/protocol status from shared taxonomy
  *
  * If no client pending (EAGAIN/EWOULDBLOCK):
  * - Returns 0 (adapter idle, normal)
  *
 * If core/adapter not initialized or socket error:
 * - Returns CFE_PSP_SIM_STEPPING_STATUS_NOT_READY
  *
  * Returns immediately (non-blocking). One command, one response per client.
  *
 * \return  CFE_PSP_SIM_STEPPING_STATUS_SUCCESS if idle or request served
 * \return  CFE_PSP_SIM_STEPPING_STATUS_NOT_READY if adapter/core is not initialized
 * \return  CFE_PSP_SIM_STEPPING_STATUS_TRANSPORT_ERROR on read/write/accept failures
 * \return  CFE_PSP_SIM_STEPPING_STATUS_PROTOCOL_ERROR on unknown opcode
  */
int32_t CFE_PSP_SimStepping_UDS_Service(void)
{
    int client_fd;
    struct sockaddr_un client_addr;
    socklen_t client_addr_len;
    UDS_WaitStepCompleteRequest_t request;
    int32_t response_status;
    ssize_t bytes_read;
    ssize_t bytes_written;

    if (!core_initialized)
    {
        return CFE_PSP_SIM_STEPPING_STATUS_NOT_READY;
    }

    if (!uds_adapter.initialized)
    {
        return CFE_PSP_SIM_STEPPING_STATUS_NOT_READY;
    }

    if (uds_adapter.listener_fd < 0)
    {
        return CFE_PSP_SIM_STEPPING_STATUS_NOT_READY;
    }

    /* Clear client address structure */
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr_len = sizeof(client_addr);

    /* Non-blocking accept: try to accept at most one pending client */
    client_fd = accept(uds_adapter.listener_fd, (struct sockaddr *)&client_addr, &client_addr_len);

    /* Handle accept result */
    if (client_fd < 0)
    {
        /* Accept returned -1: check errno to distinguish "no client pending" from error */
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            /* No client pending (non-blocking, socket is non-blocking) */
            /* Return 0: adapter idle (not an error, normal case when no request present) */
            return 0;
        }

         /* Other error (e.g., EBADF, EINVAL, system failure) */
         return CFE_PSP_SimStepping_Core_RecordDiagnostic(&stepping_core,
                                                          CFE_PSP_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                          CFE_PSP_SIM_STEPPING_STATUS_TRANSPORT_ERROR,
                                                          "UDS_Service_Accept",
                                                          (uint32_t)errno,
                                                          0);
     }

    /* Client accepted: read one fixed-size request */
    memset(&request, 0, sizeof(request));
    bytes_read = read(client_fd, &request, UDS_REQUEST_SIZE);

    if (bytes_read != UDS_REQUEST_SIZE)
     {
         /* Short read or read error */
         close(client_fd);
        return CFE_PSP_SimStepping_Core_RecordDiagnostic(&stepping_core,
                                                         CFE_PSP_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                         CFE_PSP_SIM_STEPPING_STATUS_TRANSPORT_ERROR,
                                                         "UDS_Service_Read",
                                                         (uint32_t)UDS_REQUEST_SIZE,
                                                         (uint32_t)bytes_read);
    }

    /* Dispatch on opcode */
    if (request.opcode == UDS_BEGIN_STEP_OPCODE)
    {
        /* Call inproc begin step adapter and get status */
        response_status = CFE_PSP_SimStepping_InProc_BeginStep();
        
        /* Write back int32_t status response (native byte order) */
        bytes_written = write(client_fd, &response_status, sizeof(response_status));

        if (bytes_written != sizeof(response_status))
         {
             /* Short write or write error */
             close(client_fd);
             return CFE_PSP_SimStepping_Core_RecordDiagnostic(&stepping_core,
                                                              CFE_PSP_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                              CFE_PSP_SIM_STEPPING_STATUS_TRANSPORT_ERROR,
                                                              "UDS_Service_WriteBegin",
                                                              (uint32_t)sizeof(response_status),
                                                              (uint32_t)bytes_written);
         }
    }
    else if (request.opcode == UDS_QUERY_STATE_OPCODE)
    {
        /* Handle QUERY_STATE request: call inproc adapter and get state */
        uint32_t state_value = 0;
        uint32_t trigger_count = 0;
        UDS_QueryStateResponse_t response = {0};

        /* Call inproc query adapter (fills state_value and trigger_count) */
        response.status = CFE_PSP_SimStepping_InProc_QueryState(&state_value, &trigger_count);
        
        /* Populate response struct with state and trigger count */
        response.state = state_value;
        response.trigger_count = trigger_count;

        /* Write back fixed-size response (native byte order) */
        bytes_written = write(client_fd, &response, sizeof(response));

        if (bytes_written != sizeof(response))
         {
             /* Short write or write error */
             close(client_fd);
            return CFE_PSP_SimStepping_Core_RecordDiagnostic(&stepping_core,
                                                             CFE_PSP_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                             CFE_PSP_SIM_STEPPING_STATUS_TRANSPORT_ERROR,
                                                             "UDS_Service_WriteQuery",
                                                             (uint32_t)sizeof(response),
                                                             (uint32_t)bytes_written);
        }
    }
    else if (request.opcode == UDS_WAIT_STEP_COMPLETE_OPCODE)
    {
        /* Handle WAIT_STEP_COMPLETE request: extract timeout and call inproc adapter */
        response_status = CFE_PSP_SimStepping_InProc_WaitStepComplete(request.timeout_ms);
        
        /* Write back int32_t status response (native byte order) */
        bytes_written = write(client_fd, &response_status, sizeof(response_status));

        if (bytes_written != sizeof(response_status))
         {
             /* Short write or write error */
             close(client_fd);
            return CFE_PSP_SimStepping_Core_RecordDiagnostic(&stepping_core,
                                                             CFE_PSP_SIM_STEPPING_DIAG_TRANSPORT_ERROR,
                                                             CFE_PSP_SIM_STEPPING_STATUS_TRANSPORT_ERROR,
                                                             "UDS_Service_WriteWait",
                                                             (uint32_t)sizeof(response_status),
                                                             (uint32_t)bytes_written);
        }
    }
    else
     {
         close(client_fd);
         return CFE_PSP_SimStepping_Core_RecordDiagnostic(&stepping_core,
                                                          CFE_PSP_SIM_STEPPING_DIAG_PROTOCOL_ERROR,
                                                          CFE_PSP_SIM_STEPPING_STATUS_PROTOCOL_ERROR,
                                                          "UDS_Service_UnknownOpcode",
                                                          request.opcode,
                                                          0);
    }

    /* Close client connection after successful request/response */
    close(client_fd);

    /* Return success: request processed and client served */
    return 0;
}

/**
  * \brief Shutdown UDS control adapter (thin stub implementation)
  *
  * Marks the UDS adapter as shut down. Returns conservative error status if
  * adapter was never initialized. The shared stepping core is NOT affected.
  * No actual socket cleanup at this stage; real socket cleanup will be added
  * in later T10 slices.
  *
  * \return  0 on success (adapter shut down)
  * \return  -1 if adapter not initialized or shutdown failed
  */
int32_t CFE_PSP_SimStepping_UDS_Shutdown(void)
{
    if (!uds_adapter.initialized)
    {
        return -1;
    }

    if (uds_adapter.listener_fd >= 0)
    {
        close(uds_adapter.listener_fd);
        uds_adapter.listener_fd = -1;
    }

    if (uds_adapter.socket_path[0] != '\0')
    {
        unlink(uds_adapter.socket_path);
        memset(uds_adapter.socket_path, 0, sizeof(uds_adapter.socket_path));
    }

    uds_adapter.initialized = false;

    return 0;
}

/**
 * \brief Service UDS adapter (non-blocking, single request per call)
 *
 * Thin wrapper around CFE_PSP_SimStepping_UDS_Service() for use in periodic
 * stepping hooks. Returns immediately whether a request was present or not.
 * Suitable for calling from timer ticks, stepping hooks, or tight polling loops.
 *
 * \return  0 if no client pending or client request processed successfully
 * \return  -1 if adapter not initialized or service failed
 */
int32_t CFE_PSP_SimStepping_UDS_RunOnce(void)
{
    return CFE_PSP_SimStepping_UDS_Service();
}

#else

int32_t CFE_PSP_SimStepping_UDS_Init(void)
{
    return -1;
}

int32_t CFE_PSP_SimStepping_UDS_Service(void)
{
    return -1;
}

int32_t CFE_PSP_SimStepping_UDS_Shutdown(void)
{
    return -1;
}

int32_t CFE_PSP_SimStepping_UDS_RunOnce(void)
{
    return -1;
}

#endif

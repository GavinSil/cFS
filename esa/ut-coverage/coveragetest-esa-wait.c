/**
 * @file
 * @ingroup esa
 * @brief       ESA 等待机制单元测试
 * @author      gaoyuan
 * @date        2026-03-30
 *
 * @details     测试 ESA 等待功能，包括消息等待、信号量等待、条件变量等待和延迟等待。
 */
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

/**
 * @brief       等待测试时间上下文
 * @details     控制仿真时间钩子的当前时间、推进步长与返回成功标志。
 */
typedef struct
{
    uint64 NowNs;          /*!< 当前仿真时间（纳秒） */
    uint64 AdvanceNs;      /*!< 每次查询后推进的仿真时间（纳秒） */
    bool   GetTimeSuccess; /*!< 获取时间钩子是否返回成功 */
} ESA_WaitTestContext_t;

/** 等待测试共享时间上下文 */
static ESA_WaitTestContext_t ESA_WaitCtx;
/** 保护等待测试共享时间上下文的互斥锁 */
static pthread_mutex_t ESA_WaitCtxMutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief       任务 ID 序列上下文
 * @details     为 OS_TaskGetId 桩处理器生成可预测的任务 ID 序列。
 */
typedef struct
{
    uint32 TaskGetIdCount; /*!< 剩余可分配的任务 ID 计数 */
} ESA_TaskIdSeqContext_t;

/** 任务 ID 序列测试上下文 */
static ESA_TaskIdSeqContext_t ESA_TaskIdCtx;

/**
 * @brief       等待线程执行上下文
 * @details     保存等待线程需要的资源标识、超时参数与完成结果。
 */
typedef struct
{
    osal_id_t ResourceId; /*!< 被等待的资源标识符 */
    uint32    TimeoutMs;  /*!< 等待超时时间（毫秒） */
    int32     Status;     /*!< 线程执行后的返回状态 */
    bool      Done;       /*!< 线程是否已执行完成 */
} ESA_WaitThreadInfo_t;

/**
 * @brief       模拟步进时间获取钩子
 * @details     从测试上下文返回仿真时间，并在成功路径上推进下一次查询时刻。
 * @param[out]  sim_time_ns 仿真时间输出指针
 * @retval      true        成功返回仿真时间
 * @retval      false       测试上下文要求失败或输出指针为空
 */
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

/**
 * @brief       消息等待线程入口
 * @details     在线程中调用 ESA_WaitForMessage，并把结果写回线程上下文。
 * @param[in,out] arg 指向 ESA_WaitThreadInfo_t 的线程参数
 * @retval        NULL 始终返回空指针
 */
static void *ESA_Wait_MessageThread(void *arg)
{
    ESA_WaitThreadInfo_t *thread_info;

    thread_info         = (ESA_WaitThreadInfo_t *)arg;
    thread_info->Status = ESA_WaitForMessage(thread_info->ResourceId, thread_info->TimeoutMs);
    thread_info->Done   = true;

    return NULL;
}

/**
 * @brief       信号量等待线程入口
 * @details     在线程中调用 ESA_WaitForSem，并把结果写回线程上下文。
 * @param[in,out] arg 指向 ESA_WaitThreadInfo_t 的线程参数
 * @retval        NULL 始终返回空指针
 */
static void *ESA_Wait_SemThread(void *arg)
{
    ESA_WaitThreadInfo_t *thread_info;

    thread_info         = (ESA_WaitThreadInfo_t *)arg;
    thread_info->Status = ESA_WaitForSem(thread_info->ResourceId, thread_info->TimeoutMs);
    thread_info->Done   = true;

    return NULL;
}

/**
 * @brief       条件变量等待线程入口
 * @details     在线程中调用 ESA_WaitForCondVar，并把结果写回线程上下文。
 * @param[in,out] arg 指向 ESA_WaitThreadInfo_t 的线程参数
 * @retval        NULL 始终返回空指针
 */
static void *ESA_Wait_CondVarThread(void *arg)
{
    ESA_WaitThreadInfo_t *thread_info;

    thread_info         = (ESA_WaitThreadInfo_t *)arg;
    thread_info->Status = ESA_WaitForCondVar(thread_info->ResourceId, thread_info->TimeoutMs);
    thread_info->Done   = true;

    return NULL;
}

/**
 * @brief       统计已完成线程数量
 * @details     遍历线程上下文数组，统计 Done 标志已置位的线程个数。
 * @param[in]   thread_info 线程上下文数组
 * @param[in]   thread_count 数组中的线程数量
 * @retval      done_count 已完成线程数量
 */
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

/**
 * @brief       等待 clock_gettime 调用次数达到预期
 * @details     通过短暂休眠轮询桩调用计数，等待后台线程进入等待逻辑。
 * @param[in]   expected_count 期望的 clock_gettime 调用次数
 */
static void ESA_Wait_WaitForClockGettimeCalls(uint32 expected_count)
{
    uint32 tries;

    for (tries = 0; tries < 200; ++tries)
    {
        if (UT_GetStubCount(UT_KEY(clock_gettime)) >= expected_count)
        {
            break;
        }

        clock_nanosleep(CLOCK_MONOTONIC, 0, &(struct timespec) {0, 5000000}, NULL);
    }
}

/**
 * @brief       设置测试用任务 ID 数量
 * @details     初始化任务 ID 序列上下文，为后续 OS_TaskGetId 桩调用提供数量上界。
 * @param[in]   task_count 要生成的任务数量
 */
static void ESA_Wait_SetTaskIds(uint32 task_count)
{
    ESA_TaskIdCtx.TaskGetIdCount = task_count;
}

/**
 * @brief       处理 OS_TaskGetId 测试桩调用
 * @details     为每次 OS_TaskGetId 调用返回一个可预测的对象标识符，并递减剩余计数。
 * @param[in,out] user_obj 任务 ID 序列上下文
 * @param[in]     func_key 桩函数键值（未使用）
 * @param[in]     context  桩函数上下文（未使用）
 */
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

/**
 * @brief       重置等待测试环境
 * @details     清空 UT 桩状态，恢复仿真时间上下文，并安装默认的任务 ID 处理器。
 */
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

/**
 * @brief       验证消息等待被通知唤醒
 * @details     创建消息等待线程并发送队列通知，确认等待结果为资源唤醒。
 */
void Test_ESA_WaitForMessage_BasicWakeByNotify(void)
{
    pthread_t            waiter_thread;
    ESA_WaitThreadInfo_t thread_info;
    osal_id_t            queue_id;

    queue_id               = OS_ObjectIdFromInteger(0x00020003);
    thread_info.ResourceId = queue_id;
    thread_info.TimeoutMs  = 20000;
    thread_info.Status     = -1;
    thread_info.Done       = false;

    ESA_Wait_SetTaskIds(1);

    UtAssert_BOOL_TRUE(pthread_create(&waiter_thread, NULL, ESA_Wait_MessageThread, &thread_info) == 0);
    ESA_Wait_WaitForClockGettimeCalls(1);

    ESA_NotifyQueuePut(queue_id);

    UtAssert_BOOL_TRUE(pthread_join(waiter_thread, NULL) == 0);
    UtAssert_BOOL_TRUE(thread_info.Done);
    UtAssert_INT32_EQ(thread_info.Status, ESA_WOKE_BY_RESOURCE);
}

/**
 * @brief       验证消息等待随仿真时间推进超时
 * @details     配置仿真时间步进后调用消息等待接口，确认其按超时路径返回。
 */
void Test_ESA_WaitForMessage_TimeoutBySimTimeProgression(void)
{
    osal_id_t queue_id;
    int32     status;

    queue_id              = OS_ObjectIdFromInteger(0x00020011);
    ESA_WaitCtx.NowNs     = 0;
    ESA_WaitCtx.AdvanceNs = 200000000;

    ESA_Wait_SetTaskIds(1);
    status = ESA_WaitForMessage(queue_id, 100);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

/**
 * @brief       验证信号量等待被通知唤醒
 * @details     创建信号量等待线程并发送 give 通知，确认等待结果为资源唤醒。
 */
void Test_ESA_WaitForSem_BasicWakeByNotify(void)
{
    pthread_t            waiter_thread;
    ESA_WaitThreadInfo_t thread_info;
    osal_id_t            sem_id;

    sem_id                 = OS_ObjectIdFromInteger(0x00040007);
    thread_info.ResourceId = sem_id;
    thread_info.TimeoutMs  = 20000;
    thread_info.Status     = -1;
    thread_info.Done       = false;

    ESA_Wait_SetTaskIds(1);

    UtAssert_BOOL_TRUE(pthread_create(&waiter_thread, NULL, ESA_Wait_SemThread, &thread_info) == 0);
    ESA_Wait_WaitForClockGettimeCalls(1);

    ESA_NotifySemGive(sem_id);

    UtAssert_BOOL_TRUE(pthread_join(waiter_thread, NULL) == 0);
    UtAssert_BOOL_TRUE(thread_info.Done);
    UtAssert_INT32_EQ(thread_info.Status, ESA_WOKE_BY_RESOURCE);
}

/**
 * @brief       验证非零延迟最终按超时路径返回
 * @details     通过推进仿真时间触发延迟截止点，确认延迟等待返回超时结果。
 */
void Test_ESA_WaitForDelay_TimeoutAfterNonZeroDelay(void)
{
    int32 status;

    ESA_WaitCtx.NowNs     = 0;
    ESA_WaitCtx.AdvanceNs = 200000000;

    ESA_Wait_SetTaskIds(1);
    status = ESA_WaitForDelay(100);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

/**
 * @brief       验证信号量刷新唤醒全部等待者
 * @details     创建多个信号量等待线程并发送 flush 通知，确认全部线程以刷新原因返回。
 */
void Test_ESA_NotifySemFlush_WakesAllWaiters(void)
{
    pthread_t            waiter_threads[3];
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

/**
 * @brief       验证任务注册与注销生命周期
 * @details     注册当前任务后执行延迟等待，再重复注销以确认生命周期接口具备幂等性。
 */
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

/**
 * @brief       验证条件变量单播仅唤醒一个等待者
 * @details     创建两个条件变量等待线程，先单播后广播，并确认单播阶段仅有一个线程完成。
 */
void Test_ESA_WaitForCondVar_SignalWakeOne(void)
{
    pthread_t            waiter_threads[2];
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
    UtAssert_BOOL_TRUE(clock_nanosleep(CLOCK_MONOTONIC, 0, &(struct timespec) {0, 50000000}, NULL) == 0);

    done_count = ESA_Wait_GetDoneCount(thread_info, 2);
    UtAssert_UINT32_EQ(done_count, 1);

    ESA_NotifyCondVar(condvar_id, true);

    for (idx = 0; idx < 2; ++idx)
    {
        UtAssert_BOOL_TRUE(pthread_join(waiter_threads[idx], NULL) == 0);
        UtAssert_INT32_EQ(thread_info[idx].Status, ESA_WOKE_BY_RESOURCE);
    }
}

/**
 * @brief       验证条件变量等待超时
 * @details     配置仿真时间推进后调用条件变量等待接口，确认其按超时路径返回。
 */
void Test_ESA_WaitForCondVar_Timeout(void)
{
    osal_id_t condvar_id;
    int32     status;

    condvar_id            = OS_ObjectIdFromInteger(0x00050007);
    ESA_WaitCtx.NowNs     = 0;
    ESA_WaitCtx.AdvanceNs = 200000000;

    ESA_Wait_SetTaskIds(1);
    status = ESA_WaitForCondVar(condvar_id, 100);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

/**
 * @brief       验证条件变量广播唤醒全部等待者
 * @details     创建多个条件变量等待线程并广播通知，确认所有线程都以资源唤醒返回。
 */
void Test_ESA_NotifyCondVar_BroadcastWakesAll(void)
{
    pthread_t            waiter_threads[3];
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

/**
 * @brief       验证单次队列放入仅唤醒一个消费者
 * @details     创建两个消息等待线程，先发送一次队列通知并确认仅一个线程完成，再补齐剩余唤醒。
 */
void Test_ESA_WaitForMessage_TwoConsumersOnePutWakesOne(void)
{
    pthread_t            waiter_threads[2];
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
    UtAssert_BOOL_TRUE(clock_nanosleep(CLOCK_MONOTONIC, 0, &(struct timespec) {0, 10000000}, NULL) == 0);

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

/**
 * @brief       验证零超时消息等待立即返回
 * @details     以 OS_CHECK 调用消息等待接口，确认其立即返回超时结果。
 */
void Test_ESA_WaitForMessage_ZeroTimeout_IsImmediateTimeout(void)
{
    osal_id_t queue_id;
    int32     status;

    queue_id = OS_ObjectIdFromInteger(0x00020003);
    ESA_Wait_SetTaskIds(1);
    status = ESA_WaitForMessage(queue_id, OS_CHECK);

    UtAssert_INT32_EQ(status, ESA_WOKE_BY_TIMEOUT);
}

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

/**
 * @brief       注册 ESA 等待测试用例
 * @details     将本文件中的等待、通知与生命周期测试统一加入 ut_assert 框架。
 */
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

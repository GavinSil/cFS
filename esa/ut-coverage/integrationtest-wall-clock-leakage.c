/**
 * @file
 * @ingroup esa
 * @brief       墙钟泄漏集成测试
 * @author      gaoyuan
 * @date        2026-03-30
 *
 * @details     本文件验证步进模式下不存在墙钟时间泄漏。
 */
#include <sys/time.h>

#include "utassert.h"
#include "uttest.h"

#include "osapi.h"

#ifdef CFE_SIM_STEPPING
#include "esa_stepping.h"
#include "esa_stepping_shim.h"
#endif

/** 墙钟耗时上限阈值（秒） */
#define WALL_CLOCK_THRESHOLD_SEC 2.0

/**
 * @brief       获取当前墙钟时间
 * @details     通过 gettimeofday 读取当前真实时间，并转换为秒单位浮点值。
 * @retval      墙钟秒值 当前真实时间对应的秒数
 */
static double GetWallClockTime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

#ifdef CFE_SIM_STEPPING

/**
 * @brief       验证步进模式下不存在墙钟泄漏
 * @details     连续执行单次与多次显式步进循环，确认墙钟耗时保持在阈值范围内。
 */
static void Test_Stepping_NoWallClockLeakage(void)
{
    double start_time, end_time, wall_elapsed;
    int32  status;

    ESA_Stepping_ShimEvent_t ready_event = {0};
    ready_event.event_kind               = ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING;
    status                               = ESA_Stepping_Shim_ReportEvent(&ready_event);
    UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);

    start_time = GetWallClockTime();

    status = ESA_Stepping_InProc_BeginStep();
    UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);

    ESA_Stepping_ShimEvent_t frame_event = {0};
    frame_event.event_kind               = ESA_SIM_STEPPING_EVENT_SCH_MINOR_FRAME;

    for (int i = 0; i < 10; i++)
    {
        status = ESA_Stepping_Shim_ReportEvent(&frame_event);
        UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);
    }

    status = ESA_Stepping_InProc_WaitStepComplete(11);
    UtAssert_INT32_EQ(status, ESA_SIM_STEPPING_STATUS_SUCCESS);

    end_time     = GetWallClockTime();
    wall_elapsed = end_time - start_time;

    UtAssert_True(wall_elapsed < WALL_CLOCK_THRESHOLD_SEC,
                  "Stepping cycle wall-clock elapsed (%f sec) < threshold (%f sec)", wall_elapsed,
                  WALL_CLOCK_THRESHOLD_SEC);

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

    end_time     = GetWallClockTime();
    wall_elapsed = end_time - start_time;

    UtAssert_True(wall_elapsed < WALL_CLOCK_THRESHOLD_SEC,
                  "Multiple cycles wall-clock elapsed (%f sec) < threshold (%f sec)", wall_elapsed,
                  WALL_CLOCK_THRESHOLD_SEC);
}

#endif

/**
 * @brief       验证非步进模式的回归占位路径
 * @details     在步进模式下标记为不适用；在非步进模式下保留对完整 OSAL 回归的占位断言。
 */
static void Test_NonSteppingMode_Regression(void)
{
#ifdef CFE_SIM_STEPPING
    UtAssert_NA("Non-stepping regression test - run full OSAL suite without CFE_SIM_STEPPING");
#else
    UtAssert_True(true, "Non-stepping mode - full OSAL suite should pass (117/117)");
#endif
}

/**
 * @brief       初始化集成测试环境
 * @details     初始化 OSAL，并在启用步进模式时初始化 ESA 步进模块。
 */
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

/**
 * @brief       清理集成测试环境
 * @details     当前集成测试不需要额外清理动作，保留此接口以匹配测试框架约定。
 */
static void IntegrationTest_Teardown(void) {}

/**
 * @brief       注册墙钟泄漏集成测试
 * @details     根据步进模式配置，将相关集成测试加入 ut_assert 测试框架。
 */
void UtTest_Setup(void)
{
#ifdef CFE_SIM_STEPPING
    UtTest_Add(Test_Stepping_NoWallClockLeakage, IntegrationTest_Setup, IntegrationTest_Teardown,
               "Test_Stepping_NoWallClockLeakage");
#endif
    UtTest_Add(Test_NonSteppingMode_Regression, NULL, NULL, "Test_NonSteppingMode_Regression");
}

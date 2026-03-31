/**
 * @file
 * @ingroup esa
 * @brief       模拟步进模块单元测试
 * @author      gaoyuan
 * @date        2026-03-18
 *
 * @details     本文件包含 ESA 模拟步进功能的覆盖率测试用例。
 */
#include <stdint.h>

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include "common_types.h"
#include "esa_stepping.h"
#include "esa_stepping_core.h"
#include "esa_stepping_shim.h"

extern void ESA_Init(void);

/**
 * @brief       验证步进状态码分类值
 * @details     检查公开状态码常量是否保持既定数值，避免接口语义意外漂移。
 */
void Test_sim_stepping_StatusTaxonomy(void)
{
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_SUCCESS, 0);
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_FAILURE, -1);
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN, -2);
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_NOT_READY, -3);
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_TIMEOUT, -4);
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_ILLEGAL_COMPLETE, -5);
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_TRANSPORT_ERROR, -6);
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_PROTOCOL_ERROR, -7);
    UtAssert_INT32_EQ(ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE, -8);
}

/**
 * @brief       验证未就绪状态下的接口返回路径
 * @details     在未初始化或未声明生命周期就绪前，检查步进入口、等待和查询接口的返回值。
 */
void Test_sim_stepping_NotReadyPaths(void)
{
    uint32_t state    = 1234;
    uint32_t triggers = 5678;

    UtAssert_INT32_EQ(ESA_Stepping_InProc_BeginStep(), ESA_SIM_STEPPING_STATUS_NOT_READY);
    UtAssert_INT32_EQ(ESA_Stepping_InProc_WaitStepComplete(1), ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE);
    UtAssert_INT32_EQ(ESA_Stepping_InProc_QueryState(&state, &triggers), ESA_SIM_STEPPING_STATUS_SUCCESS);
}

/**
 * @brief       验证初始化前后的查询与非法状态路径
 * @details     先检查未初始化阶段的返回码，再初始化模块并验证查询接口与非法等待路径。
 */
void Test_sim_stepping_InProcQueryAndIllegalState(void)
{
    uint32_t State        = 99;
    uint32_t TriggerCount = 99;
    uint32_t state        = 0;
    uint32_t triggers     = 0;

    /* 尚未初始化 */
    UtAssert_INT32_EQ(ESA_Stepping_InProc_BeginStep(), ESA_SIM_STEPPING_STATUS_NOT_READY);
    UtAssert_INT32_EQ(ESA_Stepping_InProc_WaitStepComplete(0), ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE);
    UtAssert_INT32_EQ(ESA_Stepping_InProc_QueryState(&State, &TriggerCount), ESA_SIM_STEPPING_STATUS_SUCCESS);

    UtAssert_VOIDCALL(ESA_Init());

    UtAssert_INT32_EQ(ESA_Stepping_InProc_QueryState(&state, &triggers), ESA_SIM_STEPPING_STATUS_SUCCESS);
    UtAssert_INT32_EQ(state, ESA_SIM_STEPPING_STATE_READY);

    UtAssert_INT32_EQ(ESA_Stepping_InProc_QueryState(NULL, &triggers), ESA_SIM_STEPPING_STATUS_SUCCESS);
    UtAssert_INT32_EQ(ESA_Stepping_InProc_QueryState(&state, NULL), ESA_SIM_STEPPING_STATUS_SUCCESS);

    UtAssert_INT32_EQ(ESA_Stepping_InProc_WaitStepComplete(1), ESA_SIM_STEPPING_STATUS_ILLEGAL_STATE);
}

/**
 * @brief       验证重复开始与完成等待路径
 * @details     构造系统就绪与小帧事件，确认重复 BEGIN_STEP 会被拒绝，且步进能够正常完成。
 */
void Test_sim_stepping_BeginDuplicateAndComplete(void)
{
    uint32_t state    = 0;
    uint32_t triggers = 0;

    UtAssert_VOIDCALL(ESA_Init());

    {
        ESA_Stepping_ShimEvent_t event = {0};
        event.event_kind               = ESA_SIM_STEPPING_EVENT_SYSTEM_READY_FOR_STEPPING;
        UtAssert_INT32_EQ(ESA_Stepping_Shim_ReportEvent(&event), ESA_SIM_STEPPING_STATUS_SUCCESS);
    }

    UtAssert_INT32_EQ(ESA_Stepping_InProc_BeginStep(), ESA_SIM_STEPPING_STATUS_SUCCESS);
    UtAssert_INT32_EQ(ESA_Stepping_InProc_BeginStep(), ESA_SIM_STEPPING_STATUS_DUPLICATE_BEGIN);

    {
        ESA_Stepping_ShimEvent_t event = {0};
        event.event_kind               = ESA_SIM_STEPPING_EVENT_SCH_MINOR_FRAME;
        UtAssert_INT32_EQ(ESA_Stepping_Shim_ReportEvent(&event), ESA_SIM_STEPPING_STATUS_SUCCESS);
    }

    UtAssert_INT32_EQ(ESA_Stepping_InProc_QueryState(&state, &triggers), ESA_SIM_STEPPING_STATUS_SUCCESS);
    UtAssert_INT32_EQ(state, ESA_SIM_STEPPING_STATE_RUNNING);

    UtAssert_INT32_EQ(ESA_Stepping_InProc_WaitStepComplete(2), ESA_SIM_STEPPING_STATUS_SUCCESS);
}

/**
 * @brief       验证生命周期就绪前的开始步进行为
 * @details     初始化核心后不注入系统就绪事件，确认 BEGIN_STEP 仍返回未就绪状态。
 */
void Test_sim_stepping_BeginNotReadyBeforeLifecycle(void)
{
    UtAssert_VOIDCALL(ESA_Init());
    UtAssert_INT32_EQ(ESA_Stepping_InProc_BeginStep(), ESA_SIM_STEPPING_STATUS_NOT_READY);
}

#define ADD_TEST(test) UtTest_Add(test, ResetTest, NULL, #test)

/**
 * @brief       重置测试环境
 * @details     清空 UT 桩状态，确保每个测试用例从一致的初始环境开始。
 */
void ResetTest(void)
{
    UT_ResetState(0);
}

/**
 * @brief       注册模拟步进测试用例
 * @details     将本文件中的所有测试函数加入 ut_assert 测试框架。
 */
void UtTest_Setup(void)
{
    ADD_TEST(Test_sim_stepping_StatusTaxonomy);
    ADD_TEST(Test_sim_stepping_NotReadyPaths);
    ADD_TEST(Test_sim_stepping_InProcQueryAndIllegalState);
    ADD_TEST(Test_sim_stepping_BeginNotReadyBeforeLifecycle);
    ADD_TEST(Test_sim_stepping_BeginDuplicateAndComplete);
}

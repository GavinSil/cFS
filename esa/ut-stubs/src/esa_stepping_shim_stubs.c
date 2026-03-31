/**
 * @file
 * @ingroup esa
 * @brief       ESA 步进 Shim 桩函数实现
 * @author      gaoyuan
 * @date        2026-03-20
 *
 * @details     本文件提供 ESA_Stepping_Shim_ReportEvent 函数的通用桩实现。
 */

#include "utgenstub.h"

/**
 * @brief       桩化步进事件上报入口
 * @details     为单元测试记录传入事件参数，并返回预配置的桩返回值。
 * @param[in]   event  步进事件描述符指针
 * @retval      桩返回值  由 UT_GenStub 预先配置的返回结果
 */
int32_t ESA_Stepping_Shim_ReportEvent(const void *event)
{
    UT_GenStub_SetupReturnBuffer(ESA_Stepping_Shim_ReportEvent, int32_t);

    UT_GenStub_AddParam(ESA_Stepping_Shim_ReportEvent, const void *, event);

    UT_GenStub_Execute(ESA_Stepping_Shim_ReportEvent, Basic, NULL);

    return UT_GenStub_GetReturnValue(ESA_Stepping_Shim_ReportEvent, int32_t);
}

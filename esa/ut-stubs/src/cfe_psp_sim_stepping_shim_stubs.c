#include "utgenstub.h"

int32_t CFE_PSP_SimStepping_Shim_ReportEvent(const void *event)
{
    UT_GenStub_SetupReturnBuffer(CFE_PSP_SimStepping_Shim_ReportEvent, int32_t);

    UT_GenStub_AddParam(CFE_PSP_SimStepping_Shim_ReportEvent, const void *, event);

    UT_GenStub_Execute(CFE_PSP_SimStepping_Shim_ReportEvent, Basic, NULL);

    return UT_GenStub_GetReturnValue(CFE_PSP_SimStepping_Shim_ReportEvent, int32_t);
}

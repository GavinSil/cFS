#include "utgenstub.h"

int32_t ESA_Stepping_Shim_ReportEvent(const void *event)
{
    UT_GenStub_SetupReturnBuffer(ESA_Stepping_Shim_ReportEvent, int32_t);

    UT_GenStub_AddParam(ESA_Stepping_Shim_ReportEvent, const void *, event);

    UT_GenStub_Execute(ESA_Stepping_Shim_ReportEvent, Basic, NULL);

    return UT_GenStub_GetReturnValue(ESA_Stepping_Shim_ReportEvent, int32_t);
}

# Decisions

- 决策时间: 2026-03-23
- 决策: 不修改 `esa_stepping_core.c` 状态机逻辑，
  仅在 `esa_stepping.c::ESA_Stepping_InProc_WaitStepComplete()` 轮询阶段补充主动驱动。
- 理由: 根因是步进模式下外部时钟链路断开导致 `completion_ready` 无法自然置位；
  通过适配层主动调用 `ReportSchMinorFrame` 更符合“外部控制器驱动时间推进”的架构边界，
  且 `ReportSchMinorFrame` 已具备幂等保护（`completion_ready` 已置位时直接返回）。

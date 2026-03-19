# SCH v7 Port — Learnings

## 2026-03-05 Session: ses_34345babdffefpQxLMdCnNnuZe

### Key API Patterns (v7)
- `CFE_MSG_GetMsgId` — OUT-PARAMETER: `CFE_MSG_GetMsgId(&buf->Msg, &msgId)` — NOT a return value
- `CFE_MSG_GetFcnCode` — OUT-PARAMETER: `CFE_MSG_GetFcnCode(&buf->Msg, &cmdCode)` 
- `CFE_MSG_GetSize` — OUT-PARAMETER: writes `CFE_MSG_Size_t` (size_t), NOT int32
- `CFE_MSG_Init` — NO `Clear` boolean: `CFE_MSG_Init(CFE_MSG_PTR(pkt.TlmHeader), CFE_SB_ValueToMsgId(MID), sizeof(pkt))`
- `CFE_SB_ReceiveBuffer` — replaces `CFE_SB_RcvMsg`
- `CFE_SB_TransmitMsg` — replaces `CFE_SB_SendMsg`, requires second `bool` param for header increment
- `CFE_SB_MsgId_Equal()` — MUST use for MsgId comparison, NOT `==`

### Type Replacements
- `CFE_SB_MsgPtr_t` → `CFE_SB_Buffer_t *` (for received buffers) or `CFE_MSG_Message_t *` (general)
- `CFE_SB_Msg_t` → `CFE_MSG_Message_t`
- `uint32 AppID` → `CFE_ES_AppId_t AppID`
- `uint32 TimerId` → `osal_id_t TimerId`
- `uint32 TimeSemaphore` → `osal_id_t TimeSemaphore`

### Macro Replacements
- `CFE_SB_CMD_HDR_SIZE` → `sizeof(CFE_MSG_CommandHeader_t)`
- `CFE_SB_TLM_HDR_SIZE` → `sizeof(CFE_MSG_TelemetryHeader_t)`
- `CFE_SB_HIGHEST_VALID_MSGID` → `0xFFFF`
- `uint8 CmdHeader[CFE_SB_CMD_HDR_SIZE]` → `CFE_MSG_CommandHeader_t CmdHeader`
- `uint8 TlmHeader[CFE_SB_TLM_HDR_SIZE]` → `CFE_MSG_TelemetryHeader_t TlmHeader`

### EVS Enum Renames
- `CFE_EVS_ERROR` → `CFE_EVS_EventType_ERROR`
- `CFE_EVS_INFORMATION` → `CFE_EVS_EventType_INFORMATION`
- `CFE_EVS_DEBUG` → `CFE_EVS_EventType_DEBUG`
- `CFE_EVS_CRITICAL` → `CFE_EVS_EventType_CRITICAL`

### ES Enum Renames
- `CFE_ES_APP_RUN` → `CFE_ES_RunStatus_APP_RUN`
- `CFE_ES_APP_ERROR` → `CFE_ES_RunStatus_APP_ERROR`

### Removed Functions
- `CFE_ES_RegisterApp()` — auto-registered in v7, remove the call
- `CFE_SB_InitMsg` → `CFE_MSG_Init` (different signature)
- `CFE_SB_SendMsg` → `CFE_SB_TransmitMsg(msg, true)`
- `CFE_SB_RcvMsg` → `CFE_SB_ReceiveBuffer`
- `CFE_SB_GetMsgId` → `CFE_MSG_GetMsgId` (out-param)
- `CFE_SB_GetCmdCode` → `CFE_MSG_GetFcnCode` (out-param)
- `CFE_SB_GetTotalMsgLength` → `CFE_MSG_GetSize` (out-param)

### Important Gotchas
- `sch_app.c:944` — inside a comment block, DO NOT EDIT
- `sch_def_schtbl.c`, `sch_def_msgtbl.c` — TABLE DATA files, DO NOT TOUCH
- `fsw/unit_test/` — out of scope
- `sch_tbldefs.h` — `uint16 MessageBuffer[]` is intentional, DO NOT CHANGE
- MsgId dispatch: Replace `switch(MessageID)` with `if/else if` + `CFE_SB_MsgId_Equal()`
- MsgId printf: Wrap with `CFE_SB_MsgIdToValue(MessageID)` when used in format strings
- `SCH_UNUSED_MID` is app constant (0x1897), NOT `CFE_SB_INVALID_MSG_ID` (0) — keep as-is

### Files NOT to Modify
- `sch_def_schtbl.c`, `sch_def_msgtbl.c`
- `fsw/unit_test/` directory
- `sch_tbldefs.h`
- `sch_msgids.h`

### Build Command
```bash
cd /workspace/cFS-sch-work && make SIMULATION=native OMIT_DEPRECATED=true prep && make
```

## 2026-03-05 F3 Scope Fidelity — Session ses_343285d03ffeYEJl4vMGSA97br

### F3 VERDICT: APPROVE (8/8 tasks compliant)

### Unaccounted changes found (all necessary, not scope creep):
- `.gitattributes` deleted (linguist metadata only, harmless)
- `CMakeLists.txt` — removed deprecated `cmake_minimum_required(VERSION 2.6.4)`
- `fsw/public_inc/sch_api.h` — `boolean`→`bool` in `SCH_GetProcessingState()` return type (required: sch_app.c uses `== true` comparison)
- `fsw/src/sch_api.c` — same `boolean`→`bool` (required to match header)
- `fsw/src/sch_apipriv.h` — same `boolean`→`bool` (required)

### Key insight: `boolean` type completely removed in cFE v7
- The `boolean` typedef is gone in v7 — `bool` (C99 `<stdbool.h>`) is the replacement everywhere
- Any struct/function that uses `boolean` MUST be updated even if not explicitly listed in the spec
- This caused extra changes in sch_msg.h, sch_app.h, sch_cmds.h, sch_api.h, sch_api.c, sch_apipriv.h beyond what the spec listed

### Protected files — confirmed clean
- sch_def_schtbl.c, sch_def_msgtbl.c, sch_tbldefs.h, sch_msgids.h, fsw/unit_test/: zero diff

### Line ~944 in sch_app.c
- Confirmed: comment block spans lines 928-943 (old `switch`/`CFE_SB_SendMsg` pseudo-code in `/* ... */`)
- Line 944 is first executable line after the comment (if-statement, not part of comment)
- Content unchanged as required

### Final state: ALL work committed in apps/SCH inner repo
- Commits: 5872abc (build fixes) and 5ef4ae7 (initial port)
- Outer repo also updated with submodule reference commits

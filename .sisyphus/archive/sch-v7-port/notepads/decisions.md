# SCH v7 Port — Decisions

## 2026-03-05 Session: ses_34345babdffefpQxLMdCnNnuZe

### Decision: No Unit Tests
User explicitly excluded unit tests. Verification is build-only + source grep.

### Decision: MsgId Integer Comparisons
Use `CFE_SB_MsgIdToValue()` wrapper for any integer comparison or printf formatting of MsgId values.

### Decision: switch → if/else if
Replace `switch(MessageID)` with `if/else if` + `CFE_SB_MsgId_Equal()` for MsgId dispatch.

### Decision: OMIT_DEPRECATED=true as Build Gate
Must pass: `make SIMULATION=native OMIT_DEPRECATED=true prep && make`

### Decision: SCH_UNUSED_MID vs CFE_SB_INVALID_MSG_ID
These are DIFFERENT values. SCH_UNUSED_MID = 0x1897, CFE_SB_INVALID_MSG_ID = 0. Keep SCH_UNUSED_MID as-is.

### Decision: Worktree
Working in `/workspace/cFS-sch-work` (detached HEAD from `migrate_trick_cfs` branch).

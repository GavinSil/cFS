# APPS SUBDIRECTORY KNOWLEDGE

Each app here is a git submodule. Lab apps are examples only — real missions add mission-specific apps.

## APP MAP

| App | Purpose | Key mechanism |
|-----|---------|---------------|
| `ci_lab` | Command Ingest | UDP socket → Software Bus |
| `to_lab` | Telemetry Output | Software Bus → UDP socket |
| `sch_lab` | Scheduler | OSAL timer fires → sends SB messages on schedule |
| `sample_app` | Template / example | Copy this to start a new app |

## APP STRUCTURE

```
myapp/
├── CMakeLists.txt          # App build definition
├── arch_build.cmake        # Per-arch build hooks
├── mission_build.cmake     # Mission-level build hooks
├── config/
│   ├── default_myapp_internal_cfg.h   # Tunable internals (queue depth, etc.)
│   ├── default_myapp_msgids.h         # MsgId values (namespaced MYAPP_*)
│   └── default_myapp_platform_cfg.h   # Platform-specific overrides
├── eds/
│   └── myapp.xml           # EDS interface definitions
├── fsw/
│   ├── src/
│   │   ├── myapp_app.c     # Main task, init, run loop
│   │   ├── myapp_cmds.c    # Command handlers
│   │   └── myapp_utils.c   # Helpers
│   └── tables/
│       └── myapp_tbl.c     # Default table data
└── unit-test/              # Coverage tests (UT-assert framework)
```

## NEW APP GUIDE

1. Copy `sample_app/` to `apps/myapp/`
2. Rename every `SAMPLE_` / `sample_` prefix to `MYAPP_` / `myapp_`
3. Add to `sample_defs/targets.cmake` under `APPLIST`
4. Add entry to `sample_defs/cpu1_cfe_es_startup.scr`:
   `CFE_APP, myapp, MYAPP_AppMain, MYAPP, 80, 16384, 0x0, 0;`
5. Define MsgIds in `config/default_myapp_msgids.h`
6. Register with EVS, create SB pipe, subscribe in your `_Init()` function

## CONFIG/ PATTERN

Each app owns its config namespace. Override files go in your mission's `<mission>_defs/` directory with the same filename — CMake picks them up automatically, no app source changes needed.

- `default_myapp_msgids.h` — define `MYAPP_CMD_MID`, `MYAPP_HK_TLM_MID`, etc.
- `default_myapp_platform_cfg.h` — pipe depths, timeouts, limits
- `default_myapp_internal_cfg.h` — internal tuning constants

## CONVENTIONS

- Module prefix: `MYAPP_FunctionName`, `MYAPP_Data_t`, `MYAPP_CONSTANT`
- MsgId compare: always `CFE_SB_MsgId_Equal()`, never `==`
- Command dispatch: `GetMsgId` → route → `GetFcnCode` → switch → verify length → execute → increment counter
- No `%f`/`%g` in EVS format strings
- No direct OSAL/PSP calls from app code — go through cFE APIs
- Unit tests live in `unit-test/`, use UT-assert stubs only (never in flight code)
- Enable unit tests at build time: `make SIMULATION=native ENABLE_UNIT_TESTS=true prep`
- Run `make install` before `make test` — tests require installed binaries.

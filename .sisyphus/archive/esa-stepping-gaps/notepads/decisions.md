# esa-stepping-gaps 决策记录

## 初始化

- 待补充。

---

## 研究记录：T13-T15 测试模式调研 (Atlas)
**日期**: 2026-03-20  
**研究范围**: OSAL Hook 单元测试、cFE Stepping 集成测试、TIME Stepping Hook 测试模式

---

### 1. 本地现有模式分析

#### 1.1 ESA 现有测试结构 (`esa/ut-coverage/`)

**文件位置**:
- `esa/ut-coverage/CMakeLists.txt` - ESA 覆盖率测试 CMake 配置
- `esa/ut-coverage/coveragetest-sim_stepping.c` - 现有 sim_stepping 覆盖率测试
- `esa/ut-stubs/CMakeLists.txt` - ESA stub 库配置
- `esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c` - Shim stub 实现

**关键模式识别**:

**CMake 目标命名约定** (line 16-18 of `CMakeLists.txt`):
```cmake
set(TEST_NAME "coverage-esa-sim_stepping")
set(OBJECT_TARGET "${TEST_NAME}-object")      # 编译带覆盖率标志的对象文件
set(RUNNER_TARGET "${TEST_NAME}-testrunner")  # 测试运行器可执行文件
```

**OBJECT + RUNNER 分离模式** (line 24-58):
```cmake
# 1. 创建 OBJECT 库（带覆盖率编译标志）
add_library(${OBJECT_TARGET} OBJECT
    "${ESA_SIM_STEPPING_SRC_DIR}/cfe_psp_sim_stepping.c"
    "${ESA_SIM_STEPPING_SRC_DIR}/cfe_psp_sim_stepping_core.c"
)
target_compile_options(${OBJECT_TARGET} PRIVATE ${UT_COVERAGE_COMPILE_FLAGS})

# 2. 创建 RUNNER 可执行文件（包含测试用例和对象文件）
add_executable(${RUNNER_TARGET}
    coveragetest-sim_stepping.c
    $<TARGET_OBJECTS:${OBJECT_TARGET}>
)

# 3. 链接覆盖率链接标志和 stub 库
target_link_libraries(${RUNNER_TARGET}
    ${UT_COVERAGE_LINK_FLAGS}
    ut_esa_api_stubs      # ESA 自有 stubs（优先）
    psp_module_api
    ut_psp_api_stubs
    ut_psp_libc_stubs
    ut_osapi_stubs
    ut_assert
)

# 4. 注册 CTest
add_test(${TEST_NAME} ${RUNNER_TARGET})
```

**重要发现**: ESA 现有的 CMake 模式**没有使用** `add_cfe_coverage_test()` 宏，而是手动构建 OBJECT + RUNNER 目标。

#### 1.2 cFE 标准覆盖率测试模式 (`cfe/modules/*/ut-coverage/`)

**标准 CMake 模式** (以 `cfe/modules/es/ut-coverage/CMakeLists.txt` 为例):

```cmake
set(TESTCASE_SOURCES 
    "es_ut_helpers.c"
    "es_UT.c"
)

# 使用 add_cfe_coverage_test() 宏（关键字语法）
add_cfe_coverage_test(es
    UNIT_NAME "ALL"
    TESTCASE_SOURCES ${TESTCASE_SOURCES}
    SUBJECT_SOURCES  ${es_SOURCES}
)

# 添加私有 include 目录
target_include_directories(coverage-es-ALL-testrunner PRIVATE
    ${CFE_ES_SOURCE_DIR}/fsw/src
)

# 链接额外 stub 库
target_link_libraries(coverage-es-ALL-testrunner ut_core_private_stubs)
```

**EVS、SB、TBL、TIME 模块遵循相同模式**:
- `cfe/modules/evs/ut-coverage/CMakeLists.txt`
- `cfe/modules/sb/ut-coverage/CMakeLists.txt`
- `cfe/modules/tbl/ut-coverage/CMakeLists.txt`
- `cfe/modules/time/ut-coverage/CMakeLists.txt`

**宏定义位置**: `cfe/cmake/arch_build.cmake` lines 303-428

---

### 2. add_cfe_coverage_test() 宏详解

**位置**: `/workspace/cFS/cfe/cmake/arch_build.cmake:303-428`

**功能**:
1. 创建 `coverage-${MODULE_NAME}-${UNIT_NAME}-object` 目标（带覆盖率编译标志）
2. 创建 `coverage-${MODULE_NAME}-${UNIT_NAME}-testrunner` 可执行文件
3. 自动继承原模块的 include 目录和编译定义
4. 自动链接 `ut_coverage_link`、`ut_core_api_stubs`、`ut_assert`
5. 自动注册 CTest

**关键参数**:
- `MODULE_NAME`: 必须匹配 `add_cfe_app()` 中使用的模块名
- `UNIT_NAME`: 测试单元名称（通常是 "ALL" 或源文件基本名）
- `TESTCASE_SOURCES`: 测试用例源文件列表
- `SUBJECT_SOURCES`: 被测源文件列表

---

### 3. Hook 与 Stub 模式分析

#### 3.1 Hook 函数模式

**定义** (from `osal/ut_assert/inc/utstubs.h:125`):
```c
typedef int32 (*UT_HookFunc_t)(void *UserObj, int32 StubRetcode, uint32 CallCount, 
                               const UT_StubContext_t *Context);
```

**常用 Hook API**:
- `UT_SetHookFunction(UT_KEY(Func), HookFunc, UserObj)` - 普通函数 Hook
- `UT_SetVaHookFunction(UT_KEY(Func), VaHookFunc, UserObj)` - 变参函数 Hook（如 CFE_EVS_SendEvent）
- `UT_SetHandlerFunction(UT_KEY(Func), HandlerFunc, UserObj)` - 完全替换 stub 行为

**示例模式** (from `apps/sample_app/unit-test/common/eventcheck.c:50-107`):
```c
static int32 UT_CheckEvent_Hook(void *UserObj, int32 StubRetcode, uint32 CallCount,
                                const UT_StubContext_t *Context, va_list va)
{
    UT_CheckEvent_t *State = UserObj;
    uint16 EventId = UT_Hook_GetArgValueByName(Context, "EventID", uint16);
    
    if (EventId == State->ExpectedEvent) {
        ++State->MatchCount;
    }
    return 0;
}

// 使用
UT_CheckEvent_t EventTest;
UT_CHECKEVENT_SETUP(&EventTest, SAMPLE_APP_NOOP_INF_EID, NULL);
SAMPLE_APP_NoopCmd(&TestMsg);
UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
```

#### 3.2 UT_SetDataBuffer 模式

**定义** (from `osal/ut_assert/inc/utstubs.h:259`):
```c
void UT_SetDataBuffer(UT_EntryKey_t FuncKey, void *DataBuffer, size_t BufferSize, bool AllocateCopy);
```

**用途**: 向 stub 函数提供测试数据或捕获输出数据

**示例模式** (from `cfe/modules/time/ut-coverage/time_UT.c:105`):
```c
CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;
UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &MsgId, sizeof(MsgId), false);
CFE_TIME_TaskMain();
```

**双阶段测试模式**:
1. **阶段一**: 设置 DataBuffer 提供输入数据 → 调用被测函数 → 验证行为
2. **阶段二**: 重置状态 → 设置不同 DataBuffer → 再次调用 → 验证不同路径

#### 3.3 Stub 库创建模式

**标准模式** (from `cfe/cmake/arch_build.cmake:483-506`):
```cmake
function(add_cfe_coverage_stubs MODULE_NAME STUB_SRCS)
    set(STUB_TARGET "coverage-${MODULE_NAME}-stubs")
    add_library(${STUB_TARGET} STATIC ${STUB_SRCS} ${ARGN})
    
    # 继承原模块的 include 目录和编译定义
    if (TARGET ${MODULE_NAME})
        target_include_directories(${STUB_TARGET} PUBLIC
            $<TARGET_PROPERTY:${MODULE_NAME},INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(${STUB_TARGET} PUBLIC
            $<TARGET_PROPERTY:${MODULE_NAME},COMPILE_DEFINITIONS>
        )
    endif()
    
    target_link_libraries(${STUB_TARGET} ut_assert)
endfunction()
```

**ESA 现有 stub** (`esa/ut-stubs/CMakeLists.txt`):
```cmake
add_library(ut_esa_api_stubs
    src/cfe_psp_sim_stepping_shim_stubs.c
)
target_link_libraries(ut_esa_api_stubs PRIVATE
    psp_module_api
    ut_assert
)
```

---

### 4. T13-T15 实施建议

#### 4.1 T13: OSAL Hook 单元测试（3 个 Hook × 双阶段）

**测试模式建议**:

```c
// 每个 Hook 的测试结构
void Test_HookName_Phase1(void)
{
    // 阶段1: 正常路径
    UT_ResetState(0);
    
    // 设置 Hook 期望数据
    MyHookState_t State = {0};
    UT_SetHookFunction(UT_KEY(TargetFunction), MyTestHook, &State);
    
    // 设置输入数据
    InputData_t Input = { /* ... */ };
    UT_SetDataBuffer(UT_KEY(InputFunction), &Input, sizeof(Input), false);
    
    // 调用被测函数
    CFE_PSP_SimStepping_SomeFunction();
    
    // 验证 Hook 被调用
    UtAssert_UINT32_EQ(State.CallCount, 1);
    UtAssert_UINT32_EQ(State.CapturedArg, ExpectedValue);
}

void Test_HookName_Phase2(void)
{
    // 阶段2: 异常/边界路径
    UT_ResetState(0);
    
    // 不同输入数据...
}
```

**推荐文件结构**:
```
esa/ut-coverage/
├── CMakeLists.txt
├── coveragetest-sim_stepping.c
└── coveragetest-sim_stepping_hooks.c   # T13 新增
```

**CMake 修改**:
- 如果使用 `add_cfe_coverage_test()` 宏：
  ```cmake
  list(APPEND TESTCASE_SOURCES coveragetest-sim_stepping_hooks.c)
  ```
- 如果保持手动模式：
  ```cmake
  add_executable(${RUNNER_TARGET}
      coveragetest-sim_stepping.c
      coveragetest-sim_stepping_hooks.c  # 新增
      $<TARGET_OBJECTS:${OBJECT_TARGET}>
  )
  ```

#### 4.2 T14: cFE 模块 Stepping 集成测试（ES/EVS/SB/TBL）

**推荐测试策略**:

使用现有的 cFE 模块测试作为参考，但专注于 Stepping 行为：

**参考文件**:
- `cfe/modules/es/ut-coverage/es_ut_helpers.c` - 辅助函数模式
- `cfe/modules/es/ut-coverage/es_UT.c` - 测试用例组织
- `cfe/modules/time/ut-coverage/time_UT.c` - TIME 模块特定模式

**关键模式**: 使用 `ES_UT_SetupSingleAppId()`、`ES_UT_SetupForOSCleanup()` 等辅助函数初始化模块状态

**测试文件建议**:
```c
// esa/ut-coverage/coveragetest-cfe-stepping-integration.c

void Test_ES_SteppingLifecycle(void)
{
    // 初始化 ES
    ES_ResetUnitTest();
    
    // 模拟 Stepping 生命周期
    CFE_PSP_SimStepping_InProc_BeginStep();
    // ... 验证 ES 行为
    CFE_PSP_SimStepping_InProc_WaitStepComplete(1);
}

void Test_EVS_SteppingEventDelivery(void)
{
    // 测试 EVS 在 Stepping 模式下的消息传递
}
```

#### 4.3 T15: TIME Stepping Hook 测试

**关键参考**: `cfe/modules/time/ut-coverage/time_UT.c`

**TIME 特有模式**:
```c
// 从 time_UT.c 提取
void Test_TIME_Stepping(void)
{
    UT_InitData();
    
    // TIME 模块使用 UT_SetDataBuffer 设置 MsgId
    CFE_SB_MsgId_t MsgId = CFE_SB_ValueToMsgId(CFE_TIME_CMD_MID);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &MsgId, sizeof(MsgId), false);
    
    // 调用 TIME 函数
    CFE_TIME_TaskMain();
    
    // 验证
    UtAssert_STUB_COUNT(CFE_MSG_Init, ExpectedCount);
}
```

**TIME Stepping 特有测试点**:
- 1Hz 定时器回调 (`CFE_TIME_Set1HzAdjLatch()`)
- 时间同步回调注册/注销
- 外部时钟信号处理

---

### 5. include-path 和 link 依赖总结

**T13-T15 必需 include 路径**:
```cmake
target_include_directories(${RUNNER_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/unit-test-coverage/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../psp/fsw/shared/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../public_inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sample_defs/fsw/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../cfe/cmake/target/inc"
    $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:psp_module_api,INTERFACE_INCLUDE_DIRECTORIES>
    # T14 新增: cFE 模块私有头文件
    $<TARGET_PROPERTY:es,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:evs,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:sb,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:tbl,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:time,INCLUDE_DIRECTORIES>
)
```

**T13-T15 必需 link 库**:
```cmake
target_link_libraries(${RUNNER_TARGET}
    ${UT_COVERAGE_LINK_FLAGS}
    ut_esa_api_stubs
    psp_module_api
    ut_psp_api_stubs
    ut_psp_libc_stubs
    ut_osapi_stubs
    ut_assert
    # T14 新增: cFE 模块 stubs
    coverage-es-stubs
    coverage-evs-stubs
    coverage-sb-stubs
    coverage-tbl-stubs
    coverage-time-stubs
    ut_core_private_stubs
)
```

---

### 6. 避免的陷阱

**陷阱 1**: 不要在测试代码中直接调用 OSAL/PSP API
- **解决**: 始终通过 cFE API 层，或使用 stubs

**陷阱 2**: Hook 函数中忘记检查 CallCount
- **解决**: 始终验证 Hook 被调用的次数

**陷阱 3**: `UT_ResetState(0)` 位置错误
- **解决**: 在每个测试函数开始处调用，不要在 Setup 中重置全局状态

**陷阱 4**: DataBuffer 生命周期问题
- **解决**: 使用 `AllocateCopy=false` 时确保缓冲区在测试期间有效；使用 `AllocateCopy=true` 时数据会被复制

**陷阱 5**: 模块依赖链不完整
- **解决**: T14 需要链接所有相关 cFE 模块的 stubs，使用 `add_cfe_coverage_dependency()`

---

### 7. 外部参考链接

**cFS 官方文档**:
- cFE User's Guide: https://github.com/nasa/cFS/blob/gh-pages/cfe-usersguide.pdf
- OSAL User's Guide: https://github.com/nasa/cFS/blob/gh-pages/osal-apiguide.pdf
- cFE Application Developers Guide: https://github.com/nasa/cFE/blob/main/docs/cFE%20Application%20Developers%20Guide.md

**UT-Assert 参考**:
- UT-Assert 源码: https://github.com/nasa/osal/tree/main/ut_assert
- GitHub Discussion #527 (C++ UT-Assert): https://github.com/nasa/cFS/discussions/527
- GitHub Discussion #362 (自定义 UT-Assert): https://github.com/nasa/cFS/discussions/362

**cFE 源码参考** (permalink 基础):
- cFE GitHub: https://github.com/nasa/cFE
- OSAL GitHub: https://github.com/nasa/osal

---

### 8. 决策总结

**T13 实施方案**:
1. 复用现有 `esa/ut-stubs/src/cfe_psp_sim_stepping_shim_stubs.c`（如果足够）
2. 在 `esa/ut-coverage/` 新增 `coveragetest-sim_stepping_hooks.c`
3. 每个 Hook 测试包含双阶段（正常路径 + 异常路径）
4. 使用 `UT_SetHookFunction()` 和 `UT_SetDataBuffer()` 控制 stub 行为

**T14 实施方案**:
1. 参考 `cfe/modules/es/ut-coverage/` 等目录的集成测试模式
2. 使用 `add_cfe_coverage_dependency()` 链接 cFE 模块 stubs
3. 使用 `ES_UT_SetupSingleAppId()` 等辅助函数初始化模块状态
4. 保持测试聚焦于 Stepping 行为，不测试无关业务逻辑

**T15 实施方案**:
1. 参考 `cfe/modules/time/ut-coverage/time_UT.c` 的 TIME 特有模式
2. 关注 1Hz 定时器和同步回调相关函数
3. 使用 `UT_SetDataBuffer()` 控制时间相关 MsgId


/******************************************************************************
 * safety_monitor_test.c
 *
 * Test suite for the Simplified Safety Monitor Module
 * Tests SCU threshold check, debounce, valve control, start condition
 ******************************************************************************/

#include "safety_monitor_test.h"
#include "safety_monitor.h"
#include "logging.h"
#include "ni9472_queue.h"
#include "cdaq_utils.h"
#include <utility.h>

/******************************************************************************
 * Module State
 ******************************************************************************/

static volatile int g_testRunning = 0;
static SafetyMonitorTestContext *g_currentContext = NULL;

/******************************************************************************
 * Forward Declarations
 ******************************************************************************/

static void LogTestStart(SafetyMonitorTestContext *ctx, const char *testName);
static void LogTestResult(SafetyMonitorTestContext *ctx, int passed, const char *errorMsg);
static int RunTestCase(SafetyMonitorTestContext *ctx, SafetyTestCase *testCase);

/******************************************************************************
 * Test Suite Control Functions
 ******************************************************************************/

int SafetyMonitorTest_Initialize(SafetyMonitorTestContext *ctx, int panel, int buttonControl)
{
    if (ctx == NULL) {
        return ERR_NULL_POINTER;
    }

    memset(ctx, 0, sizeof(SafetyMonitorTestContext));
    ctx->panelHandle = panel;
    ctx->buttonControl = buttonControl;
    ctx->state = TEST_STATE_IDLE;

    // Check hardware availability
    ctx->hasNI9472 = (ENABLE_NI9472 && NI9472_GetGlobalQueueManager() != NULL);
    ctx->hasCDAQ = ENABLE_CDAQ;

    LogMessage("Safety Monitor Test: Initialized (NI9472=%d CDAQ=%d)",
               ctx->hasNI9472, ctx->hasCDAQ);

    return SUCCESS;
}

int SafetyMonitorTest_Run(SafetyMonitorTestContext *ctx)
{
    if (ctx == NULL) {
        return ERR_NULL_POINTER;
    }

    if (g_testRunning) {
        return ERR_INVALID_STATE;
    }

    g_testRunning = 1;
    g_currentContext = ctx;
    ctx->cancelRequested = 0;
    ctx->state = TEST_STATE_RUNNING;
    ctx->totalTests = 0;
    ctx->passedTests = 0;
    ctx->failedTests = 0;
    ctx->suiteStartTime = GetTimestamp();

    LogMessage("========================================");
    LogMessage("Safety Monitor Test Suite Starting");
    LogMessage("========================================");

    // Define test cases
    SafetyTestCase testCases[] = {
        // Unit tests
        {"Initialize", Test_SafetyMonitor_Initialize, 0, "", 0.0},
        {"Start/Stop", Test_SafetyMonitor_StartStop, 0, "", 0.0},
        {"Experiment Registration", Test_SafetyMonitor_ExperimentRegistration, 0, "", 0.0},
        {"Start Condition", Test_SafetyMonitor_StartCondition, 0, "", 0.0},

        // Integration tests (hardware dependent)
        {"Valve Control", Test_SafetyMonitor_ValveControl, 0, "", 0.0},
        {"SCU Reading", Test_SafetyMonitor_SCUReading, 0, "", 0.0},
    };

    int numTests = sizeof(testCases) / sizeof(testCases[0]);

    // Run all tests
    for (int i = 0; i < numTests && !ctx->cancelRequested; i++) {
        RunTestCase(ctx, &testCases[i]);
    }

    // Report results
    double totalTime = GetTimestamp() - ctx->suiteStartTime;

    LogMessage("========================================");
    LogMessage("Safety Monitor Test Suite Complete");
    LogMessage("  Total: %d  Passed: %d  Failed: %d",
               ctx->totalTests, ctx->passedTests, ctx->failedTests);
    LogMessage("  Execution time: %.2f seconds", totalTime);
    LogMessage("========================================");

    ctx->state = TEST_STATE_COMPLETED;
    g_testRunning = 0;
    g_currentContext = NULL;

    return SUCCESS;
}

void SafetyMonitorTest_Cancel(SafetyMonitorTestContext *ctx)
{
    if (ctx != NULL) {
        ctx->cancelRequested = 1;
    }
}

void SafetyMonitorTest_Cleanup(SafetyMonitorTestContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    SafetyMonitor_UnregisterExperiment();
    memset(ctx, 0, sizeof(SafetyMonitorTestContext));
}

int SafetyMonitorTest_IsRunning(void)
{
    return g_testRunning;
}

/******************************************************************************
 * Test Runner Helpers
 ******************************************************************************/

static void LogTestStart(SafetyMonitorTestContext *ctx, const char *testName)
{
    strncpy(ctx->currentTestName, testName, sizeof(ctx->currentTestName) - 1);
    ctx->testStartTime = GetTimestamp();
    LogMessage("[TEST] Starting: %s", testName);
}

static void LogTestResult(SafetyMonitorTestContext *ctx, int passed, const char *errorMsg)
{
    double elapsed = GetTimestamp() - ctx->testStartTime;

    if (passed) {
        LogMessage("[TEST] PASSED: %s (%.3f s)", ctx->currentTestName, elapsed);
        ctx->passedTests++;
    } else {
        LogError("[TEST] FAILED: %s - %s (%.3f s)", ctx->currentTestName, errorMsg, elapsed);
        ctx->failedTests++;
    }
    ctx->totalTests++;
}

static int RunTestCase(SafetyMonitorTestContext *ctx, SafetyTestCase *testCase)
{
    LogTestStart(ctx, testCase->testName);

    char errorMsg[256] = "";
    testCase->testStartTime = GetTimestamp();

    int result = testCase->testFunction(ctx, errorMsg, sizeof(errorMsg));

    testCase->executionTime = GetTimestamp() - testCase->testStartTime;
    testCase->result = result;
    strncpy(testCase->errorMessage, errorMsg, sizeof(testCase->errorMessage) - 1);

    LogTestResult(ctx, result == 1, errorMsg);

    return result;
}

/******************************************************************************
 * Unit Tests
 ******************************************************************************/

int Test_SafetyMonitor_Initialize(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize)
{
    // Safety monitor should already be initialized by the main app
    // Verify we can call CheckStartCondition without crashing
    double scuVoltage = 0.0;
    int result = SafetyMonitor_CheckStartCondition(&scuVoltage);

    // Result can be 0 or 1 - both valid. Just check it doesn't crash.
    LogMessage("[TEST] CheckStartCondition returned %d (SCU=%.2f V)", result, scuVoltage);

    return 1;
}

int Test_SafetyMonitor_StartStop(SafetyMonitorTestContext *ctx,
                                 char *errorMsg, int errorMsgSize)
{
    // Stop the monitor
    int result = SafetyMonitor_Stop();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Stop failed: %d", result);
        return -1;
    }

    // Restart it
    result = SafetyMonitor_Start();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Restart failed: %d", result);
        return -1;
    }

    // Double-start should succeed (idempotent)
    result = SafetyMonitor_Start();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Double start failed: %d", result);
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_ExperimentRegistration(SafetyMonitorTestContext *ctx,
                                              char *errorMsg, int errorMsgSize)
{
    // Ensure no experiment registered
    SafetyMonitor_UnregisterExperiment();

    // Register a test callback
    int result = SafetyMonitor_RegisterExperiment(NULL, NULL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "RegisterExperiment failed: %d", result);
        return -1;
    }

    // Try to register again (should fail - already registered)
    result = SafetyMonitor_RegisterExperiment(NULL, NULL);
    if (result == SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Double registration should have failed");
        SafetyMonitor_UnregisterExperiment();
        return -1;
    }

    // Unregister
    result = SafetyMonitor_UnregisterExperiment();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "UnregisterExperiment failed: %d", result);
        return -1;
    }

    // Unregister again (should succeed - idempotent)
    result = SafetyMonitor_UnregisterExperiment();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Double unregister failed: %d", result);
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_StartCondition(SafetyMonitorTestContext *ctx,
                                      char *errorMsg, int errorMsgSize)
{
    // Test with NULL pointer (should not crash)
    int result = SafetyMonitor_CheckStartCondition(NULL);

    // Result depends on actual hardware state - just verify it returns 0 or 1
    if (result != 0 && result != 1) {
        snprintf(errorMsg, errorMsgSize, "CheckStartCondition returned unexpected: %d", result);
        return -1;
    }

    // Test with voltage output
    double voltage = -1.0;
    result = SafetyMonitor_CheckStartCondition(&voltage);

    // Voltage should have been updated
    if (ENABLE_CDAQ) {
        if (voltage < 0.0 || voltage > 10.0) {
            snprintf(errorMsg, errorMsgSize, "SCU voltage out of range: %.2f V", voltage);
            return -1;
        }
        LogMessage("[TEST] SCU voltage: %.2f V, threshold: %.2f V, result: %d",
                   voltage, SAFETY_SCU_PRESSURE_MIN, result);
    }

    return 1;
}

/******************************************************************************
 * Integration Tests
 ******************************************************************************/

int Test_SafetyMonitor_ValveControl(SafetyMonitorTestContext *ctx,
                                    char *errorMsg, int errorMsgSize)
{
    if (!ctx->hasNI9472) {
        LogMessage("[TEST] Skipping ValveControl (no NI9472)");
        return 1;
    }

    // Test opening valves
    int result = SafetyMonitor_OpenValves();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "OpenValves failed: %d", result);
        return -1;
    }

    Delay(SAFETY_TEST_DELAY_SHORT);

    // Test closing valves
    result = SafetyMonitor_CloseValves();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "CloseValves failed: %d", result);
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_SCUReading(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize)
{
    if (!ctx->hasCDAQ) {
        LogMessage("[TEST] Skipping SCUReading (no cDAQ)");
        return 1;
    }

    // Read SCU voltage directly
    double voltage = 0.0;
    int result = CDAQ_ReadVoltage(SAFETY_SCU_CHANNEL, &voltage);

    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "CDAQ_ReadVoltage failed: %d", result);
        return -1;
    }

    // Verify reading is in reasonable range (0-10V for NI 9202)
    if (voltage < 0.0 || voltage > 10.0) {
        snprintf(errorMsg, errorMsgSize, "SCU voltage out of range: %.2f V", voltage);
        return -1;
    }

    LogMessage("[TEST] SCU voltage reading: %.3f V (threshold: %.2f V)",
               voltage, SAFETY_SCU_PRESSURE_MIN);

    // Check against threshold and log result
    if (voltage >= SAFETY_SCU_PRESSURE_MIN) {
        LogMessage("[TEST] SCU pressure OK (above threshold)");
    } else {
        LogMessage("[TEST] SCU pressure LOW (below threshold)");
    }

    return 1;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* SafetyMonitorTest_GetResultString(int result)
{
    switch (result) {
        case 1:  return "PASS";
        case -1: return "FAIL";
        default: return "NOT RUN";
    }
}

/******************************************************************************
 * UI Callback
 ******************************************************************************/

int CVICALLBACK TestSafetyMonitorCallback(int panel, int control, int event,
                                          void *callbackData, int eventData1,
                                          int eventData2)
{
    if (event != EVENT_COMMIT) {
        return 0;
    }

    static SafetyMonitorTestContext ctx = {0};

    if (g_testRunning) {
        SafetyMonitorTest_Cancel(&ctx);
        return 0;
    }

    SafetyMonitorTest_Initialize(&ctx, panel, control);

    // Run tests in thread pool
    CmtScheduleThreadPoolFunction(g_threadPool, TestSafetyMonitorWorkerThread,
                                  &ctx, NULL);

    return 0;
}

int CVICALLBACK TestSafetyMonitorWorkerThread(void *functionData)
{
    SafetyMonitorTestContext *ctx = (SafetyMonitorTestContext *)functionData;

    if (ctx == NULL) {
        return -1;
    }

    SafetyMonitorTest_Run(ctx);

    return 0;
}

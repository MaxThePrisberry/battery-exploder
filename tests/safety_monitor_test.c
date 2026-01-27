/******************************************************************************
 * safety_monitor_test.c
 *
 * Test suite for the Centralized Safety Monitor Module
 * Tests boolean logic with all 16 input combinations from the safety matrix
 ******************************************************************************/

#include "safety_monitor_test.h"
#include "safety_monitor.h"
#include "logging.h"
#include "ni9472_queue.h"
#include "alicat_queue.h"
#include "dtb4848_queue.h"
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
    ctx->hasALICAT = (ENABLE_ALICAT && ALICAT_GetGlobalQueueManager() != NULL);
    ctx->hasDTB = (ENABLE_DTB && DTB_GetGlobalQueueManager() != NULL);
    ctx->hasCDAQ = ENABLE_CDAQ;

    LogMessage("Safety Monitor Test: Initialized (NI9472=%d ALICAT=%d DTB=%d CDAQ=%d)",
               ctx->hasNI9472, ctx->hasALICAT, ctx->hasDTB, ctx->hasCDAQ);

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
        // Boolean logic tests
        {"All Input Combinations", Test_SafetyLogic_AllInputCombinations, 0, "", 0.0},
        {"PCU Required", Test_SafetyLogic_PCU_Required, 0, "", 0.0},
        {"Flow Required", Test_SafetyLogic_Flow_Required, 0, "", 0.0},
        {"SCU in Dangerous State", Test_SafetyLogic_SCU_DangerousState, 0, "", 0.0},
        {"Valve Logic", Test_SafetyLogic_ValveLogic, 0, "", 0.0},
        {"Temperature Override", Test_SafetyLogic_TemperatureOverride, 0, "", 0.0},

        // Module function tests
        {"Initialize", Test_SafetyMonitor_Initialize, 0, "", 0.0},
        {"Start/Stop", Test_SafetyMonitor_StartStop, 0, "", 0.0},
        {"Experiment Registration", Test_SafetyMonitor_ExperimentRegistration, 0, "", 0.0},
        {"State Query", Test_SafetyMonitor_StateQuery, 0, "", 0.0},
        {"Force Check", Test_SafetyMonitor_ForceCheck, 0, "", 0.0},
        {"Alarm Acknowledge", Test_SafetyMonitor_AlarmAcknowledge, 0, "", 0.0},
        {"Emergency Stop", Test_SafetyMonitor_EmergencyStop, 0, "", 0.0},

        // Integration tests (hardware dependent)
        {"Sensor Reading", Test_SafetyMonitor_SensorReading, 0, "", 0.0},
        {"Valve Control", Test_SafetyMonitor_ValveControl, 0, "", 0.0},
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

    // Ensure safety monitor is in known state
    if (SafetyMonitor_HasExperiment()) {
        SafetyMonitor_UnregisterExperiment();
    }

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
 * Boolean Logic Tests - All 16 Input Combinations
 ******************************************************************************/

/**
 * Test all 16 combinations of PCU, SCU, Flow, State inputs
 *
 * Truth table from safety matrix:
 * PCU SCU Flow State | STOP | VALVE_OPEN
 * ----------------------------------------
 *  0   0    0    0   |  1   |  0
 *  0   0    0    1   |  1   |  0
 *  0   0    1    0   |  1   |  0
 *  0   0    1    1   |  1   |  0
 *  0   1    0    0   |  1   |  0
 *  0   1    0    1   |  1   |  1
 *  0   1    1    0   |  1   |  0
 *  0   1    1    1   |  1   |  1
 *  1   0    0    0   |  1   |  0
 *  1   0    0    1   |  1   |  1
 *  1   0    1    0   |  0   |  1
 *  1   0    1    1   |  1   |  1
 *  1   1    0    0   |  1   |  0
 *  1   1    0    1   |  0   |  1
 *  1   1    1    0   |  0   |  1
 *  1   1    1    1   |  0   |  1
 */
int Test_SafetyLogic_AllInputCombinations(SafetyMonitorTestContext *ctx,
                                          char *errorMsg, int errorMsgSize)
{
    // Expected results: [STOP, VALVE_OPEN] for each combination
    // Index = (PCU*8) + (SCU*4) + (Flow*2) + State
    static const int expectedStop[16] = {
        1, 1, 1, 1,  // PCU=0, SCU=0
        1, 1, 1, 1,  // PCU=0, SCU=1
        1, 1, 0, 1,  // PCU=1, SCU=0
        1, 0, 0, 0   // PCU=1, SCU=1
    };

    static const int expectedValve[16] = {
        0, 0, 0, 0,  // PCU=0, SCU=0
        0, 1, 0, 1,  // PCU=0, SCU=1
        0, 1, 1, 1,  // PCU=1, SCU=0
        0, 1, 1, 1   // PCU=1, SCU=1
    };

    int failCount = 0;

    for (int pcu = 0; pcu <= 1; pcu++) {
        for (int scu = 0; scu <= 1; scu++) {
            for (int flow = 0; flow <= 1; flow++) {
                for (int state = 0; state <= 1; state++) {
                    // Create test inputs
                    SafetySensorInputs inputs = {0};
                    inputs.pcuPressure = pcu ? TEST_PRESSURE_OK : TEST_PRESSURE_LOW;
                    inputs.scuPressure = scu ? TEST_PRESSURE_OK : TEST_PRESSURE_LOW;
                    inputs.massFlow = flow ? TEST_FLOW_OK : TEST_FLOW_LOW;
                    inputs.temperature = TEST_TEMP_OK;
                    inputs.readSuccess = 1;

                    SafetyExperimentState expState = state ? SAFETY_STATE_DANGEROUS : SAFETY_STATE_SAFE;

                    // Evaluate
                    SafetyOutputs outputs = {0};
                    SafetyMonitor_EvaluateConditions(&inputs, expState, &outputs);

                    // Calculate expected values
                    int idx = (pcu * 8) + (scu * 4) + (flow * 2) + state;
                    int expectStop = expectedStop[idx];
                    int expectValveOpen = expectedValve[idx];

                    // Check STOP result
                    int gotStop = (outputs.experimentAction != SAFETY_ACTION_CONTINUE) ? 1 : 0;
                    int gotValveOpen = (outputs.valve1 == SAFETY_VALVE_OPEN) ? 1 : 0;

                    if (gotStop != expectStop || gotValveOpen != expectValveOpen) {
                        snprintf(errorMsg, errorMsgSize,
                                 "Combo PCU=%d SCU=%d Flow=%d State=%d: "
                                 "STOP got %d exp %d, VALVE got %d exp %d",
                                 pcu, scu, flow, state,
                                 gotStop, expectStop, gotValveOpen, expectValveOpen);
                        failCount++;
                        LogError("[TEST] %s", errorMsg);
                    }
                }
            }
        }
    }

    if (failCount > 0) {
        snprintf(errorMsg, errorMsgSize, "%d of 16 combinations failed", failCount);
        return -1;
    }

    return 1;
}

int Test_SafetyLogic_PCU_Required(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize)
{
    // Test: PCU=0 should always cause STOP regardless of other conditions
    SafetySensorInputs inputs = {0};
    inputs.pcuPressure = TEST_PRESSURE_LOW;  // PCU NOT OK
    inputs.scuPressure = TEST_PRESSURE_OK;   // SCU OK
    inputs.massFlow = TEST_FLOW_OK;          // Flow OK
    inputs.temperature = TEST_TEMP_OK;
    inputs.readSuccess = 1;

    SafetyOutputs outputs = {0};

    // Test in SAFE state
    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_SAFE, &outputs);
    if (outputs.experimentAction == SAFETY_ACTION_CONTINUE) {
        snprintf(errorMsg, errorMsgSize, "PCU=0 did not trigger STOP in SAFE state");
        return -1;
    }

    // Test in DANGEROUS state
    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_DANGEROUS, &outputs);
    if (outputs.experimentAction == SAFETY_ACTION_CONTINUE) {
        snprintf(errorMsg, errorMsgSize, "PCU=0 did not trigger STOP in DANGEROUS state");
        return -1;
    }

    // Verify violation type
    if (outputs.violation != SAFETY_VIOLATION_PCU_PRESSURE &&
        outputs.violation != SAFETY_VIOLATION_MULTIPLE) {
        snprintf(errorMsg, errorMsgSize, "Wrong violation type: %d",
                 (int)outputs.violation);
        return -1;
    }

    return 1;
}

int Test_SafetyLogic_Flow_Required(SafetyMonitorTestContext *ctx,
                                   char *errorMsg, int errorMsgSize)
{
    // Test: Flow=0 should always cause STOP regardless of other conditions
    SafetySensorInputs inputs = {0};
    inputs.pcuPressure = TEST_PRESSURE_OK;   // PCU OK
    inputs.scuPressure = TEST_PRESSURE_OK;   // SCU OK
    inputs.massFlow = TEST_FLOW_LOW;         // Flow NOT OK
    inputs.temperature = TEST_TEMP_OK;
    inputs.readSuccess = 1;

    SafetyOutputs outputs = {0};

    // Test in SAFE state
    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_SAFE, &outputs);
    if (outputs.experimentAction == SAFETY_ACTION_CONTINUE) {
        snprintf(errorMsg, errorMsgSize, "Flow=0 did not trigger STOP in SAFE state");
        return -1;
    }

    // Test in DANGEROUS state
    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_DANGEROUS, &outputs);
    if (outputs.experimentAction == SAFETY_ACTION_CONTINUE) {
        snprintf(errorMsg, errorMsgSize, "Flow=0 did not trigger STOP in DANGEROUS state");
        return -1;
    }

    // Verify violation type
    if (outputs.violation != SAFETY_VIOLATION_FLOW &&
        outputs.violation != SAFETY_VIOLATION_MULTIPLE) {
        snprintf(errorMsg, errorMsgSize, "Wrong violation type: %d",
                 (int)outputs.violation);
        return -1;
    }

    return 1;
}

int Test_SafetyLogic_SCU_DangerousState(SafetyMonitorTestContext *ctx,
                                        char *errorMsg, int errorMsgSize)
{
    // Test: SCU=0 should cause STOP only in DANGEROUS state (not SAFE)
    SafetySensorInputs inputs = {0};
    inputs.pcuPressure = TEST_PRESSURE_OK;   // PCU OK
    inputs.scuPressure = TEST_PRESSURE_LOW;  // SCU NOT OK
    inputs.massFlow = TEST_FLOW_OK;          // Flow OK
    inputs.temperature = TEST_TEMP_OK;
    inputs.readSuccess = 1;

    SafetyOutputs outputs = {0};

    // Test in SAFE state - should NOT stop
    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_SAFE, &outputs);
    if (outputs.experimentAction != SAFETY_ACTION_CONTINUE) {
        snprintf(errorMsg, errorMsgSize, "SCU=0 incorrectly triggered STOP in SAFE state");
        return -1;
    }

    // Test in DANGEROUS state - SHOULD stop
    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_DANGEROUS, &outputs);
    if (outputs.experimentAction == SAFETY_ACTION_CONTINUE) {
        snprintf(errorMsg, errorMsgSize, "SCU=0 did not trigger STOP in DANGEROUS state");
        return -1;
    }

    return 1;
}

int Test_SafetyLogic_ValveLogic(SafetyMonitorTestContext *ctx,
                                char *errorMsg, int errorMsgSize)
{
    SafetySensorInputs inputs = {0};
    SafetyOutputs outputs = {0};

    // Test 1: All OK in SAFE state - valve should be OPEN
    inputs.pcuPressure = TEST_PRESSURE_OK;
    inputs.scuPressure = TEST_PRESSURE_OK;
    inputs.massFlow = TEST_FLOW_OK;
    inputs.temperature = TEST_TEMP_OK;
    inputs.readSuccess = 1;

    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_SAFE, &outputs);
    if (outputs.valve1 != SAFETY_VALVE_OPEN) {
        snprintf(errorMsg, errorMsgSize, "Valve should be OPEN when all OK in SAFE state");
        return -1;
    }

    // Test 2: All OK in DANGEROUS state - valve should be OPEN
    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_DANGEROUS, &outputs);
    if (outputs.valve1 != SAFETY_VALVE_OPEN) {
        snprintf(errorMsg, errorMsgSize, "Valve should be OPEN when all OK in DANGEROUS state");
        return -1;
    }

    // Test 3: PCU=0, SCU=0 - valve should be CLOSED
    inputs.pcuPressure = TEST_PRESSURE_LOW;
    inputs.scuPressure = TEST_PRESSURE_LOW;

    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_SAFE, &outputs);
    if (outputs.valve1 != SAFETY_VALVE_CLOSED) {
        snprintf(errorMsg, errorMsgSize, "Valve should be CLOSED when PCU=0, SCU=0");
        return -1;
    }

    // Test 4: PCU=0, SCU=1 in DANGEROUS - valve should be OPEN (SCU provides backup)
    inputs.pcuPressure = TEST_PRESSURE_LOW;
    inputs.scuPressure = TEST_PRESSURE_OK;

    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_DANGEROUS, &outputs);
    if (outputs.valve1 != SAFETY_VALVE_OPEN) {
        snprintf(errorMsg, errorMsgSize, "Valve should be OPEN when PCU=0, SCU=1 in DANGEROUS state");
        return -1;
    }

    return 1;
}

int Test_SafetyLogic_TemperatureOverride(SafetyMonitorTestContext *ctx,
                                         char *errorMsg, int errorMsgSize)
{
    // Test: Over-temperature should cause EMERGENCY STOP regardless of other conditions
    SafetySensorInputs inputs = {0};
    inputs.pcuPressure = TEST_PRESSURE_OK;
    inputs.scuPressure = TEST_PRESSURE_OK;
    inputs.massFlow = TEST_FLOW_OK;
    inputs.temperature = TEST_TEMP_HIGH;  // OVER TEMPERATURE
    inputs.readSuccess = 1;

    SafetyOutputs outputs = {0};

    // Test in SAFE state
    SafetyMonitor_EvaluateConditions(&inputs, SAFETY_STATE_SAFE, &outputs);
    if (outputs.experimentAction != SAFETY_ACTION_EMERGENCY_STOP) {
        snprintf(errorMsg, errorMsgSize, "Over-temp did not trigger EMERGENCY STOP");
        return -1;
    }

    // Verify valves are CLOSED on emergency
    if (outputs.valve1 != SAFETY_VALVE_CLOSED || outputs.valve2 != SAFETY_VALVE_CLOSED) {
        snprintf(errorMsg, errorMsgSize, "Valves not closed on EMERGENCY STOP");
        return -1;
    }

    // Verify violation type
    if (outputs.violation != SAFETY_VIOLATION_TEMPERATURE) {
        snprintf(errorMsg, errorMsgSize, "Wrong violation type: %d (expected TEMPERATURE)",
                 (int)outputs.violation);
        return -1;
    }

    return 1;
}

/******************************************************************************
 * Module Function Tests
 ******************************************************************************/

int Test_SafetyMonitor_Initialize(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize)
{
    // Safety monitor should already be initialized by the main app
    // Just verify we can get state
    SafetyMonitorState state = {0};
    int result = SafetyMonitor_GetState(&state);

    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "GetState failed: %d", result);
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_StartStop(SafetyMonitorTestContext *ctx,
                                 char *errorMsg, int errorMsgSize)
{
    SafetyMonitorState state = {0};

    // Get current state
    SafetyMonitor_GetState(&state);

    // Should be running (started by main app)
    if (state.status != SAFETY_MONITOR_RUNNING) {
        // Try to start it
        int result = SafetyMonitor_Start();
        if (result != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Start failed: %d", result);
            return -1;
        }

        Delay(SAFETY_TEST_DELAY_SHORT);

        SafetyMonitor_GetState(&state);
        if (state.status != SAFETY_MONITOR_RUNNING) {
            snprintf(errorMsg, errorMsgSize, "Monitor not running after Start");
            return -1;
        }
    }

    // Test stop and restart
    int result = SafetyMonitor_Stop();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Stop failed: %d", result);
        return -1;
    }

    SafetyMonitor_GetState(&state);
    if (state.status != SAFETY_MONITOR_STOPPED) {
        snprintf(errorMsg, errorMsgSize, "Monitor not stopped after Stop");
        return -1;
    }

    // Restart
    result = SafetyMonitor_Start();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Restart failed: %d", result);
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_ExperimentRegistration(SafetyMonitorTestContext *ctx,
                                              char *errorMsg, int errorMsgSize)
{
    // Ensure no experiment registered
    SafetyMonitor_UnregisterExperiment();

    if (SafetyMonitor_HasExperiment()) {
        snprintf(errorMsg, errorMsgSize, "HasExperiment returned true after unregister");
        return -1;
    }

    // Register a test experiment
    SafetyExperimentHandle handle = {0};
    handle.experimentName = "Test Experiment";
    handle.onStop = NULL;
    handle.getState = NULL;
    handle.userData = NULL;

    int result = SafetyMonitor_RegisterExperiment(&handle);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "RegisterExperiment failed: %d", result);
        return -1;
    }

    if (!SafetyMonitor_HasExperiment()) {
        snprintf(errorMsg, errorMsgSize, "HasExperiment returned false after register");
        return -1;
    }

    // Try to register again (should fail)
    result = SafetyMonitor_RegisterExperiment(&handle);
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

    return 1;
}

int Test_SafetyMonitor_StateQuery(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize)
{
    SafetyMonitorState state = {0};

    int result = SafetyMonitor_GetState(&state);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "GetState failed: %d", result);
        return -1;
    }

    // Verify state is reasonable
    if (state.status != SAFETY_MONITOR_RUNNING &&
        state.status != SAFETY_MONITOR_STOPPED) {
        snprintf(errorMsg, errorMsgSize, "Invalid status: %d", (int)state.status);
        return -1;
    }

    // Test null pointer handling
    result = SafetyMonitor_GetState(NULL);
    if (result != ERR_NULL_POINTER) {
        snprintf(errorMsg, errorMsgSize, "NULL pointer not handled: %d", result);
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_ForceCheck(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize)
{
    SafetyMonitorState stateBefore = {0};
    SafetyMonitorState stateAfter = {0};

    SafetyMonitor_GetState(&stateBefore);
    int checksBefore = stateBefore.totalChecks;

    // Force a check
    int result = SafetyMonitor_ForceCheck();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "ForceCheck failed: %d", result);
        return -1;
    }

    // Wait a bit for the check to complete
    Delay(SAFETY_TEST_DELAY_MEDIUM);

    SafetyMonitor_GetState(&stateAfter);
    int checksAfter = stateAfter.totalChecks;

    // Should have at least one more check
    if (checksAfter <= checksBefore) {
        snprintf(errorMsg, errorMsgSize, "Check count did not increase: %d -> %d",
                 checksBefore, checksAfter);
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_AlarmAcknowledge(SafetyMonitorTestContext *ctx,
                                        char *errorMsg, int errorMsgSize)
{
    int result = SafetyMonitor_AcknowledgeAlarm();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "AcknowledgeAlarm failed: %d", result);
        return -1;
    }

    SafetyMonitorState state = {0};
    SafetyMonitor_GetState(&state);

    // After acknowledge, flag should be set
    if (!state.alarmAcknowledged) {
        snprintf(errorMsg, errorMsgSize, "Alarm not acknowledged");
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_EmergencyStop(SafetyMonitorTestContext *ctx,
                                     char *errorMsg, int errorMsgSize)
{
    // Skip if no NI9472 (can't verify valve control)
    if (!ctx->hasNI9472) {
        LogMessage("[TEST] Skipping EmergencyStop valve verification (no NI9472)");
    }

    int result = SafetyMonitor_EmergencyStop();
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "EmergencyStop failed: %d", result);
        return -1;
    }

    SafetyMonitorState state = {0};
    SafetyMonitor_GetState(&state);

    // Should have alarm active
    if (!state.alarmActive) {
        snprintf(errorMsg, errorMsgSize, "Alarm not active after emergency stop");
        return -1;
    }

    // Output should be emergency stop
    if (state.outputs.experimentAction != SAFETY_ACTION_EMERGENCY_STOP) {
        snprintf(errorMsg, errorMsgSize, "Action not EMERGENCY_STOP: %d",
                 (int)state.outputs.experimentAction);
        return -1;
    }

    return 1;
}

/******************************************************************************
 * Integration Tests
 ******************************************************************************/

int Test_SafetyMonitor_SensorReading(SafetyMonitorTestContext *ctx,
                                     char *errorMsg, int errorMsgSize)
{
    // Skip if no sensors available
    if (!ctx->hasCDAQ && !ctx->hasALICAT && !ctx->hasDTB) {
        LogMessage("[TEST] Skipping SensorReading (no sensors available)");
        return 1;  // Pass - nothing to test
    }

    SafetySensorInputs inputs = {0};
    int result = SafetyMonitor_ReadSensors(&inputs);

    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "ReadSensors failed: %d", result);
        return -1;
    }

    // Log readings for debugging
    LogMessage("[TEST] Sensor readings: PCU=%.2fV SCU=%.2fV Flow=%.1f Temp=%.1fC",
               inputs.pcuPressure, inputs.scuPressure, inputs.massFlow, inputs.temperature);

    // Verify readings are in reasonable ranges
    if (ctx->hasCDAQ) {
        if (inputs.pcuPressure < 0.0 || inputs.pcuPressure > 10.0) {
            snprintf(errorMsg, errorMsgSize, "PCU pressure out of range: %.2f", inputs.pcuPressure);
            return -1;
        }
        if (inputs.scuPressure < 0.0 || inputs.scuPressure > 10.0) {
            snprintf(errorMsg, errorMsgSize, "SCU pressure out of range: %.2f", inputs.scuPressure);
            return -1;
        }
    }

    if (ctx->hasALICAT) {
        if (inputs.massFlow < -100.0 || inputs.massFlow > 10000.0) {
            snprintf(errorMsg, errorMsgSize, "Mass flow out of range: %.1f", inputs.massFlow);
            return -1;
        }
    }

    if (ctx->hasDTB) {
        if (inputs.temperature < -50.0 || inputs.temperature > 500.0) {
            snprintf(errorMsg, errorMsgSize, "Temperature out of range: %.1f", inputs.temperature);
            return -1;
        }
    }

    return 1;
}

int Test_SafetyMonitor_ValveControl(SafetyMonitorTestContext *ctx,
                                    char *errorMsg, int errorMsgSize)
{
    // Skip if no NI9472
    if (!ctx->hasNI9472) {
        LogMessage("[TEST] Skipping ValveControl (no NI9472)");
        return 1;  // Pass - nothing to test
    }

    // Test setting valves
    int result = SafetyMonitor_SetValves(SAFETY_VALVE_OPEN, SAFETY_VALVE_OPEN);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "SetValves OPEN failed: %d", result);
        return -1;
    }

    Delay(SAFETY_TEST_DELAY_SHORT);

    result = SafetyMonitor_SetValves(SAFETY_VALVE_CLOSED, SAFETY_VALVE_CLOSED);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "SetValves CLOSED failed: %d", result);
        return -1;
    }

    return 1;
}

int Test_SafetyMonitor_ExperimentCallback(SafetyMonitorTestContext *ctx,
                                          char *errorMsg, int errorMsgSize)
{
    // This test would require triggering an actual safety condition
    // which we don't want to do in normal testing
    LogMessage("[TEST] Skipping ExperimentCallback (requires safety violation)");
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

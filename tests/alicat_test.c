/******************************************************************************
 * alicat_test.c
 *
 * ALICAT BASIS 2 Mass Flow Controller Test Suite Implementation
 * Tests both low-level DLL functions and queued wrapper functions
 *
 * Authors: Maxwell Prisbrey, Nicolas Rasmont, Gabriel Meier
 ******************************************************************************/

#include "alicat_test.h"
#include "BatteryExploder.h"
#include "logging.h"
#include <toolbox.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/******************************************************************************
 * Test Configuration Constants
 ******************************************************************************/
#define TEST_MODBUS_ADDRESS     ALICAT_MODBUS_ADDRESS
#define TEST_DELAY_VERY_SHORT   0.1     // seconds
#define TEST_DELAY_BETWEEN_TESTS 0.2    // seconds

/******************************************************************************
 * Static Variables
 ******************************************************************************/

// UI test suite context
static ALICAT_TestSuiteContext *g_alicatTestSuiteContext = NULL;

// Test cases array
static ALICAT_TestCase testCases[] = {
    {"Connection Test", Test_ALICAT_Connection, 0, "", 0.0},
    {"Get Status", Test_ALICAT_GetStatus, 0, "", 0.0},
    {"Set/Get Setpoint", Test_ALICAT_SetGetSetpoint, 0, "", 0.0},
    {"Set/Get Gas Type", Test_ALICAT_SetGetGas, 0, "", 0.0},
    {"Tare Function", Test_ALICAT_Tare, 0, "", 0.0},
    {"Get Flow Rate", Test_ALICAT_GetFlowRate, 0, "", 0.0},
    {"Get Temperature", Test_ALICAT_GetTemperature, 0, "", 0.0},
    {"Get Valve Drive", Test_ALICAT_GetValveDrive, 0, "", 0.0},
    {"PID Parameters", Test_ALICAT_PIDParams, 0, "", 0.0},
    {"Flow Averaging", Test_ALICAT_FlowAveraging, 0, "", 0.0},
    {"Reference Temperature", Test_ALICAT_RefTemperature, 0, "", 0.0},
    {"Setpoint Source", Test_ALICAT_SetpointSource, 0, "", 0.0},
    {"Totalizer Functions", Test_ALICAT_Totalizer, 0, "", 0.0},
    {"Batch Volume", Test_ALICAT_BatchVolume, 0, "", 0.0},
    {"Queued Commands", Test_ALICAT_QueuedCommands, 0, "", 0.0},
    {"Setpoint Ramp", Test_ALICAT_SetpointRamp, 0, "", 0.0}
};
static int numTestCases = sizeof(testCases) / sizeof(testCases[0]);

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static double GetTime(void) {
    return Timer();
}

static void GenerateALICATTestSummary(ALICAT_TestSummary *summary, ALICAT_TestCase *tests, int numTests) {
    if (!summary || !tests) return;

    // Calculate total execution time from individual test times
    double totalTime = 0.0;
    for (int i = 0; i < numTests; i++) {
        totalTime += tests[i].executionTime;
    }

    summary->executionTime = totalTime;

    LogMessageEx(LOG_DEVICE_ALICAT, "========================================");
    LogMessageEx(LOG_DEVICE_ALICAT, "ALICAT Test Suite Summary:");
    LogMessageEx(LOG_DEVICE_ALICAT, "Total Tests: %d", summary->totalTests);
    LogMessageEx(LOG_DEVICE_ALICAT, "Passed: %d", summary->passedTests);
    LogMessageEx(LOG_DEVICE_ALICAT, "Failed: %d", summary->failedTests);
    LogMessageEx(LOG_DEVICE_ALICAT, "Total Time: %.2f seconds", totalTime);
    LogMessageEx(LOG_DEVICE_ALICAT, "Average Time: %.2f seconds",
                 (numTests > 0) ? (totalTime / numTests) : 0.0);
    LogMessageEx(LOG_DEVICE_ALICAT, "========================================");

    if (summary->failedTests > 0) {
        LogMessageEx(LOG_DEVICE_ALICAT, "Failed Tests:");
        for (int i = 0; i < numTests; i++) {
            if (tests[i].result <= 0) {
                LogMessageEx(LOG_DEVICE_ALICAT, "  - %s: %s",
                           tests[i].testName, tests[i].errorMessage);
            }
        }
    }
}

/******************************************************************************
 * UI Test Suite Helper Functions
 ******************************************************************************/

void ALICAT_UpdateTestProgress(ALICAT_TestSuiteContext *context, const char *message) {
    if (context && context->progressCallback) {
        context->progressCallback(message);
    }

    if (context && context->statusStringControl > 0 && context->panelHandle > 0) {
        SetCtrlVal(context->panelHandle, context->statusStringControl, message);
        ProcessDrawEvents();
    }
}

/******************************************************************************
 * UI Test Button Callback and Worker Thread
 ******************************************************************************/

int CVICALLBACK TestALICATCallback(int panel, int control, int event,
                                   void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT:
            // Check if this is a cancel request (test is running)
            if (g_alicatTestSuiteContext != NULL) {
                LogMessageEx(LOG_DEVICE_ALICAT, "User requested to cancel ALICAT test suite");
                ALICAT_TestSuite_Cancel(g_alicatTestSuiteContext);

                // Update button text to show cancelling
                SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancelling...");
                SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);

                return 0;
            }

            // Otherwise, this is a start request
            // Check if system is busy with another operation
            CmtGetLock(g_busyLock);
            if (g_systemBusy) {
                CmtReleaseLock(g_busyLock);
                LogWarningEx(LOG_DEVICE_ALICAT, "Cannot start test - system is busy");
                MessagePopup("System Busy",
                           "Another operation is in progress.\n"
                           "Please wait for it to complete before starting a test.");
                return 0;
            }
            g_systemBusy = 1;
            CmtReleaseLock(g_busyLock);

            // Check if ALICAT is connected through queue manager
            ALICAT_QueueManager *alicatQueueMgr = ALICAT_GetGlobalQueueManager();
            if (!alicatQueueMgr) {
                LogErrorEx(LOG_DEVICE_ALICAT, "ALICAT queue manager not initialized");
                MessagePopup("ALICAT Not Available",
                           "The ALICAT queue manager is not initialized.\n"
                           "Please check the system configuration.");

                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                return 0;
            }

            // Dim EXPERIMENTS tab control
            SetCtrlAttribute(panel, PANEL_EXPERIMENTS, ATTR_DIMMED, 1);

            // Change Test ALICAT button text to "Cancel"
            SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancel");

            // Create test context
            ALICAT_TestSuiteContext *context = calloc(1, sizeof(ALICAT_TestSuiteContext));
            if (context) {
                ALICAT_TestSuite_Initialize(context, alicatQueueMgr, panel,
                                           PANEL_MFLOW_STATUS, 0);  // Using MFLOW_STATUS for now
                context->state = TEST_STATE_PREPARING;

                // Store pointer to running context
                g_alicatTestSuiteContext = context;

                // Start test in worker thread
                CmtThreadFunctionID threadID;
                CmtScheduleThreadPoolFunction(g_threadPool,
                    TestALICATWorkerThread, context, &threadID);
            } else {
                // Failed to allocate - restore UI
                SetCtrlAttribute(panel, PANEL_EXPERIMENTS, ATTR_DIMMED, 0);
                SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Test ALICAT");

                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
            }
            break;
    }
    return 0;
}

int CVICALLBACK TestALICATWorkerThread(void *functionData) {
    ALICAT_TestSuiteContext *context = (ALICAT_TestSuiteContext*)functionData;

    // Run the test suite
    int result = ALICAT_TestSuite_Run(context);

    // Create one-line summary for status control
    char statusMsg[MEDIUM_BUFFER_SIZE];
    if (context->state == TEST_STATE_ABORTED) {
        snprintf(statusMsg, sizeof(statusMsg),
                 "Test cancelled: %d/%d passed",
                 context->summary.passedTests,
                 context->summary.totalTests);
    } else if (context->state == TEST_STATE_COMPLETED) {
        snprintf(statusMsg, sizeof(statusMsg),
                 "All tests passed (%d/%d)",
                 context->summary.passedTests,
                 context->summary.totalTests);
    } else {
        snprintf(statusMsg, sizeof(statusMsg),
                 "Tests failed: %d/%d passed",
                 context->summary.passedTests,
                 context->summary.totalTests);
    }

    // Update status control with summary
    SetCtrlVal(g_mainPanelHandle, PANEL_MFLOW_STATUS, statusMsg);

    // Log detailed results
    if (result > 0) {
        LogMessageEx(LOG_DEVICE_ALICAT, "ALICAT test suite completed successfully (%d tests passed)", result);
    } else if (result == -2) {
        LogMessageEx(LOG_DEVICE_ALICAT, "ALICAT test suite cancelled by user");
    } else if (result == 0) {
        LogWarningEx(LOG_DEVICE_ALICAT, "ALICAT test suite completed with failures");
    } else {
        LogErrorEx(LOG_DEVICE_ALICAT, "ALICAT test suite failed with error: %d", result);
    }

    // Clean up
    ALICAT_TestSuite_Cleanup(context);

    // Clear the running context pointer
    g_alicatTestSuiteContext = NULL;

    free(context);

    // Restore UI controls
    SetCtrlAttribute(g_mainPanelHandle, PANEL_EXPERIMENTS, ATTR_DIMMED, 0);

    // Re-enable all tabs
    int numTabs;
    GetNumTabPages(g_mainPanelHandle, PANEL_EXPERIMENTS, &numTabs);
    for (int i = 0; i < numTabs; i++) {
        SetTabPageAttribute(g_mainPanelHandle, PANEL_EXPERIMENTS, i, ATTR_DIMMED, 0);
    }

    // TODO: Restore Test ALICAT button when UI control is added
    // SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_ALICAT, ATTR_LABEL_TEXT, "Test ALICAT");
    // SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_ALICAT, ATTR_DIMMED, 0);

    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);

    return 0;
}

/******************************************************************************
 * UI Test Suite Functions
 ******************************************************************************/

int ALICAT_TestSuite_Initialize(ALICAT_TestSuiteContext *context, ALICAT_QueueManager *alicatQueueMgr,
                                int panel, int statusControl, int ledControl) {
    if (!context || !alicatQueueMgr) return -1;

    memset(context, 0, sizeof(ALICAT_TestSuiteContext));
    context->alicatQueueMgr = alicatQueueMgr;
    context->panelHandle = panel;
    context->statusStringControl = statusControl;
    context->ledControl = ledControl;
    context->cancelRequested = 0;
    context->state = TEST_STATE_IDLE;

    // Reset all test results
    for (int i = 0; i < numTestCases; i++) {
        testCases[i].result = 0;
        testCases[i].errorMessage[0] = '\0';
        testCases[i].executionTime = 0.0;
    }

    return 0;
}

int ALICAT_TestSuite_Run(ALICAT_TestSuiteContext *context) {
    if (!context || !context->alicatQueueMgr) return -1;

    context->state = TEST_STATE_RUNNING;
    context->cancelRequested = 0;

    LogMessageEx(LOG_DEVICE_ALICAT, "Starting ALICAT Test Suite");
    ALICAT_UpdateTestProgress(context, "Starting ALICAT Test Suite...");

    // Run each test
    for (int i = 0; i < numTestCases; i++) {
        // Check for cancellation before starting each test
        if (context->cancelRequested) {
            LogMessageEx(LOG_DEVICE_ALICAT, "Test suite cancelled before test %d", i + 1);
            break;
        }

        ALICAT_TestCase* test = &testCases[i];

        char progressMsg[256];
        snprintf(progressMsg, sizeof(progressMsg), "Running test %d/%d: %s",
                i + 1, numTestCases, test->testName);
        ALICAT_UpdateTestProgress(context, progressMsg);

        LogMessageEx(LOG_DEVICE_ALICAT, "Running test: %s", test->testName);

        double startTime = GetTime();
        test->result = test->testFunction(context->alicatQueueMgr, test->errorMessage,
                                          sizeof(test->errorMessage));
        test->executionTime = GetTime() - startTime;

        if (test->result > 0) {
            LogMessageEx(LOG_DEVICE_ALICAT, "Test PASSED: %s (%.2f seconds)",
                       test->testName, test->executionTime);
            context->summary.passedTests++;
        } else {
            LogErrorEx(LOG_DEVICE_ALICAT, "Test FAILED: %s - %s",
                     test->testName, test->errorMessage);
            context->summary.failedTests++;
        }

        context->summary.totalTests++;

        // Short delay between tests
        if (i < numTestCases - 1 && !context->cancelRequested) {
            Delay(TEST_DELAY_BETWEEN_TESTS);
        }
    }

    // Generate summary
    GenerateALICATTestSummary(&context->summary, testCases, numTestCases);

    // Set final state
    if (context->cancelRequested) {
        context->state = TEST_STATE_ABORTED;
    } else if (context->summary.failedTests == 0) {
        context->state = TEST_STATE_COMPLETED;
    } else {
        context->state = TEST_STATE_ERROR;
    }

    // Return value based on state
    if (context->state == TEST_STATE_ABORTED) {
        return -2; // Special value to indicate cancellation
    } else if (context->state == TEST_STATE_COMPLETED) {
        return context->summary.totalTests; // All passed
    } else {
        return 0; // Some failed
    }
}

void ALICAT_TestSuite_Cancel(ALICAT_TestSuiteContext *context) {
    if (context) {
        context->cancelRequested = 1;
        LogMessageEx(LOG_DEVICE_ALICAT, "Test suite cancellation requested");
    }
}

void ALICAT_TestSuite_Cleanup(ALICAT_TestSuiteContext *context) {
    if (context) {
        // No specific cleanup needed for ALICAT tests currently
        LogMessageEx(LOG_DEVICE_ALICAT, "ALICAT test suite cleanup complete");
    }
}

/******************************************************************************
 * Individual Test Implementations
 ******************************************************************************/

int Test_ALICAT_Connection(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT connection...");

    // Verify queue manager is valid
    if (!alicatQueueMgr) {
        snprintf(errorMsg, errorMsgSize, "ALICAT queue manager is NULL");
        return -1;
    }

    // Get ALICAT handle
    ALICAT_Handle *handle = ALICAT_QueueGetHandle(alicatQueueMgr, TEST_MODBUS_ADDRESS);
    if (!handle) {
        snprintf(errorMsg, errorMsgSize, "ALICAT handle not found for address %d", TEST_MODBUS_ADDRESS);
        return -1;
    }

    // Check if connected
    if (!handle->isConnected) {
        snprintf(errorMsg, errorMsgSize, "ALICAT is not connected");
        return -1;
    }

    LogDebugEx(LOG_DEVICE_ALICAT, "ALICAT connection test passed successfully");
    return 1;
}

int Test_ALICAT_GetStatus(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Get Status...");

    ALICAT_Status status;
    int result = ALICAT_GetStatusQueued(TEST_MODBUS_ADDRESS, &status, DEVICE_PRIORITY_NORMAL);

    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get Status failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    // Log status information
    LogMessageEx(LOG_DEVICE_ALICAT, "========================================");
    LogMessageEx(LOG_DEVICE_ALICAT, "ALICAT Status:");
    LogMessageEx(LOG_DEVICE_ALICAT, "  Flow Rate: %.2f sccm", status.flowRate);
    LogMessageEx(LOG_DEVICE_ALICAT, "  Setpoint: %.2f sccm", status.setpoint);
    LogMessageEx(LOG_DEVICE_ALICAT, "  Temperature: %.2f °C", status.temperature);
    LogMessageEx(LOG_DEVICE_ALICAT, "  Total Volume: %.2f", status.totalVolume);
    LogMessageEx(LOG_DEVICE_ALICAT, "  Valve Drive: %.2f %%", status.valveDrive);
    LogMessageEx(LOG_DEVICE_ALICAT, "  Gas: %s", ALICAT_GetGasName(status.selectedGas));
    LogMessageEx(LOG_DEVICE_ALICAT, "  Status Flags: 0x%04X", status.statusFlags);
    LogMessageEx(LOG_DEVICE_ALICAT, "========================================");

    LogDebugEx(LOG_DEVICE_ALICAT, "Get Status test completed successfully");
    return 1;
}

int Test_ALICAT_SetGetSetpoint(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Set/Get Setpoint...");

    // Set a test setpoint
    double testSetpoint = ALICAT_TEST_SETPOINT_MID;
    int result = ALICAT_SetSetpointQueued(TEST_MODBUS_ADDRESS, testSetpoint, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Set Setpoint failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    // Wait for setpoint to stabilize
    Delay(ALICAT_TEST_DELAY_MEDIUM);

    // Read back the setpoint
    double readSetpoint;
    result = ALICAT_GetSetpointQueued(TEST_MODBUS_ADDRESS, &readSetpoint, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get Setpoint failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "Setpoint: Set=%.2f, Read=%.2f sccm", testSetpoint, readSetpoint);

    // Verify setpoint matches
    if (fabs(readSetpoint - testSetpoint) > ALICAT_FLOW_TOLERANCE) {
        snprintf(errorMsg, errorMsgSize, "Setpoint mismatch: expected %.2f, got %.2f sccm",
                testSetpoint, readSetpoint);
        return -1;
    }

    // Reset to zero
    ALICAT_SetSetpointQueued(TEST_MODBUS_ADDRESS, 0.0, DEVICE_PRIORITY_NORMAL);

    LogDebugEx(LOG_DEVICE_ALICAT, "Set/Get Setpoint test completed successfully");
    return 1;
}

int Test_ALICAT_SetGetGas(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Set/Get Gas Type...");

    // Get initial gas type
    ALICAT_Status initialStatus;
    int result = ALICAT_GetStatusQueued(TEST_MODBUS_ADDRESS, &initialStatus, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get initial gas failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    int initialGas = initialStatus.selectedGas;
    LogMessageEx(LOG_DEVICE_ALICAT, "Initial gas: %s", ALICAT_GetGasName(initialGas));

    // Set a different gas type
    int testGas = (initialGas == GAS_AIR) ? GAS_NITROGEN : GAS_AIR;
    result = ALICAT_SetGasQueued(TEST_MODBUS_ADDRESS, testGas, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Set Gas failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    Delay(ALICAT_TEST_DELAY_SHORT);

    // Read back the gas type
    ALICAT_Status newStatus;
    result = ALICAT_GetStatusQueued(TEST_MODBUS_ADDRESS, &newStatus, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get new gas failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "New gas: %s", ALICAT_GetGasName(newStatus.selectedGas));

    // Verify gas type changed
    if (newStatus.selectedGas != testGas) {
        snprintf(errorMsg, errorMsgSize, "Gas mismatch: expected %s, got %s",
                ALICAT_GetGasName(testGas), ALICAT_GetGasName(newStatus.selectedGas));
        return -1;
    }

    // Restore original gas type
    ALICAT_SetGasQueued(TEST_MODBUS_ADDRESS, initialGas, DEVICE_PRIORITY_NORMAL);

    LogDebugEx(LOG_DEVICE_ALICAT, "Set/Get Gas Type test completed successfully");
    return 1;
}

int Test_ALICAT_Tare(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Tare...");

    // Perform tare
    int result = ALICAT_TareQueued(TEST_MODBUS_ADDRESS, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Tare failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    // Wait for tare to complete
    Delay(ALICAT_TEST_DELAY_LONG);

    LogDebugEx(LOG_DEVICE_ALICAT, "Tare test completed successfully");
    return 1;
}

int Test_ALICAT_GetFlowRate(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Get Flow Rate...");

    double flowRate;
    int result = ALICAT_GetFlowRateQueued(TEST_MODBUS_ADDRESS, &flowRate, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get Flow Rate failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "Current flow rate: %.2f sccm", flowRate);

    LogDebugEx(LOG_DEVICE_ALICAT, "Get Flow Rate test completed successfully");
    return 1;
}

int Test_ALICAT_GetTemperature(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Get Temperature...");

    double temperature;
    int result = ALICAT_GetTemperatureQueued(TEST_MODBUS_ADDRESS, &temperature, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get Temperature failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "Current temperature: %.2f °C", temperature);

    // Sanity check - temperature should be reasonable (0-100°C typical)
    if (temperature < -50.0 || temperature > 150.0) {
        snprintf(errorMsg, errorMsgSize, "Temperature out of range: %.2f °C", temperature);
        return -1;
    }

    LogDebugEx(LOG_DEVICE_ALICAT, "Get Temperature test completed successfully");
    return 1;
}

int Test_ALICAT_GetValveDrive(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Get Valve Drive...");

    double valveDrive;
    int result = ALICAT_GetValveDriveQueued(TEST_MODBUS_ADDRESS, &valveDrive, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get Valve Drive failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "Current valve drive: %.2f %%", valveDrive);

    // Sanity check - valve drive should be 0-100%
    if (valveDrive < 0.0 || valveDrive > 100.0) {
        snprintf(errorMsg, errorMsgSize, "Valve drive out of range: %.2f %%", valveDrive);
        return -1;
    }

    LogDebugEx(LOG_DEVICE_ALICAT, "Get Valve Drive test completed successfully");
    return 1;
}

int Test_ALICAT_PIDParams(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT PID Parameters...");

    // Get current PID parameters
    ALICAT_PIDParams initialParams;
    int result = ALICAT_GetPIDParamsQueued(TEST_MODBUS_ADDRESS, &initialParams, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get initial PID params failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "Initial PID: P=%u, I=%u", initialParams.pGain, initialParams.iGain);

    // Set new PID parameters
    ALICAT_PIDParams testParams;
    testParams.pGain = 5000;
    testParams.iGain = 2500;
    result = ALICAT_SetPIDParamsQueued(TEST_MODBUS_ADDRESS, &testParams, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Set PID params failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    Delay(ALICAT_TEST_DELAY_SHORT);

    // Read back PID parameters
    ALICAT_PIDParams readParams;
    result = ALICAT_GetPIDParamsQueued(TEST_MODBUS_ADDRESS, &readParams, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get new PID params failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "New PID: P=%u, I=%u", readParams.pGain, readParams.iGain);

    // Verify PID parameters
    if (readParams.pGain != testParams.pGain || readParams.iGain != testParams.iGain) {
        snprintf(errorMsg, errorMsgSize, "PID mismatch: expected P=%u I=%u, got P=%u I=%u",
                testParams.pGain, testParams.iGain, readParams.pGain, readParams.iGain);
        // Restore original and return error
        ALICAT_SetPIDParamsQueued(TEST_MODBUS_ADDRESS, &initialParams, DEVICE_PRIORITY_NORMAL);
        return -1;
    }

    // Restore original PID parameters
    ALICAT_SetPIDParamsQueued(TEST_MODBUS_ADDRESS, &initialParams, DEVICE_PRIORITY_NORMAL);

    LogDebugEx(LOG_DEVICE_ALICAT, "PID Parameters test completed successfully");
    return 1;
}

int Test_ALICAT_FlowAveraging(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Flow Averaging...");

    // Set flow averaging time
    int averagingMs = 500;  // 500ms averaging
    int result = ALICAT_SetFlowAveragingQueued(TEST_MODBUS_ADDRESS, averagingMs, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Set Flow Averaging failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    Delay(ALICAT_TEST_DELAY_SHORT);

    LogMessageEx(LOG_DEVICE_ALICAT, "Flow averaging set to %d ms", averagingMs);

    LogDebugEx(LOG_DEVICE_ALICAT, "Flow Averaging test completed successfully");
    return 1;
}

int Test_ALICAT_RefTemperature(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Reference Temperature...");

    // Set reference temperature
    double refTemp = 25.0;  // 25°C
    int result = ALICAT_SetRefTemperatureQueued(TEST_MODBUS_ADDRESS, refTemp, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Set Reference Temperature failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    Delay(ALICAT_TEST_DELAY_SHORT);

    LogMessageEx(LOG_DEVICE_ALICAT, "Reference temperature set to %.1f °C", refTemp);

    LogDebugEx(LOG_DEVICE_ALICAT, "Reference Temperature test completed successfully");
    return 1;
}

int Test_ALICAT_SetpointSource(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Setpoint Source...");

    // Set setpoint source to digital (unsaved)
    int source = SETPOINT_SOURCE_DIGITAL_UNSAVED;
    int result = ALICAT_SetSetpointSourceQueued(TEST_MODBUS_ADDRESS, source, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Set Setpoint Source failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    Delay(ALICAT_TEST_DELAY_SHORT);

    LogMessageEx(LOG_DEVICE_ALICAT, "Setpoint source set to digital (unsaved)");

    LogDebugEx(LOG_DEVICE_ALICAT, "Setpoint Source test completed successfully");
    return 1;
}

int Test_ALICAT_Totalizer(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Totalizer...");

    // Get initial total volume
    double initialVolume;
    int result = ALICAT_GetTotalVolumeQueued(TEST_MODBUS_ADDRESS, &initialVolume, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get initial total volume failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "Initial total volume: %.2f", initialVolume);

    // Reset totalizer
    result = ALICAT_ResetTotalizerQueued(TEST_MODBUS_ADDRESS, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Reset Totalizer failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    Delay(ALICAT_TEST_DELAY_SHORT);

    // Get new total volume (should be close to zero)
    double newVolume;
    result = ALICAT_GetTotalVolumeQueued(TEST_MODBUS_ADDRESS, &newVolume, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get new total volume failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "Total volume after reset: %.2f", newVolume);

    // Verify totalizer was reset (should be very close to zero)
    if (fabs(newVolume) > 10.0) {  // Allow small tolerance
        snprintf(errorMsg, errorMsgSize, "Totalizer not properly reset: %.2f", newVolume);
        return -1;
    }

    LogDebugEx(LOG_DEVICE_ALICAT, "Totalizer test completed successfully");
    return 1;
}

int Test_ALICAT_BatchVolume(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Batch Volume...");

    // Set batch volume
    double batchVolume = 1000.0;  // 1000 units
    int result = ALICAT_SetBatchVolumeQueued(TEST_MODBUS_ADDRESS, batchVolume, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Set Batch Volume failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    Delay(ALICAT_TEST_DELAY_SHORT);

    // Get batch remaining
    double remaining;
    result = ALICAT_GetBatchRemainingQueued(TEST_MODBUS_ADDRESS, &remaining, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Get Batch Remaining failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    LogMessageEx(LOG_DEVICE_ALICAT, "Batch volume: %.2f, Remaining: %.2f", batchVolume, remaining);

    LogDebugEx(LOG_DEVICE_ALICAT, "Batch Volume test completed successfully");
    return 1;
}

int Test_ALICAT_QueuedCommands(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Queued Commands...");

    // Test multiple queued commands in sequence
    int result;

    // Set setpoint
    result = ALICAT_SetSetpointQueued(TEST_MODBUS_ADDRESS, 20.0, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Queued set setpoint failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    Delay(ALICAT_TEST_DELAY_SHORT);

    // Get status
    ALICAT_Status status;
    result = ALICAT_GetStatusQueued(TEST_MODBUS_ADDRESS, &status, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Queued get status failed: %s", ALICAT_GetErrorString(result));
        return -1;
    }

    // Reset setpoint
    ALICAT_SetSetpointQueued(TEST_MODBUS_ADDRESS, 0.0, DEVICE_PRIORITY_NORMAL);

    LogMessageEx(LOG_DEVICE_ALICAT, "Queued commands executed successfully");
    LogDebugEx(LOG_DEVICE_ALICAT, "Queued Commands test completed successfully");
    return 1;
}

int Test_ALICAT_SetpointRamp(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_ALICAT, "Testing ALICAT Setpoint Ramp...");

    // Test ramping setpoint from 0 to 50 sccm
    double setpoints[] = {0.0, 10.0, 25.0, 50.0, 25.0, 0.0};
    int numSetpoints = sizeof(setpoints) / sizeof(setpoints[0]);

    for (int i = 0; i < numSetpoints; i++) {
        LogMessageEx(LOG_DEVICE_ALICAT, "Setting setpoint to %.1f sccm", setpoints[i]);

        int result = ALICAT_SetSetpointQueued(TEST_MODBUS_ADDRESS, setpoints[i], DEVICE_PRIORITY_NORMAL);
        if (result != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Setpoint ramp failed at step %d: %s",
                    i, ALICAT_GetErrorString(result));
            return -1;
        }

        Delay(ALICAT_TEST_DELAY_MEDIUM);

        // Read back flow rate
        double flowRate;
        result = ALICAT_GetFlowRateQueued(TEST_MODBUS_ADDRESS, &flowRate, DEVICE_PRIORITY_NORMAL);
        if (result == SUCCESS) {
            LogMessageEx(LOG_DEVICE_ALICAT, "  Flow rate: %.2f sccm", flowRate);
        }
    }

    LogDebugEx(LOG_DEVICE_ALICAT, "Setpoint Ramp test completed successfully");
    return 1;
}

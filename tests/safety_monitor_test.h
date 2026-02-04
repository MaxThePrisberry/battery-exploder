/******************************************************************************
 * safety_monitor_test.h
 *
 * Test suite for the Simplified Safety Monitor Module
 * Tests SCU threshold check, debounce, valve control, start condition
 ******************************************************************************/

#ifndef SAFETY_MONITOR_TEST_H
#define SAFETY_MONITOR_TEST_H

#include "common.h"
#include "safety_monitor.h"

/******************************************************************************
 * Test Configuration
 ******************************************************************************/

// Test timing
#define SAFETY_TEST_DELAY_SHORT   0.1    // 100ms
#define SAFETY_TEST_DELAY_MEDIUM  0.5    // 500ms
#define SAFETY_TEST_TIMEOUT_MS    5000   // 5 second timeout

/******************************************************************************
 * Test Context Structure
 ******************************************************************************/

typedef struct {
    // Test control
    volatile int cancelRequested;
    TestState state;

    // UI integration
    int panelHandle;
    int buttonControl;
    void (*progressCallback)(const char *message);

    // Test tracking
    int totalTests;
    int passedTests;
    int failedTests;
    double suiteStartTime;

    // Current test info
    char currentTestName[256];
    double testStartTime;

    // Hardware presence flags
    int hasNI9472;
    int hasCDAQ;

} SafetyMonitorTestContext;

/******************************************************************************
 * Test Result Structure
 ******************************************************************************/

typedef struct {
    const char *testName;
    int (*testFunction)(SafetyMonitorTestContext *ctx, char *errorMsg, int errorMsgSize);
    int result;  // 0 = not run, 1 = pass, -1 = fail
    char errorMessage[256];
    double executionTime;
    double testStartTime;
} SafetyTestCase;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

// Main callback for UI button
int CVICALLBACK TestSafetyMonitorCallback(int panel, int control, int event,
                                          void *callbackData, int eventData1,
                                          int eventData2);
int CVICALLBACK TestSafetyMonitorWorkerThread(void *functionData);

// Test suite control functions
int SafetyMonitorTest_Initialize(SafetyMonitorTestContext *ctx, int panel, int buttonControl);
int SafetyMonitorTest_Run(SafetyMonitorTestContext *ctx);
void SafetyMonitorTest_Cancel(SafetyMonitorTestContext *ctx);
void SafetyMonitorTest_Cleanup(SafetyMonitorTestContext *ctx);
int SafetyMonitorTest_IsRunning(void);

/******************************************************************************
 * Unit Tests
 ******************************************************************************/

// Test lifecycle (initialize, start, stop)
int Test_SafetyMonitor_Initialize(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_StartStop(SafetyMonitorTestContext *ctx,
                                 char *errorMsg, int errorMsgSize);

// Test experiment registration
int Test_SafetyMonitor_ExperimentRegistration(SafetyMonitorTestContext *ctx,
                                              char *errorMsg, int errorMsgSize);

// Test start condition check
int Test_SafetyMonitor_StartCondition(SafetyMonitorTestContext *ctx,
                                      char *errorMsg, int errorMsgSize);

/******************************************************************************
 * Integration Tests (require hardware)
 ******************************************************************************/

// Test valve open/close via NI 9472
int Test_SafetyMonitor_ValveControl(SafetyMonitorTestContext *ctx,
                                    char *errorMsg, int errorMsgSize);

// Test SCU voltage reading via cDAQ
int Test_SafetyMonitor_SCUReading(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* SafetyMonitorTest_GetResultString(int result);

#endif // SAFETY_MONITOR_TEST_H

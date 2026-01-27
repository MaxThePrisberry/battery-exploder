/******************************************************************************
 * safety_monitor_test.h
 *
 * Test suite for the Centralized Safety Monitor Module
 * Tests boolean logic with all 16 input combinations from the safety matrix
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

// Voltage/flow values for testing
#define TEST_PRESSURE_OK          3.5    // Above threshold
#define TEST_PRESSURE_LOW         2.5    // Below threshold
#define TEST_FLOW_OK              500.0  // Above threshold (SCCM)
#define TEST_FLOW_LOW             50.0   // Below threshold (SCCM)
#define TEST_TEMP_OK              30.0   // Below max (deg C)
#define TEST_TEMP_HIGH            250.0  // Above max (deg C)

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
    int statusStringControl;
    void (*progressCallback)(const char *message);

    // Test tracking
    int totalTests;
    int passedTests;
    int failedTests;
    double suiteStartTime;

    // Current test info
    char currentTestName[256];
    double testStartTime;

    // Hardware presence flags (for integration tests)
    int hasNI9472;
    int hasALICAT;
    int hasDTB;
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
 * Unit Tests - Boolean Logic
 * Tests all 16 input combinations from the safety matrix
 ******************************************************************************/

// Test the central EvaluateSafetyConditions function
int Test_SafetyLogic_AllInputCombinations(SafetyMonitorTestContext *ctx,
                                          char *errorMsg, int errorMsgSize);

// Individual logic rule tests
int Test_SafetyLogic_PCU_Required(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize);
int Test_SafetyLogic_Flow_Required(SafetyMonitorTestContext *ctx,
                                   char *errorMsg, int errorMsgSize);
int Test_SafetyLogic_SCU_DangerousState(SafetyMonitorTestContext *ctx,
                                        char *errorMsg, int errorMsgSize);
int Test_SafetyLogic_ValveLogic(SafetyMonitorTestContext *ctx,
                                char *errorMsg, int errorMsgSize);
int Test_SafetyLogic_TemperatureOverride(SafetyMonitorTestContext *ctx,
                                         char *errorMsg, int errorMsgSize);

/******************************************************************************
 * Unit Tests - Module Functions
 ******************************************************************************/

int Test_SafetyMonitor_Initialize(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_StartStop(SafetyMonitorTestContext *ctx,
                                 char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_ExperimentRegistration(SafetyMonitorTestContext *ctx,
                                              char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_StateQuery(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_ForceCheck(SafetyMonitorTestContext *ctx,
                                  char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_AlarmAcknowledge(SafetyMonitorTestContext *ctx,
                                        char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_EmergencyStop(SafetyMonitorTestContext *ctx,
                                     char *errorMsg, int errorMsgSize);

/******************************************************************************
 * Integration Tests (require hardware)
 ******************************************************************************/

int Test_SafetyMonitor_SensorReading(SafetyMonitorTestContext *ctx,
                                     char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_ValveControl(SafetyMonitorTestContext *ctx,
                                    char *errorMsg, int errorMsgSize);
int Test_SafetyMonitor_ExperimentCallback(SafetyMonitorTestContext *ctx,
                                          char *errorMsg, int errorMsgSize);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* SafetyMonitorTest_GetResultString(int result);

#endif // SAFETY_MONITOR_TEST_H

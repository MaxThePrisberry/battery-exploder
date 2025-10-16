/******************************************************************************
 * alicat_test.h
 *
 * ALICAT BASIS 2 Mass Flow Controller Test Suite
 * Tests both low-level DLL functions and queued wrapper functions
 *
 * Authors: Maxwell Prisbrey, Nicolas Rasmont, Gabriel Meier
 ******************************************************************************/

#ifndef ALICAT_TEST_H
#define ALICAT_TEST_H

#include "common.h"
#include "alicat_dll.h"
#include "alicat_queue.h"
#include <userint.h>

/******************************************************************************
 * Test Configuration
 ******************************************************************************/

// Test timing
#define ALICAT_TEST_DELAY_SHORT        0.5     // seconds
#define ALICAT_TEST_DELAY_MEDIUM       1.0     // seconds
#define ALICAT_TEST_DELAY_LONG         2.0     // seconds

// Test timeout
#define ALICAT_TEST_TIMEOUT_MS         5000    // 5 seconds

// Test setpoint values
#define ALICAT_TEST_SETPOINT_LOW       10.0    // sccm
#define ALICAT_TEST_SETPOINT_MID       50.0    // sccm
#define ALICAT_TEST_SETPOINT_HIGH      100.0   // sccm

// Test gas type
#define ALICAT_TEST_GAS                GAS_AIR

// Test tolerances
#define ALICAT_FLOW_TOLERANCE          5.0     // sccm
#define ALICAT_TEMP_TOLERANCE          2.0     // °C

/******************************************************************************
 * Test Result Structure
 ******************************************************************************/

typedef struct {
    int totalTests;
    int passedTests;
    int failedTests;
    char lastError[256];
    double executionTime;
} ALICAT_TestSummary;

typedef struct {
    const char *testName;
    int (*testFunction)(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
    int result;  // 0 = not run, 1 = pass, -1 = fail
    char errorMessage[256];
    double executionTime;
} ALICAT_TestCase;

/******************************************************************************
 * Test Suite Control
 ******************************************************************************/

typedef struct {
    ALICAT_QueueManager *alicatQueueMgr;
    int panelHandle;
    int statusStringControl;
    int ledControl;
    volatile int cancelRequested;
    TestState state;
    ALICAT_TestSummary summary;
    void (*progressCallback)(const char *message);
} ALICAT_TestSuiteContext;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

// Main callback and worker thread
int CVICALLBACK TestALICATCallback(int panel, int control, int event,
                                   void *callbackData, int eventData1, int eventData2);
int CVICALLBACK TestALICATWorkerThread(void *functionData);

// Test suite functions
int ALICAT_TestSuite_Initialize(ALICAT_TestSuiteContext *context, ALICAT_QueueManager *alicatQueueMgr,
                                int panel, int statusControl, int ledControl);
int ALICAT_TestSuite_Run(ALICAT_TestSuiteContext *context);
void ALICAT_TestSuite_Cancel(ALICAT_TestSuiteContext *context);
void ALICAT_TestSuite_Cleanup(ALICAT_TestSuiteContext *context);

// Utility functions
void ALICAT_UpdateTestProgress(ALICAT_TestSuiteContext *context, const char *message);

// Individual test functions
int Test_ALICAT_Connection(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_GetStatus(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_SetGetSetpoint(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_SetGetGas(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_Tare(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_GetFlowRate(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_GetTemperature(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_GetValveDrive(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_PIDParams(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_FlowAveraging(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_RefTemperature(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_SetpointSource(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_Totalizer(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_BatchVolume(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_QueuedCommands(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);
int Test_ALICAT_SetpointRamp(ALICAT_QueueManager *alicatQueueMgr, char *errorMsg, int errorMsgSize);

#endif // ALICAT_TEST_H

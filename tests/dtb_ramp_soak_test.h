/******************************************************************************
 * dtb_ramp_soak_test.h
 *
 * Header for DTB4848 Ramp-Soak test suite
 *
 * Authors: Maxwell Prisbrey, Nicolas Rasmont, Gabriel Meier
 ******************************************************************************/

#ifndef DTB_RAMP_SOAK_TEST_H
#define DTB_RAMP_SOAK_TEST_H

#include "common.h"
#include "dtb4848_dll.h"
#include "dtb4848_queue.h"
#include <userint.h>

/******************************************************************************
 * Test Configuration
 ******************************************************************************/

// Test timing
#define DTB_TEST_DELAY_SHORT        0.5     // seconds
#define DTB_TEST_DELAY_MEDIUM       1.0     // seconds
#define DTB_TEST_DELAY_LONG         2.0     // seconds

// Test timeout
#define DTB_TEST_TIMEOUT_MS         5000    // 5 seconds

/******************************************************************************
 * Test Result Structure
 ******************************************************************************/

typedef struct {
    int totalTests;
    int passedTests;
    int failedTests;
    char lastError[256];
    double executionTime;
} DTBTestSummary;

/******************************************************************************
 * Test Context Structure
 ******************************************************************************/

typedef struct {
    int numTests;
    int numPassed;
    int numFailed;
} DTBRampSoakTestContext;

/******************************************************************************
 * Test Suite Control
 ******************************************************************************/

typedef struct {
    DTBQueueManager *dtbQueueMgr;
    int panelHandle;
    int statusStringControl;
    int ledControl;
    volatile int cancelRequested;
    TestState state;
    DTBTestSummary summary;
    void (*progressCallback)(const char *message);
} DTBTestSuiteContext;

/******************************************************************************
 * Test Case Structure
 ******************************************************************************/

typedef struct {
    const char *name;
    int (*testFunc)(void);
    int passed;
    char errorMessage[256];
    double duration;
} TestCase;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

// Main callback and worker thread
int CVICALLBACK TestDTBRampSoakCallback(int panel, int control, int event,
                                        void *callbackData, int eventData1, int eventData2);
int CVICALLBACK TestDTBRampSoakWorkerThread(void *functionData);

// Test suite functions
int DTB_TestSuite_Initialize(DTBTestSuiteContext *context, DTBQueueManager *dtbQueueMgr,
                             int panel, int statusControl, int ledControl);
int DTB_TestSuite_Run(DTBTestSuiteContext *context);
void DTB_TestSuite_Cancel(DTBTestSuiteContext *context);
void DTB_TestSuite_Cleanup(DTBTestSuiteContext *context);

// Utility functions
void DTB_UpdateTestProgress(DTBTestSuiteContext *context, const char *message);

// Legacy test suite functions (kept for standalone testing)
/**
 * Initialize the test suite
 * Returns 0 on success, -1 on failure
 */
int DTBRampSoakTest_Initialize(void);

/**
 * Cleanup the test suite
 */
void DTBRampSoakTest_Cleanup(void);

/**
 * Run all tests
 * useHardware: 1 to run with real hardware, 0 for validation tests only
 * Returns 0 if all tests pass, -1 if any test fails
 */
int DTBRampSoakTest_RunAll(int useHardware);

/******************************************************************************
 * Individual Test Functions
 ******************************************************************************/

int Test_PatternSetGet(void);
int Test_StepSetGet(void);
int Test_ActualStepCount(void);
int Test_CycleCount(void);
int Test_LinkPattern(void);
int Test_StartPattern(void);
int Test_ProgramControl(void);
int Test_ProgramStatus(void);
int Test_SimpleRamp(void);
int Test_ClearPattern(void);
int Test_ClearAllPatterns(void);
int Test_PatternValidation(void);
int Test_StepValidation(void);
int Test_QueuedCommands(void);
int Test_AtomicPatternConfiguration(void);
int Test_MultiPatternChain(void);
int Test_RampSoakEdgeCases(void);

#endif // DTB_RAMP_SOAK_TEST_H

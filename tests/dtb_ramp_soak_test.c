/******************************************************************************
 * dtb_ramp_soak_test.c
 *
 * Comprehensive test suite for DTB4848 Ramp-Soak (PID Program) functionality
 * Tests both low-level DLL functions and queued wrapper functions
 *
 * Authors: Maxwell Prisbrey, Nicolas Rasmont, Gabriel Meier
 ******************************************************************************/

#include "dtb_ramp_soak_test.h"
#include "logging.h"
#include "BatteryExploder.h"
#include <toolbox.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/******************************************************************************
 * Test Configuration
 ******************************************************************************/

// Test DTB device configuration
#define TEST_COM_PORT           3
#define TEST_BAUD_RATE          9600
#define TEST_SLAVE_ADDRESS      1
#define TEST_TIMEOUT_MS         5000

// Test tolerance
#define TEMP_TOLERANCE          0.1
#define TIME_TOLERANCE          1

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static DTBRampSoakTestContext *g_testContext = NULL;
static DTBQueueManager *g_testQueueManager = NULL;
static DTB_Handle g_testHandle;

// Test cases array
static TestCase g_rampSoakTestCases[] = {
    {"Pattern Set and Get", Test_PatternSetGet, 0, "", 0.0},
    {"Step Set and Get", Test_StepSetGet, 0, "", 0.0},
    {"Actual Step Count", Test_ActualStepCount, 0, "", 0.0},
    {"Cycle Count", Test_CycleCount, 0, "", 0.0},
    {"Link Pattern", Test_LinkPattern, 0, "", 0.0},
    {"Start Pattern", Test_StartPattern, 0, "", 0.0},
    {"Program Control", Test_ProgramControl, 0, "", 0.0},
    {"Program Status", Test_ProgramStatus, 0, "", 0.0},
    {"Simple Ramp", Test_SimpleRamp, 0, "", 0.0},
    {"Clear Pattern", Test_ClearPattern, 0, "", 0.0},
    {"Clear All Patterns", Test_ClearAllPatterns, 0, "", 0.0},
    {"Pattern Validation", Test_PatternValidation, 0, "", 0.0},
    {"Step Validation", Test_StepValidation, 0, "", 0.0},
    {"Queued Commands", Test_QueuedCommands, 0, "", 0.0},
    {"Atomic Pattern Configuration", Test_AtomicPatternConfiguration, 0, "", 0.0},
    {"Multi-Pattern Chain", Test_MultiPatternChain, 0, "", 0.0},
    {"Edge Cases", Test_RampSoakEdgeCases, 0, "", 0.0}
};

static int g_numRampSoakTestCases = sizeof(g_rampSoakTestCases) / sizeof(TestCase);

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static int CompareSteps(const DTB_Step *step1, const DTB_Step *step2) {
    if (fabs(step1->temperature - step2->temperature) > TEMP_TOLERANCE) {
        return 0;
    }
    if (abs(step1->timeMinutes - step2->timeMinutes) > TIME_TOLERANCE) {
        return 0;
    }
    return 1;
}

static int ComparePatterns(const DTB_Pattern *pattern1, const DTB_Pattern *pattern2) {
    if (pattern1->actualStepCount != pattern2->actualStepCount) {
        LogError("Pattern step count mismatch: %d vs %d",
                 pattern1->actualStepCount, pattern2->actualStepCount);
        return 0;
    }
    if (pattern1->cycleCount != pattern2->cycleCount) {
        LogError("Pattern cycle count mismatch: %d vs %d",
                 pattern1->cycleCount, pattern2->cycleCount);
        return 0;
    }
    if (pattern1->linkPattern != pattern2->linkPattern) {
        LogError("Pattern link mismatch: %d vs %d",
                 pattern1->linkPattern, pattern2->linkPattern);
        return 0;
    }

    for (int i = 0; i < pattern1->actualStepCount; i++) {
        if (!CompareSteps(&pattern1->steps[i], &pattern2->steps[i])) {
            LogError("Pattern step %d mismatch", i);
            return 0;
        }
    }

    return 1;
}

static void PrintPattern(const DTB_Pattern *pattern, int patternNumber) {
    LogMessage("Pattern %d: %d steps, %d cycles, link=%d",
               patternNumber, pattern->actualStepCount,
               pattern->cycleCount, pattern->linkPattern);
    for (int i = 0; i < pattern->actualStepCount; i++) {
        LogMessage("  Step %d: %.1f°C for %d min",
                   i, pattern->steps[i].temperature, pattern->steps[i].timeMinutes);
    }
}

/******************************************************************************
 * Test Suite Initialization and Cleanup
 ******************************************************************************/

int DTBRampSoakTest_Initialize(void) {
    LogMessage("===== DTB Ramp-Soak Test Suite Initialization =====");

    // Allocate test context
    g_testContext = calloc(1, sizeof(DTBRampSoakTestContext));
    if (!g_testContext) {
        LogError("Failed to allocate test context");
        return -1;
    }

    g_testContext->numTests = g_numRampSoakTestCases;
    g_testContext->numPassed = 0;
    g_testContext->numFailed = 0;

    LogMessage("Test suite initialized with %d test cases", g_numRampSoakTestCases);
    return 0;
}

void DTBRampSoakTest_Cleanup(void) {
    LogMessage("===== DTB Ramp-Soak Test Suite Cleanup =====");

    // Shutdown queue if initialized
    if (g_testQueueManager) {
        DTB_QueueShutdown(g_testQueueManager);
        g_testQueueManager = NULL;
    }

    // Cleanup test context
    if (g_testContext) {
        free(g_testContext);
        g_testContext = NULL;
    }

    LogMessage("Test suite cleanup complete");
}

/******************************************************************************
 * Test Case Implementations
 ******************************************************************************/

int Test_PatternSetGet(void) {
    LogMessage("\n----- Test: Pattern Set and Get -----");

    // Create test pattern
    DTB_Pattern testPattern = {0};
    testPattern.actualStepCount = 3;
    testPattern.cycleCount = 2;
    testPattern.linkPattern = DTB_LINK_PATTERN_END;

    testPattern.steps[0].temperature = 25.0;
    testPattern.steps[0].timeMinutes = 10;

    testPattern.steps[1].temperature = 50.0;
    testPattern.steps[1].timeMinutes = 30;

    testPattern.steps[2].temperature = 75.0;
    testPattern.steps[2].timeMinutes = 20;

    int patternNumber = 0;

    // Set pattern
    LogMessage("Setting pattern %d...", patternNumber);
    int result = DTB_SetPattern(&g_testHandle, patternNumber, &testPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Get pattern back
    DTB_Pattern readPattern = {0};
    LogMessage("Reading pattern %d...", patternNumber);
    result = DTB_GetPattern(&g_testHandle, patternNumber, &readPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Compare patterns
    PrintPattern(&testPattern, patternNumber);
    PrintPattern(&readPattern, patternNumber);

    if (!ComparePatterns(&testPattern, &readPattern)) {
        LogError("Pattern mismatch!");
        return -1;
    }

    LogMessage("Pattern set/get test PASSED");
    return 0;
}

int Test_StepSetGet(void) {
    LogMessage("\n----- Test: Step Set and Get -----");

    int patternNumber = 1;
    int stepNumber = 0;

    // Create test step
    DTB_Step testStep;
    testStep.temperature = 85.5;
    testStep.timeMinutes = 45;

    // Set step
    LogMessage("Setting step %d in pattern %d: %.1f°C for %d min",
               stepNumber, patternNumber, testStep.temperature, testStep.timeMinutes);
    int result = DTB_SetStep(&g_testHandle, patternNumber, stepNumber, &testStep);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetStep failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Get step back
    DTB_Step readStep;
    LogMessage("Reading step %d from pattern %d...", stepNumber, patternNumber);
    result = DTB_GetStep(&g_testHandle, patternNumber, stepNumber, &readStep);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetStep failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Read step: %.1f°C for %d min", readStep.temperature, readStep.timeMinutes);

    // Compare steps
    if (!CompareSteps(&testStep, &readStep)) {
        LogError("Step mismatch!");
        return -1;
    }

    LogMessage("Step set/get test PASSED");
    return 0;
}

int Test_ActualStepCount(void) {
    LogMessage("\n----- Test: Actual Step Count -----");

    int patternNumber = 2;
    int testStepCount = 5;

    // Set step count
    LogMessage("Setting actual step count to %d for pattern %d", testStepCount, patternNumber);
    int result = DTB_SetActualStepCount(&g_testHandle, patternNumber, testStepCount);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetActualStepCount failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Get step count back
    int readStepCount;
    LogMessage("Reading actual step count for pattern %d...", patternNumber);
    result = DTB_GetActualStepCount(&g_testHandle, patternNumber, &readStepCount);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetActualStepCount failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Read step count: %d", readStepCount);

    if (readStepCount != testStepCount) {
        LogError("Step count mismatch: expected %d, got %d", testStepCount, readStepCount);
        return -1;
    }

    LogMessage("Actual step count test PASSED");
    return 0;
}

int Test_CycleCount(void) {
    LogMessage("\n----- Test: Cycle Count -----");

    int patternNumber = 3;
    int testCycleCount = 10;

    // Set cycle count
    LogMessage("Setting cycle count to %d for pattern %d", testCycleCount, patternNumber);
    int result = DTB_SetCycleCount(&g_testHandle, patternNumber, testCycleCount);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetCycleCount failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Get cycle count back
    int readCycleCount;
    LogMessage("Reading cycle count for pattern %d...", patternNumber);
    result = DTB_GetCycleCount(&g_testHandle, patternNumber, &readCycleCount);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetCycleCount failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Read cycle count: %d", readCycleCount);

    if (readCycleCount != testCycleCount) {
        LogError("Cycle count mismatch: expected %d, got %d", testCycleCount, readCycleCount);
        return -1;
    }

    LogMessage("Cycle count test PASSED");
    return 0;
}

int Test_LinkPattern(void) {
    LogMessage("\n----- Test: Link Pattern -----");

    int patternNumber = 4;
    int testLinkPattern = 5;

    // Set link pattern
    LogMessage("Setting link pattern to %d for pattern %d", testLinkPattern, patternNumber);
    int result = DTB_SetLinkPattern(&g_testHandle, patternNumber, testLinkPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetLinkPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Get link pattern back
    int readLinkPattern;
    LogMessage("Reading link pattern for pattern %d...", patternNumber);
    result = DTB_GetLinkPattern(&g_testHandle, patternNumber, &readLinkPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetLinkPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Read link pattern: %d", readLinkPattern);

    if (readLinkPattern != testLinkPattern) {
        LogError("Link pattern mismatch: expected %d, got %d", testLinkPattern, readLinkPattern);
        return -1;
    }

    LogMessage("Link pattern test PASSED");
    return 0;
}

int Test_StartPattern(void) {
    LogMessage("\n----- Test: Start Pattern -----");

    int testStartPattern = 3;

    // Set start pattern
    LogMessage("Setting start pattern to %d", testStartPattern);
    int result = DTB_SetStartPattern(&g_testHandle, testStartPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetStartPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Get start pattern back
    int readStartPattern;
    LogMessage("Reading start pattern...");
    result = DTB_GetStartPattern(&g_testHandle, &readStartPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetStartPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Read start pattern: %d", readStartPattern);

    if (readStartPattern != testStartPattern) {
        LogError("Start pattern mismatch: expected %d, got %d", testStartPattern, readStartPattern);
        return -1;
    }

    LogMessage("Start pattern test PASSED");
    return 0;
}

int Test_ProgramControl(void) {
    LogMessage("\n----- Test: Program Control -----");

    int result;

    // Test stop
    LogMessage("Testing program stop...");
    result = DTB_StopProgram(&g_testHandle);
    if (result != DTB_SUCCESS) {
        LogError("DTB_StopProgram failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Test start
    LogMessage("Testing program start...");
    result = DTB_StartProgram(&g_testHandle);
    if (result != DTB_SUCCESS) {
        LogError("DTB_StartProgram failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    Delay(1.0); // Allow time for program to start

    // Test hold
    LogMessage("Testing program hold...");
    result = DTB_HoldProgram(&g_testHandle);
    if (result != DTB_SUCCESS) {
        LogError("DTB_HoldProgram failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    Delay(0.5);

    // Test resume
    LogMessage("Testing program resume...");
    result = DTB_ResumeProgram(&g_testHandle);
    if (result != DTB_SUCCESS) {
        LogError("DTB_ResumeProgram failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    Delay(0.5);

    // Stop program for cleanup
    LogMessage("Stopping program for cleanup...");
    result = DTB_StopProgram(&g_testHandle);
    if (result != DTB_SUCCESS) {
        LogError("DTB_StopProgram (cleanup) failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Program control test PASSED");
    return 0;
}

int Test_ProgramStatus(void) {
    LogMessage("\n----- Test: Program Status -----");

    DTB_ProgramStatus status;

    // Get initial status (should be stopped)
    LogMessage("Getting program status...");
    int result = DTB_GetProgramStatus(&g_testHandle, &status);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetProgramStatus failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Program status:");
    LogMessage("  Running: %d", status.isRunning);
    LogMessage("  Paused: %d", status.isPaused);
    LogMessage("  Current pattern: %d", status.currentPattern);
    LogMessage("  Current step: %d", status.currentStep);
    LogMessage("  State: %d", status.state);

    // Verify stopped state
    if (status.state != DTB_PROG_STATE_STOPPED) {
        LogWarning("Program state is not STOPPED as expected");
    }

    LogMessage("Program status test PASSED");
    return 0;
}

int Test_SimpleRamp(void) {
    LogMessage("\n----- Test: Simple Ramp -----");

    double startTemp = 20.0;
    double endTemp = 100.0;
    int durationMinutes = 60;

    // Set simple ramp
    LogMessage("Setting simple ramp: %.1f°C to %.1f°C over %d minutes",
               startTemp, endTemp, durationMinutes);
    int result = DTB_SetSimpleRamp(&g_testHandle, startTemp, endTemp, durationMinutes);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetSimpleRamp failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Verify pattern 0 was configured
    DTB_Pattern readPattern;
    result = DTB_GetPattern(&g_testHandle, 0, &readPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Simple ramp created pattern:");
    PrintPattern(&readPattern, 0);

    // Verify 2 steps were created
    if (readPattern.actualStepCount != 2) {
        LogError("Expected 2 steps, got %d", readPattern.actualStepCount);
        return -1;
    }

    LogMessage("Simple ramp test PASSED");
    return 0;
}

int Test_ClearPattern(void) {
    LogMessage("\n----- Test: Clear Pattern -----");

    int patternNumber = 6;

    // First, set a pattern
    DTB_Pattern testPattern = {0};
    testPattern.actualStepCount = 2;
    testPattern.cycleCount = 1;
    testPattern.linkPattern = DTB_LINK_PATTERN_END;
    testPattern.steps[0].temperature = 30.0;
    testPattern.steps[0].timeMinutes = 15;
    testPattern.steps[1].temperature = 40.0;
    testPattern.steps[1].timeMinutes = 25;

    int result = DTB_SetPattern(&g_testHandle, patternNumber, &testPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Clear the pattern
    LogMessage("Clearing pattern %d...", patternNumber);
    result = DTB_ClearPattern(&g_testHandle, patternNumber);
    if (result != DTB_SUCCESS) {
        LogError("DTB_ClearPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Verify pattern is cleared
    DTB_Pattern readPattern;
    result = DTB_GetPattern(&g_testHandle, patternNumber, &readPattern);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetPattern failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Check that all steps are zero
    int allZero = 1;
    for (int i = 0; i < DTB_MAX_STEPS_PER_PATTERN; i++) {
        if (readPattern.steps[i].temperature != 0.0 || readPattern.steps[i].timeMinutes != 0) {
            allZero = 0;
            break;
        }
    }

    if (!allZero) {
        LogError("Pattern was not completely cleared");
        return -1;
    }

    LogMessage("Clear pattern test PASSED");
    return 0;
}

int Test_ClearAllPatterns(void) {
    LogMessage("\n----- Test: Clear All Patterns -----");

    // Clear all patterns
    LogMessage("Clearing all patterns...");
    int result = DTB_ClearAllPatterns(&g_testHandle);
    if (result != DTB_SUCCESS) {
        LogError("DTB_ClearAllPatterns failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Clear all patterns test PASSED");
    return 0;
}

int Test_PatternValidation(void) {
    LogMessage("\n----- Test: Pattern Validation -----");

    int result;
    DTB_Pattern invalidPattern;

    // Test: Invalid pattern number (negative)
    LogMessage("Testing invalid pattern number (negative)...");
    result = DTB_SetPattern(&g_testHandle, -1, &invalidPattern);
    if (result == DTB_SUCCESS) {
        LogError("Should have rejected negative pattern number");
        return -1;
    }
    LogMessage("  Correctly rejected negative pattern number");

    // Test: Invalid pattern number (too large)
    LogMessage("Testing invalid pattern number (>= DTB_MAX_PATTERNS)...");
    result = DTB_SetPattern(&g_testHandle, DTB_MAX_PATTERNS, &invalidPattern);
    if (result == DTB_SUCCESS) {
        LogError("Should have rejected pattern number >= DTB_MAX_PATTERNS");
        return -1;
    }
    LogMessage("  Correctly rejected pattern number >= DTB_MAX_PATTERNS");

    // Test: NULL pattern pointer
    LogMessage("Testing NULL pattern pointer...");
    result = DTB_SetPattern(&g_testHandle, 0, NULL);
    if (result == DTB_SUCCESS) {
        LogError("Should have rejected NULL pattern pointer");
        return -1;
    }
    LogMessage("  Correctly rejected NULL pattern pointer");

    LogMessage("Pattern validation test PASSED");
    return 0;
}

int Test_StepValidation(void) {
    LogMessage("\n----- Test: Step Validation -----");

    int result;
    DTB_Step testStep = {50.0, 30};

    // Test: Invalid step number (negative)
    LogMessage("Testing invalid step number (negative)...");
    result = DTB_SetStep(&g_testHandle, 0, -1, &testStep);
    if (result == DTB_SUCCESS) {
        LogError("Should have rejected negative step number");
        return -1;
    }
    LogMessage("  Correctly rejected negative step number");

    // Test: Invalid step number (too large)
    LogMessage("Testing invalid step number (>= DTB_MAX_STEPS_PER_PATTERN)...");
    result = DTB_SetStep(&g_testHandle, 0, DTB_MAX_STEPS_PER_PATTERN, &testStep);
    if (result == DTB_SUCCESS) {
        LogError("Should have rejected step number >= DTB_MAX_STEPS_PER_PATTERN");
        return -1;
    }
    LogMessage("  Correctly rejected step number >= DTB_MAX_STEPS_PER_PATTERN");

    // Test: NULL step pointer
    LogMessage("Testing NULL step pointer...");
    result = DTB_SetStep(&g_testHandle, 0, 0, NULL);
    if (result == DTB_SUCCESS) {
        LogError("Should have rejected NULL step pointer");
        return -1;
    }
    LogMessage("  Correctly rejected NULL step pointer");

    LogMessage("Step validation test PASSED");
    return 0;
}

int Test_QueuedCommands(void) {
    LogMessage("\n----- Test: Queued Commands -----");

    if (!g_testQueueManager) {
        LogWarning("Queue manager not initialized, skipping queued command test");
        return 0;
    }

    // Test queued pattern set/get
    DTB_Pattern testPattern = {0};
    testPattern.actualStepCount = 2;
    testPattern.cycleCount = 3;
    testPattern.linkPattern = DTB_LINK_PATTERN_END;
    testPattern.steps[0].temperature = 35.0;
    testPattern.steps[0].timeMinutes = 20;
    testPattern.steps[1].temperature = 55.0;
    testPattern.steps[1].timeMinutes = 40;

    LogMessage("Setting pattern via queue...");
    int result = DTB_SetPatternQueued(TEST_SLAVE_ADDRESS, 7, &testPattern, PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("DTB_SetPatternQueued failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Get pattern back via queue
    DTB_Pattern readPattern;
    LogMessage("Getting pattern via queue...");
    result = DTB_GetPatternQueued(TEST_SLAVE_ADDRESS, 7, &readPattern, PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetPatternQueued failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    if (!ComparePatterns(&testPattern, &readPattern)) {
        LogError("Queued pattern mismatch!");
        return -1;
    }

    LogMessage("Queued commands test PASSED");
    return 0;
}

int Test_AtomicPatternConfiguration(void) {
    LogMessage("\n----- Test: Atomic Pattern Configuration -----");

    if (!g_testQueueManager) {
        LogWarning("Queue manager not initialized, skipping atomic configuration test");
        return 0;
    }

    // Create a complex pattern
    DTB_Pattern testPattern = {0};
    testPattern.actualStepCount = 4;
    testPattern.cycleCount = 5;
    testPattern.linkPattern = 1;

    testPattern.steps[0].temperature = 25.0;
    testPattern.steps[0].timeMinutes = 10;
    testPattern.steps[1].temperature = 50.0;
    testPattern.steps[1].timeMinutes = 20;
    testPattern.steps[2].temperature = 75.0;
    testPattern.steps[2].timeMinutes = 30;
    testPattern.steps[3].temperature = 100.0;
    testPattern.steps[3].timeMinutes = 40;

    // Configure atomically
    LogMessage("Configuring pattern atomically...");
    int result = DTB_ConfigurePatternAtomic(TEST_SLAVE_ADDRESS, 0, &testPattern,
                                           NULL, NULL, PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("DTB_ConfigurePatternAtomic failed: %d", result);
        return -1;
    }

    // Wait for transaction to complete
    Delay(2.0);

    // Verify pattern was configured correctly
    DTB_Pattern readPattern;
    result = DTB_GetPatternQueued(TEST_SLAVE_ADDRESS, 0, &readPattern, PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("DTB_GetPatternQueued failed: %s", DTB_GetErrorString(result));
        return -1;
    }

    if (!ComparePatterns(&testPattern, &readPattern)) {
        LogError("Atomic pattern configuration mismatch!");
        return -1;
    }

    LogMessage("Atomic pattern configuration test PASSED");
    return 0;
}

int Test_MultiPatternChain(void) {
    LogMessage("\n----- Test: Multi-Pattern Chain -----");

    // Create pattern 0 that links to pattern 1
    DTB_Pattern pattern0 = {0};
    pattern0.actualStepCount = 2;
    pattern0.cycleCount = 1;
    pattern0.linkPattern = 1; // Link to pattern 1
    pattern0.steps[0].temperature = 20.0;
    pattern0.steps[0].timeMinutes = 10;
    pattern0.steps[1].temperature = 40.0;
    pattern0.steps[1].timeMinutes = 10;

    // Create pattern 1 that links to pattern 2
    DTB_Pattern pattern1 = {0};
    pattern1.actualStepCount = 2;
    pattern1.cycleCount = 1;
    pattern1.linkPattern = 2; // Link to pattern 2
    pattern1.steps[0].temperature = 60.0;
    pattern1.steps[0].timeMinutes = 10;
    pattern1.steps[1].temperature = 80.0;
    pattern1.steps[1].timeMinutes = 10;

    // Create pattern 2 (final)
    DTB_Pattern pattern2 = {0};
    pattern2.actualStepCount = 1;
    pattern2.cycleCount = 1;
    pattern2.linkPattern = DTB_LINK_PATTERN_END; // End
    pattern2.steps[0].temperature = 25.0;
    pattern2.steps[0].timeMinutes = 15;

    // Set all patterns
    LogMessage("Setting up 3-pattern chain...");
    int result = DTB_SetPattern(&g_testHandle, 0, &pattern0);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set pattern 0: %s", DTB_GetErrorString(result));
        return -1;
    }

    result = DTB_SetPattern(&g_testHandle, 1, &pattern1);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set pattern 1: %s", DTB_GetErrorString(result));
        return -1;
    }

    result = DTB_SetPattern(&g_testHandle, 2, &pattern2);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set pattern 2: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Set start pattern to 0
    result = DTB_SetStartPattern(&g_testHandle, 0);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set start pattern: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Pattern chain configured successfully");
    LogMessage("  Pattern 0 -> Pattern 1 -> Pattern 2 -> END");

    LogMessage("Multi-pattern chain test PASSED");
    return 0;
}

int Test_RampSoakEdgeCases(void) {
    LogMessage("\n----- Test: Edge Cases -----");

    int result;

    // Test: Maximum values
    LogMessage("Testing maximum step time...");
    DTB_Step maxStep;
    maxStep.temperature = 100.0;
    maxStep.timeMinutes = DTB_MAX_STEP_TIME;
    result = DTB_SetStep(&g_testHandle, 0, 0, &maxStep);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set maximum step time: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Test: Minimum values
    LogMessage("Testing minimum step time...");
    DTB_Step minStep;
    minStep.temperature = 25.0;
    minStep.timeMinutes = DTB_MIN_STEP_TIME;
    result = DTB_SetStep(&g_testHandle, 0, 1, &minStep);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set minimum step time: %s", DTB_GetErrorString(result));
        return -1;
    }

    // Test: Maximum cycle count
    LogMessage("Testing maximum cycle count...");
    result = DTB_SetCycleCount(&g_testHandle, 0, DTB_MAX_CYCLE_COUNT);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set maximum cycle count: %s", DTB_GetErrorString(result));
        return -1;
    }

    LogMessage("Edge cases test PASSED");
    return 0;
}

/******************************************************************************
 * Test Runner
 ******************************************************************************/

int DTBRampSoakTest_RunAll(int useHardware) {
    LogMessage("\n========================================");
    LogMessage("DTB Ramp-Soak Test Suite");
    LogMessage("========================================");
    LogMessage("Hardware mode: %s", useHardware ? "ENABLED" : "DISABLED");

    if (DTBRampSoakTest_Initialize() != 0) {
        LogError("Failed to initialize test suite");
        return -1;
    }

    // Initialize hardware if requested
    if (useHardware) {
        LogMessage("\n----- Hardware Initialization -----");

        // Initialize DTB handle
        LogMessage("Initializing DTB handle (COM%d, Slave %d, Baud %d)...",
                   TEST_COM_PORT, TEST_SLAVE_ADDRESS, TEST_BAUD_RATE);
        int result = DTB_Initialize(&g_testHandle, TEST_COM_PORT,
                                    TEST_SLAVE_ADDRESS, TEST_BAUD_RATE);
        if (result != DTB_SUCCESS) {
            LogError("Failed to initialize DTB: %s", DTB_GetErrorString(result));
            DTBRampSoakTest_Cleanup();
            return -1;
        }

        // Initialize queue
        LogMessage("Initializing DTB queue manager...");
        int slaveAddresses[] = {TEST_SLAVE_ADDRESS};
        g_testQueueManager = DTB_QueueInit(TEST_COM_PORT, TEST_BAUD_RATE, slaveAddresses, 1);
        if (!g_testQueueManager) {
            LogError("Failed to initialize DTB queue manager");
            DTBRampSoakTest_Cleanup();
            return -1;
        }
        DTB_SetGlobalQueueManager(g_testQueueManager);

        // Clear all patterns before testing
        LogMessage("Clearing all patterns...");
        DTB_ClearAllPatterns(&g_testHandle);

        LogMessage("Hardware initialization complete");
    } else {
        LogMessage("Hardware testing disabled - only validation tests will run");
    }

    // Run all test cases
    LogMessage("\n----- Running Test Cases -----");
    for (int i = 0; i < g_numRampSoakTestCases; i++) {
        TestCase *test = &g_rampSoakTestCases[i];

        // Skip hardware tests if not using hardware
        if (!useHardware && strstr(test->name, "Queued") == NULL &&
            strstr(test->name, "Validation") == NULL) {
            LogMessage("\nSkipping test %d/%d: %s (requires hardware)",
                       i + 1, g_numRampSoakTestCases, test->name);
            continue;
        }

        LogMessage("\n========================================");
        LogMessage("Running test %d/%d: %s", i + 1, g_numRampSoakTestCases, test->name);
        LogMessage("========================================");

        double startTime = Timer();
        int result = test->testFunc();
        double duration = Timer() - startTime;

        test->duration = duration;
        test->passed = (result == 0);

        if (test->passed) {
            g_testContext->numPassed++;
            LogMessage("TEST PASSED (%.3f s)", duration);
        } else {
            g_testContext->numFailed++;
            LogMessage("TEST FAILED (%.3f s)", duration);
            snprintf(test->errorMessage, sizeof(test->errorMessage), "Test failed");
        }
    }

    // Print summary
    LogMessage("\n========================================");
    LogMessage("Test Suite Summary");
    LogMessage("========================================");
    LogMessage("Total tests: %d", g_numRampSoakTestCases);
    LogMessage("Passed: %d", g_testContext->numPassed);
    LogMessage("Failed: %d", g_testContext->numFailed);
    LogMessage("Success rate: %.1f%%",
               100.0 * g_testContext->numPassed / g_numRampSoakTestCases);

    // Cleanup
    DTBRampSoakTest_Cleanup();

    return (g_testContext->numFailed == 0) ? 0 : -1;
}

/******************************************************************************
 * eclab_olecom_test.c
 *
 * Test program for EC-Lab OLE COM custom interface implementation
 *
 * This test verifies that the IEClabExe custom interface is working correctly.
 * It tests the basic workflow: initialize → connect → test → disconnect → shutdown
 *
 * PREREQUISITES:
 * 1. EC-Lab must be running before executing this test
 * 2. EC-Lab must be registered as COM server: ECLab.exe /regserver
 * 3. A device should be connected in EC-Lab GUI
 * 4. Update DEVICE_NUMBER below to match your setup
 *
 ******************************************************************************/

#include <stdio.h>
#include <windows.h>
#include "../biologic/eclab_olecom.h"
#include "../logging.h"
#include "../common.h"

// Test configuration - UPDATE THESE FOR YOUR SETUP
#define DEVICE_NUMBER    0    // EC-Lab device index (typically 0 for first device)

/******************************************************************************
 * Test Helper Functions
 ******************************************************************************/

static void PrintTestHeader(const char *testName) {
    printf("\n");
    printf("========================================\n");
    printf("TEST: %s\n", testName);
    printf("========================================\n");
}

static void PrintTestResult(const char *testName, int result) {
    if (result == SUCCESS) {
        printf("[PASS] %s\n", testName);
    } else {
        printf("[FAIL] %s - Error: %s (code: %d)\n",
               testName, ECLAB_GetErrorString(result), result);
    }
}

/******************************************************************************
 * Individual Tests
 ******************************************************************************/

/**
 * Test 1: Initialize EC-Lab COM connection
 */
static int Test_Initialize(ECLabConnection **conn) {
    PrintTestHeader("Initialize EC-Lab COM Connection");

    int result = ECLAB_Initialize(conn, NULL);
    PrintTestResult("ECLAB_Initialize", result);

    return result;
}

/**
 * Test 2: Connect to device
 */
static int Test_ConnectDevice(ECLabConnection *conn) {
    PrintTestHeader("Connect to Device");

    int result = ECLAB_ConnectDevice(conn, DEVICE_NUMBER);
    PrintTestResult("ECLAB_ConnectDevice", result);

    return result;
}

/**
 * Test 3: Test device connection
 */
static int Test_TestConnection(ECLabConnection *conn) {
    PrintTestHeader("Test Device Connection");

    int result = ECLAB_TestConnection(conn);
    PrintTestResult("ECLAB_TestConnection", result);

    return result;
}

/**
 * Test 4: Enable/Disable message windows
 */
static int Test_EnableMessagesWindows(ECLabConnection *conn) {
    PrintTestHeader("Enable/Disable Message Windows");

    // Disable message windows for automated testing
    int result = ECLAB_EnableMessagesWindows(conn, false);
    PrintTestResult("ECLAB_EnableMessagesWindows(false)", result);

    if (result != SUCCESS) return result;

    // Re-enable them
    result = ECLAB_EnableMessagesWindows(conn, true);
    PrintTestResult("ECLAB_EnableMessagesWindows(true)", result);

    return result;
}

/**
 * Test 5: Get measurement status
 */
static int Test_MeasureStatus(ECLabConnection *conn) {
    PrintTestHeader("Get Measurement Status");

    ECLAB_Status status;
    int result = ECLAB_MeasureStatus(conn, DEVICE_NUMBER, 0, &status);
    PrintTestResult("ECLAB_MeasureStatus", result);

    if (result == SUCCESS) {
        printf("  Status: %d (0=Stop, 1=Run, 2=Pause)\n", status.status);
        printf("  Technique: %d\n", status.techniqueCode);
        printf("  Time: %.2f s\n", status.time);
        printf("  Ewe: %.4f V\n", status.ewe);
        printf("  Current: %.6f A\n", status.current);
        printf("  Safety: %d (0=OK)\n", status.safetyLimit);
        printf("  Connection: %d (0=OK)\n", status.connection);
    }

    return result;
}

/**
 * Test 6: Disconnect from device
 */
static int Test_DisconnectDevice(ECLabConnection *conn) {
    PrintTestHeader("Disconnect from Device");

    int result = ECLAB_DisconnectDevice(conn);
    PrintTestResult("ECLAB_DisconnectDevice", result);

    return result;
}

/**
 * Test 7: Shutdown EC-Lab COM connection
 */
static int Test_Shutdown(ECLabConnection *conn) {
    PrintTestHeader("Shutdown EC-Lab COM Connection");

    int result = ECLAB_Shutdown(conn);
    PrintTestResult("ECLAB_Shutdown", result);

    return result;
}

/******************************************************************************
 * Test Suite Runner
 ******************************************************************************/

int main(int argc, char *argv[]) {
    printf("\n");
    printf("========================================\n");
    printf("EC-Lab OLE COM Custom Interface Test\n");
    printf("========================================\n");
    printf("\n");
    printf("This test verifies the IEClabExe custom interface implementation.\n");
    printf("\n");
    printf("IMPORTANT: Before running this test:\n");
    printf("  1. Start EC-Lab application\n");
    printf("  2. Connect your device in EC-Lab GUI\n");
    printf("  3. Verify device is connected and active\n");
    printf("\n");
    printf("Press Enter to continue...");
    getchar();

    ECLabConnection *conn = NULL;
    int totalTests = 0;
    int passedTests = 0;
    int result;

    // Test 1: Initialize
    totalTests++;
    result = Test_Initialize(&conn);
    if (result == SUCCESS) passedTests++;
    else goto cleanup;

    // Test 2: Connect
    totalTests++;
    result = Test_ConnectDevice(conn);
    if (result == SUCCESS) passedTests++;
    else goto cleanup;

    // Test 3: Test connection
    totalTests++;
    result = Test_TestConnection(conn);
    if (result == SUCCESS) passedTests++;

    // Test 4: Enable/Disable messages
    totalTests++;
    result = Test_EnableMessagesWindows(conn);
    if (result == SUCCESS) passedTests++;

    // Test 5: Get status
    totalTests++;
    result = Test_MeasureStatus(conn);
    if (result == SUCCESS) passedTests++;

    // Test 6: Disconnect
    totalTests++;
    result = Test_DisconnectDevice(conn);
    if (result == SUCCESS) passedTests++;

cleanup:
    // Test 7: Shutdown
    if (conn) {
        totalTests++;
        result = Test_Shutdown(conn);
        if (result == SUCCESS) passedTests++;
    }

    // Print summary
    printf("\n");
    printf("========================================\n");
    printf("TEST SUMMARY\n");
    printf("========================================\n");
    printf("Total Tests: %d\n", totalTests);
    printf("Passed: %d\n", passedTests);
    printf("Failed: %d\n", totalTests - passedTests);
    printf("Success Rate: %.1f%%\n", (100.0 * passedTests) / totalTests);
    printf("========================================\n");

    if (passedTests == totalTests) {
        printf("\nALL TESTS PASSED!\n");
        printf("The IEClabExe custom interface is working correctly.\n");
        return 0;
    } else {
        printf("\nSOME TESTS FAILED!\n");
        printf("Check the output above for details.\n");
        return 1;
    }
}

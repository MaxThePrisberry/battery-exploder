/******************************************************************************
 * safety_monitor.c
 *
 * Simplified Safety Monitor Module Implementation
 *
 * Monitors SCU pressure (cDAQ channel 0) at 2 Hz. If the voltage drops
 * below SAFETY_SCU_PRESSURE_MIN for SAFETY_DEBOUNCE_COUNT consecutive
 * readings, closes solenoid valves and invokes the registered experiment
 * stop callback.
 ******************************************************************************/

#include "safety_monitor.h"
#include "logging.h"
#include "cdaq_utils.h"
#include "ni9472/ni9472_queue.h"
#include <utility.h>

/******************************************************************************
 * Module State
 ******************************************************************************/

// Thread control
static CmtThreadLockHandle g_safetyLock = 0;
static int g_monitorThreadId = 0;
static volatile int g_stopRequested = 0;

// Module state
static int g_initialized = 0;
static SafetyMonitorStatus g_status = SAFETY_MONITOR_STOPPED;

// Registered experiment callback
static SafetyStopCallback g_onStop = NULL;
static void *g_userData = NULL;
static int g_experimentRegistered = 0;

// Debounce counter
static int g_scuBadCount = 0;

/******************************************************************************
 * Forward Declarations
 ******************************************************************************/

static int CVICALLBACK MonitorThreadFunc(void *functionData);

/******************************************************************************
 * Internal Helpers
 ******************************************************************************/

static int SetValves(int high)
{
    if (!ENABLE_NI9472) {
        return SUCCESS;
    }

    NI9472_QueueManager *mgr = NI9472_GetGlobalQueueManager();
    if (mgr == NULL || !NI9472_QueueIsRunning(mgr)) {
        return ERR_NOT_CONNECTED;
    }

    int state = high ? NI9472_CHANNEL_HIGH : NI9472_CHANNEL_LOW;
    int r1 = NI9472_SetChannelQueued(SAFETY_VALVE1_CHANNEL, state, DEVICE_PRIORITY_HIGH);
    int r2 = NI9472_SetChannelQueued(SAFETY_VALVE2_CHANNEL, state, DEVICE_PRIORITY_HIGH);

    if (r1 != SUCCESS || r2 != SUCCESS) {
        LogErrorEx(LOG_DEVICE_SAFETY, "Failed to set valves: V1=%d V2=%d", r1, r2);
        return ERR_OPERATION_FAILED;
    }

    return SUCCESS;
}

/******************************************************************************
 * Public API - Lifecycle
 ******************************************************************************/

int SafetyMonitor_Initialize(void)
{
    if (g_initialized) {
        LogWarningEx(LOG_DEVICE_SAFETY, "Safety monitor already initialized");
        return SUCCESS;
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Initializing safety monitor...");

    int result = CmtNewLock(NULL, 0, &g_safetyLock);
    if (result != 0) {
        LogErrorEx(LOG_DEVICE_SAFETY, "Failed to create thread lock: %d", result);
        return ERR_BASE_SAFETY - 1;
    }

    g_status = SAFETY_MONITOR_STOPPED;
    g_onStop = NULL;
    g_userData = NULL;
    g_experimentRegistered = 0;
    g_scuBadCount = 0;
    g_stopRequested = 0;
    g_initialized = 1;

    LogMessageEx(LOG_DEVICE_SAFETY, "Safety monitor initialized successfully");
    return SUCCESS;
}

int SafetyMonitor_Start(void)
{
    if (!g_initialized) {
        LogErrorEx(LOG_DEVICE_SAFETY, "Cannot start - not initialized");
        return ERR_NOT_INITIALIZED;
    }

    CmtGetLock(g_safetyLock);

    if (g_status == SAFETY_MONITOR_RUNNING) {
        CmtReleaseLock(g_safetyLock);
        LogWarningEx(LOG_DEVICE_SAFETY, "Monitor already running");
        return SUCCESS;
    }

    g_stopRequested = 0;
    g_scuBadCount = 0;

    int result = CmtScheduleThreadPoolFunction(DEFAULT_THREAD_POOL_HANDLE,
                                               MonitorThreadFunc,
                                               NULL, &g_monitorThreadId);
    if (result != 0) {
        CmtReleaseLock(g_safetyLock);
        LogErrorEx(LOG_DEVICE_SAFETY, "Failed to start monitor thread: %d", result);
        return ERR_THREAD_CREATE;
    }

    g_status = SAFETY_MONITOR_RUNNING;
    CmtReleaseLock(g_safetyLock);

    LogMessageEx(LOG_DEVICE_SAFETY, "Safety monitor started (rate: %d Hz)", SAFETY_MONITOR_RATE_HZ);
    return SUCCESS;
}

int SafetyMonitor_Stop(void)
{
    if (!g_initialized) {
        return SUCCESS;
    }

    if (g_status != SAFETY_MONITOR_RUNNING) {
        return SUCCESS;
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Stopping safety monitor...");

    g_stopRequested = 1;

    if (g_monitorThreadId != 0) {
        CmtWaitForThreadPoolFunctionCompletion(DEFAULT_THREAD_POOL_HANDLE,
                                               g_monitorThreadId,
                                               OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        g_monitorThreadId = 0;
    }

    if (g_safetyLock != 0) {
        CmtGetLock(g_safetyLock);
        g_status = SAFETY_MONITOR_STOPPED;
        CmtReleaseLock(g_safetyLock);
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Safety monitor stopped");
    return SUCCESS;
}

void SafetyMonitor_Cleanup(void)
{
    if (!g_initialized) {
        return;
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Cleaning up safety monitor...");

    SafetyMonitor_Stop();

    // Close valves to safe state
    SafetyMonitor_CloseValves();

    if (g_safetyLock != 0) {
        CmtDiscardLock(g_safetyLock);
        g_safetyLock = 0;
    }

    g_onStop = NULL;
    g_userData = NULL;
    g_experimentRegistered = 0;
    g_scuBadCount = 0;
    g_initialized = 0;

    LogMessageEx(LOG_DEVICE_SAFETY, "Safety monitor cleanup complete");
}

/******************************************************************************
 * Public API - Experiment Registration
 ******************************************************************************/

int SafetyMonitor_RegisterExperiment(SafetyStopCallback onStop, void *userData)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    CmtGetLock(g_safetyLock);

    if (g_experimentRegistered) {
        CmtReleaseLock(g_safetyLock);
        LogWarningEx(LOG_DEVICE_SAFETY, "Experiment already registered - unregister first");
        return ERR_INVALID_STATE;
    }

    g_onStop = onStop;
    g_userData = userData;
    g_experimentRegistered = 1;
    g_scuBadCount = 0;

    CmtReleaseLock(g_safetyLock);

    LogMessageEx(LOG_DEVICE_SAFETY, "Experiment registered with safety monitor");
    return SUCCESS;
}

int SafetyMonitor_UnregisterExperiment(void)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    CmtGetLock(g_safetyLock);

    g_onStop = NULL;
    g_userData = NULL;
    g_experimentRegistered = 0;

    CmtReleaseLock(g_safetyLock);

    LogMessageEx(LOG_DEVICE_SAFETY, "Experiment unregistered from safety monitor");
    return SUCCESS;
}

/******************************************************************************
 * Public API - Start Condition & Valve Control
 ******************************************************************************/

int SafetyMonitor_CheckStartCondition(double *scuVoltage)
{
    double voltage = 0.0;

    if (ENABLE_CDAQ) {
        int result = CDAQ_ReadVoltage(SAFETY_SCU_CHANNEL, &voltage);
        if (result != SUCCESS) {
            LogWarningEx(LOG_DEVICE_SAFETY, "Failed to read SCU pressure: %d", result);
            if (scuVoltage) *scuVoltage = 0.0;
            return 0;
        }
    } else {
        // cDAQ disabled - assume OK
        voltage = 5.0;
    }

    if (scuVoltage) {
        *scuVoltage = voltage;
    }

    int ok = (voltage >= SAFETY_SCU_PRESSURE_MIN);
    if (!ok) {
        LogWarningEx(LOG_DEVICE_SAFETY, "SCU pressure below threshold: %.2f V (min: %.2f V)",
                     voltage, SAFETY_SCU_PRESSURE_MIN);
    }

    return ok;
}

int SafetyMonitor_OpenValves(void)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Opening solenoid valves...");
    int result = SetValves(1);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_SAFETY, "Solenoid valves opened");
    }
    return result;
}

int SafetyMonitor_CloseValves(void)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Closing solenoid valves...");
    int result = SetValves(0);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_SAFETY, "Solenoid valves closed");
    }
    return result;
}

/******************************************************************************
 * Monitor Thread
 ******************************************************************************/

static int CVICALLBACK MonitorThreadFunc(void *functionData)
{
    LogMessageEx(LOG_DEVICE_SAFETY, "Monitor thread started");

    double checkInterval = 1.0 / SAFETY_MONITOR_RATE_HZ;
    double lastCheckTime = GetTimestamp();

    while (!g_stopRequested) {
        double now = GetTimestamp();

        if ((now - lastCheckTime) >= checkInterval) {
            lastCheckTime = now;

            // Read SCU pressure
            double voltage = 0.0;
            int readOK = 0;

            if (ENABLE_CDAQ) {
                int result = CDAQ_ReadVoltage(SAFETY_SCU_CHANNEL, &voltage);
                readOK = (result == SUCCESS);
                if (!readOK) {
                    LogWarningEx(LOG_DEVICE_SAFETY, "SCU pressure read failed: %d", result);
                }
            } else {
                voltage = 5.0;
                readOK = 1;
            }

            if (readOK) {
                CmtGetLock(g_safetyLock);

                if (voltage < SAFETY_SCU_PRESSURE_MIN) {
                    g_scuBadCount++;

                    if (g_scuBadCount >= SAFETY_DEBOUNCE_COUNT) {
                        LogErrorEx(LOG_DEVICE_SAFETY,
                                   "SCU PRESSURE VIOLATION: %.2f V < %.2f V (%d consecutive)",
                                   voltage, SAFETY_SCU_PRESSURE_MIN, g_scuBadCount);

                        // Close valves
                        SetValves(0);

                        // Notify experiment
                        if (g_experimentRegistered && g_onStop) {
                            char msg[256];
                            snprintf(msg, sizeof(msg),
                                     "SCU pressure below threshold: %.2f V (min: %.2f V)",
                                     voltage, SAFETY_SCU_PRESSURE_MIN);
                            g_onStop(msg, g_userData);
                        }

                        // Reset debounce after triggering to avoid repeated callbacks
                        g_scuBadCount = 0;
                    }
                } else {
                    if (g_scuBadCount > 0) {
                        LogDebugEx(LOG_DEVICE_SAFETY, "SCU pressure OK: %.2f V (cleared %d bad reads)",
                                   voltage, g_scuBadCount);
                    }
                    g_scuBadCount = 0;
                }

                CmtReleaseLock(g_safetyLock);
            }
        }

        Delay(0.05);  // 50ms sleep to avoid busy waiting
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Monitor thread exiting");
    return 0;
}

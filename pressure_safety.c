/******************************************************************************
 * pressure_safety.c
 *
 * Pressure Safety Monitoring Module Implementation
 * Monitors fume hood differential pressure for safe experiment conditions
 ******************************************************************************/

#include "pressure_safety.h"
#include "cdaq_utils.h"
#include "logging.h"
#include <toolbox.h>
#include <userint.h>

/******************************************************************************
 * Module State
 ******************************************************************************/
static struct {
    PressureSafetyState state;
    CmtThreadLockHandle stateLock;
    CmtThreadFunctionID monitorThreadId;
    volatile int shouldStop;

    // Callbacks
    VentilationLostCallback onVentLost;
    VentilationRestoredCallback onVentRestored;

    int initialized;
} g_pressureSafety = {0};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/
static int CVICALLBACK PressureMonitorThread(void *functionData);
static VentilationStatus CheckPressureThresholds(double voltage, VentilationStatus currentStatus);
static void TriggerVentilationLostAlarm(ExperimentPhase phase, double temp, double pressure);
static void TriggerVentilationRestoredNotification(double pressure);
static void CVICALLBACK DeferredAlarmPopup(void *data);
static void PlayAlarmSound(void);

/******************************************************************************
 * Public Function Implementation
 ******************************************************************************/

int PressureSafety_Initialize(void) {
    if (g_pressureSafety.initialized) {
        LogWarning("Pressure safety module already initialized");
        return SUCCESS;
    }

    LogMessage("Initializing pressure safety monitoring module...");

    // Create thread lock
    int result = CmtNewLock(NULL, 0, &g_pressureSafety.stateLock);
    if (result < 0) {
        LogError("Failed to create pressure safety state lock");
        return ERR_OPERATION_FAILED;
    }

    // Initialize state
    memset(&g_pressureSafety.state, 0, sizeof(PressureSafetyState));
    g_pressureSafety.state.ventStatus = VENTILATION_UNKNOWN;
    g_pressureSafety.state.phase = PHASE_SAFE;
    g_pressureSafety.shouldStop = 0;

    g_pressureSafety.initialized = 1;
    LogMessage("Pressure safety module initialized");

    return SUCCESS;
}

int PressureSafety_StartMonitoring(double initialTemp,
                                   VentilationLostCallback onVentLost,
                                   VentilationRestoredCallback onVentRestored) {
    if (!g_pressureSafety.initialized) {
        LogError("Pressure safety module not initialized");
        return ERR_NOT_INITIALIZED;
    }

    if (g_pressureSafety.state.monitoringEnabled) {
        LogWarning("Pressure monitoring already active");
        return SUCCESS;
    }

    LogMessage("Starting pressure safety monitoring...");
    LogMessage("  Initial temperature: %.1f °C", initialTemp);
    LogMessage("  Safe temperature threshold: %.1f °C", PRESSURE_SAFE_TEMP_THRESHOLD);
    LogMessage("  Pressure OK threshold: > %.2f V", PRESSURE_THRESHOLD_OK_MIN);
    LogMessage("  Pressure LOST threshold: < %.2f V", PRESSURE_THRESHOLD_LOST_MAX);

    // Set callbacks
    g_pressureSafety.onVentLost = onVentLost;
    g_pressureSafety.onVentRestored = onVentRestored;

    // Initialize state
    CmtGetLock(g_pressureSafety.stateLock);
    g_pressureSafety.state.currentTemperature = initialTemp;
    g_pressureSafety.state.phase = (initialTemp >= PRESSURE_SAFE_TEMP_THRESHOLD) ?
                                   PHASE_CRITICAL : PHASE_SAFE;
    g_pressureSafety.state.consecutiveBadReadings = 0;
    g_pressureSafety.state.alarmActive = 0;
    g_pressureSafety.state.monitoringEnabled = 1;
    g_pressureSafety.shouldStop = 0;
    CmtReleaseLock(g_pressureSafety.stateLock);

    LogMessage("  Starting phase: %s", PressureSafety_PhaseToString(g_pressureSafety.state.phase));

    // Start monitoring thread
    int result = CmtScheduleThreadPoolFunction(DEFAULT_THREAD_POOL_HANDLE,
                                               PressureMonitorThread,
                                               NULL,
                                               &g_pressureSafety.monitorThreadId);
    if (result < 0) {
        LogError("Failed to start pressure monitoring thread");
        g_pressureSafety.state.monitoringEnabled = 0;
        return ERR_OPERATION_FAILED;
    }

    LogMessage("Pressure safety monitoring started");
    return SUCCESS;
}

int PressureSafety_StopMonitoring(void) {
    if (!g_pressureSafety.initialized) {
        return ERR_NOT_INITIALIZED;
    }

    if (!g_pressureSafety.state.monitoringEnabled) {
        return SUCCESS;
    }

    LogMessage("Stopping pressure safety monitoring...");

    // Signal thread to stop
    g_pressureSafety.shouldStop = 1;

    // Wait for thread to finish (with timeout)
    int status;
    int result = CmtWaitForThreadPoolFunctionCompletion(DEFAULT_THREAD_POOL_HANDLE,
                                                        g_pressureSafety.monitorThreadId,
                                                        OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    if (result < 0) {
        LogWarning("Pressure monitoring thread did not stop cleanly");
    }

    CmtGetLock(g_pressureSafety.stateLock);
    g_pressureSafety.state.monitoringEnabled = 0;
    g_pressureSafety.state.alarmActive = 0;
    CmtReleaseLock(g_pressureSafety.stateLock);

    LogMessage("Pressure safety monitoring stopped");
    return SUCCESS;
}

int PressureSafety_UpdateTemperature(double temperature) {
    if (!g_pressureSafety.initialized) {
        return ERR_NOT_INITIALIZED;
    }

    CmtGetLock(g_pressureSafety.stateLock);

    ExperimentPhase oldPhase = g_pressureSafety.state.phase;
    g_pressureSafety.state.currentTemperature = temperature;

    // Update phase based on temperature
    if (temperature >= PRESSURE_SAFE_TEMP_THRESHOLD) {
        g_pressureSafety.state.phase = PHASE_CRITICAL;
    } else {
        g_pressureSafety.state.phase = PHASE_SAFE;
    }

    // Log phase transition
    if (oldPhase != g_pressureSafety.state.phase) {
        LogMessage("Experiment phase transition: %s -> %s (T = %.1f °C)",
                  PressureSafety_PhaseToString(oldPhase),
                  PressureSafety_PhaseToString(g_pressureSafety.state.phase),
                  temperature);

        if (g_pressureSafety.state.phase == PHASE_CRITICAL) {
            LogWarning("CRITICAL PHASE ENTERED - Thermal runaway may occur");
            LogWarning("If ventilation is lost, alarm will sound but experiment will continue");
        }
    }

    CmtReleaseLock(g_pressureSafety.stateLock);

    return SUCCESS;
}

int PressureSafety_GetStatus(VentilationStatus *status) {
    if (!g_pressureSafety.initialized || !status) {
        return ERR_INVALID_PARAMETER;
    }

    CmtGetLock(g_pressureSafety.stateLock);
    *status = g_pressureSafety.state.ventStatus;
    CmtReleaseLock(g_pressureSafety.stateLock);

    return SUCCESS;
}

int PressureSafety_GetState(PressureSafetyState *state) {
    if (!g_pressureSafety.initialized || !state) {
        return ERR_INVALID_PARAMETER;
    }

    CmtGetLock(g_pressureSafety.stateLock);
    memcpy(state, &g_pressureSafety.state, sizeof(PressureSafetyState));
    CmtReleaseLock(g_pressureSafety.stateLock);

    return SUCCESS;
}

int PressureSafety_CheckStartConditions(double *pressureVoltage) {
    if (!g_pressureSafety.initialized) {
        return 0;  // Not safe if not initialized
    }

    // Read current pressure
    double voltage;
    int result = CDAQ_ReadVoltage(0, &voltage);
    if (result != SUCCESS) {
        LogError("Failed to read pressure sensor for start condition check");
        if (pressureVoltage) *pressureVoltage = 0.0;
        return 0;  // Not safe if can't read
    }

    if (pressureVoltage) {
        *pressureVoltage = voltage;
    }

    // Check if ventilation is OK
    VentilationStatus status = CheckPressureThresholds(voltage, VENTILATION_UNKNOWN);

    if (status == VENTILATION_OK) {
        LogMessage("Pre-start check: Ventilation OK (%.2f V)", voltage);
        return 1;
    } else {
        LogError("Pre-start check FAILED: Ventilation not adequate (%.2f V)", voltage);
        return 0;
    }
}

int PressureSafety_AcknowledgeAlarm(void) {
    if (!g_pressureSafety.initialized) {
        return ERR_NOT_INITIALIZED;
    }

    CmtGetLock(g_pressureSafety.stateLock);
    if (g_pressureSafety.state.alarmActive) {
        LogMessage("Alarm acknowledged by user");
        g_pressureSafety.state.alarmActive = 0;
    }
    CmtReleaseLock(g_pressureSafety.stateLock);

    return SUCCESS;
}

void PressureSafety_Cleanup(void) {
    if (!g_pressureSafety.initialized) {
        return;
    }

    LogMessage("Cleaning up pressure safety module...");

    // Stop monitoring if active
    if (g_pressureSafety.state.monitoringEnabled) {
        PressureSafety_StopMonitoring();
    }

    // Release lock
    if (g_pressureSafety.stateLock) {
        CmtDiscardLock(g_pressureSafety.stateLock);
        g_pressureSafety.stateLock = 0;
    }

    g_pressureSafety.initialized = 0;
    LogMessage("Pressure safety module cleaned up");
}

const char* PressureSafety_StatusToString(VentilationStatus status) {
    switch (status) {
        case VENTILATION_UNKNOWN: return "UNKNOWN";
        case VENTILATION_OK: return "OK";
        case VENTILATION_DEGRADED: return "DEGRADED";
        case VENTILATION_LOST: return "LOST";
        default: return "INVALID";
    }
}

const char* PressureSafety_PhaseToString(ExperimentPhase phase) {
    switch (phase) {
        case PHASE_SAFE: return "SAFE";
        case PHASE_CRITICAL: return "CRITICAL";
        default: return "INVALID";
    }
}

/******************************************************************************
 * Internal Function Implementation
 ******************************************************************************/

static int CVICALLBACK PressureMonitorThread(void *functionData) {
    LogMessage("Pressure monitoring thread started");

    while (!g_pressureSafety.shouldStop) {
        // Read pressure voltage
        double voltage;
        int result = CDAQ_ReadVoltage(0, &voltage);

        if (result == SUCCESS) {
            CmtGetLock(g_pressureSafety.stateLock);

            g_pressureSafety.state.currentPressureVoltage = voltage;
            g_pressureSafety.state.lastCheckTime = Timer();

            // Check thresholds with hysteresis
            VentilationStatus oldStatus = g_pressureSafety.state.ventStatus;
            VentilationStatus newStatus = CheckPressureThresholds(voltage, oldStatus);

            // Handle status changes with debouncing
            if (newStatus == VENTILATION_LOST) {
                g_pressureSafety.state.consecutiveBadReadings++;

                if (g_pressureSafety.state.consecutiveBadReadings >= PRESSURE_ALARM_DEBOUNCE_COUNT) {
                    if (oldStatus != VENTILATION_LOST) {
                        g_pressureSafety.state.ventStatus = VENTILATION_LOST;

                        // Trigger alarm
                        ExperimentPhase phase = g_pressureSafety.state.phase;
                        double temp = g_pressureSafety.state.currentTemperature;

                        CmtReleaseLock(g_pressureSafety.stateLock);

                        LogError("VENTILATION LOST! Phase: %s, Temp: %.1f °C, Pressure: %.2f V",
                                PressureSafety_PhaseToString(phase), temp, voltage);

                        TriggerVentilationLostAlarm(phase, temp, voltage);

                        CmtGetLock(g_pressureSafety.stateLock);
                    }
                }
            } else if (newStatus == VENTILATION_OK) {
                g_pressureSafety.state.consecutiveBadReadings = 0;

                if (oldStatus == VENTILATION_LOST) {
                    g_pressureSafety.state.ventStatus = VENTILATION_OK;
                    g_pressureSafety.state.alarmActive = 0;

                    CmtReleaseLock(g_pressureSafety.stateLock);

                    LogMessage("Ventilation restored (%.2f V)", voltage);
                    TriggerVentilationRestoredNotification(voltage);

                    CmtGetLock(g_pressureSafety.stateLock);
                } else {
                    g_pressureSafety.state.ventStatus = newStatus;
                }
            } else {
                g_pressureSafety.state.ventStatus = newStatus;
            }

            CmtReleaseLock(g_pressureSafety.stateLock);
        }

        // Sleep for monitoring interval
        Delay(1.0 / PRESSURE_MONITOR_RATE_HZ);
    }

    LogMessage("Pressure monitoring thread stopped");
    return 0;
}

static VentilationStatus CheckPressureThresholds(double voltage, VentilationStatus currentStatus) {
    // Hysteresis logic:
    // - To go from OK/UNKNOWN to LOST: voltage must drop below LOST_MAX
    // - To go from LOST to OK: voltage must rise above OK_MIN
    // - DEGRADED state is in between

    if (voltage < PRESSURE_THRESHOLD_LOST_MAX) {
        return VENTILATION_LOST;
    } else if (voltage >= PRESSURE_THRESHOLD_OK_MIN) {
        return VENTILATION_OK;
    } else {
        // In hysteresis zone
        if (currentStatus == VENTILATION_LOST) {
            return VENTILATION_LOST;  // Stay in LOST until reaches OK_MIN
        } else {
            return VENTILATION_DEGRADED;  // Somewhere in between
        }
    }
}

static void TriggerVentilationLostAlarm(ExperimentPhase phase, double temp, double pressure) {
    // Call user callback if provided
    if (g_pressureSafety.onVentLost) {
        g_pressureSafety.onVentLost(phase, temp, pressure);
    }

    // Set alarm active
    CmtGetLock(g_pressureSafety.stateLock);
    g_pressureSafety.state.alarmActive = 1;
    CmtReleaseLock(g_pressureSafety.stateLock);

    // Different response based on phase
    if (phase == PHASE_SAFE) {
        LogError("SAFE PHASE: Experiment should be STOPPED");
    } else {
        LogError("CRITICAL PHASE: SOUND ALARM - Experiment will CONTINUE");

        // Sound alarm
        PlayAlarmSound();

        // Show popup dialog (deferred to UI thread)
        typedef struct {
            double temperature;
            double pressure;
        } AlarmData;

        AlarmData *data = malloc(sizeof(AlarmData));
        if (data) {
            data->temperature = temp;
            data->pressure = pressure;
            PostDeferredCall(DeferredAlarmPopup, data);
        }
    }
}

static void TriggerVentilationRestoredNotification(double pressure) {
    // Call user callback if provided
    if (g_pressureSafety.onVentRestored) {
        g_pressureSafety.onVentRestored(pressure);
    }
}

static void CVICALLBACK DeferredAlarmPopup(void *data) {
    typedef struct {
        double temperature;
        double pressure;
    } AlarmData;

    AlarmData *alarmData = (AlarmData *)data;

    char message[512];
    snprintf(message, sizeof(message),
             "***** VENTILATION FAILURE *****\n\n"
             "Fume hood ventilation has been lost!\n\n"
             "Current Status:\n"
             "  Temperature: %.1f °C (CRITICAL PHASE)\n"
             "  Pressure: %.2f V\n\n"
             "Action: Experiment continuing (controlled runaway)\n\n"
             "PERSONNEL SHOULD EVACUATE THE AREA",
             alarmData->temperature,
             alarmData->pressure);

    MessagePopup("CRITICAL ALARM - EVACUATE", message);

    free(data);
}

static void PlayAlarmSound(void) {
    // Play system beep multiple times
    for (int i = 0; i < PRESSURE_ALARM_SOUND_BEEPS; i++) {
        Beep();
        Delay(0.3);  // 300ms between beeps
    }
}

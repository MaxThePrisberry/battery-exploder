/***************************************************************************************
 * Example: Temperature Ramp Experiment with Pressure Safety Integration
 *
 * This file shows the code changes needed to integrate pressure safety monitoring
 * into the temperature ramp experiment (exp_temp_ramp.c)
 *
 * Search for "// PRESSURE SAFETY:" comments to see integration points
 ***************************************************************************************/

// PRESSURE SAFETY: Add include at top of file
#include "pressure_safety.h"

// PRESSURE SAFETY: Add to experiment state structure
typedef struct {
    // ... existing fields ...
    int pressureMonitoringActive;
    int ventilationLost;
    // ... existing fields ...
} TempRampExperimentState;

// PRESSURE SAFETY: Callback for ventilation loss events
static void OnVentilationLost(ExperimentPhase phase, double temp, double pressure) {
    LogError("═══════════════════════════════════════════════");
    LogError("VENTILATION LOST DURING TEMPERATURE RAMP");
    LogError("  Phase: %s", PressureSafety_PhaseToString(phase));
    LogError("  Temperature: %.1f °C", temp);
    LogError("  Pressure: %.2f V", pressure);
    LogError("═══════════════════════════════════════════════");

    if (phase == PHASE_SAFE) {
        // Safe phase - stop experiment
        LogError("SAFE PHASE: Stopping experiment immediately");
        g_tempRampState.shouldStop = 1;  // Your experiment stop flag
        g_tempRampState.ventilationLost = 1;
    } else {
        // Critical phase - alarm already sounded, continue
        LogError("CRITICAL PHASE: Experiment will continue (controlled runaway)");
        LogError("Alarm has been sounded - personnel should evacuate");
        g_tempRampState.ventilationLost = 1;
    }
}

// PRESSURE SAFETY: Optional callback for ventilation restored
static void OnVentilationRestored(double pressure) {
    LogMessage("Ventilation restored (%.2f V)", pressure);
    g_tempRampState.ventilationLost = 0;
}

// PRESSURE SAFETY: Pre-start validation function
static int ValidateStartConditions(void) {
    LogMessage("Validating experiment start conditions...");

    // Check ventilation
    double pressureVoltage;
    if (!PressureSafety_CheckStartConditions(&pressureVoltage)) {
        LogError("Start validation FAILED: Ventilation not adequate (%.2f V)", pressureVoltage);

        // Show error dialog to user
        char msg[512];
        snprintf(msg, sizeof(msg),
                "Cannot start experiment:\n\n"
                "Fume hood ventilation is not adequate.\n"
                "Current pressure: %.2f V\n"
                "Required: > %.2f V\n\n"
                "Please ensure extraction system is running\n"
                "and operating properly before starting.",
                pressureVoltage,
                PRESSURE_THRESHOLD_OK_MIN);

        MessagePopup("Experiment Start Failed - Ventilation", msg);
        return 0;  // Not safe to start
    }

    LogMessage("  Ventilation: OK (%.2f V)", pressureVoltage);

    // ... other checks (battery connected, etc.) ...

    LogMessage("All start conditions validated");
    return 1;  // Safe to start
}

// PRESSURE SAFETY: Modify your experiment start function
int StartTemperatureRampExperiment(double initialTemp, double finalTemp,
                                   double rampRate, /* ... other params ... */) {
    LogMessage("═══════════════════════════════════════════════");
    LogMessage("Starting Temperature Ramp Experiment");
    LogMessage("  Initial Temperature: %.1f °C", initialTemp);
    LogMessage("  Final Temperature: %.1f °C", finalTemp);
    LogMessage("  Ramp Rate: %.1f °C/min", rampRate);
    LogMessage("═══════════════════════════════════════════════");

    // PRESSURE SAFETY: Validate start conditions
    if (!ValidateStartConditions()) {
        LogError("Experiment start aborted - validation failed");
        return ERR_OPERATION_FAILED;
    }

    // Initialize experiment state
    memset(&g_tempRampState, 0, sizeof(g_tempRampState));
    g_tempRampState.shouldStop = 0;
    g_tempRampState.ventilationLost = 0;

    // PRESSURE SAFETY: Start pressure monitoring
    double currentBatteryTemp = GetCurrentBatteryTemperature();  // Your function
    int result = PressureSafety_StartMonitoring(currentBatteryTemp,
                                                OnVentilationLost,
                                                OnVentilationRestored);
    if (result != SUCCESS) {
        LogError("Failed to start pressure monitoring: %s", GetErrorString(result));
        MessagePopup("Error", "Failed to initialize pressure safety monitoring");
        return ERR_OPERATION_FAILED;
    }

    g_tempRampState.pressureMonitoringActive = 1;
    LogMessage("Pressure safety monitoring active");

    // ... rest of experiment initialization ...

    // Start experiment thread
    result = CmtScheduleThreadPoolFunction(DEFAULT_THREAD_POOL_HANDLE,
                                          TempRampExperimentThread,
                                          NULL,
                                          &g_tempRampState.threadId);

    return result;
}

// PRESSURE SAFETY: Modify your experiment loop
static int CVICALLBACK TempRampExperimentThread(void *functionData) {
    LogMessage("Temperature ramp experiment thread started");

    double currentTemp = GetCurrentBatteryTemperature();
    double targetTemp = g_tempRampState.initialTemp;

    while (!g_tempRampState.shouldStop) {
        // Update current temperature
        currentTemp = GetCurrentBatteryTemperature();

        // PRESSURE SAFETY: Update temperature for phase tracking
        if (g_tempRampState.pressureMonitoringActive) {
            PressureSafety_UpdateTemperature(currentTemp);
        }

        // PRESSURE SAFETY: Check if we should stop due to ventilation loss
        if (g_tempRampState.ventilationLost && currentTemp < PRESSURE_SAFE_TEMP_THRESHOLD) {
            LogError("Stopping experiment due to ventilation loss in safe phase");
            break;
        }

        // Set new temperature setpoint
        if (targetTemp < g_tempRampState.finalTemp) {
            targetTemp += g_tempRampState.rampRate * (UPDATE_INTERVAL_SEC / 60.0);
            if (targetTemp > g_tempRampState.finalTemp) {
                targetTemp = g_tempRampState.finalTemp;
            }

            // Update DTB setpoint
            DTB_SetSetpointQueued(DTB1_SLAVE_ADDRESS, targetTemp, DEVICE_PRIORITY_NORMAL);
        }

        // Perform EIS measurement if needed
        if (ShouldPerformEISMeasurement()) {
            PerformEISMeasurement();
        }

        // Log current state
        if (ShouldLogState()) {
            LogExperimentState(currentTemp, targetTemp);

            // PRESSURE SAFETY: Log safety state
            PressureSafetyState safetyState;
            if (PressureSafety_GetState(&safetyState) == SUCCESS) {
                LogMessage("  Safety: Phase=%s, Vent=%s, Pressure=%.2fV",
                          PressureSafety_PhaseToString(safetyState.phase),
                          PressureSafety_StatusToString(safetyState.ventStatus),
                          safetyState.currentPressureVoltage);
            }
        }

        // Sleep for update interval
        Delay(UPDATE_INTERVAL_SEC);
    }

    // PRESSURE SAFETY: Stop monitoring
    if (g_tempRampState.pressureMonitoringActive) {
        PressureSafety_StopMonitoring();
        g_tempRampState.pressureMonitoringActive = 0;
        LogMessage("Pressure safety monitoring stopped");
    }

    // Cleanup
    LogMessage("Temperature ramp experiment thread stopped");
    return 0;
}

// PRESSURE SAFETY: Emergency stop function (if you have one)
void EmergencyStopExperiment(void) {
    LogError("EMERGENCY STOP TRIGGERED");

    // Stop experiment
    g_tempRampState.shouldStop = 1;

    // Stop pressure monitoring
    if (g_tempRampState.pressureMonitoringActive) {
        PressureSafety_StopMonitoring();
    }

    // ... other emergency stop actions ...
}

/******************************************************************************
 * Optional: UI Integration for Real-Time Pressure Display
 ******************************************************************************/

// PRESSURE SAFETY: Add to your status update function (called at 1 Hz)
void UpdateExperimentStatusUI(void) {
    // ... existing status updates ...

    // Update pressure safety status
    PressureSafetyState safetyState;
    if (PressureSafety_GetState(&safetyState) == SUCCESS) {
        // Update pressure voltage display
        SetCtrlVal(panelHandle, PANEL_NUM_CH0_VOLTAGE, safetyState.currentPressureVoltage);

        // Update ventilation status LED
        // Assuming: 0=Red, 1=Green, 2=Yellow
        int ledValue;
        switch (safetyState.ventStatus) {
            case VENTILATION_OK:
                ledValue = 1;  // Green
                break;
            case VENTILATION_DEGRADED:
                ledValue = 2;  // Yellow
                break;
            case VENTILATION_LOST:
                ledValue = 0;  // Red
                break;
            default:
                ledValue = 2;  // Yellow for unknown
                break;
        }
        SetCtrlVal(panelHandle, PANEL_LED_VENTILATION, ledValue);

        // Update phase display
        const char *phaseStr = PressureSafety_PhaseToString(safetyState.phase);
        SetCtrlVal(panelHandle, PANEL_STR_PHASE, phaseStr);

        // Show alarm indicator if active
        SetCtrlVal(panelHandle, PANEL_LED_ALARM, safetyState.alarmActive);
    }
}

/******************************************************************************
 * Summary of Integration Steps:
 *
 * 1. Add #include "pressure_safety.h" at top of exp_temp_ramp.c
 * 2. Add pressure fields to experiment state structure
 * 3. Add OnVentilationLost callback function
 * 4. Add ValidateStartConditions function
 * 5. In experiment start: call ValidateStartConditions()
 * 6. In experiment start: call PressureSafety_StartMonitoring()
 * 7. In experiment loop: call PressureSafety_UpdateTemperature()
 * 8. In experiment loop: check ventilation loss flag
 * 9. In experiment cleanup: call PressureSafety_StopMonitoring()
 * 10. Optional: Update UI with safety status
 ******************************************************************************/

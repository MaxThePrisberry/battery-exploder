# Pressure Safety Monitoring System
## Integration Guide

## Overview

The pressure safety monitoring system provides critical safety interlocks for thermal runaway experiments by monitoring fume hood differential pressure and implementing phase-based safety responses.

## Safety Logic

### Two-Phase Safety Model

```
SAFE PHASE (Temperature < 80°C)
├─ Pre-start: Ventilation must be OK to start experiment
├─ During: Continuous pressure monitoring at 1 Hz
└─ If ventilation lost: STOP EXPERIMENT immediately

════════════════ 80°C Critical Threshold ════════════════

CRITICAL PHASE (Temperature ≥ 80°C)
├─ Batteries can ignite unpredictably
├─ During: Continuous pressure monitoring at 1 Hz
└─ If ventilation lost:
    • SOUND ALARM (evacuation warning)
    • SHOW POP-UP DIALOG
    • CONTINUE EXPERIMENT (controlled runaway safer than stopping)
    • LOG CRITICAL EVENT
```

## Configuration

All thresholds are configurable in `pressure_safety.h`:

```c
// Pressure thresholds (edit based on your sensor calibration)
#define PRESSURE_THRESHOLD_OK_MIN       2.0    // Volts for "ventilation OK"
#define PRESSURE_THRESHOLD_LOST_MAX     1.5    // Volts for "ventilation LOST"

// Temperature threshold
#define PRESSURE_SAFE_TEMP_THRESHOLD    80.0   // Degrees C

// Monitoring configuration
#define PRESSURE_MONITOR_RATE_HZ        1      // Check every 1 second
#define PRESSURE_ALARM_DEBOUNCE_COUNT   3      // 3 consecutive bad readings
#define PRESSURE_ALARM_SOUND_BEEPS      5      // Number of alarm beeps
```

### Hysteresis Behavior

```
Voltage Range:
    ┌──────────────────────────────────┐
    │  > 2.0V      VENTILATION_OK      │ ← Must reach here to clear alarm
    ├──────────────────────────────────┤
    │  1.5V-2.0V   DEGRADED/Hysteresis │ ← Prevents alarm flapping
    ├──────────────────────────────────┤
    │  < 1.5V      VENTILATION_LOST    │ ← Triggers alarm
    └──────────────────────────────────┘
```

## Hardware Requirements

- **NI 9202** in cDAQ slot 1, channel 0 configured for differential pressure sensor
- Differential pressure sensor with voltage output (e.g., 0-5V or 0-10V)
- Proper sensor wiring: AI+ and AI- connected

## Integration Example

### 1. Initialize in Main Application

```c
// In BatteryExploder.c main()
#include "pressure_safety.h"

int main() {
    // ... other initialization ...

    // Initialize pressure safety module
    if (ENABLE_CDAQ) {
        int result = PressureSafety_Initialize();
        if (result != SUCCESS) {
            LogError("Failed to initialize pressure safety: %s", GetErrorString(result));
        }
    }

    // ... rest of initialization ...
}
```

### 2. Add Cleanup

```c
// In PanelCallback (EVENT_CLOSE)
PressureSafety_Cleanup();
```

### 3. Integrate with Temperature Ramp Experiment

```c
// In exp_temp_ramp.c

// Callback for ventilation loss
static void OnVentilationLost(ExperimentPhase phase, double temp, double pressure) {
    if (phase == PHASE_SAFE) {
        // Stop experiment
        LogError("Stopping experiment due to ventilation loss");
        g_experimentShouldStop = 1;  // Set your stop flag
    } else {
        // Critical phase - just log, alarm already sounded
        LogError("Ventilation lost in CRITICAL phase - continuing");
    }
}

// In your experiment start function
int StartTemperatureRampExperiment(...) {
    // Check pre-start conditions
    double pressure;
    if (!PressureSafety_CheckStartConditions(&pressure)) {
        LogError("Cannot start: Ventilation not adequate (%.2f V)", pressure);
        MessagePopup("Experiment Start Failed",
                    "Fume hood ventilation is not adequate.\n"
                    "Please ensure extraction system is running.");
        return ERR_OPERATION_FAILED;
    }

    // Start pressure monitoring
    double initialTemp = GetCurrentBatteryTemperature();
    int result = PressureSafety_StartMonitoring(initialTemp,
                                                OnVentilationLost,
                                                NULL);  // Optional restore callback
    if (result != SUCCESS) {
        LogError("Failed to start pressure monitoring");
        return result;
    }

    // ... start experiment ...

    // In your experiment loop, update temperature
    while (experimentRunning) {
        double currentTemp = GetCurrentBatteryTemperature();
        PressureSafety_UpdateTemperature(currentTemp);

        // ... rest of experiment logic ...
    }

    // Stop monitoring when experiment ends
    PressureSafety_StopMonitoring();

    return SUCCESS;
}
```

### 4. Optional: Real-Time Status Display in UI

```c
// In status monitoring thread or experiment loop
PressureSafetyState safetyState;
if (PressureSafety_GetState(&safetyState) == SUCCESS) {
    // Update UI controls
    SetCtrlVal(panel, PANEL_PRESSURE_VOLTAGE, safetyState.currentPressureVoltage);

    // Update status LED
    switch (safetyState.ventStatus) {
        case VENTILATION_OK:
            SetCtrlVal(panel, PANEL_VENT_LED, 1);  // Green
            break;
        case VENTILATION_DEGRADED:
            SetCtrlVal(panel, PANEL_VENT_LED, 2);  // Yellow
            break;
        case VENTILATION_LOST:
            SetCtrlVal(panel, PANEL_VENT_LED, 0);  // Red
            break;
    }

    // Show phase
    SetCtrlVal(panel, PANEL_PHASE_STRING,
               PressureSafety_PhaseToString(safetyState.phase));
}
```

## API Reference

### Initialization

```c
int PressureSafety_Initialize(void);
void PressureSafety_Cleanup(void);
```

### Monitoring Control

```c
int PressureSafety_StartMonitoring(double initialTemp,
                                   VentilationLostCallback onVentLost,
                                   VentilationRestoredCallback onVentRestored);

int PressureSafety_StopMonitoring(void);

int PressureSafety_UpdateTemperature(double temperature);
```

### Status Queries

```c
int PressureSafety_CheckStartConditions(double *pressureVoltage);
int PressureSafety_GetStatus(VentilationStatus *status);
int PressureSafety_GetState(PressureSafetyState *state);
```

### Emergency Control

```c
int PressureSafety_AcknowledgeAlarm(void);  // Silence alarm (emergency only)
```

## Calibration Procedure

1. **Establish Baseline**:
   - Turn on fume hood extraction
   - Read voltage when ventilation is known to be good
   - Set `PRESSURE_THRESHOLD_OK_MIN` slightly below this value

2. **Establish Alarm Threshold**:
   - Turn off fume hood extraction
   - Read voltage when ventilation is known to be inadequate
   - Set `PRESSURE_THRESHOLD_LOST_MAX` slightly above this value

3. **Verify Hysteresis**:
   - Ensure `LOST_MAX < OK_MIN` for proper hysteresis
   - Typical gap: 0.3-0.5V to prevent oscillation

## Testing

### Test Scenarios

1. **Normal Operation**:
   - Start with good ventilation
   - Start experiment
   - Monitor should show VENTILATION_OK

2. **Pre-Start Check**:
   - Turn off ventilation
   - Try to start experiment
   - Should be blocked with error message

3. **Safe Phase Ventilation Loss**:
   - Start experiment at low temperature (< 80°C)
   - Turn off ventilation during experiment
   - Experiment should stop after debounce period (3 seconds)

4. **Critical Phase Ventilation Loss**:
   - Heat battery above 80°C
   - Turn off ventilation
   - Alarm should sound, popup should appear
   - Experiment should continue

5. **Ventilation Restoration**:
   - After ventilation loss, restore it
   - Alarm should clear when voltage rises above OK_MIN

## Safety Notes

⚠️ **CRITICAL**: This system is a safety interlock. Do not disable or bypass without proper authorization and alternative safety measures.

- Always verify sensor is working before experiments
- Test alarm system regularly
- Have evacuation procedures in place
- Ensure personnel are trained on alarm response
- Consider adding redundant pressure sensors
- Log all safety events for review

## Troubleshooting

### "Pre-start check FAILED" Message

- Check fume hood is running
- Verify sensor wiring (AI+ and AI-)
- Check sensor power supply
- Verify voltage readings with multimeter
- Adjust `PRESSURE_THRESHOLD_OK_MIN` if needed

### Alarm Triggering Unexpectedly

- Check for intermittent ventilation issues
- Increase `PRESSURE_ALARM_DEBOUNCE_COUNT` to reduce sensitivity
- Verify sensor is not vibrating or loose
- Check for electrical noise on sensor lines

### Alarm Not Triggering When Expected

- Verify temperature reporting to pressure safety module
- Check `PRESSURE_SAFE_TEMP_THRESHOLD` is correct
- Ensure `PressureSafety_UpdateTemperature()` is being called
- Check alarm thresholds are properly configured

## Future Enhancements

Potential improvements to consider:

1. **Redundant Sensors**: Monitor multiple pressure points
2. **Network Notifications**: Email/SMS alerts for critical events
3. **Data Logging**: Record all pressure/temperature data
4. **Predictive Monitoring**: Trend analysis for early warning
5. **Remote Shutdown**: Emergency stop from remote location
6. **Status Web Interface**: Monitor experiments remotely

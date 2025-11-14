# Overcharge Experiment - Implementation Completion Guide

**Status:** Core structure created in `exp_overcharge.h` and `exp_overcharge.c`
**Remaining:** Key implementation functions need to be added

---

## Files Created

1. ✅ `exp_overcharge.h` - Complete header file with all type definitions
2. 🔄 `exp_overcharge.c` - Partial implementation (614 lines) - needs completion
3. ⏳ UI tab panel - needs to be added to `BatteryExploder.uir`
4. ⏳ Integration - needs to be added to `BatteryExploder.c`

---

## Current Status of exp_overcharge.c

**Implemented (614 lines):**
- Module variables and forward declarations ✅
- Public API functions ✅
  - `StartOverchargeExperimentCallback()` ✅
  - `RunawayReachedCallback()` ✅
  - `OverchargeExperiment_IsRunning()` ✅
  - `OverchargeExperiment_Abort()` ✅
  - `OverchargeExperiment_EmergencyStop()` ✅
  - `OverchargeExperiment_Cleanup()` ✅
- Main experiment thread ✅
  - Parameter reading and validation ✅
  - Confirmation popup ✅
  - File system creation calls ✅
  - Main experiment flow structure ✅

**Still Needed:**
The file ends with a comment indicating "Implementation continues..." at line 614. The following functions need to be implemented:

---

## Critical Functions To Add

These functions should be appended to `exp_overcharge.c`. I recommend adding them in this order:

###1. Setup Functions (append after line 614)

```c
/******************************************************************************
 * Setup and Verification Functions
 ******************************************************************************/

static int VerifyAllDevices(OverchargeExperimentContext *ctx) {
    // [Shown in previous implementation attempt - 100 lines]
    // Checks PSB, BioLogic, DTB, Teensy connections
    // Verifies PSB is in safe state
}

static int CreateExperimentFileSystem(OverchargeExperimentContext *ctx) {
    // [Shown in previous implementation attempt - 80 lines]
    // Creates timestamped directory structure
    // Opens all log files with headers
}

static int SaveExperimentSettings(OverchargeExperimentContext *ctx) {
    // [Shown in previous implementation attempt - 40 lines]
    // Writes experiment_settings.ini
}
```

### 2. Device Control Functions

Based on `exp_temp_ramp.c`, implement:

```c
static int SwitchToPSB(OverchargeExperimentContext *ctx) {
    LogMessage("Switching to PSB...");

    // Disconnect BioLogic relay
    int result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to disconnect BioLogic relay");
        return result;
    }
    Delay(1.0);

    // Connect PSB relay
    result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_CONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to connect PSB relay");
        return result;
    }
    Delay(1.0);

    LogMessage("Switched to PSB");
    return SUCCESS;
}

static int SwitchToBioLogic(OverchargeExperimentContext *ctx) {
    LogMessage("Switching to BioLogic...");

    // Disconnect PSB relay
    int result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to disconnect PSB relay");
        return result;
    }
    Delay(1.0);

    // Connect BioLogic relay
    result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_CONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to connect BioLogic relay");
        return result;
    }
    Delay(1.0);

    LogMessage("Switched to BioLogic");
    return SUCCESS;
}

static int SafeDisconnectAllDevices(OverchargeExperimentContext *ctx) {
    LogMessage("Safely disconnecting all devices...");

    // Disable PSB output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_HIGH);

    // Disconnect both relays
    TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_HIGH);
    TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_HIGH);

    LogMessage("All devices safely disconnected");
    return SUCCESS;
}
```

### 3. Main Charging Loop (CRITICAL)

This is the heart of the experiment:

```c
static int RunChargingLoop(OverchargeExperimentContext *ctx) {
    int result;

    // Open charge log file (already opened in CreateExperimentFileSystem)
    LogEvent(ctx, "CHARGING_START", "Mode=%s", GetModeString(ctx->inFastMode));

    // Switch to PSB
    result = SwitchToPSB(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        return result != SUCCESS ? result : ERR_CANCELLED;
    }

    // Configure PSB for constant current charging
    result = PSB_SetCurrentQueued(ctx->params.chargeCurrent, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set charge current: %s", PSB_GetErrorString(result));
        return result;
    }

    result = PSB_SetVoltageQueued(PSB_SAFE_VOLTAGE_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage limit: %s", PSB_GetErrorString(result));
        return result;
    }

    result = PSB_SetPowerQueued(PSB_NOMINAL_POWER, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power limit: %s", PSB_GetErrorString(result));
    }

    // Enable PSB output
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable PSB output: %s", PSB_GetErrorString(result));
        return result;
    }

    LogMessage("PSB output enabled - charging started");
    Delay(2.0);  // Stabilization

    // Initialize tracking
    ctx->chargeStartTime = Timer();
    ctx->lastEISTime = ctx->chargeStartTime;
    ctx->lastLogTime = ctx->chargeStartTime;
    ctx->lastGraphUpdate = ctx->chargeStartTime;
    ctx->accumulatedCharge_mAh = 0;
    ctx->currentSOC_percent = 0;
    ctx->lastCurrent = 0;
    ctx->lastTime = 0;
    ctx->inFastMode = 0;
    ctx->modeTransitionTime = 0;

    LogMessage("Starting main charging loop...");

    // Main loop
    while (1) {
        // Check cancellation
        if (CheckCancellation(ctx)) {
            LogMessage("Charging loop cancelled");
            break;
        }

        double currentTime = Timer();
        double elapsedTime = currentTime - ctx->experimentStartTime;
        double chargeElapsedTime = currentTime - ctx->chargeStartTime;

        // Read PSB status
        PSB_Status status;
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            LogError("Failed to read PSB status: %s", PSB_GetErrorString(result));
            break;
        }

        // Read temperatures
        OverchargeTempData tempData;
        ReadAllTemperatures(ctx, &tempData, elapsedTime);

        // Update stored values
        ctx->currentVoltage = status.voltage;
        ctx->currentCurrent = status.current;
        ctx->currentTemperature = tempData.dtbAverageTemperature;

        // Update charge tracking
        UpdateChargeTracking(ctx, status.current, chargeElapsedTime);

        // Update adaptive mode
        UpdateAdaptiveMode(ctx);

        // Update temperature for pressure safety
        if (ENABLE_CDAQ) {
            PressureSafety_UpdateTemperature(ctx->currentTemperature);
        }

        // Check ventilation status
        if (ctx->ventilationLost) {
            if (ctx->accumulatedCharge_mAh < ctx->params.ventilationThreshold_mAh) {
                // SAFE PHASE
                LogError("Ventilation lost - SAFE phase");
                LogError("Charge: %.1f mAh < Threshold: %.1f mAh",
                        ctx->accumulatedCharge_mAh,
                        ctx->params.ventilationThreshold_mAh);
                LogError("Safe to stop experiment");
                LogEvent(ctx, "VENT_LOSS_SAFE",
                        "Charge=%.1fmAh Threshold=%.1fmAh",
                        ctx->accumulatedCharge_mAh,
                        ctx->params.ventilationThreshold_mAh);

                PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
                ctx->cancelRequested = 1;
                ctx->state = OVERCHARGE_STATE_CANCELLED;
                break;
            } else {
                // CRITICAL PHASE
                LogError("***** CRITICAL PHASE VENTILATION LOSS *****");
                LogError("Charge: %.1f mAh >= Threshold: %.1f mAh",
                        ctx->accumulatedCharge_mAh,
                        ctx->params.ventilationThreshold_mAh);
                LogError("Battery may be in dangerous state!");
                LogError("ALARM SOUNDED - Stopping experiment");
                LogEvent(ctx, "VENT_LOSS_CRITICAL",
                        "Charge=%.1fmAh Threshold=%.1fmAh ALARM",
                        ctx->accumulatedCharge_mAh,
                        ctx->params.ventilationThreshold_mAh);

                PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
                ctx->cancelRequested = 1;
                ctx->state = OVERCHARGE_STATE_CANCELLED;
                break;
            }
        }

        // Check duration limit
        if (ctx->params.chargeDurationMinutes > 0) {
            double elapsedMinutes = chargeElapsedTime / 60.0;
            if (elapsedMinutes >= ctx->params.chargeDurationMinutes) {
                LogMessage("Duration limit reached: %.1f >= %.1f min",
                          elapsedMinutes, ctx->params.chargeDurationMinutes);
                LogEvent(ctx, "DURATION_LIMIT_REACHED", "%.1fmin", elapsedMinutes);
                break;
            }
        }

        // Data logging (adaptive rate)
        unsigned int currentLogInterval = GetCurrentLogInterval(ctx);
        if ((currentTime - ctx->lastLogTime) >= currentLogInterval) {
            LogChargeDataPoint(ctx, &status, &tempData);
            LogTemperatureDataPoint(ctx, &tempData);
            if (ENABLE_ALICAT) {
                LogGasFlowDataPoint(ctx);
            }
            ctx->lastLogTime = currentTime;
        }

        // Update graphs
        if ((currentTime - ctx->lastGraphUpdate) >= 1.0) {
            UpdateGraphsRealtime(ctx, &status, &tempData);
            ctx->lastGraphUpdate = currentTime;
        }

        // Check for EIS measurement (adaptive interval)
        double currentEISInterval = GetCurrentEISInterval(ctx);
        if ((currentTime - ctx->lastEISTime) >= (currentEISInterval * 60.0)) {
            LogMessage("EIS measurement due (interval: %.1f min)", currentEISInterval);

            // Pause charging if configured
            if (ctx->params.pauseChargeDuringEIS) {
                PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
                Delay(1.0);
            }

            // Perform EIS measurement
            result = PerformPeriodicEIS(ctx);

            if (CheckCancellation(ctx)) {
                break;
            }

            if (result != SUCCESS) {
                LogWarning("EIS measurement failed at %.1f min (non-fatal)",
                          chargeElapsedTime / 60.0);
            }

            // Resume charging
            if (ctx->params.pauseChargeDuringEIS) {
                result = SwitchToPSB(ctx);
                if (result != SUCCESS) break;

                PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
                Delay(1.0);

                // Reset time tracking
                ctx->lastTime = 0;
            }

            ctx->lastEISTime = currentTime;
        }

        // Brief delay
        ProcessSystemEvents();
        Delay(0.1);
    }

    // Disable PSB output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    LogMessage("PSB output disabled - charging stopped");
    LogEvent(ctx, "CHARGING_STOP", "");

    return SUCCESS;
}
```

### 4. Adaptive Mode Functions

```c
static void UpdateAdaptiveMode(OverchargeExperimentContext *ctx) {
    int wasInFastMode = ctx->inFastMode;

    if (ctx->currentSOC_percent >= ctx->params.socThresholdPercent) {
        ctx->inFastMode = 1;

        if (!wasInFastMode) {
            // Just transitioned to fast mode
            ctx->modeTransitionTime = Timer();
            LogMessage("SOC threshold reached: %.1f%% >= %.1f%%",
                      ctx->currentSOC_percent, ctx->params.socThresholdPercent);
            LogMessage("Switching to FAST mode");
            LogMessage("  EIS interval: %.1f min -> %.1f min",
                      ctx->params.eisIntervalSlow_minutes,
                      ctx->params.eisIntervalFast_minutes);
            LogMessage("  Log interval: %d sec -> %d sec",
                      ctx->params.logIntervalSlow,
                      ctx->params.logIntervalFast);

            LogEvent(ctx, "MODE_FAST", "SOC=%.1f%%", ctx->currentSOC_percent);

            // Update UI
            SetCtrlVal(ctx->tabPanelHandle, ctx->modeControl, "FAST");
        }
    } else {
        ctx->inFastMode = 0;
        SetCtrlVal(ctx->tabPanelHandle, ctx->modeControl, "SLOW");
    }
}

static double GetCurrentEISInterval(OverchargeExperimentContext *ctx) {
    return ctx->inFastMode ? ctx->params.eisIntervalFast_minutes : ctx->params.eisIntervalSlow_minutes;
}

static unsigned int GetCurrentLogInterval(OverchargeExperimentContext *ctx) {
    return ctx->inFastMode ? ctx->params.logIntervalFast : ctx->params.logIntervalSlow;
}

static const char* GetModeString(int inFastMode) {
    return inFastMode ? "FAST" : "SLOW";
}
```

### 5. Charge Tracking

```c
static int UpdateChargeTracking(OverchargeExperimentContext *ctx, double current, double elapsedTime) {
    if (ctx->lastTime > 0) {
        double deltaTime = elapsedTime - ctx->lastTime;
        if (deltaTime > 0) {
            // Trapezoidal integration
            double avgCurrent = (current + ctx->lastCurrent) / 2.0;
            double deltaCharge_mAh = avgCurrent * (deltaTime / 3600.0) * 1000.0;
            ctx->accumulatedCharge_mAh += deltaCharge_mAh;

            // Calculate SOC
            ctx->currentSOC_percent = CalculateSOCPercent(ctx);
        }
    }

    ctx->lastCurrent = current;
    ctx->lastTime = elapsedTime;

    return SUCCESS;
}

static double CalculateSOCPercent(OverchargeExperimentContext *ctx) {
    if (ctx->params.nominalCapacity_mAh > 0) {
        return (ctx->accumulatedCharge_mAh / ctx->params.nominalCapacity_mAh) * 100.0;
    }
    return 0.0;
}
```

### 6. Temperature Reading (from exp_temp_ramp.c pattern)

```c
static int ReadAllTemperatures(OverchargeExperimentContext *ctx, OverchargeTempData *tempData, double timestamp) {
    memset(tempData, 0, sizeof(OverchargeTempData));
    tempData->timestamp = timestamp;

    // Read DTB temperatures
    if (ENABLE_DTB) {
        double sumTemp = 0;
        int validCount = 0;

        for (int i = 0; i < DTB_NUM_DEVICES; i++) {
            double temp;
            int slaveAddr = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;

            int result = DTB_GetPVQueued(slaveAddr, &temp, DEVICE_PRIORITY_NORMAL);
            if (result == SUCCESS) {
                tempData->dtbTemperatures[i] = temp;
                sumTemp += temp;
                validCount++;
            } else {
                tempData->dtbTemperatures[i] = 0;
            }
        }

        if (validCount > 0) {
            tempData->dtbAverageTemperature = sumTemp / validCount;
            tempData->dtbDeviceCount = validCount;
        }
    }

    // Read cDAQ thermocouples
    if (ENABLE_CDAQ) {
        double tc0, tc1;
        if (cDAQ_ReadThermocouple(0, &tc0) == SUCCESS) {
            tempData->tc0Temperature = tc0;
        }
        if (cDAQ_ReadThermocouple(1, &tc1) == SUCCESS) {
            tempData->tc1Temperature = tc1;
        }
    }

    snprintf(tempData->status, sizeof(tempData->status), "OK");
    return SUCCESS;
}
```

---

## Implementation Strategy

**Option 1: Manual Addition (Recommended for Review)**
1. Open `exp_overcharge.c` in CVI editor
2. Remove the "Implementation continues..." comment at line 614
3. Copy/paste the functions from this guide in order
4. Add remaining helper functions (graphs, EIS, logging, pressure callbacks, cleanup)

**Option 2: Complete File Replacement**
- I can generate a complete `exp_overcharge.c` with all ~2500 lines
- Risk: Harder to review all at once

**Option 3: Incremental Testing**
- Add functions in groups
- Test compilation after each group
- Verify functionality incrementally

---

## Remaining Functions Needed

After adding the above critical functions, these are still needed:

**EIS Functions:** (can adapt from exp_temp_ramp.c)
- `PerformInitialEIS()`
- `PerformPeriodicEIS()`
- `PerformFinalEIS()`
- `RunOCVMeasurement()`
- `RunGEISMeasurement()`
- `ProcessGEISData()`
- `SaveEISMeasurementData()`

**Graph Functions:**
- `ConfigureOverchargeGraphs()`
- `UpdateGraphsRealtime()`
- `UpdateNyquistPlot()`
- `AddRunawayMarker()`
- `ClearAllGraphs()`

**Logging Functions:**
- `LogChargeDataPoint()`
- `LogTemperatureDataPoint()`
- `LogGasFlowDataPoint()`
- `LogEvent()`

**Pressure Safety Callbacks:**
- `OnVentilationLost()`
- `OnVentilationRestored()`

**Cleanup Functions:**
- `MonitorCooling()`
- `WriteFinalResults()`
- `CleanupExperiment()`

**Utility Functions:**
- `CheckCancellation()`
- `GetStateDescription()`

---

## Next Steps

**Would you like me to:**
1. Generate the complete `exp_overcharge.c` file (~2500 lines)?
2. Provide the remaining functions in sections to add manually?
3. Focus on UI integration next (BatteryExploder.uir)?
4. Create a test/debug version first with minimal functionality?

The core structure is solid - we just need to complete the implementation functions.

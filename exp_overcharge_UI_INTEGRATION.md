# Overcharge Experiment - UI Integration Instructions

This document provides step-by-step instructions for integrating the Overcharge Thermal Runaway experiment module into the Battery Exploder application.

## Overview

The overcharge experiment requires:
1. **New UI Tab Panel** in BatteryExploder.uir with ~25 controls
2. **Code Integration** in BatteryExploder.c
3. **Control ID Definitions** in BatteryExploder.h

## Part 1: UI Panel Design (BatteryExploder.uir)

### Step 1: Open BatteryExploder.uir in LabWindows/CVI 2020

1. Launch LabWindows/CVI 2020
2. Open the project: `BatteryExploder.prj`
3. Open the UI file: `BatteryExploder.uir`

### Step 2: Create New Tab Page

1. Select the **Tab Control** on the main panel (PANEL)
2. Right-click → **Edit Tab Pages**
3. Add new tab page: **"Overcharge Runaway"**
4. Position it after the existing experiment tabs

### Step 3: Add UI Controls to "Overcharge Runaway" Tab

Create the following controls in this layout:

```
┌─────────────────────────────────────────────────────────────────┐
│ OVERCHARGE THERMAL RUNAWAY EXPERIMENT                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│ [Battery Configuration]                                         │
│   Nominal Capacity (mAh): [    3000    ]                       │
│                                                                 │
│ [Charging Parameters]                                           │
│   Charge Current (A):     [     4.5    ]                       │
│   Charge Duration (min):  [    120     ] (0 = unlimited)       │
│                                                                 │
│ [Safety Threshold]                                              │
│   Ventilation Safe Threshold (%): [    20     ]                │
│   = [    600    ] mAh (calculated)                             │
│                                                                 │
│ [Adaptive Mode Configuration]                                   │
│   SOC Threshold for Fast Mode (%): [   100    ]                │
│                                                                 │
│ [EIS Measurements]                                              │
│   Slow EIS Interval (min): [    20     ] (SOC < threshold)     │
│   Fast EIS Interval (min): [     5     ] (SOC >= threshold)    │
│   [✓] Pause charging during EIS                                │
│                                                                 │
│ [Data Logging]                                                  │
│   Slow Log Interval (sec): [    10     ] (SOC < threshold)     │
│   Fast Log Interval (sec): [     2     ] (SOC >= threshold)    │
│                                                                 │
│ [Controls]                                                      │
│   [ Start Overcharge ]  [ Stop ]  [🔥 Runaway Reached ]        │
│                                                                 │
│ [Status Display]                                                │
│   Status: [ Idle                                            ]  │
│   Mode:   [ SLOW                                            ]  │
│                                                                 │
│ [Real-time Data]                                                │
│   Charge Delivered:  [    0.0 mAh (  0.0%)                 ]  │
│   Elapsed Time:      [    0.0 min                          ]  │
│   Voltage:           [    0.00 V                           ]  │
│   Current:           [    0.00 A                           ]  │
│   Temperature:       [    0.0 °C                           ]  │
│   Ventilation:       [ OK                                   ]  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Step 4: Control Specifications

Create each control with the following properties:

#### Input Controls (Numeric)

| Control ID | Label | Type | Range | Default | Decimal Places |
|------------|-------|------|-------|---------|----------------|
| `OVERCHARGE_NUM_NOMINAL_CAPACITY` | "Nominal Capacity (mAh)" | Numeric | 100-20000 | 3000 | 0 |
| `OVERCHARGE_NUM_CHARGE_CURRENT` | "Charge Current (A)" | Numeric | 0.1-60.0 | 4.0 | 2 |
| `OVERCHARGE_NUM_CHARGE_DURATION` | "Charge Duration (min)" | Numeric | 0-480 | 120 | 0 |
| `OVERCHARGE_NUM_VENT_THRESHOLD_PCT` | "Ventilation Safe Threshold (%)" | Numeric | 1-100 | 20 | 1 |
| `OVERCHARGE_NUM_VENT_THRESHOLD_MAH` | "= ___ mAh (calculated)" | Numeric | Read-only | 600 | 0 |
| `OVERCHARGE_NUM_SOC_THRESHOLD` | "SOC Threshold for Fast Mode (%)" | Numeric | 50-200 | 100 | 1 |
| `OVERCHARGE_NUM_EIS_INTERVAL_SLOW` | "Slow EIS Interval (min)" | Numeric | 1-120 | 20 | 1 |
| `OVERCHARGE_NUM_EIS_INTERVAL_FAST` | "Fast EIS Interval (min)" | Numeric | 1-120 | 5 | 1 |
| `OVERCHARGE_NUM_LOG_INTERVAL_SLOW` | "Slow Log Interval (sec)" | Numeric | 1-300 | 10 | 0 |
| `OVERCHARGE_NUM_LOG_INTERVAL_FAST` | "Fast Log Interval (sec)" | Numeric | 1-300 | 2 | 0 |

#### Checkbox Control

| Control ID | Label | Default |
|------------|-------|---------|
| `OVERCHARGE_CHK_PAUSE_DURING_EIS` | "Pause charging during EIS" | Checked |

#### Button Controls

| Control ID | Label | Callback Function |
|------------|-------|-------------------|
| `OVERCHARGE_BTN_START` | "Start Overcharge" | `StartOverchargeExperimentCallback` |
| `OVERCHARGE_BTN_RUNAWAY_REACHED` | "🔥 Runaway Reached" | `RunawayReachedCallback` |

**Button Properties:**
- `OVERCHARGE_BTN_START`: Initially enabled, changes to "Stop" when running
- `OVERCHARGE_BTN_RUNAWAY_REACHED`: Initially dimmed, enabled during experiment

#### String Display Controls (Read-Only Text Boxes)

| Control ID | Label | Initial Value |
|------------|-------|---------------|
| `OVERCHARGE_STR_STATUS` | "Status:" | "Idle" |
| `OVERCHARGE_STR_MODE` | "Mode:" | "SLOW" |
| `OVERCHARGE_STR_CHARGE_DELIVERED` | "Charge Delivered:" | "0.0 mAh (0.0%)" |
| `OVERCHARGE_STR_ELAPSED_TIME` | "Elapsed Time:" | "0.0 min" |
| `OVERCHARGE_STR_VOLTAGE` | "Voltage:" | "0.00 V" |
| `OVERCHARGE_STR_CURRENT` | "Current:" | "0.00 A" |
| `OVERCHARGE_STR_TEMPERATURE` | "Temperature:" | "0.0 °C" |
| `OVERCHARGE_STR_VENTILATION` | "Ventilation:" | "OK" |

### Step 5: Add Callback for Calculated Field

The ventilation threshold in mAh should auto-calculate when capacity or percentage changes. Add a callback:

**Control:** `OVERCHARGE_NUM_NOMINAL_CAPACITY` and `OVERCHARGE_NUM_VENT_THRESHOLD_PCT`
**Event:** `EVENT_VAL_CHANGED`
**Callback:** `UpdateVentilationThresholdCallback`

```c
// Add to BatteryExploder.c
int CVICALLBACK UpdateVentilationThresholdCallback(int panel, int control, int event,
                                                    void *callbackData,
                                                    int eventData1, int eventData2) {
    if (event != EVENT_VAL_CHANGED) return 0;

    double capacity, percent;
    GetCtrlVal(panel, OVERCHARGE_NUM_NOMINAL_CAPACITY, &capacity);
    GetCtrlVal(panel, OVERCHARGE_NUM_VENT_THRESHOLD_PCT, &percent);

    double threshold_mAh = capacity * (percent / 100.0);
    SetCtrlVal(panel, OVERCHARGE_NUM_VENT_THRESHOLD_MAH, threshold_mAh);

    return 0;
}
```

## Part 2: BatteryExploder.h Integration

### Step 6: Add Control ID Definitions

After the UI file is saved, LabWindows/CVI will auto-generate control IDs. Add these to `BatteryExploder.h`:

```c
// Overcharge Experiment Tab Controls
#define OVERCHARGE_NUM_NOMINAL_CAPACITY    <auto-generated>
#define OVERCHARGE_NUM_CHARGE_CURRENT      <auto-generated>
#define OVERCHARGE_NUM_CHARGE_DURATION     <auto-generated>
#define OVERCHARGE_NUM_VENT_THRESHOLD_PCT  <auto-generated>
#define OVERCHARGE_NUM_VENT_THRESHOLD_MAH  <auto-generated>
#define OVERCHARGE_NUM_SOC_THRESHOLD       <auto-generated>
#define OVERCHARGE_NUM_EIS_INTERVAL_SLOW   <auto-generated>
#define OVERCHARGE_NUM_EIS_INTERVAL_FAST   <auto-generated>
#define OVERCHARGE_NUM_LOG_INTERVAL_SLOW   <auto-generated>
#define OVERCHARGE_NUM_LOG_INTERVAL_FAST   <auto-generated>
#define OVERCHARGE_CHK_PAUSE_DURING_EIS    <auto-generated>
#define OVERCHARGE_BTN_START               <auto-generated>
#define OVERCHARGE_BTN_RUNAWAY_REACHED     <auto-generated>
#define OVERCHARGE_STR_STATUS              <auto-generated>
#define OVERCHARGE_STR_MODE                <auto-generated>
#define OVERCHARGE_STR_CHARGE_DELIVERED    <auto-generated>
#define OVERCHARGE_STR_ELAPSED_TIME        <auto-generated>
#define OVERCHARGE_STR_VOLTAGE             <auto-generated>
#define OVERCHARGE_STR_CURRENT             <auto-generated>
#define OVERCHARGE_STR_TEMPERATURE         <auto-generated>
#define OVERCHARGE_STR_VENTILATION         <auto-generated>
```

**Note:** The actual numeric values will be auto-generated by CVI when you save the .uir file. Use the Resource Editor to view them.

## Part 3: BatteryExploder.c Integration

### Step 7: Add Include Statement

At the top of `BatteryExploder.c`, add:

```c
#include "exp_overcharge.h"
```

### Step 8: Add Module Cleanup

In the `PanelCallback()` function (or wherever cleanup is performed), add:

```c
int CVICALLBACK PanelCallback(int panel, int event, void *callbackData,
                              int eventData1, int eventData2) {
    switch (event) {
        case EVENT_CLOSE:
            // ... existing cleanup code ...

            // Add overcharge experiment cleanup
            OverchargeExperiment_Cleanup();

            // ... rest of cleanup ...
            break;
    }
    return 0;
}
```

### Step 9: Add Emergency Stop Handling

If there's an emergency stop function, add:

```c
void EmergencyStopAll(void) {
    // ... existing emergency stop code ...

    // Add overcharge experiment emergency stop
    OverchargeExperiment_EmergencyStop();

    // ... rest of emergency stop ...
}
```

### Step 10: Link Callbacks in UI File

In LabWindows/CVI UI Editor:

1. Select `OVERCHARGE_BTN_START` button
2. Set callback function: `StartOverchargeExperimentCallback`
3. Select `OVERCHARGE_BTN_RUNAWAY_REACHED` button
4. Set callback function: `RunawayReachedCallback`

These functions are already defined in `exp_overcharge.c` and will be linked automatically.

## Part 4: Project Configuration

### Step 11: Add Files to Project

In LabWindows/CVI Project:

1. Right-click on project → **Add Files to Project**
2. Add `exp_overcharge.c`
3. Add `exp_overcharge.h`

The project will automatically detect dependencies.

### Step 12: Verify Include Paths

Ensure the project include paths contain:
- Project root directory (for `exp_overcharge.h`)
- All device module directories (already configured)

## Part 5: Build and Test

### Step 13: Initial Compilation Test

1. **Build → Compile** or press `Ctrl+K`
2. Check for compilation errors
3. Common issues to watch for:
   - Missing control ID definitions (regenerate from .uir)
   - Include path issues (check project settings)
   - Function prototype mismatches (verify exp_overcharge.h)

### Step 14: Address Common Compilation Issues

**If you get "undefined control ID" errors:**
- Save BatteryExploder.uir
- Use **Edit → Create UI Control Constants File** to regenerate BatteryExploder.h
- Verify all `OVERCHARGE_*` constants are present

**If you get "unresolved external" errors:**
- Verify exp_overcharge.c is in the project
- Check that all device queue managers are initialized in main()

**If you get callback errors:**
- Verify callback function signatures match CVI requirements
- Check that callbacks are linked in .uir file

### Step 15: Runtime Testing (Without Hardware)

1. **Build → Build** or press `F7`
2. **Build → Run** or press `Ctrl+F5`
3. Navigate to "Overcharge Runaway" tab
4. Test UI interactions:
   - Change nominal capacity → verify threshold mAh updates
   - Change threshold % → verify threshold mAh updates
   - Verify default values are correct
   - Verify "Runaway Reached" button is dimmed

### Step 16: Runtime Testing (With Hardware - CAUTION)

⚠️ **SAFETY WARNING:** Overcharge experiments are DANGEROUS. Follow all safety protocols.

**Pre-experiment checklist:**
1. ✅ Fume hood ventilation running and verified
2. ✅ Pressure sensor calibrated and reading correctly
3. ✅ Battery installed in blast chamber
4. ✅ All safety equipment in place (fire extinguisher, etc.)
5. ✅ Personnel trained on emergency procedures
6. ✅ Remote monitoring configured
7. ✅ Proper personal protective equipment worn

**Test sequence:**
1. Enter safe test parameters (low current, short duration)
2. Click "Start Overcharge"
3. Verify confirmation popup appears with correct parameters
4. Verify all device verification checks pass
5. Verify ventilation pre-check passes
6. Monitor initial EIS measurement
7. Monitor charging loop starts correctly
8. Test "Runaway Reached" button becomes enabled
9. Verify graphs update in real-time
10. Test "Stop" button to abort experiment
11. Verify cleanup completes successfully
12. Check data files are created correctly

## Part 6: Post-Integration Verification

### Step 17: Verify File System Creation

After a test run, verify these files/directories exist:

```
data/
└── exp_overcharge_YYYYMMDD_HHMMSS/
    ├── experiment_settings.ini
    ├── experiment.log
    ├── charge_data.csv
    ├── temperature_profile.csv
    ├── gas_flow_data.csv (if ALICAT enabled)
    ├── events.txt
    ├── summary.txt (after completion)
    └── eis_measurements/
        ├── eis_000_0.0mAh_0.0pct.csv
        ├── eis_001_XXX.XmAh_XX.Xpct.csv
        └── ...
```

### Step 18: Verify Data File Formats

Check that CSV files have correct headers:

**charge_data.csv:**
```csv
Time_s,Voltage_V,Current_A,Power_W,Charge_mAh,Charge_Percent,Temp_DTB_C,Temp_TC0_C,Temp_TC1_C,Mode
```

**temperature_profile.csv:**
```csv
Time_s,DTB1_C,DTB2_C,TC0_C,TC1_C,TC2_C,TC3_C,TC4_C,TC5_C,TC6_C,TC7_C,Avg_DTB_C,Avg_TC_C
```

### Step 19: Verify Graph Display

1. Graphs should update smoothly during experiment
2. Graph 1 (Voltage & Current) should show dual Y-axes
3. Graph 2 (Temperature & Charge) should show dual Y-axes
4. Graph 3 (Nyquist) should update after each EIS measurement
5. Runaway marker (red line) should appear when button pressed

## Troubleshooting

### UI Issues

**Problem:** Controls not responding
**Solution:** Verify callbacks are correctly linked in .uir file

**Problem:** Calculated field not updating
**Solution:** Check UpdateVentilationThresholdCallback is assigned to both controls

**Problem:** Graphs not visible
**Solution:** Verify PANEL_GRAPH_1, PANEL_GRAPH_2, PANEL_GRAPH_BIOLOGIC constants are correct

### Runtime Issues

**Problem:** "Device not connected" error
**Solution:** Ensure all required devices are connected and initialized before starting

**Problem:** "Ventilation not adequate" error
**Solution:** Check cDAQ pressure sensor is reading correctly (should be > 0.8V typically)

**Problem:** Experiment hangs during EIS
**Solution:** Check Bio-Logic firmware is loaded correctly, verify timeout settings

**Problem:** File write errors
**Solution:** Check disk space, verify permissions on data directory

### Integration Issues

**Problem:** Compilation errors about missing controls
**Solution:** Save .uir file and regenerate control constants

**Problem:** Linker errors about exp_overcharge functions
**Solution:** Ensure exp_overcharge.c is added to project, rebuild all

**Problem:** Module conflicts with other experiments
**Solution:** Verify only one experiment runs at a time (g_systemBusy flag)

## Additional Notes

### Control ID Constants

After saving the .uir file, LabWindows/CVI automatically generates numeric constants for each control. These will be different for each project. Use the **Resource Editor** to view the actual values.

### Thread Safety

The experiment runs in a background thread. All UI updates from the experiment thread use `PostDeferredCall()` or direct `SetCtrlVal()` calls, which are thread-safe in LabWindows/CVI.

### Memory Management

The experiment allocates memory for EIS measurements dynamically. This is freed in `CleanupExperiment()`. Monitor memory usage during long experiments with many EIS measurements.

### Pressure Safety Integration

The overcharge experiment integrates with the existing `pressure_safety.h/c` module. Ensure this module is properly configured and calibrated before running overcharge experiments.

## Appendix: Complete Callback Listings

### Required Callbacks in BatteryExploder.c

```c
// Main experiment control
int CVICALLBACK StartOverchargeExperimentCallback(int panel, int control, int event,
                                                   void *callbackData,
                                                   int eventData1, int eventData2);

// Runaway indication
int CVICALLBACK RunawayReachedCallback(int panel, int control, int event,
                                       void *callbackData,
                                       int eventData1, int eventData2);

// Calculated field update
int CVICALLBACK UpdateVentilationThresholdCallback(int panel, int control, int event,
                                                    void *callbackData,
                                                    int eventData1, int eventData2);
```

The first two are defined in `exp_overcharge.c`. Only `UpdateVentilationThresholdCallback` needs to be added to `BatteryExploder.c`.

---

**Document Version:** 1.0
**Last Updated:** 2024
**Author:** Generated for Battery Exploder Project

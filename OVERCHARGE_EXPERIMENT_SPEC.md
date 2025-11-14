# Overcharge Experiment Implementation Specification

**Project:** Battery Exploder
**Module:** `exp_overcharge.h` / `exp_overcharge.c`
**Purpose:** Thermal runaway induction via constant-current overcharge with periodic EIS measurements
**Date:** 2025-11-14

---

## 1. Overview

### 1.1 Experiment Objectives
- Intentionally overcharge lithium-ion batteries to induce thermal runaway
- Perform periodic electrochemical impedance spectroscopy (EIS) measurements during overcharge
- Monitor voltage, current, temperature, and gas flow throughout the process
- Provide comprehensive data logging for thermal runaway analysis
- Implement safety monitoring via fume hood ventilation (differential pressure sensor)

### 1.2 Key Features
- **Simple user control:** User specifies current and duration
- **Manual runaway detection:** User presses "Runaway Reached" button when observed
- **Time-based EIS measurements:** Periodic impedance measurements at fixed time intervals
- **Charge tracking:** Coulomb counting with optional % SOC display
- **Ventilation safety:** Automatic response to ventilation loss based on charge delivered
- **Real-time graphing:** Three dual-axis graphs showing critical parameters
- **Comprehensive logging:** All data saved with timestamps for post-analysis

### 1.3 Stop Conditions
1. **User presses "Stop"** - Cancel experiment
2. **User presses "Runaway Reached"** - Mark event, continue logging (does not stop)
3. **Duration exceeded** - User-specified max time reached
4. **Ventilation loss (any time)** - Always stops experiment
   - Before threshold: Stop immediately (safe phase, no alarm)
   - After threshold: Stop immediately + Sound alarm (critical phase, personnel warning)
5. **Emergency conditions** - Device failures, communication errors

**Note:** Ventilation loss *always* stops the experiment for safety. The alarm in the critical phase (after threshold) warns personnel that the battery may be in a dangerous state even though the experiment has stopped.

---

## 2. User Interface Design

### 2.1 UI Tab Panel: "Overcharge Runaway"

```
╔══════════════════════════════════════════════════════════════════╗
║  OVERCHARGE EXPERIMENT                                           ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                  ║
║  BATTERY CONFIGURATION                                           ║
║  ┌────────────────────────────────────────────────────────────┐ ║
║  │ Nominal Capacity (mAh):        [____3000____]             │ ║
║  │   (Optional - for % SOC display and threshold calculation)│ ║
║  └────────────────────────────────────────────────────────────┘ ║
║                                                                  ║
║  CHARGING PARAMETERS                                             ║
║  ┌────────────────────────────────────────────────────────────┐ ║
║  │ Charge Current (A):            [_____4.5____]             │ ║
║  │ Charge Duration (min):         [____120_____]             │ ║
║  │                                (0 = unlimited)             │ ║
║  └────────────────────────────────────────────────────────────┘ ║
║                                                                  ║
║  SAFETY THRESHOLD                                                ║
║  ┌────────────────────────────────────────────────────────────┐ ║
║  │ Ventilation Safe Threshold:    [_____20____] %            │ ║
║  │   = 600 mAh (calculated)                                  │ ║
║  │                                                            │ ║
║  │ Stop if ventilation lost before this charge level.        │ ║
║  │ After this threshold: too dangerous to stop → alarm only. │ ║
║  └────────────────────────────────────────────────────────────┘ ║
║                                                                  ║
║  EIS MEASUREMENTS                                                ║
║  ┌────────────────────────────────────────────────────────────┐ ║
║  │ SOC Threshold for Fast Mode:   [____100____] %            │ ║
║  │ Slow EIS Interval (min):       [_____20____]              │ ║
║  │ Fast EIS Interval (min):       [______5____]              │ ║
║  │ [✓] Pause charging during EIS                             │ ║
║  │                                                            │ ║
║  │ When SOC < threshold: Use slow interval                   │ ║
║  │ When SOC ≥ threshold: Use fast interval                   │ ║
║  └────────────────────────────────────────────────────────────┘ ║
║                                                                  ║
║  DATA LOGGING                                                    ║
║  ┌────────────────────────────────────────────────────────────┐ ║
║  │ Slow Log Interval (sec):       [_____10____]              │ ║
║  │ Fast Log Interval (sec):       [______2____]              │ ║
║  │                                                            │ ║
║  │ When SOC < threshold: Use slow interval                   │ ║
║  │ When SOC ≥ threshold: Use fast interval                   │ ║
║  └────────────────────────────────────────────────────────────┘ ║
║                                                                  ║
║  ──────────────────────────────────────────────────────────────  ║
║                                                                  ║
║  CONTROLS                                                        ║
║  [   Start Overcharge   ]  [ Stop ]  [ 🔥 Runaway Reached ]    ║
║                                                                  ║
║  ──────────────────────────────────────────────────────────────  ║
║                                                                  ║
║  STATUS                                                          ║
║  ┌────────────────────────────────────────────────────────────┐ ║
║  │ Status: Charging...                                        │ ║
║  │ Mode: SLOW  (SOC < 100% threshold)                        │ ║
║  │                                                            │ ║
║  │ Charge Delivered:     1250.5 mAh  (41.7%)                 │ ║
║  │ Time Elapsed:         16.5 min                            │ ║
║  │ Current Voltage:      4.15 V                              │ ║
║  │ Current Current:      4.48 A                              │ ║
║  │ Current Temperature:  45.2 °C                             │ ║
║  │ Last EIS:             10.0 min ago                        │ ║
║  │ Next EIS in:          9.5 min (slow: 20 min interval)     │ ║
║  │                                                            │ ║
║  │ Ventilation: OK ✓                                         │ ║
║  └────────────────────────────────────────────────────────────┘ ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
```

### 2.2 UI Control IDs (to be added to BatteryExploder.uir)

```c
// Tab page panel controls
#define OVERCHARGE_NUM_NOMINAL_CAPACITY       2   // numeric
#define OVERCHARGE_NUM_CHARGE_CURRENT         3   // numeric
#define OVERCHARGE_NUM_CHARGE_DURATION        4   // numeric
#define OVERCHARGE_NUM_VENT_THRESHOLD_PCT     5   // numeric
#define OVERCHARGE_NUM_VENT_THRESHOLD_MAH     6   // numeric (display only)
#define OVERCHARGE_NUM_SOC_THRESHOLD          7   // numeric - SOC threshold for fast mode
#define OVERCHARGE_NUM_EIS_INTERVAL_SLOW      8   // numeric - slow EIS interval
#define OVERCHARGE_NUM_EIS_INTERVAL_FAST      9   // numeric - fast EIS interval
#define OVERCHARGE_CHK_PAUSE_DURING_EIS       10  // checkbox
#define OVERCHARGE_NUM_LOG_INTERVAL_SLOW      11  // numeric - slow logging
#define OVERCHARGE_NUM_LOG_INTERVAL_FAST      12  // numeric - fast logging
#define OVERCHARGE_BTN_START                  13  // command button
#define OVERCHARGE_BTN_STOP                   14  // command button (dimmed initially)
#define OVERCHARGE_BTN_RUNAWAY_REACHED        15  // command button
#define OVERCHARGE_STR_STATUS                 16  // string
#define OVERCHARGE_STR_MODE                   17  // string (display) - "SLOW" or "FAST"
#define OVERCHARGE_NUM_CHARGE_DELIVERED       18  // numeric (display)
#define OVERCHARGE_NUM_CHARGE_PERCENT         19  // numeric (display)
#define OVERCHARGE_NUM_TIME_ELAPSED           20  // numeric (display)
#define OVERCHARGE_NUM_CURRENT_VOLTAGE        21  // numeric (display)
#define OVERCHARGE_NUM_CURRENT_CURRENT        22  // numeric (display)
#define OVERCHARGE_NUM_CURRENT_TEMP           23  // numeric (display)
#define OVERCHARGE_STR_LAST_EIS               24  // string (display)
#define OVERCHARGE_STR_VENTILATION            25  // string (display)
```

---

## 3. Graph Configuration

### 3.1 Three Dual-Axis Graphs

**Graph 1 (PANEL_GRAPH_1): Voltage + Current vs Time**
- **Left Y-axis:** Voltage (V) - Blue solid line
- **Right Y-axis:** Current (A) - Red solid line
- **X-axis:** Time (minutes)
- **Purpose:** Monitor charging behavior and detect anomalies

**Graph 2 (PANEL_GRAPH_2): Temperature + Charge Delivered vs Time**
- **Left Y-axis:** Temperature (°C) - Orange solid line
- **Right Y-axis:** Charge Delivered (mAh or %) - Green solid line
- **X-axis:** Time (minutes)
- **Purpose:** Track thermal response and overcharge progression

**Graph 3 (PANEL_GRAPH_BIOLOGIC): Nyquist Plot (Latest EIS)**
- **X-axis:** Z' Real Impedance (Ohms)
- **Y-axis:** -Z'' Imaginary Impedance (Ohms)
- **Purpose:** Show impedance evolution during overcharge
- **Updates:** After each EIS measurement

### 3.2 Graph Update Rate
- Real-time plots (Graphs 1 & 2): Update every 1 second
- Nyquist plot (Graph 3): Update after each EIS measurement

### 3.3 Event Markers
When user presses "Runaway Reached":
- Add vertical red line on Graphs 1 & 2 at current time
- Add annotation marker showing "Runaway Reached" event
- Log timestamp and current readings to events.txt

---

## 4. Experiment Workflow

### 4.1 Initialization Phase

```
1. User Interface Validation
   ├─ Read all UI parameters
   ├─ Validate inputs:
   │  ├─ Charge current > 0 and < PSB_SAFE_CURRENT_MAX
   │  ├─ Duration >= 0 (0 = unlimited)
   │  ├─ If nominal capacity provided: > 0
   │  ├─ Ventilation threshold: 0-100%
   │  ├─ EIS interval > 0
   │  └─ Log interval > 0
   └─ Calculate absolute ventilation threshold (mAh)

2. Device Verification
   ├─ PSB power supply connected and ready
   ├─ Bio-Logic SP-150e connected
   ├─ DTB temperature controllers connected
   ├─ Teensy relay controller connected
   ├─ cDAQ system connected (for thermocouples and pressure)
   └─ ALICAT mass flow controller connected (optional)

3. Ventilation Pre-Check (CRITICAL SAFETY)
   ├─ Call PressureSafety_CheckStartConditions()
   ├─ If ventilation inadequate:
   │  ├─ Show error popup
   │  └─ Abort experiment start
   └─ If OK: Continue to next step

4. User Confirmation Popup
   ├─ Display all parameters
   ├─ Show calculated ventilation threshold (mAh)
   ├─ Show safety warnings
   ├─ Get user confirmation
   └─ If confirmed: Continue

5. File System Setup
   ├─ Create timestamped experiment directory:
   │  └─ data/exp_overcharge_YYYYMMDD_HHMMSS/
   ├─ Create subdirectories:
   │  └─ eis_measurements/
   ├─ Open log files:
   │  ├─ experiment.log
   │  ├─ charge_data.csv
   │  ├─ temperature_profile.csv
   │  ├─ gas_flow_data.csv (if ALICAT enabled)
   │  └─ events.txt
   └─ Write experiment_settings.ini

6. Initialize Monitoring Systems
   ├─ Start pressure safety monitoring:
   │  └─ PressureSafety_StartMonitoring(initialTemp, OnVentLost, OnVentRestored)
   ├─ Configure graphs (3 dual-axis plots)
   └─ Initialize relay states (both OFF)

7. Set UI State
   ├─ Change "Start" button to "Stop"
   ├─ Enable "Runaway Reached" button
   ├─ Dim input controls
   └─ Update status display
```

### 4.2 Initial EIS Measurement

```
1. Switch to Bio-Logic
   ├─ Disable PSB output
   ├─ TNY_SetPin(TNY_PSB_PIN, DISCONNECTED)
   ├─ Delay 1 second
   ├─ TNY_SetPin(TNY_BIOLOGIC_PIN, CONNECTED)
   └─ Delay 1 second

2. Perform OCV Measurement
   ├─ Load OCV technique
   ├─ Set parameters (duration, sample rate)
   ├─ Run technique
   ├─ Wait for completion
   └─ Retrieve data

3. Perform GEIS Measurement
   ├─ Load GEIS technique
   ├─ Set parameters (freq range, amplitude)
   ├─ Run technique
   ├─ Wait for completion
   └─ Retrieve impedance data

4. Process and Save EIS Data
   ├─ Extract Z_real, Z_imag, frequencies
   ├─ Save to CSV file: eis_measurements/eis_001_t000.0min.csv
   ├─ Update Nyquist plot (Graph 3)
   └─ Log measurement details

5. Switch Back to PSB
   ├─ TNY_SetPin(TNY_BIOLOGIC_PIN, DISCONNECTED)
   ├─ Delay 1 second
   ├─ TNY_SetPin(TNY_PSB_PIN, CONNECTED)
   └─ Delay 1 second
```

### 4.3 Main Charging Loop

```
1. Configure PSB for Constant Current Charging
   ├─ PSB_SetCurrent(chargeCurrent)
   ├─ PSB_SetVoltage(PSB_SAFE_VOLTAGE_MAX)  // Upper safety limit
   ├─ PSB_SetPower(PSB_NOMINAL_POWER)
   └─ PSB_SetOutputEnable(1)

2. Initialize Tracking Variables
   ├─ experimentStartTime = Timer()
   ├─ chargeStartTime = Timer()
   ├─ lastEISTime = chargeStartTime
   ├─ lastLogTime = chargeStartTime
   ├─ lastGraphUpdate = chargeStartTime
   ├─ accumulatedCharge_mAh = 0
   ├─ lastCurrent = 0
   └─ lastTime = 0

3. Main Loop (continues until stop condition)
   ├─ Check cancellation flags
   │  ├─ User pressed "Stop"?
   │  ├─ Emergency stop?
   │  └─ System error?
   │
   ├─ Read PSB Status
   │  ├─ PSB_GetStatus(&status)
   │  ├─ voltage = status.voltage
   │  ├─ current = status.current
   │  └─ power = status.power
   │
   ├─ Read All Temperatures
   │  ├─ DTB temperatures (all devices)
   │  ├─ cDAQ thermocouples (32 channels)
   │  ├─ Calculate average temperature
   │  └─ Update PressureSafety_UpdateTemperature(avgTemp)
   │
   ├─ Read Gas Flow (if ALICAT enabled)
   │  └─ ALICAT_GetStatus(&flowData)
   │
   ├─ Update Charge Tracking (Coulomb Counting)
   │  ├─ deltaTime = currentTime - lastTime
   │  ├─ avgCurrent = (current + lastCurrent) / 2
   │  ├─ deltaCharge_mAh = avgCurrent * (deltaTime / 3600.0) * 1000.0
   │  ├─ accumulatedCharge_mAh += deltaCharge_mAh
   │  ├─ If nominalCapacity > 0:
   │  │  └─ currentSOC_percent = (accumulatedCharge_mAh / nominalCapacity_mAh) * 100
   │  ├─ lastCurrent = current
   │  └─ lastTime = currentTime
   │
   ├─ Update UI Display Fields
   │  ├─ Charge delivered (mAh and %)
   │  ├─ Time elapsed
   │  ├─ Current voltage
   │  ├─ Current current
   │  ├─ Current temperature
   │  └─ Time since last EIS
   │
   ├─ Check Ventilation Status
   │  ├─ PressureSafety_GetStatus(&ventStatus)
   │  ├─ If ventStatus == VENTILATION_LOST:
   │  │  ├─ If accumulatedCharge_mAh < ventilationThreshold_mAh:
   │  │  │  ├─ LogError("Ventilation lost - SAFE phase")
   │  │  │  ├─ LogError("Safe to stop (charge: %.1f < threshold: %.1f mAh)")
   │  │  │  ├─ PSB_SetOutputEnable(0)
   │  │  │  ├─ cancelRequested = 1
   │  │  │  └─ Break loop
   │  │  └─ Else: // Past threshold - CRITICAL phase
   │  │     ├─ LogError("Ventilation lost - CRITICAL phase")
   │  │     ├─ LogError("Charge: %.1f >= threshold: %.1f mAh", charge, threshold)
   │  │     ├─ LogError("Battery may be unsafe - ALARM SOUNDED")
   │  │     ├─ Sound alarm (system beep or via pressure safety module)
   │  │     ├─ PSB_SetOutputEnable(0)
   │  │     ├─ cancelRequested = 1
   │  │     └─ Break loop (STOP experiment even in critical phase)
   │  └─ Update ventilation status display
   │
   ├─ Check Duration Limit
   │  ├─ If chargeDuration > 0:
   │  │  └─ If elapsed_min >= chargeDuration:
   │  │     ├─ LogMessage("Duration limit reached")
   │  │     └─ Break loop
   │  └─ (If chargeDuration == 0, run indefinitely)
   │
   ├─ Check Adaptive Mode (SOC-based)
   │  ├─ Determine current mode based on SOC:
   │  │  ├─ If currentSOC >= socThresholdPercent:
   │  │  │  ├─ If not already in fast mode:
   │  │  │  │  ├─ LogMessage("SOC threshold reached: %.1f%% >= %.1f%%", SOC, threshold)
   │  │  │  │  ├─ LogMessage("Switching to FAST mode")
   │  │  │  │  ├─ LogMessage("  EIS interval: %.1f min -> %.1f min", slow, fast)
   │  │  │  │  ├─ LogMessage("  Log interval: %d sec -> %d sec", slow, fast)
   │  │  │  │  ├─ inFastMode = 1
   │  │  │  │  ├─ modeTransitionTime = currentTime
   │  │  │  │  └─ LogEvent(ctx, "MODE_FAST", "SOC=%.1f%%", currentSOC)
   │  │  │  └─ currentEISInterval = eisIntervalFast_minutes
   │  │  │     currentLogInterval = logIntervalFast
   │  │  └─ Else:
   │  │     └─ currentEISInterval = eisIntervalSlow_minutes
   │  │        currentLogInterval = logIntervalSlow
   │  ├─ Update mode display on UI
   │  └─
   │
   ├─ Data Logging (if interval reached)
   │  ├─ If (currentTime - lastLogTime) >= currentLogInterval:
   │  │  ├─ Write to charge_data.csv:
   │  │  │  └─ Time,V,I,P,mAh,%,Temp_DTB,Temp_TC0,Temp_TC1,...
   │  │  ├─ Write to temperature_profile.csv:
   │  │  │  └─ Time,DTB1,DTB2,...,TC0-TC31,Avg
   │  │  ├─ Write to gas_flow_data.csv (if enabled):
   │  │  │  └─ Time,Flow,Temp,Setpoint
   │  │  ├─ Flush all files
   │  │  └─ lastLogTime = currentTime
   │  └─
   │
   ├─ Update Graphs (if interval reached)
   │  ├─ If (currentTime - lastGraphUpdate) >= 1.0:
   │  │  ├─ elapsedMinutes = (currentTime - chargeStartTime) / 60.0
   │  │  ├─ Graph 1:
   │  │  │  ├─ PlotPoint(voltage, LEFT_YAXIS, BLUE)
   │  │  │  └─ PlotPoint(current, RIGHT_YAXIS, RED)
   │  │  ├─ Graph 2:
   │  │  │  ├─ PlotPoint(temperature, LEFT_YAXIS, ORANGE)
   │  │  │  └─ PlotPoint(charge_mAh or %, RIGHT_YAXIS, GREEN)
   │  │  └─ lastGraphUpdate = currentTime
   │  └─
   │
   ├─ Check for EIS Measurement (time-based, adaptive)
   │  ├─ If (currentTime - lastEISTime) >= (currentEISInterval * 60.0):
   │  │  ├─ LogMessage("EIS measurement due at %.1f min", elapsed_min)
   │  │  ├─
   │  │  ├─ If pauseChargeDuringEIS:
   │  │  │  └─ PSB_SetOutputEnable(0)
   │  │  ├─
   │  │  ├─ Switch to Bio-Logic
   │  │  ├─ Perform EIS Measurement
   │  │  │  ├─ Run OCV technique
   │  │  │  ├─ Run GEIS technique
   │  │  │  ├─ Process impedance data
   │  │  │  ├─ Save to eis_measurements/eis_NNN_tMMM.Mmin.csv
   │  │  │  └─ Update Nyquist plot (Graph 3)
   │  │  ├─ Switch back to PSB
   │  │  ├─
   │  │  ├─ If pauseChargeDuringEIS:
   │  │  │  ├─ PSB_SetOutputEnable(1)
   │  │  │  └─ Reset lastTime (for coulomb counting)
   │  │  ├─
   │  │  └─ lastEISTime = currentTime
   │  └─
   │
   ├─ Check "Runaway Reached" Button
   │  ├─ If user pressed button:
   │  │  ├─ LogMessage("***** USER INDICATED RUNAWAY REACHED *****")
   │  │  ├─ Record timestamp
   │  │  ├─ Log current readings to events.txt
   │  │  ├─ Add vertical red marker on graphs
   │  │  ├─ Update status display
   │  │  └─ Continue experiment (do not stop)
   │  └─
   │
   ├─ Process System Events
   │  └─ ProcessSystemEvents()
   │
   └─ Brief Delay
      └─ Delay(0.1)  // 100ms to prevent excessive CPU usage

4. Loop Exit
   └─ One of the stop conditions met
```

### 4.4 Shutdown and Cleanup

```
1. Disable PSB Output
   ├─ PSB_SetOutputEnable(0)
   └─ Verify output disabled

2. Final EIS Measurement (if safe and useful)
   ├─ If not cancelled AND ventilation OK:
   │  ├─ Switch to Bio-Logic
   │  ├─ Perform final EIS measurement
   │  ├─ Save data
   │  └─ Update Nyquist plot
   └─ Switch relays to safe state (both OFF)

3. Post-Experiment Cooling Monitoring
   ├─ Log message: "Continuing temperature monitoring for 5 minutes"
   ├─ For 5 minutes:
   │  ├─ Read temperatures every 10 seconds
   │  ├─ Log to temperature_profile.csv
   │  ├─ Update temperature graph
   │  └─ Check for cancellation (allow early exit)
   └─ Log message: "Cooling monitoring complete"

4. Stop Monitoring Systems
   ├─ PressureSafety_StopMonitoring()
   └─ Log final ventilation status

5. Write Final Results
   ├─ Create summary.txt with:
   │  ├─ Experiment parameters
   │  ├─ Total charge delivered (mAh and % if applicable)
   │  ├─ Total time elapsed
   │  ├─ Final voltage, current, temperature
   │  ├─ Number of EIS measurements completed
   │  ├─ Runaway reached? (Y/N and timestamp)
   │  ├─ Ventilation events (if any)
   │  ├─ Stop reason
   │  └─ Peak temperature reached
   └─ Close all log files

6. Update UI
   ├─ Change "Stop" button back to "Start"
   ├─ Disable "Runaway Reached" button
   ├─ Re-enable input controls
   ├─ Update status to final state:
   │  ├─ "Completed"
   │  ├─ "Cancelled"
   │  ├─ "Stopped - Ventilation Lost"
   │  └─ "Error"
   └─ Keep graphs visible for review

7. Release Resources
   ├─ Free EIS measurement data arrays
   ├─ Clear system busy flag
   ├─ Release thread pool function
   └─ Log: "Experiment cleanup complete"
```

---

## 5. Data Structures

### 5.1 Experiment Parameters

```c
typedef struct {
    // Battery configuration
    double nominalCapacity_mAh;     // Optional, 0 if not provided

    // Charging parameters
    double chargeCurrent;           // Constant current (A)
    double chargeDurationMinutes;   // Max duration (0 = unlimited)

    // Safety
    double ventilationThreshold_mAh; // Calculated from % and nominal capacity

    // Adaptive mode threshold
    double socThresholdPercent;     // SOC threshold for switching to fast mode (%)

    // EIS configuration (adaptive)
    double eisIntervalSlow_minutes; // EIS interval when SOC < threshold
    double eisIntervalFast_minutes; // EIS interval when SOC >= threshold
    int pauseChargeDuringEIS;       // 1 = pause, 0 = continue

    // Logging (adaptive)
    unsigned int logIntervalSlow;   // Data logging interval when SOC < threshold (seconds)
    unsigned int logIntervalFast;   // Data logging interval when SOC >= threshold (seconds)
} OverchargeParams;
```

### 5.2 Experiment Context

```c
typedef struct {
    OverchargeExperimentState state;
    OverchargeParams params;

    // Cancellation and events
    volatile int cancelRequested;       // User pressed Stop
    volatile int runawayReached;        // User pressed Runaway Reached
    volatile int ventilationLost;       // System-detected via pressure sensor
    double runawayReachedTime;          // Timestamp when user triggered

    // Timing
    double experimentStartTime;
    double chargeStartTime;
    double lastEISTime;
    double lastLogTime;
    double lastGraphUpdate;

    // Adaptive mode tracking
    int inFastMode;                     // 0 = slow mode, 1 = fast mode
    double modeTransitionTime;          // When we switched to fast mode (0 if not yet)

    // Charge tracking (coulomb counting)
    double accumulatedCharge_mAh;
    double currentSOC_percent;          // Only if nominal capacity provided
    double lastCurrent;                 // For trapezoidal integration
    double lastTime;                    // For delta time calculation

    // Current readings
    double currentVoltage;
    double currentCurrent;
    double currentTemperature;
    double currentPressure;

    // EIS measurements
    TempRampEISMeasurement *eisMeasurements;  // Dynamic array
    int eisMeasurementCount;
    int eisMeasurementCapacity;

    // File system
    char experimentDirectory[MAX_PATH_LENGTH];
    FILE *chargeLogFile;               // charge_data.csv
    FILE *temperatureLogFile;          // temperature_profile.csv
    FILE *gasFlowLogFile;              // gas_flow_data.csv (if enabled)
    FILE *eventLogFile;                // events.txt
    FILE *experimentLogFile;           // experiment.log

    // UI handles
    int mainPanelHandle;
    int tabPanelHandle;
    int buttonControl;
    int runawayButtonControl;
    int statusControl;
    int graph1Handle;                  // PANEL_GRAPH_1
    int graph2Handle;                  // PANEL_GRAPH_2
    int graph3Handle;                  // PANEL_GRAPH_BIOLOGIC

    // Graph plot handles
    int voltagePlotHandle;             // Graph 1 left axis
    int currentPlotHandle;             // Graph 1 right axis
    int tempPlotHandle;                // Graph 2 left axis
    int chargePlotHandle;              // Graph 2 right axis
    int nyquistPlotHandle;             // Graph 3

    // Device handles
    PSB_Handle *psbHandle;
    int biologicID;

} OverchargeContext;
```

### 5.3 Experiment States

```c
typedef enum {
    OVERCHARGE_STATE_IDLE = 0,
    OVERCHARGE_STATE_PREPARING,
    OVERCHARGE_STATE_INITIAL_EIS,
    OVERCHARGE_STATE_CHARGING,
    OVERCHARGE_STATE_EIS_MEASUREMENT,
    OVERCHARGE_STATE_COOLING,
    OVERCHARGE_STATE_COMPLETED,
    OVERCHARGE_STATE_ERROR,
    OVERCHARGE_STATE_CANCELLED
} OverchargeExperimentState;
```

---

## 6. Pressure Safety Integration

### 6.1 System Overview

Uses existing `pressure_safety.h/c` module:
- Monitors NI cDAQ 9202 channel 0 (differential pressure sensor)
- Voltage reading indicates fume hood ventilation strength
- Provides callbacks when ventilation is lost or restored
- Automatically sounds alarm when ventilation lost during critical phase
- Supports temperature-based phase determination (SAFE < 30°C, CRITICAL ≥ 30°C)

**For Overcharge Experiment:**
- We use charge-based phase determination instead of temperature
- Still leverage pressure_safety for monitoring and alarm
- Implement custom stop logic in experiment loop based on charge threshold

### 6.2 Integration Points

**Pre-Start Check:**
```c
// Before allowing experiment to start
double pressureVoltage;
if (!PressureSafety_CheckStartConditions(&pressureVoltage)) {
    MessagePopup("Error",
                 "Fume hood ventilation is not adequate.\n"
                 "Current reading: %.2f V\n"
                 "Required minimum: %.2f V",
                 pressureVoltage, PRESSURE_THRESHOLD_OK_MIN);
    return ERR_VENTILATION;
}
```

**Start Monitoring:**
```c
result = PressureSafety_StartMonitoring(currentTemp,
                                       OnVentilationLost,
                                       OnVentilationRestored);
```

**Callbacks:**
```c
static void OnVentilationLost(ExperimentPhase phase, double temperature, double pressure) {
    LogError("***** VENTILATION LOST *****");
    LogError("Temperature: %.1f C, Pressure: %.2f V", temperature, pressure);

    // Note: The phase parameter from pressure_safety is temperature-based
    // We'll determine SAFE vs CRITICAL based on charge delivered instead
    // The actual stop logic is in the main loop

    g_experimentContext.ventilationLost = 1;

    // Log to events.txt
    fprintf(g_experimentContext.eventLogFile,
            "%.3f,VENTILATION_LOST,Temp=%.1f,Pressure=%.2f\n",
            Timer() - g_experimentContext.experimentStartTime,
            temperature, pressure);
    fflush(g_experimentContext.eventLogFile);
}

static void OnVentilationRestored(double pressure) {
    LogMessage("*** Ventilation restored *** (%.2f V)", pressure);
    g_experimentContext.ventilationLost = 0;

    // Log to events.txt
    fprintf(g_experimentContext.eventLogFile,
            "%.3f,VENTILATION_RESTORED,Pressure=%.2f\n",
            Timer() - g_experimentContext.experimentStartTime,
            pressure);
    fflush(g_experimentContext.eventLogFile);
}
```

**In Main Loop:**
```c
if (ctx->ventilationLost) {
    if (ctx->accumulatedCharge_mAh < ctx->params.ventilationThreshold_mAh) {
        // SAFE PHASE - stop experiment (no alarm)
        LogError("Ventilation lost - SAFE phase");
        LogError("Charge: %.1f mAh < Threshold: %.1f mAh",
                ctx->accumulatedCharge_mAh,
                ctx->params.ventilationThreshold_mAh);
        LogError("Safe to stop experiment");

        // Log event
        LogEvent(ctx, "VENT_LOSS_SAFE",
                "Charge=%.1f mAh, Threshold=%.1f mAh",
                ctx->accumulatedCharge_mAh,
                ctx->params.ventilationThreshold_mAh);

        PSB_SetOutputEnable(0);
        ctx->cancelRequested = 1;
        ctx->state = OVERCHARGE_STATE_CANCELLED;
        break;

    } else {
        // CRITICAL PHASE - stop experiment with alarm
        LogError("***** CRITICAL PHASE VENTILATION LOSS *****");
        LogError("Charge: %.1f mAh >= Threshold: %.1f mAh",
                ctx->accumulatedCharge_mAh,
                ctx->params.ventilationThreshold_mAh);
        LogError("Battery may be in dangerous state!");
        LogError("ALARM SOUNDED - Personnel should re-establish ventilation or evacuate");
        LogError("Stopping experiment");

        // Log critical event
        LogEvent(ctx, "VENT_LOSS_CRITICAL",
                "Charge=%.1f mAh, Threshold=%.1f mAh, ALARM",
                ctx->accumulatedCharge_mAh,
                ctx->params.ventilationThreshold_mAh);

        // Sound alarm (pressure_safety module handles this automatically)
        // The alarm is already sounding from the pressure_safety module
        // when ventilation is lost during critical phase

        PSB_SetOutputEnable(0);
        ctx->cancelRequested = 1;
        ctx->state = OVERCHARGE_STATE_CANCELLED;
        break;
    }
}

// Update temperature for phase determination in pressure_safety module
// Note: pressure_safety uses temperature-based phases, we use charge-based
// This is mainly for its internal alarm logic
PressureSafety_UpdateTemperature(ctx->currentTemperature);
```

**Stop Monitoring:**
```c
// At experiment end
PressureSafety_StopMonitoring();
```

### 6.3 Threshold Calculation

```c
// When reading UI parameters
GetCtrlVal(panel, OVERCHARGE_NUM_NOMINAL_CAPACITY,
           &ctx->params.nominalCapacity_mAh);
GetCtrlVal(panel, OVERCHARGE_NUM_VENT_THRESHOLD_PCT,
           &ventThresholdPercent);

if (ctx->params.nominalCapacity_mAh > 0) {
    ctx->params.ventilationThreshold_mAh =
        ctx->params.nominalCapacity_mAh * (ventThresholdPercent / 100.0);

    // Update display
    SetCtrlVal(panel, OVERCHARGE_NUM_VENT_THRESHOLD_MAH,
               ctx->params.ventilationThreshold_mAh);

    LogMessage("Ventilation safe threshold: %.1f%% of %.1f mAh = %.1f mAh",
               ventThresholdPercent,
               ctx->params.nominalCapacity_mAh,
               ctx->params.ventilationThreshold_mAh);
} else {
    MessagePopup("Error",
                 "Please enter nominal battery capacity for safety threshold calculation.");
    return ERR_INVALID_PARAM;
}
```

---

## 7. File System Structure

```
data/
└── exp_overcharge_YYYYMMDD_HHMMSS/
    ├── experiment_settings.ini        # All user parameters
    ├── experiment.log                 # Main experiment log (timestamped events)
    ├── charge_data.csv               # Primary data: Time,V,I,P,mAh,%,Temps
    ├── temperature_profile.csv        # Detailed temp: Time,DTB1,DTB2,TC0-TC31
    ├── gas_flow_data.csv             # ALICAT: Time,Flow,Temp,Setpoint (if enabled)
    ├── events.txt                     # Critical events: runaway, vent loss, etc.
    ├── eis_measurements/
    │   ├── eis_001_t000.0min.csv     # Initial EIS (before charging)
    │   ├── eis_002_t010.0min.csv     # At 10 minutes
    │   ├── eis_003_t020.0min.csv     # At 20 minutes
    │   └── ...
    └── summary.txt                    # Final experiment summary
```

### 7.1 File Formats

**experiment_settings.ini:**
```ini
[Experiment_Info]
Type=Overcharge
Date=2025-11-14
Time=14:30:00
Directory=C:\Users\...\data\exp_overcharge_20251114_143000

[Battery_Configuration]
Nominal_Capacity_mAh=3000.0

[Charging_Parameters]
Charge_Current_A=4.5
Charge_Duration_min=120
Duration_Unlimited=0

[Safety]
Ventilation_Threshold_Percent=20.0
Ventilation_Threshold_mAh=600.0

[Adaptive_Mode]
SOC_Threshold_Percent=100.0

[EIS_Configuration]
EIS_Interval_Slow_min=20.0
EIS_Interval_Fast_min=5.0
Pause_Charging_During_EIS=1

[Logging]
Log_Interval_Slow_sec=10
Log_Interval_Fast_sec=2

[Device_Configuration]
PSB_Enabled=1
BioLogic_Enabled=1
DTB_Enabled=1
ALICAT_Enabled=1
Teensy_Enabled=1
cDAQ_Enabled=1
```

**charge_data.csv:**
```csv
Time_s,Voltage_V,Current_A,Power_W,Charge_mAh,Charge_Percent,Temp_DTB_C,Temp_TC0_C,Temp_TC1_C,Mode
0.000,3.752,4.485,16.83,0.0,0.0,25.3,24.8,25.1,SLOW
10.000,3.798,4.492,17.06,12.5,0.4,25.4,24.9,25.2,SLOW
20.000,3.845,4.488,17.25,24.9,0.8,25.6,25.0,25.3,SLOW
...
1200.000,4.156,4.478,18.61,1503.2,50.1,48.5,47.8,48.2,SLOW
1202.000,4.158,4.481,18.63,1505.7,50.2,48.7,47.9,48.3,FAST
1204.000,4.161,4.479,18.64,1508.1,50.3,48.9,48.1,48.5,FAST
...
```

**temperature_profile.csv:**
```csv
Time_s,DTB1_C,DTB2_C,TC0_C,TC1_C,TC2_C,...,TC31_C,Avg_DTB_C,Avg_TC_C
0.000,25.3,25.2,24.8,25.1,24.9,...,25.0,25.25,25.02
10.000,25.4,25.3,24.9,25.2,25.0,...,25.1,25.35,25.08
...
```

**events.txt:**
```
# Overcharge Experiment Events Log
# Format: Timestamp_s,Event_Type,Details

0.000,EXPERIMENT_START,Current=4.5A Duration=120min
0.125,INITIAL_EIS_START,
45.230,INITIAL_EIS_COMPLETE,OCV=3.752V Points=101
45.500,CHARGING_START,Mode=SLOW
605.234,EIS_START,Interval=20.0min (slow)
660.891,EIS_COMPLETE,OCV=3.892V Points=101
950.125,USER_RUNAWAY_REACHED,V=4.123V T=78.5C mAh=1183.2
1200.000,MODE_FAST,SOC=100.0% Threshold=100.0%
1200.001,MODE_CHANGE,EIS:20.0min->5.0min Log:10s->2s
1205.450,VENTILATION_LOST,Temp=85.2C Pressure=2.78V
1205.451,VENTILATION_CRITICAL,Charge=1502mAh>Threshold=600mAh ALARM
1205.452,EXPERIMENT_STOP,Ventilation lost in critical phase
1245.678,FINAL_EIS_COMPLETE,
1545.890,EXPERIMENT_END,Total=1545.9s Charge=1850.3mAh
```

**summary.txt:**
```
OVERCHARGE EXPERIMENT SUMMARY
==============================

Experiment ID: exp_overcharge_20251114_143000
Date: 2025-11-14 14:30:00
Status: COMPLETED

PARAMETERS
----------
Battery Nominal Capacity: 3000.0 mAh
Charge Current: 4.5 A
Charge Duration: 120 min (limit reached)
Ventilation Safe Threshold: 20.0% (600.0 mAh)
SOC Threshold for Fast Mode: 100.0%
EIS Interval (Slow): 20.0 min
EIS Interval (Fast): 5.0 min
Pause During EIS: Yes
Log Interval (Slow): 10 sec
Log Interval (Fast): 2 sec

RESULTS
-------
Total Charge Delivered: 1850.3 mAh (61.7% of nominal)
Total Experiment Time: 25.8 min (1545.9 sec)
Final Voltage: 4.215 V
Final Current: 4.482 A
Peak Temperature: 92.3 °C
EIS Measurements Completed: 8 (3 slow + 5 fast)
Mode Transition: 20.0 min (SOC reached 100.0%)

EVENTS
------
Runaway Reached (user): YES at 15.8 min (V=4.123V, T=78.5°C)
Ventilation Lost: YES at 20.1 min (critical phase - continued)
Ventilation Restored: YES at 23.0 min

Stop Reason: Duration limit reached (120 min)

VENTILATION SUMMARY
-------------------
Pre-start Check: PASSED (3.12 V)
Ventilation Events: 1 loss, 1 restoration
Critical Phase Entry: 10.1 min (600 mAh threshold)
Final Ventilation Status: OK (3.05 V)

DATA FILES
----------
charge_data.csv: 246 data points
temperature_profile.csv: 246 data points
eis_measurements/: 5 measurements
events.txt: 12 events logged
```

**EIS measurement file (eis_NNN_tMMM.Mmin.csv):**
```csv
# Overcharge Experiment EIS Measurement
# Measurement: 2
# Time: 10.0 min
# Charge Delivered: 183.5 mAh (6.1%)
# Temperature: 26.8 C
# OCV: 3.892 V

Frequency_Hz,Z_Real_Ohm,Z_Imag_Ohm,Z_Mag_Ohm,Z_Phase_deg
100000.0,0.0235,-0.0012,0.0235,-2.92
50000.0,0.0241,-0.0015,0.0241,-3.56
...
0.1,0.0892,-0.0234,0.0923,-14.71
```

---

## 8. Key Functions

### 8.1 Main Entry Points

```c
/**
 * Main callback for starting/stopping overcharge experiment
 */
int CVICALLBACK StartOverchargeExperimentCallback(int panel, int control, int event,
                                                   void *callbackData,
                                                   int eventData1, int eventData2);

/**
 * Callback for "Runaway Reached" button
 */
int CVICALLBACK RunawayReachedCallback(int panel, int control, int event,
                                       void *callbackData,
                                       int eventData1, int eventData2);

/**
 * Check if overcharge experiment is running
 */
int OverchargeExperiment_IsRunning(void);

/**
 * Abort running experiment
 */
int OverchargeExperiment_Abort(void);

/**
 * Emergency stop - immediate halt
 */
int OverchargeExperiment_EmergencyStop(void);

/**
 * Module cleanup
 */
void OverchargeExperiment_Cleanup(void);
```

### 8.2 Internal Functions

```c
// Main experiment thread
static int OverchargeExperimentThread(void *functionData);

// Setup and verification
static int VerifyAllDevices(OverchargeContext *ctx);
static int CreateExperimentFileSystem(OverchargeContext *ctx);
static int SaveExperimentSettings(OverchargeContext *ctx);

// Device control
static int SwitchToPSB(OverchargeContext *ctx);
static int SwitchToBioLogic(OverchargeContext *ctx);
static int SafeDisconnectAllDevices(OverchargeContext *ctx);

// Experiment phases
static int PerformInitialEIS(OverchargeContext *ctx);
static int RunChargingLoop(OverchargeContext *ctx);
static int PerformPeriodicEIS(OverchargeContext *ctx);
static int PerformFinalEIS(OverchargeContext *ctx);
static int MonitorCooling(OverchargeContext *ctx, int durationSeconds);

// EIS measurement functions (adapted from temp_ramp)
static int RunOCVMeasurement(OverchargeContext *ctx, TempRampEISMeasurement *measurement);
static int RunGEISMeasurement(OverchargeContext *ctx, TempRampEISMeasurement *measurement);
static int ProcessGEISData(BIO_TechniqueData *geisData, TempRampEISMeasurement *measurement);
static int SaveEISMeasurementData(OverchargeContext *ctx, TempRampEISMeasurement *measurement);

// Charge tracking (coulomb counting)
static int UpdateChargeTracking(OverchargeContext *ctx, double current, double deltaTime);
static double CalculateSOCPercent(OverchargeContext *ctx);

// Temperature reading (from temp_ramp)
static int ReadAllTemperatures(OverchargeContext *ctx, TempRampTempData *tempData, double timestamp);

// Data logging
static int LogChargeDataPoint(OverchargeContext *ctx, PSB_Status *status, TempRampTempData *tempData);
static int LogTemperatureDataPoint(OverchargeContext *ctx, TempRampTempData *tempData);
static int LogGasFlowDataPoint(OverchargeContext *ctx, ALICAT_Status *flowData);
static int LogEvent(OverchargeContext *ctx, const char *eventType, const char *details);

// Graph management
static int ConfigureOverchargeGraphs(OverchargeContext *ctx);
static void UpdateGraphsRealtime(OverchargeContext *ctx, PSB_Status *status, TempRampTempData *tempData);
static void UpdateNyquistPlot(OverchargeContext *ctx, TempRampEISMeasurement *measurement);
static void AddRunawayMarker(OverchargeContext *ctx, double timeMinutes);
static void ClearAllGraphs(OverchargeContext *ctx);

// Pressure safety callbacks
static void OnVentilationLost(ExperimentPhase phase, double temperature, double pressure);
static void OnVentilationRestored(double pressure);

// Results and cleanup
static int WriteFinalResults(OverchargeContext *ctx);
static void CleanupExperiment(OverchargeContext *ctx);

// Adaptive mode functions
static void UpdateAdaptiveMode(OverchargeContext *ctx);
static double GetCurrentEISInterval(OverchargeContext *ctx);
static unsigned int GetCurrentLogInterval(OverchargeContext *ctx);
static const char* GetModeString(int inFastMode);

// Utility functions
static int CheckCancellation(OverchargeContext *ctx);
static const char* GetStateDescription(OverchargeExperimentState state);
```

---

## 9. Constants and Configuration

```c
// File System
#define OVERCHARGE_DATA_DIR               "data"
#define OVERCHARGE_SUMMARY_FILE           "summary.txt"
#define OVERCHARGE_SETTINGS_FILE          "experiment_settings.ini"
#define OVERCHARGE_LOG_FILE               "experiment.log"
#define OVERCHARGE_CHARGE_FILE            "charge_data.csv"
#define OVERCHARGE_TEMP_FILE              "temperature_profile.csv"
#define OVERCHARGE_FLOW_FILE              "gas_flow_data.csv"
#define OVERCHARGE_EVENT_FILE             "events.txt"
#define OVERCHARGE_EIS_DIR                "eis_measurements"

// Experiment Limits
#define OVERCHARGE_MAX_DURATION_MIN       480      // 8 hours absolute max
#define OVERCHARGE_MIN_EIS_INTERVAL       1.0      // 1 minute minimum
#define OVERCHARGE_MAX_EIS_INTERVAL       120.0    // 2 hours maximum
#define OVERCHARGE_MIN_LOG_INTERVAL       1        // 1 second minimum
#define OVERCHARGE_MAX_LOG_INTERVAL       300      // 5 minutes maximum

// Default Values
#define OVERCHARGE_DEFAULT_CURRENT           4.0      // A
#define OVERCHARGE_DEFAULT_DURATION          120      // minutes
#define OVERCHARGE_DEFAULT_SOC_THRESHOLD     100.0    // percent
#define OVERCHARGE_DEFAULT_EIS_INTERVAL_SLOW 20.0     // minutes
#define OVERCHARGE_DEFAULT_EIS_INTERVAL_FAST 5.0      // minutes
#define OVERCHARGE_DEFAULT_LOG_INTERVAL_SLOW 10       // seconds
#define OVERCHARGE_DEFAULT_LOG_INTERVAL_FAST 2        // seconds
#define OVERCHARGE_DEFAULT_VENT_THRESHOLD    20.0     // percent

// EIS Configuration
#define OVERCHARGE_MAX_EIS_RETRY          2        // Retry failed measurements
#define OVERCHARGE_EIS_RETRY_DELAY        5.0      // Seconds between retries

// Cooling Monitoring
#define OVERCHARGE_COOLING_DURATION       300      // 5 minutes post-experiment
#define OVERCHARGE_COOLING_INTERVAL       10       // Log every 10 seconds

// CSV Headers
#define OVERCHARGE_CHARGE_HEADER "Time_s,Voltage_V,Current_A,Power_W,Charge_mAh,Charge_Percent,Temp_DTB_C,Temp_TC0_C,Temp_TC1_C,Mode"
#define OVERCHARGE_TEMP_HEADER   "Time_s,DTB1_C,DTB2_C,TC0_C,TC1_C,TC2_C,TC3_C,TC4_C,TC5_C,TC6_C,TC7_C,Avg_DTB_C,Avg_TC_C"
#define OVERCHARGE_FLOW_HEADER   "Time_s,Flow_SLPM,Temp_C,Setpoint_SLPM"
#define OVERCHARGE_EVENT_HEADER  "# Overcharge Experiment Events Log\n# Format: Timestamp_s,Event_Type,Details"
```

---

## 10. Implementation Steps

### Phase 1: Basic Structure
1. Create `exp_overcharge.h` with all type definitions and constants
2. Create `exp_overcharge.c` skeleton with function prototypes
3. Add UI tab panel to `BatteryExploder.uir` with all controls
4. Implement basic callback structure (Start/Stop/Runaway buttons)
5. Add module initialization to `BatteryExploder.c`

### Phase 2: Core Functionality
6. Implement experiment thread and state machine
7. Implement device verification and initialization
8. Implement file system creation and logging
9. Implement charge tracking (coulomb counting)
10. Implement PSB charging control loop

### Phase 3: EIS Integration
11. Adapt EIS functions from `exp_temp_ramp.c`
12. Implement relay switching logic
13. Implement time-based EIS scheduling
14. Implement EIS data saving and processing

### Phase 4: Graphing
15. Implement dual-axis graph configuration
16. Implement real-time graph updating
17. Implement Nyquist plot updates
18. Implement runaway event markers

### Phase 5: Safety Integration
19. Integrate pressure safety monitoring
20. Implement charge-based safety logic
21. Implement ventilation status display
22. Test ventilation loss scenarios

### Phase 6: Polish
23. Implement final results summary
24. Implement cooling monitoring
25. Add comprehensive error handling
26. Add validation and user prompts
27. Testing and debugging

### Phase 7: Documentation
28. Add inline code documentation
29. Update CLAUDE.md with module info
30. Create user guide section
31. Document safety procedures

---

## 11. Safety Considerations

### 11.1 Pre-Experiment Checklist
- [ ] Battery properly contained in fume hood
- [ ] Fume hood extraction system running
- [ ] Differential pressure sensor verified working
- [ ] All temperature sensors connected and reading
- [ ] Gas flow sensor active (if applicable)
- [ ] Fire safety equipment nearby
- [ ] Emergency stop accessible
- [ ] Personnel briefed on emergency procedures

### 11.2 During Experiment
- Continuous ventilation monitoring with automatic response
- Real-time temperature tracking
- User can manually indicate runaway without stopping data collection
- Charge-based safety thresholds (SAFE vs CRITICAL phases)
- Ventilation loss ALWAYS stops experiment (alarm if in critical phase)
- All events logged for forensic analysis

**Ventilation Loss Behavior (same as Temperature Ramp experiment):**
- **SAFE Phase** (charge < threshold): Stop immediately, no alarm
- **CRITICAL Phase** (charge ≥ threshold): Stop immediately + Sound alarm
  - Alarm warns personnel battery may be dangerous even though stopped
  - Personnel should re-establish ventilation or evacuate

### 11.3 Emergency Response
- Emergency stop button immediately disables PSB output
- Relay isolation via Teensy disconnects battery
- Temperature monitoring continues even after stop
- Alarm sounds if ventilation lost during critical phase
- Post-experiment cooling monitoring for safe shutdown

---

## 12. Testing Plan

### 12.1 Unit Tests
- Coulomb counting accuracy
- Graph updates
- File I/O operations
- UI parameter validation
- Threshold calculations

### 12.2 Integration Tests
- Pressure safety integration
- EIS measurement cycle
- Device switching (PSB ↔ Bio-Logic)
- Event logging
- Graph marker placement

### 12.3 System Tests
- Full experiment run (short duration)
- Ventilation loss simulation (before threshold)
- Ventilation loss simulation (after threshold)
- Runaway reached button functionality
- Emergency stop procedures
- Duration limit enforcement

### 12.4 Safety Tests
- Pre-start ventilation check
- Automatic stop on early ventilation loss
- Alarm-only on late ventilation loss
- Ventilation restoration detection
- Multiple ventilation events

---

## 13. Known Limitations and Future Enhancements

### 13.1 Current Limitations
- No automatic runaway detection (intentional - manual trigger only)
- No temperature-based safety limits (user must monitor)
- No voltage-based safety limits (user must monitor)
- Single current setpoint (no multi-stage charging)
- SOC threshold applies to both EIS and logging (cannot be set independently)

### 13.2 Potential Future Enhancements
- Optional automatic runaway detection (dV/dt, dT/dt, etc.)
- Configurable safety limits as warnings (not stops)
- Multi-stage charging profiles
- Automatic EIS frequency optimization based on impedance trends
- Real-time impedance parameter extraction (R0, R1, C1, etc.)
- Export data in additional formats (HDF5, MATLAB, etc.)
- Remote monitoring capabilities
- Email/SMS alerts on critical events

---

## End of Specification

**Document Version:** 1.0
**Last Updated:** 2025-11-14
**Status:** Ready for Review

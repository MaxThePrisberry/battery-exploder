/******************************************************************************
 * exp_overcharge.c
 *
 * Overcharge Thermal Runaway Experiment Module Implementation
 * Constant-current overcharge with periodic EIS measurements
 ******************************************************************************/

#include "common.h"
#include "exp_overcharge.h"
#include "BatteryExploder.h"
#include "logging.h"
#include "status.h"
#include "battery_utils.h"
#include "biologic/biologic_abstract.h"
#include <ansi_c.h>
#include <analysis.h>
#include <utility.h>
#include <time.h>
#include <windows.h>  // For COM initialization (CoInitializeEx, CoUninitialize)

/******************************************************************************
 * Module Variables
 ******************************************************************************/

// Experiment context and thread management
static OverchargeExperimentContext g_experimentContext = {0};
static CmtThreadFunctionID g_experimentThreadId = 0;

// Controls to be dimmed during experiment
static const int numControls = 8;
static const int controls[8] = {
    OVERCHARGE_NUM_NOMINAL_CAPACITY,
    OVERCHARGE_NUM_CHARGE_CURRENT,
    OVERCHARGE_NUM_CHARGE_DURATION,
    OVERCHARGE_NUM_SOC_THRESHOLD,
    OVERCHARGE_NUM_EIS_INTERVAL_SLOW,
    OVERCHARGE_NUM_EIS_INTERVAL_FAST,
    OVERCHARGE_NUM_LOG_INTERVAL_SLOW,
    OVERCHARGE_NUM_LOG_INTERVAL_FAST
};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

// Main experiment thread
static int OverchargeExperimentThread(void *functionData);

// Setup and verification
static int VerifyAllDevices(OverchargeExperimentContext *ctx);
static int CreateExperimentFileSystem(OverchargeExperimentContext *ctx);
static int SaveExperimentSettings(OverchargeExperimentContext *ctx);

// Device control
static int SwitchToPSB(OverchargeExperimentContext *ctx);
static int SwitchToBioLogic(OverchargeExperimentContext *ctx);
static int SafeDisconnectAllDevices(OverchargeExperimentContext *ctx);

// Experiment phases
static int PerformInitialEIS(OverchargeExperimentContext *ctx);
static int RunChargingLoop(OverchargeExperimentContext *ctx);
static int PerformPeriodicEIS(OverchargeExperimentContext *ctx, int measurementIndex);
static int PerformFinalEIS(OverchargeExperimentContext *ctx);
static int MonitorCooling(OverchargeExperimentContext *ctx, int durationSeconds);

// EIS measurement functions
static int PerformEISMeasurement(OverchargeExperimentContext *ctx, OverchargeEISMeasurement *measurement);
static int ProcessGEISData(OverchargeExperimentContext *ctx, OverchargeEISMeasurement *measurement, BIO_TechniqueData *geisData);
static int SaveEISMeasurement(OverchargeExperimentContext *ctx, OverchargeEISMeasurement *measurement);

// Charge tracking
static void UpdateChargeTracking(OverchargeExperimentContext *ctx, double currentTime);
static double CalculateSOCPercent(OverchargeExperimentContext *ctx);

// Temperature reading
static int ReadAllTemperatures(OverchargeExperimentContext *ctx);

// Data logging
static int LogChargeDataPoint(OverchargeExperimentContext *ctx, double timestamp);
static int LogTemperatureDataPoint(OverchargeExperimentContext *ctx, double timestamp);
static int LogGasFlowDataPoint(OverchargeExperimentContext *ctx);
static int LogEvent(OverchargeExperimentContext *ctx, const char *eventType, const char *details, ...);

// Graph management
static int ConfigureOverchargeGraphs(OverchargeExperimentContext *ctx);
static void UpdateGraphsRealtime(OverchargeExperimentContext *ctx, double timestamp);
static void UpdateNyquistPlot(OverchargeExperimentContext *ctx, OverchargeEISMeasurement *measurement);
static void AddRunawayMarker(OverchargeExperimentContext *ctx, double timeMinutes);
static void ClearOverchargeGraphs(OverchargeExperimentContext *ctx);

// Adaptive mode functions
static void UpdateAdaptiveMode(OverchargeExperimentContext *ctx);
static double GetCurrentEISInterval(OverchargeExperimentContext *ctx);
static double GetCurrentLogInterval(OverchargeExperimentContext *ctx);
static const char* GetModeString(int inFastMode);

// Pressure safety callbacks
static void OnVentilationLost(ExperimentPhase phase, double temperature, double pressure);
static void OnVentilationRestored(double pressure);

// Results and cleanup
static int WriteFinalResults(OverchargeExperimentContext *ctx);
static void CleanupExperiment(OverchargeExperimentContext *ctx);

// Utility functions
static int CheckCancellation(OverchargeExperimentContext *ctx);
static const char* GetStateDescription(OverchargeExperimentState state);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int CVICALLBACK StartOverchargeExperimentCallback(int panel, int control, int event,
                                                   void *callbackData,
                                                   int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }

    // Check if experiment is already running
    if (OverchargeExperiment_IsRunning()) {
        // This is a Stop request
        LogMessage("User requested to stop overcharge experiment");
        g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = OVERCHARGE_STATE_CANCELLED;
        return 0;
    }

    // Check if system is busy
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        MessagePopup("System Busy",
                     "Another operation is in progress.\n"
                     "Please wait for it to complete before starting the overcharge experiment.");
        return 0;
    }
    g_systemBusy = 1;
    CmtReleaseLock(g_busyLock);

    // Initialize experiment context
    memset(&g_experimentContext, 0, sizeof(g_experimentContext));
    g_experimentContext.state = OVERCHARGE_STATE_PREPARING;
    g_experimentContext.mainPanelHandle = g_mainPanelHandle;
    g_experimentContext.tabPanelHandle = panel;
    g_experimentContext.buttonControl = control;
    g_experimentContext.runawayButtonControl = OVERCHARGE_BTN_RUNAWAY_REACHED;
    g_experimentContext.statusControl = OVERCHARGE_STR_STATUS;
    g_experimentContext.modeControl = OVERCHARGE_STR_MODE;
    g_experimentContext.graph1Handle = PANEL_GRAPH_1;
    g_experimentContext.graph2Handle = PANEL_GRAPH_2;
    g_experimentContext.graph3Handle = PANEL_GRAPH_BIOLOGIC;

    // Read experiment parameters from UI
    double nominalCapacity, ventThresholdPercent;
    GetCtrlVal(panel, OVERCHARGE_NUM_NOMINAL_CAPACITY, &nominalCapacity);
    GetCtrlVal(panel, OVERCHARGE_NUM_CHARGE_CURRENT, &g_experimentContext.params.chargeCurrent);
    GetCtrlVal(panel, OVERCHARGE_NUM_CHARGE_DURATION, &g_experimentContext.params.chargeDurationMinutes);
    GetCtrlVal(panel, OVERCHARGE_NUM_VENT_THRESHOLD_PC, &ventThresholdPercent);
    GetCtrlVal(panel, OVERCHARGE_NUM_SOC_THRESHOLD, &g_experimentContext.params.socThresholdPercent);
    GetCtrlVal(panel, OVERCHARGE_NUM_EIS_INTERVAL_SLOW, &g_experimentContext.params.eisIntervalSlow_minutes);
    GetCtrlVal(panel, OVERCHARGE_NUM_EIS_INTERVAL_FAST, &g_experimentContext.params.eisIntervalFast_minutes);
    GetCtrlVal(panel, OVERCHARGE_CHK_PAUSE_DURING_EIS, &g_experimentContext.params.pauseChargeDuringEIS);
    GetCtrlVal(panel, OVERCHARGE_NUM_LOG_INTERVAL_SLOW, &g_experimentContext.params.logIntervalSlow);
    GetCtrlVal(panel, OVERCHARGE_NUM_LOG_INTERVAL_FAST, &g_experimentContext.params.logIntervalFast);

    g_experimentContext.params.nominalCapacity_mAh = nominalCapacity;

    // Validate parameters
    if (g_experimentContext.params.chargeCurrent <= 0 ||
        g_experimentContext.params.chargeCurrent > PSB_SAFE_CURRENT_MAX) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        char msg[256];
        sprintf(msg, "Charge current must be between 0 and %.1f A.", PSB_SAFE_CURRENT_MAX);
        MessagePopup("Invalid Charge Current", msg);
        return 0;
    }

    if (g_experimentContext.params.nominalCapacity_mAh <= 0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        MessagePopup("Invalid Nominal Capacity",
                     "Please enter a valid nominal battery capacity (mAh).\n"
                     "This is required for SOC calculation and safety thresholds.");
        return 0;
    }

    // Calculate ventilation threshold
    g_experimentContext.params.ventilationThreshold_mAh =
        nominalCapacity * (ventThresholdPercent / 100.0);

    if (g_experimentContext.params.ventilationThreshold_mAh <= 0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        MessagePopup("Invalid Ventilation Threshold",
                     "Ventilation threshold percentage must be greater than 0.");
        return 0;
    }

    // Validate EIS intervals
    if (g_experimentContext.params.eisIntervalSlow_minutes < OVERCHARGE_MIN_EIS_INTERVAL ||
        g_experimentContext.params.eisIntervalSlow_minutes > OVERCHARGE_MAX_EIS_INTERVAL) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        char msg[256];
        sprintf(msg, "Slow EIS interval must be between %.1f and %.1f minutes.",
                OVERCHARGE_MIN_EIS_INTERVAL, OVERCHARGE_MAX_EIS_INTERVAL);
        MessagePopup("Invalid EIS Interval", msg);
        return 0;
    }

    if (g_experimentContext.params.eisIntervalFast_minutes < OVERCHARGE_MIN_EIS_INTERVAL ||
        g_experimentContext.params.eisIntervalFast_minutes > OVERCHARGE_MAX_EIS_INTERVAL) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        char msg[256];
        sprintf(msg, "Fast EIS interval must be between %.1f and %.1f minutes.",
                OVERCHARGE_MIN_EIS_INTERVAL, OVERCHARGE_MAX_EIS_INTERVAL);
        MessagePopup("Invalid EIS Interval", msg);
        return 0;
    }

    // Verify all required devices are connected
    int result = VerifyAllDevices(&g_experimentContext);
    if (result != SUCCESS) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        g_experimentContext.state = OVERCHARGE_STATE_ERROR;
        return 0;
    }

    // Change button text to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");

    // Enable runaway reached button
    SetCtrlAttribute(panel, g_experimentContext.runawayButtonControl, ATTR_DIMMED, 0);

    // Dim appropriate controls
    DimExperimentControls(g_mainPanelHandle, panel, 1, controls, numControls);

    // Start experiment thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, OverchargeExperimentThread,
                                             &g_experimentContext, &g_experimentThreadId);
    if (error != 0) {
        // Failed to start thread
        g_experimentContext.state = OVERCHARGE_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start");
        SetCtrlAttribute(panel, g_experimentContext.runawayButtonControl, ATTR_DIMMED, 1);
        DimExperimentControls(g_mainPanelHandle, panel, 0, controls, numControls);

        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        MessagePopup("Error", "Failed to start overcharge experiment thread.");
        return 0;
    }

    return 0;
}

int CVICALLBACK RunawayReachedCallback(int panel, int control, int event,
                                       void *callbackData,
                                       int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }

    if (!OverchargeExperiment_IsRunning()) {
        return 0;
    }

    // User indicated thermal runaway has been reached
    if (!g_experimentContext.runawayReached) {
        g_experimentContext.runawayReached = 1;
        g_experimentContext.runawayReachedTime = Timer();

        LogMessage("***** USER INDICATED THERMAL RUNAWAY REACHED *****");
        LogMessage("Time: %.1f min", (g_experimentContext.runawayReachedTime - g_experimentContext.experimentStartTime) / 60.0);
        LogMessage("Voltage: %.3f V", g_experimentContext.currentVoltage);
        LogMessage("Current: %.3f A", g_experimentContext.currentCurrent);
        LogMessage("Temperature: %.1f C", g_experimentContext.currentTemperature);
        LogMessage("Charge delivered: %.1f mAh (%.1f%%)",
                   g_experimentContext.accumulatedCharge_mAh,
                   g_experimentContext.currentSOC_percent);

        // Log event
        char details[256];
        snprintf(details, sizeof(details),
                 "V=%.3fV T=%.1fC mAh=%.1f SOC=%.1f%%",
                 g_experimentContext.currentVoltage,
                 g_experimentContext.currentTemperature,
                 g_experimentContext.accumulatedCharge_mAh,
                 g_experimentContext.currentSOC_percent);
        LogEvent(&g_experimentContext, "USER_RUNAWAY_REACHED", details);

        // Add marker to graphs
        double elapsedMin = (g_experimentContext.runawayReachedTime - g_experimentContext.chargeStartTime) / 60.0;
        AddRunawayMarker(&g_experimentContext, elapsedMin);

        // Experiment continues - this is just a marker
        LogMessage("Experiment continuing with data collection...");
    }

    return 0;
}

int OverchargeExperiment_IsRunning(void) {
    return !(g_experimentContext.state == OVERCHARGE_STATE_IDLE ||
             g_experimentContext.state == OVERCHARGE_STATE_COMPLETED ||
             g_experimentContext.state == OVERCHARGE_STATE_ERROR ||
             g_experimentContext.state == OVERCHARGE_STATE_CANCELLED);
}

int OverchargeExperiment_Abort(void) {
    if (OverchargeExperiment_IsRunning()) {
        LogMessage("Aborting overcharge experiment...");
        g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = OVERCHARGE_STATE_CANCELLED;

        // Wait for thread to complete
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                  OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}

int OverchargeExperiment_EmergencyStop(void) {
    if (OverchargeExperiment_IsRunning()) {
        LogMessage("EMERGENCY STOP - Overcharge experiment");
        g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = OVERCHARGE_STATE_ERROR;

        // Immediately disconnect all devices
        SafeDisconnectAllDevices(&g_experimentContext);

        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                  OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}

void OverchargeExperiment_Cleanup(void) {
    if (OverchargeExperiment_IsRunning()) {
        OverchargeExperiment_Abort();
    }
}

/******************************************************************************
 * Main Experiment Thread Implementation
 ******************************************************************************/

static int OverchargeExperimentThread(void *functionData) {
    OverchargeExperimentContext *ctx = (OverchargeExperimentContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;

    // Initialize COM for this thread (MTA mode to match main thread)
    // Each thread that uses COM interfaces must call CoInitializeEx, even in MTA mode.
    // This is critical for EC-Lab OLE COM operations to work from this thread.
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LogError("Experiment thread: CoInitializeEx failed (0x%08X)", hr);
        LogError("EC-Lab operations may fail from this thread!");
        // Continue anyway - non-EC-Lab devices will still work
    } else {
        LogMessage("Experiment thread: COM initialized successfully (MTA mode)");
    }

    LogMessage("=== Starting Overcharge Thermal Runaway Experiment ===");

    // Record experiment start time
    ctx->experimentStartTime = Timer();

    // Check for early cancellation
    if (CheckCancellation(ctx)) {
        LogMessage("Overcharge experiment cancelled before confirmation");
        goto cleanup;
    }

    // Show comprehensive confirmation popup
    char durationStr[50];
    if (ctx->params.chargeDurationMinutes > 0) {
        snprintf(durationStr, sizeof(durationStr), "%.0f min", ctx->params.chargeDurationMinutes);
    } else {
        strcpy(durationStr, "Unlimited");
    }

    snprintf(message, sizeof(message),
        "OVERCHARGE THERMAL RUNAWAY EXPERIMENT\n"
        "========================================\n\n"
        "PARAMETERS:\n"
        "� Battery Capacity: %.0f mAh\n"
        "� Charge Current: %.2f A\n"
        "� Charge Duration: %s\n"
        "� Ventilation Safe Threshold: %.1f%% (%.1f mAh)\n"
        "� SOC Fast Mode Threshold: %.1f%%\n\n"
        "EIS CONFIGURATION:\n"
        "� Slow EIS Interval: %.1f min (SOC < %.1f%%)\n"
        "� Fast EIS Interval: %.1f min (SOC >= %.1f%%)\n"
        "� Pause During EIS: %s\n\n"
        "DATA LOGGING:\n"
        "� Slow Log Interval: %.0f sec (SOC < %.1f%%)\n"
        "� Fast Log Interval: %.0f sec (SOC >= %.1f%%)\n\n"
        "SAFETY:\n"
        "� Ventilation monitoring: ENABLED\n"
        "� Pre-start check: REQUIRED\n"
        "� Manual runaway trigger: ENABLED\n\n"
        "WARNING: This experiment intentionally overcharges the battery\n"
        "to induce thermal runaway. Ensure all safety protocols are followed.\n\n"
        "Continue with experiment?",
        ctx->params.nominalCapacity_mAh,
        ctx->params.chargeCurrent,
        durationStr,
        (ctx->params.ventilationThreshold_mAh / ctx->params.nominalCapacity_mAh) * 100.0,
        ctx->params.ventilationThreshold_mAh,
        ctx->params.socThresholdPercent,
        ctx->params.eisIntervalSlow_minutes,
        ctx->params.socThresholdPercent,
        ctx->params.eisIntervalFast_minutes,
        ctx->params.socThresholdPercent,
        ctx->params.pauseChargeDuringEIS ? "Yes" : "No",
        ctx->params.logIntervalSlow,
        ctx->params.socThresholdPercent,
        ctx->params.logIntervalFast,
        ctx->params.socThresholdPercent);

    int response = ConfirmPopup("Confirm Overcharge Experiment", message);
    if (!response || CheckCancellation(ctx)) {
        LogMessage("Overcharge experiment cancelled by user");
        ctx->state = OVERCHARGE_STATE_CANCELLED;
        goto cleanup;
    }

    // Create file system and initialize logging
    result = CreateExperimentFileSystem(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogError("Failed to create experiment file system");
        MessagePopup("Error", "Failed to create experiment directory.\nPlease check disk space and permissions.");
        ctx->state = OVERCHARGE_STATE_ERROR;
        goto cleanup;
    }

    SetExternalLogFile(ctx->experimentLogFile);

    // Save experiment settings for future reference
    SaveExperimentSettings(ctx);

    // Configure graphs and UI
    ConfigureOverchargeGraphs(ctx);

    // Initialize relay states (safety: both OFF)
    LogMessage("Initializing relay states...");
    if (ENABLE_TNY) {
        result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
        if (result != SUCCESS) {
            LogError("Failed to initialize PSB relay: %s", GetErrorString(result));
        }

        result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
        if (result != SUCCESS) {
            LogError("Failed to initialize BioLogic relay: %s", GetErrorString(result));
        }
    }

    // Check ventilation pre-start conditions (CRITICAL SAFETY CHECK)
    if (ENABLE_CDAQ) {
        LogMessage("Checking fume hood ventilation conditions...");
        double pressureVoltage;
        if (!PressureSafety_CheckStartConditions(&pressureVoltage)) {
            LogError("Cannot start experiment: Ventilation not adequate (%.2f V)", pressureVoltage);
            char ventMsg[512];
            snprintf(ventMsg, sizeof(ventMsg),
                    "Fume hood ventilation is not adequate.\n\n"
                    "Please ensure the extraction system is running\n"
                    "and pressure sensor is reading correctly.\n\n"
                    "Current pressure reading: %.2f V\n"
                    "Required minimum: %.2f V",
                    pressureVoltage, PRESSURE_THRESHOLD_OK_MIN);
            MessagePopup("Experiment Start Failed", ventMsg);
            ctx->state = OVERCHARGE_STATE_ERROR;
            goto cleanup;
        }
        LogMessage("Ventilation check PASSED (%.2f V) - safe to start", pressureVoltage);

        // Start pressure safety monitoring
        LogMessage("Starting pressure safety monitoring...");
        result = PressureSafety_StartMonitoring(25.0,  // Initial temp estimate
                                               OnVentilationLost,
                                               OnVentilationRestored);
        if (result != SUCCESS) {
            LogError("Failed to start pressure safety monitoring: %s", GetErrorString(result));
            MessagePopup("Warning",
                        "Failed to start pressure safety monitoring.\n"
                        "Experiment will continue without pressure monitoring.\n\n"
                        "DO NOT proceed unless you are certain ventilation is adequate!");
        } else {
            LogMessage("Pressure safety monitoring started successfully");
        }
    } else {
        LogWarning("cDAQ disabled - no ventilation monitoring");
        MessagePopup("Warning",
                    "Ventilation monitoring is DISABLED.\n\n"
                    "Ensure manual ventilation checks are performed!");
    }

    // Log experiment start event
    LogEvent(ctx, "EXPERIMENT_START",
             "Current=%.2fA Duration=%s",
             ctx->params.chargeCurrent,
             durationStr);

    // INITIAL EIS MEASUREMENT
    LogMessage("=== Performing Initial EIS Measurement ===");
    ctx->state = OVERCHARGE_STATE_INITIAL_EIS;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Performing initial EIS measurement...");
    SetCtrlVal(ctx->tabPanelHandle, ctx->modeControl, "INIT");

    result = PerformInitialEIS(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            LogError("Initial EIS measurement failed");
            ctx->state = OVERCHARGE_STATE_ERROR;
        }
        goto cleanup;
    }

    // MAIN CHARGING LOOP
    LogMessage("=== Starting Main Charging Loop ===");
    ctx->state = OVERCHARGE_STATE_CHARGING;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Charging...");

    result = RunChargingLoop(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = OVERCHARGE_STATE_ERROR;
        }
        goto cleanup;
    }

    // FINAL EIS MEASUREMENT (if safe)
    if (!ctx->ventilationLost) {
        LogMessage("=== Performing Final EIS Measurement ===");
        result = PerformFinalEIS(ctx);
        if (result != SUCCESS) {
            LogWarning("Final EIS measurement failed (non-fatal)");
        }
    } else {
        LogWarning("Skipping final EIS - ventilation lost");
    }

    // COOLING MONITORING
    LogMessage("=== Monitoring Cooling Phase ===");
    ctx->state = OVERCHARGE_STATE_COOLING;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Monitoring cooling...");

    MonitorCooling(ctx, OVERCHARGE_COOLING_DURATION);

    // Record experiment completion
    ctx->experimentEndTime = Timer();
    ctx->state = OVERCHARGE_STATE_COMPLETED;
    LogMessage("=== OVERCHARGE EXPERIMENT COMPLETED ===");
    LogMessage("Total experiment time: %.1f hours",
               (ctx->experimentEndTime - ctx->experimentStartTime) / 3600.0);

    // Write comprehensive results
    result = WriteFinalResults(ctx);
    if (result != SUCCESS) {
        LogError("Failed to write final results");
    }

cleanup:
    // Stop pressure monitoring
    if (ENABLE_CDAQ) {
        PressureSafety_StopMonitoring();
    }

    // Always perform cleanup
    CleanupExperiment(ctx);

    // Update final status
    const char *finalStatus;
    switch (ctx->state) {
        case OVERCHARGE_STATE_COMPLETED:
            finalStatus = "Overcharge experiment completed";
            break;
        case OVERCHARGE_STATE_CANCELLED:
            finalStatus = ctx->ventilationLost ?
                         "Overcharge experiment stopped - ventilation lost" :
                         "Overcharge experiment cancelled by user";
            break;
        case OVERCHARGE_STATE_ERROR:
            finalStatus = "Overcharge experiment failed";
            break;
        default:
            finalStatus = "Overcharge experiment ended unexpectedly";
            break;
    }

    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, finalStatus);
    SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, finalStatus);

    // Restore button text and UI
    SetCtrlAttribute(ctx->tabPanelHandle, ctx->buttonControl, ATTR_LABEL_TEXT, "Start");
    SetCtrlAttribute(ctx->tabPanelHandle, ctx->runawayButtonControl, ATTR_DIMMED, 1);
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, controls, numControls);

    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);

    // Clear thread ID
    g_experimentThreadId = 0;

    // Uninitialize COM for this thread
    CoUninitialize();

    return 0;
}

/******************************************************************************
 * Setup and Verification Functions
 ******************************************************************************/

static int VerifyAllDevices(OverchargeExperimentContext *ctx) {
    // Check PSB connection (REQUIRED)
    PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
    if (!psbQueueMgr) {
        MessagePopup("PSB Not Connected",
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running the overcharge experiment.");
        return ERR_NOT_CONNECTED;
    }

    ctx->psbHandle = PSB_QueueGetHandle(psbQueueMgr);
    if (!ctx->psbHandle || !ctx->psbHandle->isConnected) {
        MessagePopup("PSB Not Connected",
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running the overcharge experiment.");
        return ERR_NOT_CONNECTED;
    }

    // Check if BioLogic abstraction layer is initialized (works for both Direct DLL and EC-Lab modes)
    // NOTE: Do NOT check BioQueueManager - it's NULL in EC-Lab mode (uses OLE COM, not queue system)
    if (!BIO_IsAbstractInitialized()) {
        MessagePopup("BioLogic Not Connected",
                     "The BioLogic potentiostat is not initialized.\n\n"
                     "For Direct DLL mode: Connect device via USB.\n"
                     "For EC-Lab mode: Start EC-Lab and connect device.");
        return ERR_NOT_CONNECTED;
    }

    // Get current mode for logging
    BIO_ControlMode mode = BIO_GetCurrentMode();
    const char *modeName = (mode == BIO_MODE_ECLAB_OLECOM) ? "EC-Lab OLE COM" : "Direct DLL";
    LogMessage("BioLogic initialized in %s mode", modeName);

    // Note: biologicID is only used in Direct DLL mode, not needed for EC-Lab
    ctx->biologicID = 0;  // Not used with abstraction layer

    // Check DTB connection (REQUIRED for temperature monitoring)
    if (ENABLE_DTB) {
        DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
        if (!dtbQueueMgr) {
            MessagePopup("DTB Not Connected",
                         "The DTB temperature controller is REQUIRED for overcharge experiments.\n"
                         "Please ensure it is connected before running.");
            return ERR_NOT_CONNECTED;
        }
    } else {
        LogWarning("DTB temperature control disabled - experiment will run without DTB temp monitoring");
    }

    // Check Teensy connection (REQUIRED for relay control)
    TNYQueueManager *tnyQueueMgr = TNY_GetGlobalQueueManager();
    if (!tnyQueueMgr) {
        MessagePopup("Teensy Not Connected",
                     "The Teensy relay controller is not connected.\n"
                     "Please ensure it is connected before running the overcharge experiment.");
        return ERR_NOT_CONNECTED;
    }

    // Verify PSB is in safe state
    PSB_Status status;
    if (PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL) == PSB_SUCCESS) {
        if (status.outputEnabled) {
            MessagePopup("PSB Output Enabled",
                         "The PSB output must be disabled before starting the experiment.\n"
                         "Please turn off the output and try again.");
            return ERR_INVALID_STATE;
        }
    } else {
        MessagePopup("Communication Error",
                     "Failed to communicate with the PSB.\n"
                     "Please check the connection and try again.");
        return ERR_COMM_FAILED;
    }

    LogMessage("All required devices verified and ready");
    return SUCCESS;
}

static int CreateExperimentFileSystem(OverchargeExperimentContext *ctx) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    // Create timestamped experiment directory
    snprintf(ctx->experimentDirectory, sizeof(ctx->experimentDirectory),
             "%s%sexp_overcharge_%04d%02d%02d_%02d%02d%02d",
             OVERCHARGE_DATA_DIR, PATH_SEPARATOR,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    if (CreateDirectoryPath(OVERCHARGE_DATA_DIR) != SUCCESS) {
        LogError("Failed to create data directory: %s", OVERCHARGE_DATA_DIR);
        return ERR_BASE_FILE;
    }

    if (CreateDirectoryPath(ctx->experimentDirectory) != SUCCESS) {
        LogError("Failed to create experiment directory: %s", ctx->experimentDirectory);
        return ERR_BASE_FILE;
    }

    // Create EIS subdirectory
    char eisDir[MAX_PATH_LENGTH];
    snprintf(eisDir, sizeof(eisDir), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_EIS_DIR);
    if (CreateDirectoryPath(eisDir) != SUCCESS) {
        LogError("Failed to create EIS directory: %s", eisDir);
        return ERR_BASE_FILE;
    }

    // Open log files
    char filepath[MAX_PATH_LENGTH];

    snprintf(filepath, sizeof(filepath), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_LOG_FILE);
    ctx->experimentLogFile = fopen(filepath, "w");
    if (!ctx->experimentLogFile) {
        LogError("Failed to create experiment log file: %s", filepath);
        return ERR_BASE_FILE;
    }

    snprintf(filepath, sizeof(filepath), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_CHARGE_FILE);
    ctx->chargeLogFile = fopen(filepath, "w");
    if (!ctx->chargeLogFile) {
        LogError("Failed to create charge log file: %s", filepath);
        return ERR_BASE_FILE;
    }
    fprintf(ctx->chargeLogFile, "%s\n", OVERCHARGE_CHARGE_HEADER);
    fflush(ctx->chargeLogFile);

    snprintf(filepath, sizeof(filepath), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_TEMP_FILE);
    ctx->temperatureLogFile = fopen(filepath, "w");
    if (!ctx->temperatureLogFile) {
        LogError("Failed to create temperature log file: %s", filepath);
        return ERR_BASE_FILE;
    }
    fprintf(ctx->temperatureLogFile, "%s\n", OVERCHARGE_TEMP_HEADER);
    fflush(ctx->temperatureLogFile);

    if (ENABLE_ALICAT) {
        snprintf(filepath, sizeof(filepath), "%s%s%s",
                 ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_FLOW_FILE);
        ctx->gasFlowLogFile = fopen(filepath, "w");
        if (!ctx->gasFlowLogFile) {
            LogError("Failed to create gas flow log file: %s", filepath);
            return ERR_BASE_FILE;
        }
        fprintf(ctx->gasFlowLogFile, "%s\n", OVERCHARGE_FLOW_HEADER);
        fflush(ctx->gasFlowLogFile);
    }

    snprintf(filepath, sizeof(filepath), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_EVENT_FILE);
    ctx->eventLogFile = fopen(filepath, "w");
    if (!ctx->eventLogFile) {
        LogError("Failed to create event log file: %s", filepath);
        return ERR_BASE_FILE;
    }
    fprintf(ctx->eventLogFile, "%s\n", OVERCHARGE_EVENT_HEADER);
    fflush(ctx->eventLogFile);

    LogMessage("Experiment file system created: %s", ctx->experimentDirectory);
    return SUCCESS;
}

static int SaveExperimentSettings(OverchargeExperimentContext *ctx) {
    char filepath[MAX_PATH_LENGTH];
    snprintf(filepath, sizeof(filepath), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_SETTINGS_FILE);

    FILE *file = fopen(filepath, "w");
    if (!file) {
        LogError("Failed to create settings file: %s", filepath);
        return ERR_BASE_FILE;
    }

    fprintf(file, "[Experiment_Info]\n");
    fprintf(file, "Type=Overcharge\n");
    fprintf(file, "Directory=%s\n\n", ctx->experimentDirectory);

    fprintf(file, "[Battery_Configuration]\n");
    fprintf(file, "Nominal_Capacity_mAh=%.1f\n\n", ctx->params.nominalCapacity_mAh);

    fprintf(file, "[Charging_Parameters]\n");
    fprintf(file, "Charge_Current_A=%.2f\n", ctx->params.chargeCurrent);
    fprintf(file, "Charge_Duration_min=%.0f\n", ctx->params.chargeDurationMinutes);
    fprintf(file, "Duration_Unlimited=%d\n\n", ctx->params.chargeDurationMinutes == 0 ? 1 : 0);

    fprintf(file, "[Safety]\n");
    fprintf(file, "Ventilation_Threshold_Percent=%.1f\n",
            (ctx->params.ventilationThreshold_mAh / ctx->params.nominalCapacity_mAh) * 100.0);
    fprintf(file, "Ventilation_Threshold_mAh=%.1f\n\n", ctx->params.ventilationThreshold_mAh);

    fprintf(file, "[Adaptive_Mode]\n");
    fprintf(file, "SOC_Threshold_Percent=%.1f\n\n", ctx->params.socThresholdPercent);

    fprintf(file, "[EIS_Configuration]\n");
    fprintf(file, "EIS_Interval_Slow_min=%.1f\n", ctx->params.eisIntervalSlow_minutes);
    fprintf(file, "EIS_Interval_Fast_min=%.1f\n", ctx->params.eisIntervalFast_minutes);
    fprintf(file, "Pause_Charging_During_EIS=%d\n\n", ctx->params.pauseChargeDuringEIS);

    fprintf(file, "[Logging]\n");
    fprintf(file, "Log_Interval_Slow_sec=%.1f\n", ctx->params.logIntervalSlow);
    fprintf(file, "Log_Interval_Fast_sec=%.1f\n\n", ctx->params.logIntervalFast);

    fprintf(file, "[Device_Configuration]\n");
    fprintf(file, "PSB_Enabled=%d\n", ENABLE_PSB);
    fprintf(file, "BioLogic_Enabled=%d\n", ENABLE_BIOLOGIC);
    fprintf(file, "DTB_Enabled=%d\n", ENABLE_DTB);
    fprintf(file, "ALICAT_Enabled=%d\n", ENABLE_ALICAT);
    fprintf(file, "Teensy_Enabled=%d\n", ENABLE_TNY);
    fprintf(file, "cDAQ_Enabled=%d\n", ENABLE_CDAQ);

    fclose(file);
    LogMessage("Experiment settings saved");
    return SUCCESS;
}

/******************************************************************************
 * Device Control Functions
 ******************************************************************************/

static int SwitchToPSB(OverchargeExperimentContext *ctx)
{
    int result;

    LogMessage("=== DIAGNOSTIC: SwitchToPSB() called ===");

    // Safety: Disable BioLogic and PSB outputs first
    if (ENABLE_BIOLOGIC) {
        LogMessage("DIAGNOSTIC: Stopping BioLogic channel...");
        BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_NORMAL);
    }
    if (ENABLE_PSB) {
        LogMessage("DIAGNOSTIC: Disabling PSB output...");
        PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    }
    Delay(0.5);

    // Switch relays: Disconnect BioLogic, then Connect PSB
    if (ENABLE_TNY) {
        // Disconnect BioLogic relay first
        LogMessage("DIAGNOSTIC: Disconnecting BioLogic relay (pin %d)...", TNY_BIOLOGIC_PIN);
        result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
        if (result != SUCCESS) {
            LogError("DIAGNOSTIC: Failed to disconnect BioLogic relay: %s", GetErrorString(result));
            return result;
        }
        LogMessage("DIAGNOSTIC: BioLogic relay disconnected successfully");

        Delay(TNY_SWITCH_DELAY_MS / 1000.0);

        // Connect PSB relay
        LogMessage("DIAGNOSTIC: Connecting PSB relay (pin %d) to STATE_CONNECTED (%d)...",
                   TNY_PSB_PIN, TNY_STATE_CONNECTED);
        result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_CONNECTED, DEVICE_PRIORITY_NORMAL);
        if (result != SUCCESS) {
            LogError("DIAGNOSTIC: Failed to connect PSB relay: %s", GetErrorString(result));
            return result;
        }
        LogMessage("DIAGNOSTIC: PSB relay connected successfully");

        Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    } else {
        LogWarning("DIAGNOSTIC: ENABLE_TNY is 0 - relays not being controlled!");
    }

    LogMessage("=== DIAGNOSTIC: SwitchToPSB() completed successfully ===");
    return SUCCESS;
}

static int SwitchToBioLogic(OverchargeExperimentContext *ctx)
{
    int result;

    LogMessage("Switching to Bio-Logic (EIS mode)...");

    // Safety: Disable PSB output first
    if (ENABLE_PSB) {
        PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    }
    Delay(0.5);

    // Switch relays: Disconnect PSB, then Connect BioLogic
    if (ENABLE_TNY) {
        // Disconnect PSB relay first
        result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
        if (result != SUCCESS) {
            LogError("Failed to disconnect PSB relay: %s", GetErrorString(result));
            return result;
        }

        Delay(TNY_SWITCH_DELAY_MS / 1000.0);

        // Connect BioLogic relay
        result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_CONNECTED, DEVICE_PRIORITY_NORMAL);
        if (result != SUCCESS) {
            LogError("Failed to connect BioLogic relay: %s", GetErrorString(result));
            return result;
        }

        Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    }

    LogMessage("Successfully switched to BioLogic");
    return SUCCESS;
}

static int SafeDisconnectAllDevices(OverchargeExperimentContext *ctx)
{
    LogMessage("Safely disconnecting all devices...");

    // Disable PSB output
    if (ENABLE_PSB) {
        PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_HIGH);
    }

    // Stop Bio-Logic channel
    if (ENABLE_BIOLOGIC) {
        BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_HIGH);
    }

    // Open all relays
    if (ENABLE_TNY) {
        TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_HIGH);
        TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_HIGH);
    }

    Delay(0.5);
    LogMessage("All devices safely disconnected");
    return SUCCESS;
}

/******************************************************************************
 * Main Charging Loop
 ******************************************************************************/

static int RunChargingLoop(OverchargeExperimentContext *ctx)
{
    int result;
    PSB_Status psbStatus;
    double loopStartTime;
    double nextEISTime;
    double now;
    int eisCount = 0;

    LogMessage("Starting main charging loop...");
    LogEvent(ctx, "Charging_Started", "Beginning constant-current overcharge");

    // Switch to PSB
    result = SwitchToPSB(ctx);
    if (result != SUCCESS) {
        LogError("Failed to switch to PSB: %s", GetErrorString(result));
        return result;
    }

    // Configure PSB for constant-current charging
    LogMessage("=== DIAGNOSTIC: Configuring PSB for constant-current mode ===");
    LogMessage("DIAGNOSTIC: Target current: %.2f A", ctx->params.chargeCurrent);
    LogMessage("DIAGNOSTIC: Max voltage: %.2f V", PSB_NOMINAL_VOLTAGE);

    LogMessage("DIAGNOSTIC: Setting PSB current to %.2f A...", ctx->params.chargeCurrent);
    result = PSB_SetCurrentQueued(ctx->params.chargeCurrent, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("DIAGNOSTIC: Failed to set PSB current: %s (error code: %d)", GetErrorString(result), result);
        return result;
    }
    LogMessage("DIAGNOSTIC: PSB current set successfully");

    LogMessage("DIAGNOSTIC: Setting PSB voltage to %.2f V...", PSB_NOMINAL_VOLTAGE);
    result = PSB_SetVoltageQueued(PSB_NOMINAL_VOLTAGE, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("DIAGNOSTIC: Failed to set PSB voltage: %s (error code: %d)", GetErrorString(result), result);
        return result;
    }
    LogMessage("DIAGNOSTIC: PSB voltage set successfully");

    LogMessage("DIAGNOSTIC: Setting PSB current limits (0.0 to %.2f A)...", ctx->params.chargeCurrent * 1.1);
    result = PSB_SetCurrentLimitsQueued(0.0, ctx->params.chargeCurrent * 1.1, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("DIAGNOSTIC: Failed to set PSB current limits: %s (error code: %d)", GetErrorString(result), result);
        return result;
    }
    LogMessage("DIAGNOSTIC: PSB current limits set successfully");

    // Enable PSB output
    LogMessage("=== DIAGNOSTIC: Enabling PSB output ===");
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("DIAGNOSTIC: Failed to enable PSB output: %s (error code: %d)", GetErrorString(result), result);
        return result;
    }
    LogMessage("DIAGNOSTIC: PSB output enabled successfully");

    Delay(1.0);  // Allow PSB to stabilize

    // Verify PSB is actually outputting
    LogMessage("=== DIAGNOSTIC: Verifying PSB status after enable ===");
    result = PSB_GetStatusQueued(&psbStatus, DEVICE_PRIORITY_NORMAL);
    if (result == SUCCESS) {
        LogMessage("DIAGNOSTIC: PSB Status after enable:");
        LogMessage("  Voltage: %.3f V", psbStatus.voltage);
        LogMessage("  Current: %.3f A", psbStatus.current);
        LogMessage("  Power: %.3f W", psbStatus.power);
        LogMessage("  Output Enabled: %d", psbStatus.outputEnabled);

        if (fabs(psbStatus.current) < 0.01) {
            LogWarning("DIAGNOSTIC: WARNING - PSB current is near zero! Battery may not be connected!");
        }
        if (!psbStatus.outputEnabled) {
            LogError("DIAGNOSTIC: ERROR - PSB output enabled is 0! Output is not actually enabled!");
        }
    } else {
        LogError("DIAGNOSTIC: Failed to read initial PSB status: %s", GetErrorString(result));
    }

    // Initialize timing
    loopStartTime = Timer();
    ctx->chargeStartTime = loopStartTime;
    ctx->lastLogTime = loopStartTime;
    ctx->lastGraphUpdate = loopStartTime;

    // Schedule first EIS measurement based on current mode
    double eisInterval = GetCurrentEISInterval(ctx);
    nextEISTime = loopStartTime + (eisInterval * 60.0);

    LogMessage("Charging loop started. EIS interval: %.1f minutes", eisInterval);

    // Main charging loop
    int loopCount = 0;
    while (1) {
        now = Timer();
        loopCount++;

        // Check for cancellation
        if (CheckCancellation(ctx)) {
            LogMessage("Charging loop cancelled");
            break;
        }

        // Read PSB status
        result = PSB_GetStatusQueued(&psbStatus, DEVICE_PRIORITY_NORMAL);
        if (result != SUCCESS) {
            LogError("Failed to read PSB status: %s", GetErrorString(result));
            ctx->state = OVERCHARGE_STATE_ERROR;
            break;
        }

        // DIAGNOSTIC: Log detailed PSB status every 10 loops for first minute
        if (loopCount <= 600 && loopCount % 10 == 0) {
            LogMessage("DIAGNOSTIC [Loop %d]: V=%.3f V, I=%.3f A, P=%.3f W, Out=%d",
                      loopCount, psbStatus.voltage, psbStatus.current,
                      psbStatus.power, psbStatus.outputEnabled);
        }

        // Update current readings
        ctx->currentVoltage = psbStatus.voltage;
        ctx->currentCurrent = psbStatus.current;

        // Read temperatures
        result = ReadAllTemperatures(ctx);
        if (result != SUCCESS) {
            LogWarning("Failed to read temperatures: %s", GetErrorString(result));
        }

        // Update charge tracking (coulomb counting)
        UpdateChargeTracking(ctx, now);

        // Update adaptive mode (check SOC threshold)
        UpdateAdaptiveMode(ctx);

        // Check ventilation status (SAFE vs CRITICAL logic)
        if (ctx->ventilationLost) {
            double chargeThreshold = ctx->params.ventilationThreshold_mAh;
            int isCriticalPhase = (ctx->accumulatedCharge_mAh >= chargeThreshold);

            if (isCriticalPhase) {
                LogError("CRITICAL PHASE: Ventilation has been lost after %.1f mAh threshold!",
                        chargeThreshold);
                LogError("Alarm has been sounded - PERSONNEL SHOULD RE-ESTABLISH VENTILATION OR EVACUATE");
                LogEvent(ctx, "Ventilation_Lost_Critical",
                        "Ventilation lost in critical phase - stopping with alarm");
            } else {
                LogError("SAFE PHASE: Ventilation lost before critical threshold - stopping safely");
                LogEvent(ctx, "Ventilation_Lost_Safe",
                        "Ventilation lost in safe phase - stopping");
            }

            ctx->state = OVERCHARGE_STATE_CANCELLED;
            break;
        }

        // Check duration limit (if specified)
        if (ctx->params.chargeDurationMinutes > 0) {
            double elapsedMinutes = (now - loopStartTime) / 60.0;
            if (elapsedMinutes >= ctx->params.chargeDurationMinutes) {
                LogMessage("Duration limit reached: %.1f minutes", elapsedMinutes);
                LogEvent(ctx, "Duration_Limit_Reached", "Max charge duration reached");
                ctx->state = OVERCHARGE_STATE_COMPLETED;
                break;
            }
        }

        // Data logging at adaptive rate
        double logInterval = GetCurrentLogInterval(ctx);
        if ((now - ctx->lastLogTime) >= logInterval) {
            LogChargeDataPoint(ctx, now - loopStartTime);
            LogTemperatureDataPoint(ctx, now - loopStartTime);
            if (ENABLE_ALICAT) {
                LogGasFlowDataPoint(ctx);
            }
            ctx->lastLogTime = now;
        }

        // Update graphs at 2 Hz
        if ((now - ctx->lastGraphUpdate) >= 0.5) {
            UpdateGraphsRealtime(ctx, now - loopStartTime);
            ctx->lastGraphUpdate = now;
        }

        // Check if EIS measurement is due
        if (now >= nextEISTime) {
            LogMessage("Periodic EIS measurement #%d due at %.1f minutes",
                      eisCount + 1, (now - loopStartTime) / 60.0);

            // Pause charging if configured
            if (ctx->params.pauseChargeDuringEIS) {
                LogMessage("Pausing charging for EIS measurement...");
                PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
                Delay(2.0);  // Allow transients to settle
            }

            // Perform EIS measurement
            result = PerformPeriodicEIS(ctx, eisCount);
            if (result == SUCCESS) {
                eisCount++;
                LogMessage("Periodic EIS #%d completed successfully", eisCount);
            } else {
                LogError("Periodic EIS #%d failed: %s", eisCount, GetErrorString(result));
            }

            // Resume charging if paused
            if (ctx->params.pauseChargeDuringEIS) {
                LogMessage("Resuming charging...");
                result = SwitchToPSB(ctx);
                if (result != SUCCESS) {
                    LogError("Failed to switch back to PSB: %s", GetErrorString(result));
                    ctx->state = OVERCHARGE_STATE_ERROR;
                    break;
                }

                PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
                Delay(1.0);
            }

            // Schedule next EIS (use current interval in case mode changed)
            eisInterval = GetCurrentEISInterval(ctx);
            nextEISTime = now + (eisInterval * 60.0);
            LogMessage("Next EIS scheduled in %.1f minutes", eisInterval);
        }

        // Check for "Runaway Reached" button press
        if (ctx->runawayReached && ctx->runawayReachedTime == 0) {
            ctx->runawayReachedTime = now;
            LogWarning("USER TRIGGERED: Thermal runaway reached at %.1f minutes",
                      (now - loopStartTime) / 60.0);
            LogEvent(ctx, "Runaway_Reached", "User indicated thermal runaway has occurred");

            // Add marker to graphs
            AddRunawayMarker(ctx, now - loopStartTime);

            // Continue charging (don't stop - user decides when to stop)
            LogMessage("Continuing charging after runaway indication...");
        }

        // Loop delay
        Delay(0.1);
    }

    // Disable PSB output
    LogMessage("Disabling PSB output...");
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_HIGH);
    Delay(0.5);

    LogMessage("Charging loop completed");
    return SUCCESS;
}

/******************************************************************************
 * EIS Measurement Functions
 ******************************************************************************/

static int PerformInitialEIS(OverchargeExperimentContext *ctx)
{
    int result;
    OverchargeEISMeasurement *measurement;

    LogMessage("Performing initial baseline EIS measurement...");
    LogEvent(ctx, "Initial_EIS_Started", "Baseline EIS before charging");

    // Allocate measurement structure
    if (ctx->eisMeasurementCount >= ctx->eisMeasurementCapacity) {
        ctx->eisMeasurementCapacity = (ctx->eisMeasurementCapacity == 0) ? 10 : ctx->eisMeasurementCapacity * 2;
        ctx->eisMeasurements = realloc(ctx->eisMeasurements,
                                      ctx->eisMeasurementCapacity * sizeof(OverchargeEISMeasurement));
        if (!ctx->eisMeasurements) {
            LogError("Failed to allocate memory for EIS measurements");
            return ERR_OUT_OF_MEMORY;
        }
    }

    measurement = &ctx->eisMeasurements[ctx->eisMeasurementCount];
    memset(measurement, 0, sizeof(OverchargeEISMeasurement));

    measurement->measurementIndex = ctx->eisMeasurementCount;
    measurement->chargeDelivered_mAh = 0.0;
    measurement->socPercent = 0.0;
    measurement->timestamp = 0.0;

    // Read temperatures
    ReadAllTemperatures(ctx);
    measurement->tempData = ctx->currentTempData;

    // Perform EIS
    result = PerformEISMeasurement(ctx, measurement);
    if (result != SUCCESS) {
        LogError("Initial EIS measurement failed: %s", GetErrorString(result));
        return result;
    }

    ctx->eisMeasurementCount++;
    LogMessage("Initial EIS measurement completed successfully");
    LogEvent(ctx, "Initial_EIS_Completed", "Baseline EIS successful");

    return SUCCESS;
}

static int PerformPeriodicEIS(OverchargeExperimentContext *ctx, int measurementIndex)
{
    int result;
    OverchargeEISMeasurement *measurement;
    double timestamp = Timer() - ctx->experimentStartTime;

    LogMessage("Performing periodic EIS measurement #%d...", measurementIndex + 1);

    // Allocate measurement structure
    if (ctx->eisMeasurementCount >= ctx->eisMeasurementCapacity) {
        ctx->eisMeasurementCapacity *= 2;
        ctx->eisMeasurements = realloc(ctx->eisMeasurements,
                                      ctx->eisMeasurementCapacity * sizeof(OverchargeEISMeasurement));
        if (!ctx->eisMeasurements) {
            LogError("Failed to allocate memory for EIS measurements");
            return ERR_OUT_OF_MEMORY;
        }
    }

    measurement = &ctx->eisMeasurements[ctx->eisMeasurementCount];
    memset(measurement, 0, sizeof(OverchargeEISMeasurement));

    measurement->measurementIndex = ctx->eisMeasurementCount;
    measurement->chargeDelivered_mAh = ctx->accumulatedCharge_mAh;
    measurement->socPercent = ctx->currentSOC_percent;
    measurement->timestamp = timestamp;

    // Read temperatures
    ReadAllTemperatures(ctx);
    measurement->tempData = ctx->currentTempData;

    // Perform EIS
    result = PerformEISMeasurement(ctx, measurement);
    if (result != SUCCESS) {
        LogError("Periodic EIS #%d failed: %s", measurementIndex + 1, GetErrorString(result));
        return result;
    }

    ctx->eisMeasurementCount++;
    LogMessage("Periodic EIS #%d completed", measurementIndex + 1);

    // Update Nyquist plot
    UpdateNyquistPlot(ctx, measurement);

    return SUCCESS;
}

static int PerformFinalEIS(OverchargeExperimentContext *ctx)
{
    int result;
    OverchargeEISMeasurement *measurement;
    double timestamp = Timer() - ctx->experimentStartTime;

    LogMessage("Performing final EIS measurement...");
    LogEvent(ctx, "Final_EIS_Started", "Final EIS after charging complete");

    // Allocate measurement structure
    if (ctx->eisMeasurementCount >= ctx->eisMeasurementCapacity) {
        ctx->eisMeasurementCapacity *= 2;
        ctx->eisMeasurements = realloc(ctx->eisMeasurements,
                                      ctx->eisMeasurementCapacity * sizeof(OverchargeEISMeasurement));
        if (!ctx->eisMeasurements) {
            LogError("Failed to allocate memory for EIS measurements");
            return ERR_OUT_OF_MEMORY;
        }
    }

    measurement = &ctx->eisMeasurements[ctx->eisMeasurementCount];
    memset(measurement, 0, sizeof(OverchargeEISMeasurement));

    measurement->measurementIndex = ctx->eisMeasurementCount;
    measurement->chargeDelivered_mAh = ctx->accumulatedCharge_mAh;
    measurement->socPercent = ctx->currentSOC_percent;
    measurement->timestamp = timestamp;

    // Read temperatures
    ReadAllTemperatures(ctx);
    measurement->tempData = ctx->currentTempData;

    // Perform EIS
    result = PerformEISMeasurement(ctx, measurement);
    if (result != SUCCESS) {
        LogError("Final EIS measurement failed: %s", GetErrorString(result));
        return result;
    }

    ctx->eisMeasurementCount++;
    LogMessage("Final EIS measurement completed successfully");
    LogEvent(ctx, "Final_EIS_Completed", "Final EIS successful");

    // Update Nyquist plot
    UpdateNyquistPlot(ctx, measurement);

    return SUCCESS;
}

static int PerformEISMeasurement(OverchargeExperimentContext *ctx, OverchargeEISMeasurement *measurement)
{
    int result;
    int retryCount = 0;
    BIO_TechniqueData *ocvData = NULL;
    BIO_TechniqueData *geisData = NULL;

    // Switch to Bio-Logic
    result = SwitchToBioLogic(ctx);
    if (result != SUCCESS) {
        LogError("Failed to switch to Bio-Logic for EIS: %s", GetErrorString(result));
        return result;
    }

    // Retry loop for EIS measurement
    while (retryCount <= OVERCHARGE_MAX_EIS_RETRY) {
        if (retryCount > 0) {
            LogMessage("Retrying EIS measurement (attempt %d/%d)...",
                      retryCount + 1, OVERCHARGE_MAX_EIS_RETRY + 1);
            Delay(OVERCHARGE_EIS_RETRY_DELAY);
        }

        // Perform OCV measurement
        LogMessage("Measuring open-circuit voltage...");
        result = BIO_Abstract_RunOCV(0,  // channel
                                    OCV_DURATION_S,
                                    OCV_SAMPLE_INTERVAL_S,
                                    OCV_RECORD_EVERY_DE,
                                    OCV_RECORD_EVERY_DT,
                                    OCV_E_RANGE,
                                    &ocvData,
                                    OCV_TIMEOUT_MS,
                                    NULL,  // progressCallback
                                    NULL,  // userData
                                    &(ctx->cancelRequested));
        if (result != SUCCESS) {
            LogError("OCV measurement failed: %s", GetErrorString(result));
            retryCount++;
            continue;
        }

        // Extract OCV voltage
        if (ocvData && ocvData->convertedData) {
            BIO_ConvertedData *convData = ocvData->convertedData;
            if (convData->numPoints > 0 && convData->numVariables >= 2 && convData->data[1] != NULL) {
                int lastPoint = convData->numPoints - 1;
                measurement->ocvVoltage = convData->data[1][lastPoint];
                LogMessage("OCV: %.4f V", measurement->ocvVoltage);
            }
        }

        // Perform GEIS measurement
        LogMessage("Performing GEIS measurement...");
        result = BIO_Abstract_RunGEIS(0,  // channel
                                     GEIS_VS_INITIAL,
                                     GEIS_INITIAL_CURRENT,
                                     GEIS_DURATION_S,
                                     GEIS_RECORD_EVERY_DT,
                                     GEIS_RECORD_EVERY_DE,
                                     GEIS_INITIAL_FREQ,
                                     GEIS_FINAL_FREQ,
                                     GEIS_SWEEP_LINEAR,
                                     GEIS_AMPLITUDE_I,
                                     GEIS_FREQ_NUMBER,
                                     GEIS_AVERAGE_N,
                                     GEIS_CORRECTION,
                                     GEIS_WAIT_FOR_STEADY,
                                     GEIS_I_RANGE,
                                     &geisData,
                                     GEIS_TIMEOUT_MS,
                                     NULL,  // progressCallback
                                     NULL,  // userData
                                     &(ctx->cancelRequested));
        if (result != SUCCESS) {
            LogError("GEIS measurement failed: %s", GetErrorString(result));
            if (ocvData) BIO_FreeTechniqueData(ocvData);
            retryCount++;
            continue;
        }

        // Process GEIS data
        result = ProcessGEISData(ctx, measurement, geisData);
        if (result != SUCCESS) {
            LogError("Failed to process GEIS data: %s", GetErrorString(result));
            if (ocvData) BIO_FreeTechniqueData(ocvData);
            if (geisData) BIO_FreeTechniqueData(geisData);
            retryCount++;
            continue;
        }

        // Success!
        measurement->ocvData = ocvData;
        measurement->geisData = geisData;
        measurement->retryCount = retryCount;

        // Save to file
        SaveEISMeasurement(ctx, measurement);

        LogMessage("EIS measurement successful (Z at 1 kHz: %.4f + %.4fj Ohm)",
                  measurement->zReal[0], measurement->zImag[0]);

        return SUCCESS;
    }

    LogError("EIS measurement failed after %d retries", retryCount);
    return ERR_OPERATION_FAILED;
}

static int ProcessGEISData(OverchargeExperimentContext *ctx,
                          OverchargeEISMeasurement *measurement,
                          BIO_TechniqueData *geisData)
{
    int i;
    int numPoints;
    BIO_ConvertedData *convData;
    int processIndex = -1;

    if (!geisData || !geisData->convertedData) {
        LogError("No GEIS data to process");
        return ERR_INVALID_PARAMETER;
    }

    convData = geisData->convertedData;
    numPoints = convData->numPoints;

    if (geisData->rawData) {
        processIndex = geisData->rawData->processIndex;
    }

    LogMessage("Processing GEIS: %d points, %d variables (process %d)",
              convData->numPoints, convData->numVariables, processIndex);

    // Allocate impedance arrays
    measurement->frequencies = malloc(numPoints * sizeof(double));
    measurement->zReal = malloc(numPoints * sizeof(double));
    measurement->zImag = malloc(numPoints * sizeof(double));

    if (!measurement->frequencies || !measurement->zReal || !measurement->zImag) {
        LogError("Failed to allocate memory for impedance data");
        if (measurement->frequencies) free(measurement->frequencies);
        if (measurement->zReal) free(measurement->zReal);
        if (measurement->zImag) free(measurement->zImag);
        measurement->frequencies = NULL;
        measurement->zReal = NULL;
        measurement->zImag = NULL;
        return ERR_OUT_OF_MEMORY;
    }

    // Handle both Direct DLL and EC-Lab formats
    if (processIndex == 1 && convData->numVariables >= 11) {
        // Direct DLL format: freq at [0], Re(Z) at [4], -Im(Z) at [5]
        LogMessage("Using Direct DLL format (process 1, %d variables)", convData->numVariables);
        for (i = 0; i < numPoints; i++) {
            measurement->frequencies[i] = convData->data[0][i];
            measurement->zReal[i] = convData->data[4][i];
            measurement->zImag[i] = convData->data[5][i];
        }
    } else if (convData->numVariables == 4) {
        // EC-Lab format: time at [0], freq at [1], Re(Z) at [2], -Im(Z) at [3]
        LogMessage("Using EC-Lab format (4 variables)");
        for (i = 0; i < numPoints; i++) {
            measurement->frequencies[i] = convData->data[1][i];
            measurement->zReal[i] = convData->data[2][i];
            measurement->zImag[i] = convData->data[3][i];
        }
    } else {
        LogWarning("Unexpected GEIS format: process %d, %d variables",
                  processIndex, convData->numVariables);
        free(measurement->frequencies);
        free(measurement->zReal);
        free(measurement->zImag);
        measurement->frequencies = NULL;
        measurement->zReal = NULL;
        measurement->zImag = NULL;
        return ERR_OPERATION_FAILED;
    }

    measurement->numPoints = numPoints;
    LogMessage("Extracted %d impedance points (%.2f Hz to %.2f Hz)",
              numPoints, measurement->frequencies[0], measurement->frequencies[numPoints - 1]);

    return SUCCESS;
}

static int SaveEISMeasurement(OverchargeExperimentContext *ctx, OverchargeEISMeasurement *measurement)
{
    char filepath[MAX_PATH_LENGTH];
    char eisDir[MAX_PATH_LENGTH];
    FILE *file;
    int i;

    // Create EIS directory if needed
    snprintf(eisDir, sizeof(eisDir), "%s%s%s", ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_EIS_DIR);
    if (CreateDirectoryPath(eisDir) != SUCCESS) {
        LogError("Failed to create EIS directory: %s", eisDir);
        return ERR_BASE_FILE;
    }

    // Generate filename
    snprintf(measurement->filename, sizeof(measurement->filename),
            "eis_%03d_%.1fmAh_%.1fpct.csv",
            measurement->measurementIndex,
            measurement->chargeDelivered_mAh,
            measurement->socPercent);

    snprintf(filepath, sizeof(filepath), "%s%s%s", eisDir, PATH_SEPARATOR, measurement->filename);

    // Write CSV file
    file = fopen(filepath, "w");
    if (!file) {
        LogError("Failed to create EIS file: %s", filepath);
        return ERR_BASE_FILE;
    }

    // Header
    fprintf(file, "# Overcharge EIS Measurement #%d\n", measurement->measurementIndex);
    fprintf(file, "# Timestamp: %.2f s\n", measurement->timestamp);
    fprintf(file, "# Charge Delivered: %.2f mAh\n", measurement->chargeDelivered_mAh);
    fprintf(file, "# SOC: %.1f %%\n", measurement->socPercent);
    fprintf(file, "# OCV: %.4f V\n", measurement->ocvVoltage);
    fprintf(file, "# Temperature (DTB avg): %.2f C\n", measurement->tempData.dtbAverageTemperature);
    fprintf(file, "# Temperature (TC0): %.2f C\n", measurement->tempData.tc0Temperature);
    fprintf(file, "# Temperature (TC1): %.2f C\n", measurement->tempData.tc1Temperature);
    fprintf(file, "# Retries: %d\n", measurement->retryCount);
    fprintf(file, "#\n");
    fprintf(file, "Frequency_Hz,Z_Real_Ohm,Z_Imag_Ohm,Z_Magnitude_Ohm,Z_Phase_deg\n");

    // Data points
    for (i = 0; i < measurement->numPoints; i++) {
        double mag = sqrt(measurement->zReal[i] * measurement->zReal[i] +
                         measurement->zImag[i] * measurement->zImag[i]);
        double phase = atan2(measurement->zImag[i], measurement->zReal[i]) * 180.0 / M_PI;

        fprintf(file, "%.6e,%.6e,%.6e,%.6e,%.6e\n",
               measurement->frequencies[i],
               measurement->zReal[i],
               measurement->zImag[i],
               mag,
               phase);
    }

    fclose(file);
    LogMessage("Saved EIS measurement to: %s", measurement->filename);

    return SUCCESS;
}

/******************************************************************************
 * Charge Tracking Functions
 ******************************************************************************/

static void UpdateChargeTracking(OverchargeExperimentContext *ctx, double currentTime)
{
    double dt;
    double avgCurrent;
    double charge_mAs;

    if (ctx->lastTime == 0) {
        // First call - initialize
        ctx->lastTime = currentTime;
        ctx->lastCurrent = ctx->currentCurrent;
        return;
    }

    // Calculate time delta
    dt = currentTime - ctx->lastTime;
    if (dt <= 0) return;

    // Trapezoidal integration: Q = (I1 + I2) / 2 * dt
    avgCurrent = (ctx->lastCurrent + ctx->currentCurrent) / 2.0;
    charge_mAs = avgCurrent * dt * 1000.0;  // Convert A*s to mA*s

    // Accumulate charge (convert mA*s to mAh)
    ctx->accumulatedCharge_mAh += charge_mAs / 3600.0;

    // Calculate SOC if nominal capacity provided
    if (ctx->params.nominalCapacity_mAh > 0) {
        ctx->currentSOC_percent = (ctx->accumulatedCharge_mAh / ctx->params.nominalCapacity_mAh) * 100.0;
    }

    // Update for next iteration
    ctx->lastTime = currentTime;
    ctx->lastCurrent = ctx->currentCurrent;
}

static double CalculateSOCPercent(OverchargeExperimentContext *ctx)
{
    if (ctx->params.nominalCapacity_mAh > 0) {
        return (ctx->accumulatedCharge_mAh / ctx->params.nominalCapacity_mAh) * 100.0;
    }
    return 0.0;
}

/******************************************************************************
 * Adaptive Mode Functions
 ******************************************************************************/

static void UpdateAdaptiveMode(OverchargeExperimentContext *ctx)
{
    int wasInFastMode = ctx->inFastMode;

    // Check SOC threshold (only if nominal capacity provided)
    if (ctx->params.nominalCapacity_mAh > 0) {
        if (ctx->currentSOC_percent >= ctx->params.socThresholdPercent) {
            ctx->inFastMode = 1;
        } else {
            ctx->inFastMode = 0;
        }
    } else {
        // No capacity provided - stay in slow mode
        ctx->inFastMode = 0;
    }

    // Log transition
    if (ctx->inFastMode && !wasInFastMode) {
        double now = Timer();
        ctx->modeTransitionTime = now;
        LogMessage("ADAPTIVE MODE: Switched to FAST mode at SOC=%.1f%%", ctx->currentSOC_percent);
        LogEvent(ctx, "Mode_Transition_Fast", "Switched to fast EIS/logging intervals");

        // Update UI
        SetCtrlVal(ctx->mainPanelHandle, ctx->modeControl, "FAST");
    } else if (!ctx->inFastMode && wasInFastMode) {
        LogMessage("ADAPTIVE MODE: Switched to SLOW mode at SOC=%.1f%%", ctx->currentSOC_percent);
        LogEvent(ctx, "Mode_Transition_Slow", "Switched to slow EIS/logging intervals");

        // Update UI
        SetCtrlVal(ctx->mainPanelHandle, ctx->modeControl, "SLOW");
    }
}

static double GetCurrentEISInterval(OverchargeExperimentContext *ctx)
{
    if (ctx->inFastMode) {
        return ctx->params.eisIntervalFast_minutes;
    } else {
        return ctx->params.eisIntervalSlow_minutes;
    }
}

static double GetCurrentLogInterval(OverchargeExperimentContext *ctx)
{
    if (ctx->inFastMode) {
        return (double)ctx->params.logIntervalFast;
    } else {
        return (double)ctx->params.logIntervalSlow;
    }
}

static const char* GetModeString(int inFastMode)
{
    return inFastMode ? "FAST" : "SLOW";
}

/******************************************************************************
 * Temperature Reading Function
 ******************************************************************************/

static int ReadAllTemperatures(OverchargeExperimentContext *ctx)
{
    int result = SUCCESS;
    OverchargeTempData *tempData = &ctx->currentTempData;
    memset(tempData, 0, sizeof(OverchargeTempData));

    // Read DTB temperatures if enabled
    if (ENABLE_DTB) {
        DTB_Status dtbStatus[DTB_NUM_DEVICES];
        int i;

        tempData->dtbDeviceCount = DTB_NUM_DEVICES;

        for (i = 0; i < DTB_NUM_DEVICES; i++) {
            int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
            result = DTB_GetStatusQueued(slaveAddress, &dtbStatus[i], DEVICE_PRIORITY_LOW);
            if (result == SUCCESS) {
                tempData->dtbTemperatures[i] = dtbStatus[i].processValue;
                if (i == 0) {
                    tempData->dtbSetpoint = dtbStatus[i].setPoint;
                }
            } else {
                LogWarning("Failed to read DTB %d temperature", i + 1);
                tempData->dtbTemperatures[i] = 0.0;
            }
        }

        // Calculate average DTB temperature
        double sum = 0.0;
        int count = 0;
        for (i = 0; i < DTB_NUM_DEVICES; i++) {
            if (tempData->dtbTemperatures[i] > 0) {
                sum += tempData->dtbTemperatures[i];
                count++;
            }
        }
        if (count > 0) {
            tempData->dtbAverageTemperature = sum / count;
            ctx->currentTemperature = tempData->dtbAverageTemperature;
        }
    }

    // Read cDAQ thermocouples if enabled
    if (ENABLE_CDAQ) {
        CDAQ_ReadTC(2, 0, &tempData->tc0Temperature);
        CDAQ_ReadTC(2, 1, &tempData->tc1Temperature);
    }

    return SUCCESS;
}

/******************************************************************************
 * Data Logging Functions
 ******************************************************************************/

static int LogChargeDataPoint(OverchargeExperimentContext *ctx, double timestamp)
{
    if (!ctx->chargeLogFile) return ERR_BASE_FILE;

    // Time_s,Voltage_V,Current_A,Power_W,Charge_mAh,Charge_Percent,Temp_DTB_C,Temp_TC0_C,Temp_TC1_C,Mode
    fprintf(ctx->chargeLogFile, "%.2f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%s\n",
           timestamp,
           ctx->currentVoltage,
           ctx->currentCurrent,
           ctx->currentVoltage * ctx->currentCurrent,
           ctx->accumulatedCharge_mAh,
           ctx->currentSOC_percent,
           ctx->currentTempData.dtbAverageTemperature,
           ctx->currentTempData.tc0Temperature,
           ctx->currentTempData.tc1Temperature,
           GetModeString(ctx->inFastMode));
    fflush(ctx->chargeLogFile);

    return SUCCESS;
}

static int LogTemperatureDataPoint(OverchargeExperimentContext *ctx, double timestamp)
{
    if (!ctx->temperatureLogFile) return ERR_BASE_FILE;

    OverchargeTempData *td = &ctx->currentTempData;

    // Time_s,DTB1_C,DTB2_C,TC0_C,TC1_C,TC2_C,TC3_C,TC4_C,TC5_C,TC6_C,TC7_C,Avg_DTB_C,Avg_TC_C
    fprintf(ctx->temperatureLogFile, "%.2f,%.2f,%.2f,%.2f,%.2f,0.0,0.0,0.0,0.0,0.0,0.0,%.2f,%.2f\n",
           timestamp,
           td->dtbTemperatures[0],
           (td->dtbDeviceCount > 1) ? td->dtbTemperatures[1] : 0.0,
           td->tc0Temperature,
           td->tc1Temperature,
           td->dtbAverageTemperature,
           (td->tc0Temperature + td->tc1Temperature) / 2.0);
    fflush(ctx->temperatureLogFile);

    return SUCCESS;
}

static int LogGasFlowDataPoint(OverchargeExperimentContext *ctx)
{
    if (!ctx->gasFlowLogFile || !ENABLE_ALICAT) return SUCCESS;

    ALICAT_Status alicatStatus;
    int result = ALICAT_GetStatusQueued(ALICAT_MODBUS_ADDRESS, &alicatStatus, DEVICE_PRIORITY_LOW);
    if (result != SUCCESS) {
        return result;
    }

    double timestamp = Timer() - ctx->experimentStartTime;

    // Time_s,Flow_SLPM,Temp_C,Setpoint_SLPM
    fprintf(ctx->gasFlowLogFile, "%.2f,%.4f,%.2f,%.4f\n",
           timestamp,
           alicatStatus.flowRate,
           alicatStatus.temperature,
           alicatStatus.setpoint);
    fflush(ctx->gasFlowLogFile);

    return SUCCESS;
}

static int LogEvent(OverchargeExperimentContext *ctx, const char *eventType, const char *details, ...)
{
    if (!ctx->eventLogFile) return ERR_BASE_FILE;

    double timestamp = Timer() - ctx->experimentStartTime;

    // Format the details string with variable arguments
    char detailsBuffer[512];
    va_list args;
    va_start(args, details);
    vsnprintf(detailsBuffer, sizeof(detailsBuffer), details, args);
    va_end(args);

    fprintf(ctx->eventLogFile, "%.2f,%s,%s\n", timestamp, eventType, detailsBuffer);
    fflush(ctx->eventLogFile);

    return SUCCESS;
}

/******************************************************************************
 * Graph Functions
 ******************************************************************************/

static int ConfigureOverchargeGraphs(OverchargeExperimentContext *ctx)
{
    int graph1 = ctx->graph1Handle;
    int graph2 = ctx->graph2Handle;
    int graph3 = ctx->graph3Handle;

    // Clear all graphs
    ClearOverchargeGraphs(ctx);

    // GRAPH 1: Voltage (left axis) + Current (right axis) vs Time
    SetCtrlAttribute(ctx->mainPanelHandle, graph1, ATTR_LABEL_TEXT, "Voltage & Current vs Time");
    SetCtrlAttribute(ctx->mainPanelHandle, graph1, ATTR_XNAME, "Time (min)");
    SetCtrlAttribute(ctx->mainPanelHandle, graph1, ATTR_YNAME, "Voltage (V)");
    // SetCtrlAttribute(ctx->mainPanelHandle, graph1, ATTR_Y2NAME, "Current (A)");  // Not available in CVI 2020

    // Plots will be created automatically when first data is added
    // PlotY(ctx->mainPanelHandle, graph1, NULL, 0, VAL_DOUBLE, VAL_THIN_LINE,
    //       VAL_NO_POINT, VAL_SOLID, 1, VAL_RED);
    // GetPlotAttribute(ctx->mainPanelHandle, graph1, 1, ATTR_PLOT_HANDLE, &ctx->voltagePlotHandle);  // Not available in CVI 2020

    // PlotY(ctx->mainPanelHandle, graph1, NULL, 0, VAL_DOUBLE, VAL_THIN_LINE,
    //       VAL_NO_POINT, VAL_SOLID, 1, VAL_BLUE);
    // GetPlotAttribute(ctx->mainPanelHandle, graph1, 2, ATTR_PLOT_HANDLE, &ctx->currentPlotHandle);  // Not available in CVI 2020
    // SetPlotAttribute(ctx->mainPanelHandle, graph1, ctx->currentPlotHandle, ATTR_PLOT_YAXIS, VAL_RIGHT_YAXIS);

    // GRAPH 2: Temperature (left axis) + Charge (right axis) vs Time
    SetCtrlAttribute(ctx->mainPanelHandle, graph2, ATTR_LABEL_TEXT, "Temperature & Charge vs Time");
    SetCtrlAttribute(ctx->mainPanelHandle, graph2, ATTR_XNAME, "Time (min)");
    SetCtrlAttribute(ctx->mainPanelHandle, graph2, ATTR_YNAME, "Temperature (C)");
    // SetCtrlAttribute(ctx->mainPanelHandle, graph2, ATTR_Y2NAME, "Charge (mAh)");  // Not available in CVI 2020

    // Plots will be created automatically when first data is added
    // PlotY(ctx->mainPanelHandle, graph2, NULL, 0, VAL_DOUBLE, VAL_THIN_LINE,
    //       VAL_NO_POINT, VAL_SOLID, 1, VAL_GREEN);
    // GetPlotAttribute(ctx->mainPanelHandle, graph2, 1, ATTR_PLOT_HANDLE, &ctx->tempPlotHandle);  // Not available in CVI 2020

    // PlotY(ctx->mainPanelHandle, graph2, NULL, 0, VAL_DOUBLE, VAL_THIN_LINE,
    //       VAL_NO_POINT, VAL_SOLID, 1, VAL_MAGENTA);
    // GetPlotAttribute(ctx->mainPanelHandle, graph2, 2, ATTR_PLOT_HANDLE, &ctx->chargePlotHandle);  // Not available in CVI 2020
    // SetPlotAttribute(ctx->mainPanelHandle, graph2, ctx->chargePlotHandle, ATTR_PLOT_YAXIS, VAL_RIGHT_YAXIS);

    // GRAPH 3: Nyquist Plot (Z_real vs -Z_imag)
    SetCtrlAttribute(ctx->mainPanelHandle, graph3, ATTR_LABEL_TEXT, "Nyquist Plot (Latest EIS)");
    SetCtrlAttribute(ctx->mainPanelHandle, graph3, ATTR_XNAME, "Z Real (Ohm)");
    SetCtrlAttribute(ctx->mainPanelHandle, graph3, ATTR_YNAME, "-Z Imag (Ohm)");

    // Plot will be created automatically when first EIS data is added
    // PlotXY(ctx->mainPanelHandle, graph3, NULL, NULL, 0, VAL_DOUBLE, VAL_DOUBLE,
    //        VAL_THIN_LINE, VAL_SMALL_SOLID_SQUARE, VAL_SOLID, 1, VAL_DK_CYAN);
    // GetPlotAttribute(ctx->mainPanelHandle, graph3, 1, ATTR_PLOT_HANDLE, &ctx->nyquistPlotHandle);  // Not available in CVI 2020

    LogMessage("Graphs configured successfully");
    return SUCCESS;
}

static void UpdateGraphsRealtime(OverchargeExperimentContext *ctx, double timestamp)
{
    double timeMinutes = timestamp / 60.0;

    // Update voltage plot
    PlotLine(ctx->mainPanelHandle, ctx->graph1Handle, timeMinutes, ctx->currentVoltage,
            timeMinutes, ctx->currentVoltage, VAL_RED);

    // Update current plot
    PlotLine(ctx->mainPanelHandle, ctx->graph1Handle, timeMinutes, ctx->currentCurrent,
            timeMinutes, ctx->currentCurrent, VAL_BLUE);

    // Update temperature plot
    PlotLine(ctx->mainPanelHandle, ctx->graph2Handle, timeMinutes, ctx->currentTemperature,
            timeMinutes, ctx->currentTemperature, VAL_GREEN);

    // Update charge plot
    PlotLine(ctx->mainPanelHandle, ctx->graph2Handle, timeMinutes, ctx->accumulatedCharge_mAh,
            timeMinutes, ctx->accumulatedCharge_mAh, VAL_MAGENTA);

    // Refresh graphs
    RefreshGraph(ctx->mainPanelHandle, ctx->graph1Handle);
    RefreshGraph(ctx->mainPanelHandle, ctx->graph2Handle);
}

static void UpdateNyquistPlot(OverchargeExperimentContext *ctx, OverchargeEISMeasurement *measurement)
{
    int i;
    double *zImag_neg;

    if (!measurement || measurement->numPoints == 0) return;

    // Allocate array for negative imaginary impedance
    zImag_neg = malloc(measurement->numPoints * sizeof(double));
    if (!zImag_neg) {
        LogError("Failed to allocate memory for Nyquist plot");
        return;
    }

    // Negate imaginary impedance for Nyquist convention
    for (i = 0; i < measurement->numPoints; i++) {
        zImag_neg[i] = -measurement->zImag[i];
    }

    // Clear previous plot
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graph3Handle, -1, VAL_DELAYED_DRAW);

    // Plot new data
    PlotXY(ctx->mainPanelHandle, ctx->graph3Handle,
          measurement->zReal, zImag_neg, measurement->numPoints,
          VAL_DOUBLE, VAL_DOUBLE, VAL_THIN_LINE, VAL_SMALL_SOLID_SQUARE,
          VAL_SOLID, 1, VAL_DK_CYAN);

    RefreshGraph(ctx->mainPanelHandle, ctx->graph3Handle);

    free(zImag_neg);
}

static void AddRunawayMarker(OverchargeExperimentContext *ctx, double timeMinutes)
{
    // Add vertical line markers on graphs at runaway time
    double ymin, ymax;

    // Graph 1: Voltage & Current
    GetAxisScalingMode(ctx->mainPanelHandle, ctx->graph1Handle, VAL_LEFT_YAXIS, NULL, &ymin, &ymax);
    PlotLine(ctx->mainPanelHandle, ctx->graph1Handle,
            timeMinutes, ymin, timeMinutes, ymax, VAL_RED);

    // Graph 2: Temperature & Charge
    GetAxisScalingMode(ctx->mainPanelHandle, ctx->graph2Handle, VAL_LEFT_YAXIS, NULL, &ymin, &ymax);
    PlotLine(ctx->mainPanelHandle, ctx->graph2Handle,
            timeMinutes, ymin, timeMinutes, ymax, VAL_RED);

    RefreshGraph(ctx->mainPanelHandle, ctx->graph1Handle);
    RefreshGraph(ctx->mainPanelHandle, ctx->graph2Handle);

    LogMessage("Added runaway marker to graphs at %.1f minutes", timeMinutes);
}

static void ClearOverchargeGraphs(OverchargeExperimentContext *ctx)
{
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graph1Handle, -1, VAL_IMMEDIATE_DRAW);
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graph2Handle, -1, VAL_IMMEDIATE_DRAW);
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graph3Handle, -1, VAL_IMMEDIATE_DRAW);
}

/******************************************************************************
 * Pressure Safety Callbacks
 ******************************************************************************/

static void OnVentilationLost(ExperimentPhase phase, double temperature, double pressure)
{
    double chargeThreshold = g_experimentContext.params.ventilationThreshold_mAh;
    int isCriticalPhase = (g_experimentContext.accumulatedCharge_mAh >= chargeThreshold);

    if (isCriticalPhase) {
        LogError("CRITICAL PHASE: Ventilation has been lost after %.1f mAh threshold!", chargeThreshold);
        LogError("Current charge: %.1f mAh (%.1f%% SOC)",
                g_experimentContext.accumulatedCharge_mAh,
                g_experimentContext.currentSOC_percent);
        LogError("Temperature: %.1f C, Pressure: %.2f V", temperature, pressure);
        LogError("Alarm has been sounded - PERSONNEL SHOULD RE-ESTABLISH VENTILATION OR EVACUATE");
        LogError("Stopping experiment due to ventilation loss");
    } else {
        LogError("SAFE PHASE: Ventilation lost before critical threshold");
        LogError("Current charge: %.1f mAh < %.1f mAh threshold",
                g_experimentContext.accumulatedCharge_mAh, chargeThreshold);
        LogError("Temperature: %.1f C, Pressure: %.2f V", temperature, pressure);
        LogError("Stopping experiment due to ventilation loss");
    }

    // Set flag to stop experiment
    g_experimentContext.ventilationLost = 1;
    g_experimentContext.cancelRequested = 1;
    g_experimentContext.state = OVERCHARGE_STATE_CANCELLED;
}

static void OnVentilationRestored(double pressure)
{
    LogMessage("Ventilation restored (pressure: %.2f V)", pressure);
    LogMessage("However, experiment has already been stopped and will not resume");
}

/******************************************************************************
 * Cleanup and Results Functions
 ******************************************************************************/

static int MonitorCooling(OverchargeExperimentContext *ctx, int durationSeconds)
{
    double startTime = Timer();
    double now;
    int elapsed;
    int lastLogTime = 0;

    LogMessage("Monitoring cooling for %d seconds...", durationSeconds);
    LogEvent(ctx, "Cooling_Started", "Begin post-experiment cooling monitoring");

    while (1) {
        now = Timer();
        elapsed = (int)(now - startTime);

        // Check if cooling period complete
        if (elapsed >= durationSeconds) {
            break;
        }

        // Check for cancellation
        if (CheckCancellation(ctx)) {
            LogMessage("Cooling monitoring cancelled");
            break;
        }

        // Log temperature every OVERCHARGE_COOLING_INTERVAL seconds
        if (elapsed - lastLogTime >= OVERCHARGE_COOLING_INTERVAL) {
            ReadAllTemperatures(ctx);
            LogMessage("Cooling: T=%.1f C (elapsed: %d/%d s)",
                      ctx->currentTemperature, elapsed, durationSeconds);

            double timestamp = now - ctx->experimentStartTime;
            LogTemperatureDataPoint(ctx, timestamp);

            lastLogTime = elapsed;
        }

        Delay(1.0);
    }

    LogMessage("Cooling monitoring complete");
    LogEvent(ctx, "Cooling_Completed", "Cooling monitoring finished");
    return SUCCESS;
}

static int WriteFinalResults(OverchargeExperimentContext *ctx)
{
    char filepath[MAX_PATH_LENGTH];
    FILE *file;
    int i;

    snprintf(filepath, sizeof(filepath), "%s%s%s",
            ctx->experimentDirectory, PATH_SEPARATOR, OVERCHARGE_SUMMARY_FILE);

    file = fopen(filepath, "w");
    if (!file) {
        LogError("Failed to create summary file: %s", filepath);
        return ERR_BASE_FILE;
    }

    // Write comprehensive summary
    fprintf(file, "OVERCHARGE THERMAL RUNAWAY EXPERIMENT SUMMARY\n");
    fprintf(file, "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=\n\n");

    fprintf(file, "EXPERIMENT INFORMATION\n");
    fprintf(file, "Directory: %s\n", ctx->experimentDirectory);
    fprintf(file, "State: %s\n", GetStateDescription(ctx->state));
    fprintf(file, "Total Duration: %.2f hours\n\n",
           (ctx->experimentEndTime - ctx->experimentStartTime) / 3600.0);

    fprintf(file, "BATTERY CONFIGURATION\n");
    fprintf(file, "Nominal Capacity: %.0f mAh\n\n", ctx->params.nominalCapacity_mAh);

    fprintf(file, "CHARGING PARAMETERS\n");
    fprintf(file, "Charge Current: %.2f A\n", ctx->params.chargeCurrent);
    fprintf(file, "Charge Duration Limit: %.0f min%s\n",
           ctx->params.chargeDurationMinutes,
           ctx->params.chargeDurationMinutes == 0 ? " (unlimited)" : "");
    fprintf(file, "Total Charge Delivered: %.2f mAh (%.1f%% SOC)\n\n",
           ctx->accumulatedCharge_mAh, ctx->currentSOC_percent);

    fprintf(file, "SAFETY CONFIGURATION\n");
    fprintf(file, "Ventilation Safe Threshold: %.1f%% (%.0f mAh)\n",
           (ctx->params.ventilationThreshold_mAh / ctx->params.nominalCapacity_mAh) * 100.0,
           ctx->params.ventilationThreshold_mAh);
    fprintf(file, "Ventilation Lost: %s\n\n", ctx->ventilationLost ? "YES" : "NO");

    fprintf(file, "ADAPTIVE MODE CONFIGURATION\n");
    fprintf(file, "SOC Threshold: %.1f%%\n", ctx->params.socThresholdPercent);
    fprintf(file, "Slow EIS Interval: %.1f min\n", ctx->params.eisIntervalSlow_minutes);
    fprintf(file, "Fast EIS Interval: %.1f min\n", ctx->params.eisIntervalFast_minutes);
    fprintf(file, "Slow Log Interval: %.1f sec\n", ctx->params.logIntervalSlow);
    fprintf(file, "Fast Log Interval: %.1f sec\n", ctx->params.logIntervalFast);
    fprintf(file, "Mode Transition Time: %s\n\n",
           ctx->modeTransitionTime > 0 ?
           "Reached fast mode" : "Stayed in slow mode");

    fprintf(file, "THERMAL RUNAWAY EVENT\n");
    if (ctx->runawayReached) {
        fprintf(file, "User Indicated Runaway: YES\n");
        fprintf(file, "Runaway Time: %.1f min\n",
               (ctx->runawayReachedTime - ctx->experimentStartTime) / 60.0);
        fprintf(file, "Charge at Runaway: %.2f mAh (%.1f%% SOC)\n\n",
               ctx->accumulatedCharge_mAh, ctx->currentSOC_percent);
    } else {
        fprintf(file, "User Indicated Runaway: NO\n\n");
    }

    fprintf(file, "EIS MEASUREMENTS\n");
    fprintf(file, "Total EIS Measurements: %d\n", ctx->eisMeasurementCount);
    if (ctx->eisMeasurementCount > 0) {
        fprintf(file, "\nMeasurement Details:\n");
        fprintf(file, "Index  Time(min)  Charge(mAh)  SOC(%%)  OCV(V)  Temp(C)  Retries  Filename\n");
        fprintf(file, "-----  ---------  -----------  ------  ------  -------  -------  --------\n");
        for (i = 0; i < ctx->eisMeasurementCount; i++) {
            OverchargeEISMeasurement *m = &ctx->eisMeasurements[i];
            fprintf(file, "%5d  %9.1f  %11.2f  %6.1f  %6.4f  %7.1f  %7d  %s\n",
                   m->measurementIndex,
                   m->timestamp / 60.0,
                   m->chargeDelivered_mAh,
                   m->socPercent,
                   m->ocvVoltage,
                   m->tempData.dtbAverageTemperature,
                   m->retryCount,
                   m->filename);
        }
    }
    fprintf(file, "\n");

    fprintf(file, "DATA FILES\n");
    fprintf(file, "Charge Data: %s\n", OVERCHARGE_CHARGE_FILE);
    fprintf(file, "Temperature Profile: %s\n", OVERCHARGE_TEMP_FILE);
    if (ENABLE_ALICAT) {
        fprintf(file, "Gas Flow Data: %s\n", OVERCHARGE_FLOW_FILE);
    }
    fprintf(file, "Events Log: %s\n", OVERCHARGE_EVENT_FILE);
    fprintf(file, "Experiment Log: %s\n", OVERCHARGE_LOG_FILE);
    fprintf(file, "EIS Directory: %s\n\n", OVERCHARGE_EIS_DIR);

    fclose(file);
    LogMessage("Final results written to: %s", OVERCHARGE_SUMMARY_FILE);

    return SUCCESS;
}

static void CleanupExperiment(OverchargeExperimentContext *ctx)
{
    int i;

    LogMessage("Cleaning up overcharge experiment...");

    // Safely disconnect all devices
    SafeDisconnectAllDevices(ctx);

    // Clear external log file BEFORE closing it to prevent logging to closed file
    ClearExternalLogFile();

    // Close all log files
    if (ctx->chargeLogFile) {
        fclose(ctx->chargeLogFile);
        ctx->chargeLogFile = NULL;
    }

    if (ctx->temperatureLogFile) {
        fclose(ctx->temperatureLogFile);
        ctx->temperatureLogFile = NULL;
    }

    if (ctx->gasFlowLogFile) {
        fclose(ctx->gasFlowLogFile);
        ctx->gasFlowLogFile = NULL;
    }

    if (ctx->eventLogFile) {
        fclose(ctx->eventLogFile);
        ctx->eventLogFile = NULL;
    }

    if (ctx->experimentLogFile) {
        fclose(ctx->experimentLogFile);
        ctx->experimentLogFile = NULL;
    }

    // Free EIS measurement data
    if (ctx->eisMeasurements) {
        for (i = 0; i < ctx->eisMeasurementCount; i++) {
            OverchargeEISMeasurement *m = &ctx->eisMeasurements[i];
            if (m->frequencies) free(m->frequencies);
            if (m->zReal) free(m->zReal);
            if (m->zImag) free(m->zImag);
            if (m->ocvData) BIO_FreeTechniqueData(m->ocvData);
            if (m->geisData) BIO_FreeTechniqueData(m->geisData);
        }
        free(ctx->eisMeasurements);
        ctx->eisMeasurements = NULL;
    }

    LogMessage("Cleanup complete");
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

static int CheckCancellation(OverchargeExperimentContext *ctx)
{
    return ctx->cancelRequested ||
           ctx->state == OVERCHARGE_STATE_CANCELLED ||
           ctx->state == OVERCHARGE_STATE_ERROR;
}

static const char* GetStateDescription(OverchargeExperimentState state)
{
    switch (state) {
        case OVERCHARGE_STATE_IDLE:         return "Idle";
        case OVERCHARGE_STATE_PREPARING:    return "Preparing";
        case OVERCHARGE_STATE_INITIAL_EIS:  return "Initial EIS";
        case OVERCHARGE_STATE_CHARGING:     return "Charging";
        case OVERCHARGE_STATE_EIS_MEASUREMENT: return "EIS Measurement";
        case OVERCHARGE_STATE_COOLING:      return "Cooling";
        case OVERCHARGE_STATE_COMPLETED:    return "Completed";
        case OVERCHARGE_STATE_ERROR:        return "Error";
        case OVERCHARGE_STATE_CANCELLED:    return "Cancelled";
        default:                            return "Unknown";
    }
}

/******************************************************************************
 * exp_temp_ramp.c
 * 
 * Temperature Ramp EIS Experiment Module Implementation
 * EIS measurements during controlled temperature ramping
 ******************************************************************************/

#include "common.h"
#include "exp_temp_ramp.h"
#include "BatteryExploder.h"
#include "logging.h"
#include "status.h"
#include "battery_utils.h"
#include <ansi_c.h>
#include <analysis.h>
#include <utility.h>
#include <time.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

static TempRampExperimentContext g_experimentContext = {0};
static CmtThreadFunctionID g_experimentThreadId = 0;

// Controls to be dimmed during experiment
static const int numControls = 6;
static const int controls[6] = {
    RUNAWAY_INITIAL_TEMP_RWY,
    RUNAWAY_FINAL_TEMP_RWY,
    RUNAWAY_RAMP_RATE_RWY,
    RUNAWAY_NUM_EIS_INTERVAL_RWY,
    RUNAWAY_CBX_CONT_TRAMP_EIS,
	RUNAWAY_RING_RAMP_MODE,
};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int TempRampExperimentThread(void *functionData);
static int VerifyDevicesAndInitialize(TempRampExperimentContext *ctx);
static int CreateExperimentFileSystem(TempRampExperimentContext *ctx);
static int SaveExperimentSettings(TempRampExperimentContext *ctx);

static int PerformAutoTuning(TempRampExperimentContext *ctx);
static int ReachInitialTemperature(TempRampExperimentContext *ctx);
static int StabilizeAtInitialTemperature(TempRampExperimentContext *ctx);
static int RunTemperatureRampWithEIS(TempRampExperimentContext *ctx);
static int RunTemperatureRampWithEIS_V2(TempRampExperimentContext *ctx);
static int PerformEISMeasurementWithRampControl(TempRampExperimentContext *ctx);
static int HoldAtFinalTemperature(TempRampExperimentContext *ctx);

static int UpdateTemperatureSetpoint(TempRampExperimentContext *ctx, double newSetpoint);
static int StopRampAndSwitchToSetpointHold(TempRampExperimentContext *ctx);
static int ReadAllTemperatures(TempRampExperimentContext *ctx, TempRampTempData *tempData, double timestamp);
static int LogTemperatureDataPoint(TempRampExperimentContext *ctx, TempRampTempData *tempData);
static bool CheckExperimentCancellation(void *userData);

static int PerformEISMeasurement(TempRampExperimentContext *ctx);
static int RunOCVMeasurement(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement);
static int RunGEISMeasurement(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement);
static void EISProgressCallback(double progress, void *userData);
static void EISStatusCallback(const char *status, void *userData);
static int CVICALLBACK TemperatureMonitorThread(void *functionData);
static int ProcessGEISData(BIO_TechniqueData *geisData, TempRampEISMeasurement *measurement);
static int SaveEISMeasurementData(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement);
static int RetryEISMeasurement(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement);

static int SwitchToBioLogic(TempRampExperimentContext *ctx);
static int SafeDisconnectAllDevices(TempRampExperimentContext *ctx);

static void UpdateNyquistPlot(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement);
static void UpdateTemperaturePlot(TempRampExperimentContext *ctx, TempRampTempData *tempData);
static void ClearAllExperimentGraphs(TempRampExperimentContext *ctx);
static int ConfigureExperimentGraphs(TempRampExperimentContext *ctx);

static int WriteComprehensiveResults(TempRampExperimentContext *ctx);
static void CleanupExperiment(TempRampExperimentContext *ctx);
static int CheckCancellation(TempRampExperimentContext *ctx);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int CVICALLBACK StartTempRampExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    if (TempRampExperiment_IsRunning()) {
        LogMessage("User requested to stop temperature ramp experiment");
        g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = TEMP_RAMP_STATE_CANCELLED;
        return 0;
    }
    
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        MessagePopup("System Busy", 
                     "Another operation is in progress.\n"
                     "Please wait for it to complete.");
        return 0;
    }
    g_systemBusy = 1;
    CmtReleaseLock(g_busyLock);
    
    // Initialize context
    memset(&g_experimentContext, 0, sizeof(g_experimentContext));
    g_experimentContext.cancelRequested = 0;
    g_experimentContext.emergencyStop = 0;
    g_experimentContext.state = TEMP_RAMP_STATE_PREPARING;
    g_experimentContext.mainPanelHandle = g_mainPanelHandle;
    g_experimentContext.tabPanelHandle = panel;
    g_experimentContext.buttonControl = control;
    g_experimentContext.outputControl = RUNAWAY_TEMP_RAMP_NUM_OUTPUT;
    g_experimentContext.statusControl = RUNAWAY_STR_RWY_STATUS;
    g_experimentContext.graphTempHandle = PANEL_GRAPH_1;
    g_experimentContext.graphNyquistHandle = PANEL_GRAPH_BIOLOGIC;
    
    // Read parameters
    GetCtrlVal(panel, RUNAWAY_INITIAL_TEMP_RWY, &g_experimentContext.params.initialTemp);
    GetCtrlVal(panel, RUNAWAY_FINAL_TEMP_RWY, &g_experimentContext.params.finalTemp);
    GetCtrlVal(panel, RUNAWAY_RAMP_RATE_RWY, &g_experimentContext.params.rampRate);
    GetCtrlVal(panel, RUNAWAY_NUM_EIS_INTERVAL_RWY, &g_experimentContext.params.eisInterval);
    GetCtrlVal(panel, RUNAWAY_CBX_CONT_TRAMP_EIS, &g_experimentContext.params.continueRampDuringEIS);
	GetCtrlVal(panel, RUNAWAY_RING_RAMP_MODE, &g_experimentContext.params.useRampSoak);
	GetCtrlVal(panel, RUNAWAY_CBX_AUTO_TUNE, &g_experimentContext.params.autoTuneBeforeRamp);
    
    // Validate parameters
    if (!ENABLE_DTB) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        MessagePopup("DTB Required", 
                     "Temperature control (DTB) must be enabled for this experiment.");
        return 0;
    }
    
    if (g_experimentContext.params.initialTemp < 5.0 || g_experimentContext.params.initialTemp > 100.0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        MessagePopup("Invalid Temperature", 
                     "Initial temperature must be between 5�C and 100�C.");
        return 0;
    }
    
    if (g_experimentContext.params.finalTemp < 5.0 || g_experimentContext.params.finalTemp > 100.0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        MessagePopup("Invalid Temperature", 
                     "Final temperature must be between 5�C and 100�C.");
        return 0;
    }
    
    if (g_experimentContext.params.finalTemp <= g_experimentContext.params.initialTemp) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        MessagePopup("Invalid Temperature Range", 
                     "Final temperature must be greater than initial temperature.");
        return 0;
    }
    
    if (g_experimentContext.params.rampRate <= 0.0 || g_experimentContext.params.rampRate > 10.0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        MessagePopup("Invalid Ramp Rate", 
                     "Ramp rate must be between 0.1 and 10.0 �C/min.");
        return 0;
    }
    
    if (g_experimentContext.params.eisInterval < 1.0 || g_experimentContext.params.eisInterval > 60.0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        MessagePopup("Invalid EIS Interval", 
                     "EIS measurement interval must be between 1 and 60 minutes.");
        return 0;
    }
    
    // Verify devices
    int result = VerifyDevicesAndInitialize(&g_experimentContext);
    if (result != SUCCESS) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        g_experimentContext.state = TEMP_RAMP_STATE_ERROR;
        return 0;
    }
    
    // Change button to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");
    DimExperimentControls(g_mainPanelHandle, panel, 1, controls, numControls);
    
    // Start experiment thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, TempRampExperimentThread, 
                                            &g_experimentContext, &g_experimentThreadId);
    if (error != 0) {
        g_experimentContext.state = TEMP_RAMP_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start");
        DimExperimentControls(g_mainPanelHandle, panel, 0, controls, numControls);
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start experiment thread.");
        return 0;
    }
    
    return 0;
}

int TempRampExperiment_IsRunning(void) {
    return !(g_experimentContext.state == TEMP_RAMP_STATE_IDLE ||
             g_experimentContext.state == TEMP_RAMP_STATE_COMPLETED ||
             g_experimentContext.state == TEMP_RAMP_STATE_ERROR ||
             g_experimentContext.state == TEMP_RAMP_STATE_CANCELLED);
}

int TempRampExperiment_Abort(void) {
    if (TempRampExperiment_IsRunning()) {
        LogMessage("Aborting temperature ramp experiment...");
        g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = TEMP_RAMP_STATE_CANCELLED;
        
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}

int TempRampExperiment_EmergencyStop(void) {
    if (TempRampExperiment_IsRunning()) {
        LogMessage("EMERGENCY STOP - Temperature ramp experiment");
        g_experimentContext.emergencyStop = 1;
        g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = TEMP_RAMP_STATE_ERROR;
        
        SafeDisconnectAllDevices(&g_experimentContext);
        
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}

void TempRampExperiment_Cleanup(void) {
    if (TempRampExperiment_IsRunning()) {
        TempRampExperiment_Abort();
    }
}

/******************************************************************************
 * Main Experiment Thread
 ******************************************************************************/

static int TempRampExperimentThread(void *functionData) {
    TempRampExperimentContext *ctx = (TempRampExperimentContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    
    LogMessage("=== Starting Temperature Ramp EIS Experiment ===");
    
    ctx->experimentStartTime = Timer();
    
    if (CheckCancellation(ctx)) {
        LogMessage("Experiment cancelled before confirmation");
        goto cleanup;
    }
    
    // Calculate experiment duration
    double tempRange = ctx->params.finalTemp - ctx->params.initialTemp;
    double rampDuration = tempRange / ctx->params.rampRate;  // minutes
    int expectedMeasurements = (int)(rampDuration / ctx->params.eisInterval) + 2;
    
    snprintf(message, sizeof(message),
        "TEMPERATURE RAMP EIS EXPERIMENT\n"
        "================================\n\n"
        "PARAMETERS:\n"
        "� Initial Temperature: %.1f deg C\n"
        "� Final Temperature: %.1f deg C\n"
        "� Ramp Rate: %.1f deg C/min\n"
        "� EIS Interval: %.1f minutes\n"
        "� Ramp Mode: %s during EIS\n"
        "� Implementation: %s\n"
        "� Auto-Tuning: %s\n\n"
        "EXPERIMENT SEQUENCE:\n"
        "%s"
        "1. Reach %.1f deg C and stabilize\n"
        "2. Ramp to %.1f deg C at %.1f deg C/min\n"
        "3. EIS measurements every %.1f min\n"
        "4. Hold at %.1f deg C briefly\n\n"
        "ESTIMATED:\n"
        "� Ramp Duration: %.1f minutes\n"
        "� Expected Measurements: ~%d\n"
        "� Total Time: %.1f minutes\n\n"
        "Continue with experiment?",
        ctx->params.initialTemp,
        ctx->params.finalTemp,
        ctx->params.rampRate,
        ctx->params.eisInterval,
        ctx->params.continueRampDuringEIS ? "CONTINUE" : "PAUSE",
        ctx->params.useRampSoak ? "DTB Ramp-Soak" : "Manual Ramping",
        ctx->params.autoTuneBeforeRamp ? "ENABLED" : "DISABLED",
        ctx->params.autoTuneBeforeRamp ? "0. Auto-tune PID parameters\n" : "",
        ctx->params.initialTemp,
        ctx->params.finalTemp,
        ctx->params.rampRate,
        ctx->params.eisInterval,
        ctx->params.finalTemp,
        rampDuration,
        expectedMeasurements,
        rampDuration + (ctx->params.autoTuneBeforeRamp ? 15.0 : 10.0));
    
    int response = ConfirmPopup("Confirm Temperature Ramp Experiment", message);
    if (!response || CheckCancellation(ctx)) {
        LogMessage("Experiment cancelled by user");
        ctx->state = TEMP_RAMP_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Create file system
    result = CreateExperimentFileSystem(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogError("Failed to create experiment file system");
        MessagePopup("Error", "Failed to create experiment directory.");
        ctx->state = TEMP_RAMP_STATE_ERROR;
        goto cleanup;
    }
    
    SetExternalLogFile(ctx->experimentLogFile);
    SaveExperimentSettings(ctx);
    ConfigureExperimentGraphs(ctx);
    
    // Initialize relay states (only BioLogic will be used)
    LogMessage("Initializing relay states...");
    TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    
    // Allocate EIS measurement array
    ctx->eisMeasurementCapacity = expectedMeasurements + 10;  // Extra capacity
    ctx->eisMeasurements = (TempRampEISMeasurement*)calloc(ctx->eisMeasurementCapacity,
                                                           sizeof(TempRampEISMeasurement));
    if (!ctx->eisMeasurements) {
        LogError("Failed to allocate EIS measurement array");
        ctx->state = TEMP_RAMP_STATE_ERROR;
        goto cleanup;
    }

    // PHASE 0 (OPTIONAL): Auto-Tuning
    if (ctx->params.autoTuneBeforeRamp) {
        LogMessage("=== PHASE 0: Auto-Tuning PID Parameters ===");
        ctx->state = TEMP_RAMP_STATE_AUTO_TUNING;
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl,
                   "Auto-tuning PID parameters...");

        result = PerformAutoTuning(ctx);
        if (result != SUCCESS || CheckCancellation(ctx)) {
            if (!CheckCancellation(ctx)) {
                ctx->state = TEMP_RAMP_STATE_ERROR;
            }
            goto cleanup;
        }
    }

    // PHASE 1: Reach Initial Temperature
    LogMessage("=== PHASE 1: Reaching Initial Temperature ===");
    ctx->state = TEMP_RAMP_STATE_REACHING_INITIAL_TEMP;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl,
               "Reaching initial temperature...");

    result = ReachInitialTemperature(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = TEMP_RAMP_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // PHASE 2: Stabilize at Initial Temperature
    LogMessage("=== PHASE 2: Stabilizing at Initial Temperature ===");
    ctx->state = TEMP_RAMP_STATE_STABILIZING_INITIAL;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, 
               "Stabilizing at initial temperature...");
    
    result = StabilizeAtInitialTemperature(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = TEMP_RAMP_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // PHASE 3: Temperature Ramp with EIS Measurements
    LogMessage("=== PHASE 3: Temperature Ramp with EIS Measurements ===");
    ctx->state = TEMP_RAMP_STATE_RAMPING;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl,
               "Running temperature ramp...");

    // Choose implementation based on useRampSoak parameter
    if (ctx->params.useRampSoak) {
        LogMessage("Using new DTB ramp-soak implementation");
        result = RunTemperatureRampWithEIS_V2(ctx);
    } else {
        LogMessage("Using legacy manual ramping implementation");
        result = RunTemperatureRampWithEIS(ctx);
    }

    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = TEMP_RAMP_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // PHASE 4: Hold at Final Temperature
    LogMessage("=== PHASE 4: Holding at Final Temperature ===");
    ctx->state = TEMP_RAMP_STATE_HOLDING_FINAL;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, 
               "Holding at final temperature...");
    
    result = HoldAtFinalTemperature(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = TEMP_RAMP_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Complete
    ctx->experimentEndTime = Timer();
    ctx->state = TEMP_RAMP_STATE_COMPLETED;
    LogMessage("=== TEMPERATURE RAMP EXPERIMENT COMPLETED SUCCESSFULLY ===");
    LogMessage("Total experiment time: %.1f minutes", 
               (ctx->experimentEndTime - ctx->experimentStartTime) / 60.0);
    LogMessage("EIS measurements taken: %d", ctx->eisMeasurementCount);
    
    result = WriteComprehensiveResults(ctx);
    if (result != SUCCESS) {
        LogError("Failed to write comprehensive results");
    }
    
cleanup:
    CleanupExperiment(ctx);
    
    const char *finalStatus;
    switch (ctx->state) {
        case TEMP_RAMP_STATE_COMPLETED:
            finalStatus = "Temperature ramp experiment completed successfully";
            break;
        case TEMP_RAMP_STATE_CANCELLED:
            finalStatus = "Temperature ramp experiment cancelled by user";
            break;
        case TEMP_RAMP_STATE_ERROR:
            finalStatus = ctx->emergencyStop ? "Temperature ramp experiment emergency stopped" : 
                                               "Temperature ramp experiment failed";
            break;
        default:
            finalStatus = "Temperature ramp experiment ended unexpectedly";
            break;
    }
    
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, finalStatus);
    SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, finalStatus);
    
    SetCtrlAttribute(ctx->tabPanelHandle, ctx->buttonControl, ATTR_LABEL_TEXT, "Start");
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, controls, numControls);
    
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
    
    g_experimentThreadId = 0;
    
    return 0;
}

/******************************************************************************
 * Phase Implementation Functions
 ******************************************************************************/

static int PerformAutoTuning(TempRampExperimentContext *ctx) {
    LogMessage("Starting DTB auto-tuning procedure...");

    // Calculate a suitable auto-tuning target temperature (midpoint of ramp)
    double autoTuneTemp = (ctx->params.initialTemp + ctx->params.finalTemp) / 2.0;
    LogMessage("Auto-tuning target temperature: %.1f deg C", autoTuneTemp);

    // Set the target temperature for all DTB controllers
    int result;
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;

        result = DTB_SetSetPointQueued(slaveAddress, autoTuneTemp, DEVICE_PRIORITY_NORMAL);
        if (result != DTB_SUCCESS) {
            LogError("Failed to set auto-tune setpoint for DTB device %d: %s",
                    slaveAddress, DTB_GetErrorString(result));
            return result;
        }
    }

    // Start RUN mode for all DTB controllers
    result = DTB_SetRunStopAllQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("Failed to start DTB for auto-tuning: %s", DTB_GetErrorString(result));
        return result;
    }

    Delay(2.0);  // Allow DTB to start

    // Start auto-tuning on all DTB controllers
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;

        result = DTB_StartAutoTuningQueued(slaveAddress, DEVICE_PRIORITY_NORMAL);
        if (result != DTB_SUCCESS) {
            LogError("Failed to start auto-tuning for DTB device %d: %s",
                    slaveAddress, DTB_GetErrorString(result));
            return result;
        }

        LogMessage("Auto-tuning started for DTB device %d (AT LED should be flashing)", slaveAddress);
    }

    // Monitor auto-tuning status
    LogMessage("Monitoring auto-tuning progress (this may take 5-15 minutes)...");
    LogMessage("Watch for AT LED to stop flashing on DTB controllers");

    double startTime = Timer();
    double maxAutoTuneTime = 1800.0;  // 30 minutes maximum
    int allCompleted = 0;

    while (!allCompleted && !CheckCancellation(ctx)) {
        double elapsedTime = Timer() - startTime;

        // Check if auto-tuning timeout exceeded
        if (elapsedTime > maxAutoTuneTime) {
            LogWarning("Auto-tuning timeout exceeded (%.1f min), stopping auto-tuning",
                      elapsedTime / 60.0);
            break;
        }

        // Check auto-tuning status for all devices
        allCompleted = 1;
        for (int i = 0; i < DTB_NUM_DEVICES; i++) {
            int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
            DTB_Status status;

            result = DTB_GetStatusQueued(slaveAddress, &status, DEVICE_PRIORITY_NORMAL);
            if (result != DTB_SUCCESS) {
                LogWarning("Failed to get auto-tuning status for DTB device %d", slaveAddress);
                continue;
            }

            if (status.autoTuning) {
                allCompleted = 0;  // Still tuning
            }
        }

        if (allCompleted) {
            LogMessage("Auto-tuning completed for all DTB devices after %.1f minutes",
                      elapsedTime / 60.0);
            break;
        }

        // Update status every 30 seconds
        if ((int)elapsedTime % 30 == 0) {
            LogMessage("Auto-tuning in progress... (%.1f min elapsed)", elapsedTime / 60.0);
        }

        Delay(5.0);  // Check every 5 seconds
    }

    // Stop auto-tuning (in case of timeout or cancellation)
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
        DTB_StopAutoTuningQueued(slaveAddress, DEVICE_PRIORITY_NORMAL);
    }

    if (CheckCancellation(ctx)) {
        LogMessage("Auto-tuning cancelled by user");
        return ERR_CANCELLED;
    }

    LogMessage("Auto-tuning phase completed - optimized PID parameters now active");
    LogMessage("DTB will use these parameters for the temperature ramp");

    return SUCCESS;
}

static int ReachInitialTemperature(TempRampExperimentContext *ctx) {
    LogMessage("Setting DTB target to %.1f deg C", ctx->params.initialTemp);
    
    int result = UpdateTemperatureSetpoint(ctx, ctx->params.initialTemp);
    if (result != SUCCESS) {
        LogError("Failed to set initial temperature");
        return result;
    }
    
    // Start DTB
    result = DTB_SetRunStopAllQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("Failed to start DTB: %s", DTB_GetErrorString(result));
        return result;
    }
    
    double startTime = Timer();
    double lastCheckTime = startTime;
    double lastLogTime = startTime;
    
    LogMessage("Waiting for temperature to reach %.1f �C...", ctx->params.initialTemp);
    
    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        double currentTime = Timer();
        
        if ((currentTime - lastCheckTime) >= TEMP_RAMP_CHECK_INTERVAL) {
            TempRampTempData tempData;
            ReadAllTemperatures(ctx, &tempData, currentTime - ctx->experimentStartTime);
            
            ctx->currentTemperature = tempData.dtbAverageTemperature;
            
            // Log temperature periodically
            if ((currentTime - lastLogTime) >= 10.0) {
                LogTemperatureDataPoint(ctx, &tempData);
                UpdateTemperaturePlot(ctx, &tempData);
                lastLogTime = currentTime;
            }
            
            double tempDiff = fabs(ctx->currentTemperature - ctx->params.initialTemp);
            
            LogMessage("Current temperature: %.1f �C (target: %.1f �C, diff: %.1f �C)", 
                      ctx->currentTemperature, ctx->params.initialTemp, tempDiff);
            
            if (tempDiff <= TEMP_RAMP_TOLERANCE) {
                LogMessage("Initial temperature reached");
                ctx->initialTempReached = 1;
                return SUCCESS;
            }
            
            char statusMsg[MEDIUM_BUFFER_SIZE];
            snprintf(statusMsg, sizeof(statusMsg), 
                     "Reaching initial temp: %.1f/%.1f �C", 
                     ctx->currentTemperature, ctx->params.initialTemp);
            SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
            
            lastCheckTime = currentTime;
        }
        
        if ((currentTime - startTime) > TEMP_RAMP_TIMEOUT_SEC) {
            LogError("Timeout reaching initial temperature");
            return ERR_TIMEOUT;
        }
        
        ProcessSystemEvents();
        Delay(1.0);
    }
}

static int StabilizeAtInitialTemperature(TempRampExperimentContext *ctx) {
    LogMessage("Stabilizing at %.1f �C for %.0f seconds...", 
               ctx->params.initialTemp, TEMP_RAMP_STABILIZE_TIME);
    
    double startTime = Timer();
    double lastLogTime = startTime;
    
    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        double currentTime = Timer();
        double elapsedTime = currentTime - startTime;
        
        if (elapsedTime >= TEMP_RAMP_STABILIZE_TIME) {
            LogMessage("Temperature stabilization completed");
            return SUCCESS;
        }
        
        if ((currentTime - lastLogTime) >= 10.0) {
            TempRampTempData tempData;
            ReadAllTemperatures(ctx, &tempData, currentTime - ctx->experimentStartTime);
            LogTemperatureDataPoint(ctx, &tempData);
            UpdateTemperaturePlot(ctx, &tempData);
            
            double remainingTime = TEMP_RAMP_STABILIZE_TIME - elapsedTime;
            char statusMsg[MEDIUM_BUFFER_SIZE];
            snprintf(statusMsg, sizeof(statusMsg), 
                     "Stabilizing: %.1f �C (%.0f sec remaining)", 
                     tempData.dtbAverageTemperature, remainingTime);
            SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
            
            lastLogTime = currentTime;
        }
        
        ProcessSystemEvents();
        Delay(1.0);
    }
}

static int RunTemperatureRampWithEIS(TempRampExperimentContext *ctx) {
    ctx->rampStartTime = Timer() - ctx->experimentStartTime;
    ctx->lastEISTime = ctx->rampStartTime;
    ctx->lastTempLogTime = Timer();
    ctx->totalEISTime = 0.0;
    
    double rampDuration = (ctx->params.finalTemp - ctx->params.initialTemp) / 
                         ctx->params.rampRate;  // minutes
    
    LogMessage("Starting temperature ramp from %.1f to %.1f �C", 
               ctx->params.initialTemp, ctx->params.finalTemp);
    LogMessage("Ramp rate: %.1f �C/min, Duration: %.1f minutes", 
               ctx->params.rampRate, rampDuration);
    LogMessage("EIS measurements every %.1f minutes", ctx->params.eisInterval);
    LogMessage("Ramp mode: %s during EIS measurements", 
               ctx->params.continueRampDuringEIS ? "CONTINUE" : "PAUSE");
    
    // Perform initial EIS measurement
    LogMessage("Taking initial EIS measurement at %.1f �C", ctx->currentTemperature);
    
    // Set state for temperature monitor thread to work correctly
    ctx->state = TEMP_RAMP_STATE_EIS_MEASUREMENT;
    
    double eisStartTime = Timer();
    int result = PerformEISMeasurement(ctx);
    double eisEndTime = Timer();
    double eisDuration = eisEndTime - eisStartTime;
    
    // Return to ramping state
    ctx->state = TEMP_RAMP_STATE_RAMPING;
    
    if (!ctx->params.continueRampDuringEIS) {
        ctx->totalEISTime += eisDuration;
        LogMessage("EIS measurement took %.1f seconds (ramp paused)", eisDuration);
    } else {
        LogMessage("EIS measurement took %.1f seconds (ramp continued)", eisDuration);
    }
    
    if (result != SUCCESS) {
        LogWarning("Initial EIS measurement failed, continuing anyway");
    }
    
    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        double currentTime = Timer();
        
        // Calculate elapsed ramp time
        // If pausing during EIS, subtract total EIS time
        double rawElapsedTime = (currentTime - ctx->experimentStartTime - ctx->rampStartTime);
        double elapsedRampTime;
        
        if (ctx->params.continueRampDuringEIS) {
            elapsedRampTime = rawElapsedTime / 60.0;  // minutes
        } else {
            elapsedRampTime = (rawElapsedTime - ctx->totalEISTime) / 60.0;  // minutes
        }
        
        // Calculate target temperature based on elapsed ramp time
        double targetTemp = ctx->params.initialTemp + (elapsedRampTime * ctx->params.rampRate);
        
        if (targetTemp >= ctx->params.finalTemp) {
            targetTemp = ctx->params.finalTemp;
            ctx->finalTempReached = 1;
        }
        
        // Update DTB setpoint
        if (fabs(targetTemp - ctx->targetTemperature) > 0.1) {
            result = UpdateTemperatureSetpoint(ctx, targetTemp);
            if (result != SUCCESS) {
                LogError("Failed to update temperature setpoint");
                return result;
            }
        }
        
        // Read and log temperature
        if ((currentTime - ctx->lastTempLogTime) >= 10.0) {
            TempRampTempData tempData;
            ReadAllTemperatures(ctx, &tempData, currentTime - ctx->experimentStartTime);
            LogTemperatureDataPoint(ctx, &tempData);
            UpdateTemperaturePlot(ctx, &tempData);
            
            char statusMsg[MEDIUM_BUFFER_SIZE];
            snprintf(statusMsg, sizeof(statusMsg), 
                     "Ramping: %.1f �C (target: %.1f �C, ramp time: %.1f min)", 
                     tempData.dtbAverageTemperature, targetTemp, elapsedRampTime);
            SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
            SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, tempData.dtbAverageTemperature);
            
            ctx->lastTempLogTime = currentTime;
        }
        
        // Check if time for EIS measurement
        double timeSinceLastEIS = (currentTime - ctx->experimentStartTime - ctx->lastEISTime) / 60.0;  // minutes
        
        if (timeSinceLastEIS >= ctx->params.eisInterval) {
            LogMessage("Time for EIS measurement (%.1f minutes elapsed since last)", timeSinceLastEIS);
            
            TempRampTempData tempData;
            ReadAllTemperatures(ctx, &tempData, currentTime - ctx->experimentStartTime);
            
            LogMessage("Current temperature: %.1f �C, Target: %.1f �C", 
                      tempData.dtbAverageTemperature, targetTemp);
            
            ctx->state = TEMP_RAMP_STATE_EIS_MEASUREMENT;
            
            eisStartTime = Timer();
            result = PerformEISMeasurement(ctx);
            eisEndTime = Timer();
            eisDuration = eisEndTime - eisStartTime;
            
            if (!ctx->params.continueRampDuringEIS) {
                ctx->totalEISTime += eisDuration;
                LogMessage("EIS measurement took %.1f seconds (ramp paused, total pause: %.1f min)", 
                          eisDuration, ctx->totalEISTime / 60.0);
            } else {
                LogMessage("EIS measurement took %.1f seconds (ramp continued)", eisDuration);
            }
            
            if (CheckCancellation(ctx)) {
                return ERR_CANCELLED;
            }
            
            if (result != SUCCESS) {
                LogWarning("EIS measurement failed, continuing ramp");
            }
            
            ctx->state = TEMP_RAMP_STATE_RAMPING;
            ctx->lastEISTime = currentTime - ctx->experimentStartTime;
        }
        
        // Check if final temperature reached
        if (ctx->finalTempReached) {
            LogMessage("Final temperature reached");
            
            // One final EIS measurement
            LogMessage("Taking final EIS measurement at %.1f �C", ctx->params.finalTemp);
            ctx->state = TEMP_RAMP_STATE_EIS_MEASUREMENT;
            
            eisStartTime = Timer();
            result = PerformEISMeasurement(ctx);
            eisEndTime = Timer();
            eisDuration = eisEndTime - eisStartTime;
            
            if (!ctx->params.continueRampDuringEIS) {
                ctx->totalEISTime += eisDuration;
                LogMessage("Final EIS took %.1f seconds (total EIS time: %.1f min)", 
                          eisDuration, ctx->totalEISTime / 60.0);
            }
            
            if (result != SUCCESS) {
                LogWarning("Final EIS measurement failed");
            }
            
            return SUCCESS;
        }
        
        ProcessSystemEvents();
        Delay(1.0);
    }
}

static int HoldAtFinalTemperature(TempRampExperimentContext *ctx) {
    LogMessage("Holding at %.1f �C for %.0f seconds...",
               ctx->params.finalTemp, TEMP_RAMP_HOLD_TIME);

    double startTime = Timer();
    double lastLogTime = startTime;

    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }

        double currentTime = Timer();
        double elapsedTime = currentTime - startTime;

        if (elapsedTime >= TEMP_RAMP_HOLD_TIME) {
            LogMessage("Final temperature hold completed");
            return SUCCESS;
        }

        if ((currentTime - lastLogTime) >= 10.0) {
            TempRampTempData tempData;
            ReadAllTemperatures(ctx, &tempData, currentTime - ctx->experimentStartTime);
            LogTemperatureDataPoint(ctx, &tempData);
            UpdateTemperaturePlot(ctx, &tempData);

            double remainingTime = TEMP_RAMP_HOLD_TIME - elapsedTime;
            char statusMsg[MEDIUM_BUFFER_SIZE];
            snprintf(statusMsg, sizeof(statusMsg),
                     "Holding at final temp: %.1f �C (%.0f sec remaining)",
                     tempData.dtbAverageTemperature, remainingTime);
            SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);

            lastLogTime = currentTime;
        }

        ProcessSystemEvents();
        Delay(1.0);
    }
}

/******************************************************************************
 * New DTB Ramp-Soak Based Implementation (V2)
 ******************************************************************************/

/**
 * Helper function to perform EIS measurement with DTB ramp control
 * Handles pause/resume of DTB program during EIS if configured
 */
static int PerformEISMeasurementWithRampControl(TempRampExperimentContext *ctx) {
    double eisStartTime = Timer();

    // If pause mode enabled, hold the DTB program
    if (!ctx->params.continueRampDuringEIS) {
        LogMessage("Pausing DTB ramp for EIS measurement");

        // Hold program for all DTB devices
        for (int i = 0; i < DTB_NUM_DEVICES; i++) {
            int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
            int result = DTB_HoldProgramQueued(slaveAddress, DEVICE_PRIORITY_NORMAL);
            if (result != DTB_SUCCESS) {
                LogWarning("Failed to hold DTB program for device %d: %s",
                          slaveAddress, DTB_GetErrorString(result));
            }
        }
    }

    // Perform EIS measurement (existing code)
    int result = PerformEISMeasurement(ctx);

    double eisEndTime = Timer();
    double eisDuration = eisEndTime - eisStartTime;

    // Resume ramp if it was paused
    if (!ctx->params.continueRampDuringEIS) {
        LogMessage("Resuming DTB ramp");

        // Resume program for all DTB devices
        for (int i = 0; i < DTB_NUM_DEVICES; i++) {
            int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
            int resumeResult = DTB_ResumeProgramQueued(slaveAddress, DEVICE_PRIORITY_NORMAL);
            if (resumeResult != DTB_SUCCESS) {
                LogWarning("Failed to resume DTB program for device %d: %s",
                          slaveAddress, DTB_GetErrorString(resumeResult));
            }
        }

        // Track total pause time
        ctx->totalEISTime += eisDuration;
        LogMessage("EIS measurement took %.1f seconds (ramp paused, total pause: %.1f min)",
                  eisDuration, ctx->totalEISTime / 60.0);
    } else {
        LogMessage("EIS measurement took %.1f seconds (ramp continued)", eisDuration);
    }

    return result;
}

/**
 * Run temperature ramp with EIS measurements using DTB ramp-soak feature
 * This is the new simplified implementation that uses DTB's built-in ramping
 */
static int RunTemperatureRampWithEIS_V2(TempRampExperimentContext *ctx) {
    int result;

    // Calculate ramp parameters with overshoot for temperature-based termination
    double tempRange = ctx->params.finalTemp - ctx->params.initialTemp;
    double inflatedTempRange = tempRange * TEMP_RAMP_OVERSHOOT_FACTOR;
    double inflatedEndTemp = ctx->params.initialTemp + inflatedTempRange;
    int inflatedRampMinutes = (int)((inflatedTempRange / ctx->params.rampRate) + 0.5);

    LogMessage("=== Using DTB Ramp-Soak Implementation ===");
    LogMessage("Configuring DTB ramp: %.1f -> %.1f �C (target: %.1f �C) over %d minutes",
               ctx->params.initialTemp, inflatedEndTemp, ctx->params.finalTemp, inflatedRampMinutes);
    LogMessage("Using %.0f%% overshoot safety margin for temperature-based early termination",
               (TEMP_RAMP_OVERSHOOT_FACTOR - 1.0) * 100.0);

    // Configure simple ramp on all DTB devices with inflated endpoint
    // Pattern 0: Simple ramp with no hold at end (soakTime = 0)
    // Using inflated end temperature provides safety margin for early termination
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;

        result = DTB_SetSimpleRampQueued(
            slaveAddress,                // Slave address
            0,                           // Pattern number (use pattern 0)
            ctx->params.initialTemp,     // Start temperature
            inflatedEndTemp,             // End temperature (inflated for safety)
            inflatedRampMinutes,         // Ramp duration in minutes (inflated)
            0,                           // Soak time (0 = no hold at end)
            DEVICE_PRIORITY_NORMAL
        );

        if (result != DTB_SUCCESS) {
            LogError("Failed to set simple ramp for DTB device %d: %s",
                    slaveAddress, DTB_GetErrorString(result));
            return result;
        }

        LogMessage("DTB device %d ramp configured", slaveAddress);
    }

    // CRITICAL: Set DTB setpoint to start temperature before starting program
    // The DTB ramp-soak program requires the current setpoint to match the start temperature
    LogMessage("Setting DTB setpoint to start temperature: %.1f deg C", ctx->params.initialTemp);
    result = UpdateTemperatureSetpoint(ctx, ctx->params.initialTemp);
    if (result != SUCCESS) {
        LogError("Failed to set initial setpoint before starting program");
        return result;
    }

    // Set start pattern and begin ramp for all devices
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;

        result = DTB_SetStartPatternQueued(slaveAddress, 0, DEVICE_PRIORITY_NORMAL);
        if (result != DTB_SUCCESS) {
            LogError("Failed to set start pattern for DTB device %d: %s",
                    slaveAddress, DTB_GetErrorString(result));
            return result;
        }

        result = DTB_StartProgramQueued(slaveAddress, DEVICE_PRIORITY_NORMAL);
        if (result != DTB_SUCCESS) {
            LogError("Failed to start program for DTB device %d: %s",
                    slaveAddress, DTB_GetErrorString(result));
            return result;
        }

        LogMessage("DTB device %d ramp program started", slaveAddress);
    }

    // Initialize timing
    ctx->rampStartTime = Timer() - ctx->experimentStartTime;
    ctx->lastEISTime = ctx->rampStartTime;
    ctx->lastTempLogTime = Timer();
    ctx->totalEISTime = 0.0;

    LogMessage("Temperature ramp started from %.1f to %.1f �C (target: %.1f �C)",
               ctx->params.initialTemp, inflatedEndTemp, ctx->params.finalTemp);
    LogMessage("Ramp rate: %.1f �C/min, Inflated duration: %d minutes",
               ctx->params.rampRate, inflatedRampMinutes);
    LogMessage("EIS measurements every %.1f minutes", ctx->params.eisInterval);
    LogMessage("Ramp mode: %s during EIS measurements",
               ctx->params.continueRampDuringEIS ? "CONTINUE" : "PAUSE");
    LogMessage("Will terminate early when target %.1f �C is reached", ctx->params.finalTemp);

    // Perform initial EIS measurement
    LogMessage("Taking initial EIS measurement at %.1f �C", ctx->currentTemperature);
    ctx->state = TEMP_RAMP_STATE_EIS_MEASUREMENT;

    result = PerformEISMeasurementWithRampControl(ctx);

    ctx->state = TEMP_RAMP_STATE_RAMPING;

    if (result != SUCCESS) {
        LogWarning("Initial EIS measurement failed, continuing anyway");
    }

    // Calculate expected program completion time (inflated - used as safety timeout)
    // The DTB doesn't report completion via Modbus, so we use time-based detection
    double expectedRampDuration = inflatedRampMinutes * 60.0;  // Convert to seconds

    // Main monitoring loop
    while (!ctx->finalTempReached) {
        if (CheckCancellation(ctx)) {
            // Stop the program on all devices
            for (int i = 0; i < DTB_NUM_DEVICES; i++) {
                int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
                DTB_StopProgramQueued(slaveAddress, DEVICE_PRIORITY_NORMAL);
            }
            // Reset control method back to PID mode
            for (int i = 0; i < DTB_NUM_DEVICES; i++) {
                int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
                DTB_SetControlMethodQueued(slaveAddress, CONTROL_METHOD_PID, DEVICE_PRIORITY_NORMAL);
            }
            return ERR_CANCELLED;
        }

        double currentTime = Timer();
        double elapsedTime = (currentTime - ctx->experimentStartTime - ctx->rampStartTime) / 60.0;  // minutes

        // Time-based completion detection (with pause compensation)
        double effectiveElapsedTime;
        if (ctx->params.continueRampDuringEIS) {
            effectiveElapsedTime = (currentTime - ctx->experimentStartTime - ctx->rampStartTime);
        } else {
            effectiveElapsedTime = (currentTime - ctx->experimentStartTime - ctx->rampStartTime - ctx->totalEISTime);
        }

        // Check if ramp should be complete (with 30 second tolerance)
        if (effectiveElapsedTime >= (expectedRampDuration - 30.0)) {
            LogMessage("DTB ramp program duration reached (%.1f minutes elapsed)",
                      effectiveElapsedTime / 60.0);
            ctx->finalTempReached = 1;
            break;
        }

        // Read and log temperatures periodically
        if ((currentTime - ctx->lastTempLogTime) >= 10.0) {
            TempRampTempData tempData;
            ReadAllTemperatures(ctx, &tempData, currentTime - ctx->experimentStartTime);
            LogTemperatureDataPoint(ctx, &tempData);
            UpdateTemperaturePlot(ctx, &tempData);

            // Check if ANY device has reached target temperature (Option A: most conservative)
            int targetReached = 0;
            for (int i = 0; i < tempData.dtbDeviceCount; i++) {
                if (tempData.dtbTemperatures[i] >= (ctx->params.finalTemp - TEMP_TARGET_TOLERANCE)) {
                    targetReached = 1;
                    LogMessage("DTB device %d reached target: %.2f deg C (target: %.2f deg C)",
                              i+1, tempData.dtbTemperatures[i], ctx->params.finalTemp);
                    break;
                }
            }

            // If target reached, stop ramp and switch to setpoint hold
            if (targetReached && !ctx->finalTempReached) {
                result = StopRampAndSwitchToSetpointHold(ctx);
                if (result != SUCCESS) {
                    LogWarning("Failed to transition to setpoint hold, continuing");
                }
                // finalTempReached is set by StopRampAndSwitchToSetpointHold()
                // This will cause the loop to exit and proceed to final EIS measurement
            }

            char statusMsg[MEDIUM_BUFFER_SIZE];
            double remainingTime = (expectedRampDuration - effectiveElapsedTime) / 60.0;
            snprintf(statusMsg, sizeof(statusMsg),
                     "Ramping: %.1f �C (%.1f/%.1f min, %.1f min remaining)",
                     tempData.dtbAverageTemperature, elapsedTime, (double)inflatedRampMinutes, remainingTime);
            SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
            SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, tempData.dtbAverageTemperature);

            ctx->lastTempLogTime = currentTime;
        }

        // Check program status to detect unexpected stops
        DTB_ProgramStatus progStatus;
        result = DTB_GetProgramStatusQueued(DTB1_SLAVE_ADDRESS, &progStatus, DEVICE_PRIORITY_NORMAL);

        if (result == DTB_SUCCESS) {
            if (progStatus.state == DTB_PROG_STATE_STOPPED) {
                LogWarning("DTB program stopped unexpectedly");
                return ERR_OPERATION_FAILED;
            }
        }

        // Check if time for EIS measurement
        double timeSinceLastEIS = (currentTime - ctx->experimentStartTime - ctx->lastEISTime) / 60.0;  // minutes

        if (timeSinceLastEIS >= ctx->params.eisInterval) {
            LogMessage("Time for EIS measurement (%.1f minutes elapsed since last)", timeSinceLastEIS);

            TempRampTempData tempData;
            ReadAllTemperatures(ctx, &tempData, currentTime - ctx->experimentStartTime);

            LogMessage("Current temperature: %.1f �C", tempData.dtbAverageTemperature);

            ctx->state = TEMP_RAMP_STATE_EIS_MEASUREMENT;

            result = PerformEISMeasurementWithRampControl(ctx);

            if (CheckCancellation(ctx)) {
                // Stop the program on all devices
                for (int i = 0; i < DTB_NUM_DEVICES; i++) {
                    int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
                    DTB_StopProgramQueued(slaveAddress, DEVICE_PRIORITY_NORMAL);
                }
                // Reset control method back to PID mode
                for (int i = 0; i < DTB_NUM_DEVICES; i++) {
                    int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
                    DTB_SetControlMethodQueued(slaveAddress, CONTROL_METHOD_PID, DEVICE_PRIORITY_NORMAL);
                }
                return ERR_CANCELLED;
            }

            if (result != SUCCESS) {
                LogWarning("EIS measurement failed, continuing ramp");
            }

            ctx->state = TEMP_RAMP_STATE_RAMPING;
            ctx->lastEISTime = currentTime - ctx->experimentStartTime;
        }

        ProcessSystemEvents();
        Delay(1.0);
    }

    // Final EIS measurement at end temperature
    LogMessage("Taking final EIS measurement at %.1f �C", ctx->params.finalTemp);
    ctx->state = TEMP_RAMP_STATE_EIS_MEASUREMENT;

    result = PerformEISMeasurementWithRampControl(ctx);

    if (result != SUCCESS) {
        LogWarning("Final EIS measurement failed");
    }

    // Stop the program on all devices (cleanup)
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
        DTB_StopProgramQueued(slaveAddress, DEVICE_PRIORITY_NORMAL);
    }

    // Reset control method back to PID mode for all devices
    // This is CRITICAL - if we don't reset from CONTROL_METHOD_PID_PROG (3) back to
    // CONTROL_METHOD_PID (0), subsequent experiments will fail with Modbus exception 0x03
    // because the DTB won't accept setpoint commands while in Program Control Mode
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
        int result = DTB_SetControlMethodQueued(slaveAddress, CONTROL_METHOD_PID, DEVICE_PRIORITY_NORMAL);
        if (result != DTB_SUCCESS) {
            LogWarning("Failed to reset control method for DTB slave %d: %s",
                      slaveAddress, DTB_GetErrorString(result));
        }
    }

    return SUCCESS;
}

/******************************************************************************
 * Temperature Control Functions
 ******************************************************************************/

static int UpdateTemperatureSetpoint(TempRampExperimentContext *ctx, double newSetpoint) {
    int result = DTB_SetSetPointAllQueued(newSetpoint, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("Failed to update temperature setpoint: %s", DTB_GetErrorString(result));
        return result;
    }

    ctx->targetTemperature = newSetpoint;
    return SUCCESS;
}

static int StopRampAndSwitchToSetpointHold(TempRampExperimentContext *ctx) {
    LogMessage("Terminating ramp program early - target %.1f deg C reached", ctx->params.finalTemp);

    // Stop ramp-soak program on all DTB devices
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
        int result = DTB_StopProgramQueued(slaveAddress, DEVICE_PRIORITY_HIGH);
        if (result != DTB_SUCCESS) {
            LogWarning("Failed to stop DTB %d program: %s", i+1, DTB_GetErrorString(result));
        }
    }

    // Switch back to normal PID control
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
        int result = DTB_SetControlMethodQueued(slaveAddress, CONTROL_METHOD_PID, DEVICE_PRIORITY_HIGH);
        if (result != DTB_SUCCESS) {
            LogWarning("Failed to set DTB %d control method: %s", i+1, DTB_GetErrorString(result));
        }
    }

    // Set explicit setpoint at target temperature
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
        int result = DTB_SetSetPointQueued(slaveAddress, ctx->params.finalTemp, DEVICE_PRIORITY_HIGH);
        if (result != DTB_SUCCESS) {
            LogWarning("Failed to set DTB %d setpoint: %s", i+1, DTB_GetErrorString(result));
        }
    }

    // Ensure heating is enabled
    int result = DTB_SetRunStopAllQueued(1, DEVICE_PRIORITY_HIGH);
    if (result != DTB_SUCCESS) {
        LogWarning("Failed to enable DTB heating: %s", DTB_GetErrorString(result));
    }

    ctx->finalTempReached = 1;
    LogMessage("Now holding at %.1f deg C using setpoint control", ctx->params.finalTemp);

    return SUCCESS;
}

static bool CheckExperimentCancellation(void *userData) {
    TempRampExperimentContext *ctx = (TempRampExperimentContext*)userData;
    return ctx && (ctx->cancelRequested || ctx->emergencyStop);
}

static int ReadAllTemperatures(TempRampExperimentContext *ctx, TempRampTempData *tempData, double timestamp) {
    tempData->timestamp = timestamp;
    tempData->dtbDeviceCount = 0;
    tempData->dtbAverageTemperature = 0.0;
    tempData->dtbSetpoint = ctx->targetTemperature;

    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        tempData->dtbTemperatures[i] = 0.0;
    }

    if (ENABLE_DTB) {
        DTB_Status dtbStatuses[MAX_DTB_DEVICES];
        int numDevices = 0;

        // Use cancel-aware status query to allow experiment cancellation during blocking calls
        int result = DTB_GetStatusAllQueuedEx(dtbStatuses, &numDevices, DEVICE_PRIORITY_NORMAL,
                                              CheckExperimentCancellation, ctx);

        if (result == DTB_SUCCESS) {
            double tempSum = 0.0;
            tempData->dtbDeviceCount = numDevices;

            for (int i = 0; i < numDevices && i < DTB_NUM_DEVICES; i++) {
                tempData->dtbTemperatures[i] = dtbStatuses[i].processValue;
                tempSum += dtbStatuses[i].processValue;
            }

            tempData->dtbAverageTemperature = tempSum / numDevices;
            snprintf(tempData->status, sizeof(tempData->status),
                     "DTB Avg: %.1f�C (%d devices)", tempData->dtbAverageTemperature, numDevices);
        } else if (result == ERR_CANCELLED) {
            strcpy(tempData->status, "DTB: Cancelled");
            return ERR_CANCELLED;
        } else {
            strcpy(tempData->status, "DTB: Error");
        }
    } else {
        strcpy(tempData->status, "DTB: Disabled");
    }

    if (ENABLE_CDAQ) {
        CDAQ_ReadTC(2, 0, &tempData->tc0Temperature);
        CDAQ_ReadTC(2, 1, &tempData->tc1Temperature);
    } else {
        tempData->tc0Temperature = 0.0;
        tempData->tc1Temperature = 0.0;
    }

    return SUCCESS;
}

static int LogTemperatureDataPoint(TempRampExperimentContext *ctx, TempRampTempData *tempData) {
    if (!ctx->temperatureLogFile) {
        return ERR_INVALID_STATE;
    }
    
    fprintf(ctx->temperatureLogFile, "%.3f,%.2f,%.2f,%.2f,%.2f\n",
            tempData->timestamp,
            tempData->dtbAverageTemperature,
            tempData->dtbSetpoint,
            tempData->tc0Temperature,
            tempData->tc1Temperature);
    
    fflush(ctx->temperatureLogFile);
    return SUCCESS;
}

/******************************************************************************
 * EIS Measurement Functions
 ******************************************************************************/

// Global context pointer for callbacks (needed because callbacks can't pass complex user data)
static TempRampExperimentContext *g_eisCallbackContext = NULL;
static volatile int g_temperatureMonitorRunning = 0;
static CmtThreadFunctionID g_tempMonitorThreadId = 0;

static int CVICALLBACK TemperatureMonitorThread(void *functionData) {
    TempRampExperimentContext *ctx = (TempRampExperimentContext*)functionData;
    
    LogDebug("Temperature monitor thread started");
    
    while (g_temperatureMonitorRunning && !CheckCancellation(ctx)) {
        double currentTime = Timer();
        
        // Read and log temperature
        TempRampTempData tempData;
        ReadAllTemperatures(ctx, &tempData, currentTime - ctx->experimentStartTime);
        LogTemperatureDataPoint(ctx, &tempData);
        UpdateTemperaturePlot(ctx, &tempData);
        
        // If continuing ramp during EIS, update the temperature setpoint
        // IMPORTANT: Only do this in manual ramping mode. In ramp-soak mode, the DTB program
        // controls the setpoint autonomously and manual updates will cause Modbus exception 0x03
        if (ctx->params.continueRampDuringEIS &&
            ctx->state == TEMP_RAMP_STATE_EIS_MEASUREMENT &&
            !ctx->params.useRampSoak) {

            double rawElapsedTime = (currentTime - ctx->experimentStartTime - ctx->rampStartTime);
            double elapsedRampTime = rawElapsedTime / 60.0;  // minutes

            double targetTemp = ctx->params.initialTemp + (elapsedRampTime * ctx->params.rampRate);

            // Don't exceed final temperature
            if (targetTemp > ctx->params.finalTemp) {
                targetTemp = ctx->params.finalTemp;
            }

            // Update setpoint if it changed significantly
            if (fabs(targetTemp - ctx->targetTemperature) > 0.1) {
                UpdateTemperatureSetpoint(ctx, targetTemp);
                LogDebug("Setpoint updated by monitor: %.1f �C (measured: %.1f �C)",
                        targetTemp, tempData.dtbAverageTemperature);
            }
        }
        
        ProcessSystemEvents();
        
        // Update every 5 seconds for smooth temperature control
        Delay(5.0);
    }
    
    LogDebug("Temperature monitor thread stopped");
    return 0;
}

static void EISProgressCallback(double progress, void *userData) {
    if (!g_eisCallbackContext) return;
    
    TempRampExperimentContext *ctx = g_eisCallbackContext;
    
    // Just update status with EIS progress
    // Temperature monitoring is now handled by dedicated thread
    char statusMsg[MEDIUM_BUFFER_SIZE];
    snprintf(statusMsg, sizeof(statusMsg), 
             "EIS in progress");
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
    
    ProcessSystemEvents();
}

static void EISStatusCallback(const char *status, void *userData) {
    if (!g_eisCallbackContext || !status) return;
    
    // Optional: Log status messages from BioLogic
    LogDebug("BioLogic: %s", status);
}

static int PerformEISMeasurement(TempRampExperimentContext *ctx) {
    if (CheckCancellation(ctx)) {
        return ERR_CANCELLED;
    }
    
    if (ctx->eisMeasurementCount >= ctx->eisMeasurementCapacity) {
        LogError("EIS measurement array full");
        return ERR_OPERATION_FAILED;
    }
    
    TempRampEISMeasurement *measurement = &ctx->eisMeasurements[ctx->eisMeasurementCount];
    
    memset(measurement, 0, sizeof(TempRampEISMeasurement));
    measurement->measurementIndex = ctx->eisMeasurementCount;
    measurement->timestamp = Timer() - ctx->experimentStartTime;
    
    ReadAllTemperatures(ctx, &measurement->tempData, measurement->timestamp);
    measurement->temperature = measurement->tempData.dtbAverageTemperature;
    
    char statusMsg[MEDIUM_BUFFER_SIZE];
    snprintf(statusMsg, sizeof(statusMsg), 
             "EIS measurement at %.1f �C...", measurement->temperature);
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
    
    // Start dedicated temperature monitoring thread for continuous updates during EIS
    g_temperatureMonitorRunning = 1;
    int threadError = CmtScheduleThreadPoolFunction(g_threadPool, TemperatureMonitorThread, 
                                                   ctx, &g_tempMonitorThreadId);
    if (threadError != 0) {
        LogWarning("Failed to start temperature monitor thread: %d", threadError);
        g_temperatureMonitorRunning = 0;
        // Continue anyway - EIS can still work without the monitor
    } else {
        LogDebug("Temperature monitor thread started for EIS measurement");
    }
    
    int result = RetryEISMeasurement(ctx, measurement);
    
    // Stop temperature monitoring thread
    g_temperatureMonitorRunning = 0;
    if (g_tempMonitorThreadId != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_tempMonitorThreadId,
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        g_tempMonitorThreadId = 0;
        LogDebug("Temperature monitor thread stopped");
    }
    
    if (result != SUCCESS) {
        LogError("EIS measurement failed at %.1f �C", measurement->temperature);
        return result;
    }
    
    UpdateNyquistPlot(ctx, measurement);
    
    result = SaveEISMeasurementData(ctx, measurement);
    if (result != SUCCESS) {
        LogWarning("Failed to save EIS data");
    }
    
    ctx->eisMeasurementCount++;
    
    LogMessage("EIS measurement %d completed at %.1f �C (OCV: %.3f V)", 
               measurement->measurementIndex + 1, measurement->temperature, measurement->ocvVoltage);
    
    return SUCCESS;
}

static int RetryEISMeasurement(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement) {
    int result;
    
    while (measurement->retryCount <= TEMP_RAMP_MAX_EIS_RETRY) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        result = SwitchToBioLogic(ctx);
        if (result != SUCCESS) {
            LogError("Failed to switch to BioLogic");
            return result;
        }
        
        if (measurement->retryCount > 0) {
            LogMessage("EIS retry %d after %.1f sec delay", 
                      measurement->retryCount, TEMP_RAMP_EIS_RETRY_DELAY);
            Delay(TEMP_RAMP_EIS_RETRY_DELAY);
        }
        
        result = RunOCVMeasurement(ctx, measurement);
        if (result != SUCCESS) {
            if (measurement->retryCount < TEMP_RAMP_MAX_EIS_RETRY) {
                LogWarning("OCV failed (attempt %d), retrying...", measurement->retryCount + 1);
                measurement->retryCount++;
                continue;
            } else {
                LogError("OCV failed after %d retries", TEMP_RAMP_MAX_EIS_RETRY + 1);
                return result;
            }
        }
        
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        result = RunGEISMeasurement(ctx, measurement);
        if (result != SUCCESS) {
            if (measurement->retryCount < TEMP_RAMP_MAX_EIS_RETRY) {
                LogWarning("GEIS failed (attempt %d), retrying...", measurement->retryCount + 1);
                measurement->retryCount++;
                continue;
            } else {
                LogError("GEIS failed after %d retries", TEMP_RAMP_MAX_EIS_RETRY + 1);
                return result;
            }
        }
        
        result = ProcessGEISData(measurement->geisData, measurement);
        if (result != SUCCESS) {
            LogWarning("Failed to process GEIS data");
        }
        
        if (measurement->retryCount > 0) {
            LogMessage("EIS succeeded after %d retries", measurement->retryCount);
        }
        return SUCCESS;
    }
    
    return ERR_OPERATION_FAILED;
}

static int RunOCVMeasurement(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement) {
    LogDebug("Starting OCV measurement...");
    
    measurement->ocvVoltage = 0.0;
    
    // Set global context for callbacks
    g_eisCallbackContext = ctx;
    
    int result = BIO_RunOCVQueued(ctx->biologicID, 0,
                                OCV_DURATION_S,
                                OCV_SAMPLE_INTERVAL_S,
                                OCV_RECORD_EVERY_DE,
                                OCV_RECORD_EVERY_DT,
                                OCV_E_RANGE,
                                true,
                                &measurement->ocvData,
                                OCV_TIMEOUT_MS,
                                DEVICE_PRIORITY_NORMAL,
                                EISProgressCallback, 
                                EISStatusCallback, 
                                &(ctx->cancelRequested));
    
    // Clear global context
    g_eisCallbackContext = NULL;
    
    if (result != SUCCESS) {
        LogError("OCV measurement failed: %s", BIO_GetErrorString(result));
        BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_NORMAL);
        Delay(0.5);
        return result;
    }
    
    if (measurement->ocvData && measurement->ocvData->convertedData) {
        BIO_ConvertedData *convData = measurement->ocvData->convertedData;
        
        if (convData->numPoints > 0 && convData->numVariables >= 2 && convData->data[1] != NULL) {
            int lastPoint = convData->numPoints - 1;
            measurement->ocvVoltage = convData->data[1][lastPoint];
            LogDebug("OCV: %.3f V", measurement->ocvVoltage);
        }
    }
    
    return SUCCESS;
}

static int RunGEISMeasurement(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement) {
    LogDebug("Starting GEIS measurement...");
    
    // Set global context for callbacks
    g_eisCallbackContext = ctx;
    
    int result = BIO_RunGEISQueued(ctx->biologicID, 0,
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
                                 true,
                                 &measurement->geisData,
                                 GEIS_TIMEOUT_MS,
                                 DEVICE_PRIORITY_NORMAL,
                                 EISProgressCallback, 
                                 EISStatusCallback, 
                                 &(ctx->cancelRequested));
    
    // Clear global context
    g_eisCallbackContext = NULL;
    
    if (result != SUCCESS) {
        LogError("GEIS measurement failed: %s", BIO_GetErrorString(result));
        return result;
    }
    
    LogDebug("GEIS complete");
    return SUCCESS;
}

static int ProcessGEISData(BIO_TechniqueData *geisData, TempRampEISMeasurement *measurement) {
    if (!geisData || !geisData->convertedData) {
        LogWarning("No GEIS data to process");
        return ERR_INVALID_PARAMETER;
    }
    
    BIO_ConvertedData *convData = geisData->convertedData;
    int processIndex = -1;
    
    if (geisData->rawData) {
        processIndex = geisData->rawData->processIndex;
    }
    
    LogDebug("Processing GEIS: %d points, %d variables (process %d)", 
             convData->numPoints, convData->numVariables, processIndex);
    
    if (processIndex == 1 && convData->numVariables >= 11) {
        measurement->frequencies = (double*)calloc(convData->numPoints, sizeof(double));
        measurement->zReal = (double*)calloc(convData->numPoints, sizeof(double));
        measurement->zImag = (double*)calloc(convData->numPoints, sizeof(double));
        
        if (!measurement->frequencies || !measurement->zReal || !measurement->zImag) {
            LogError("Failed to allocate impedance arrays");
            if (measurement->frequencies) free(measurement->frequencies);
            if (measurement->zReal) free(measurement->zReal);
            if (measurement->zImag) free(measurement->zImag);
            measurement->frequencies = NULL;
            measurement->zReal = NULL;
            measurement->zImag = NULL;
            return ERR_OUT_OF_MEMORY;
        }
        
        for (int i = 0; i < convData->numPoints; i++) {
            measurement->frequencies[i] = convData->data[0][i];
            measurement->zReal[i] = convData->data[4][i];
            measurement->zImag[i] = convData->data[5][i];
        }
        
        measurement->numPoints = convData->numPoints;
        LogDebug("Extracted %d impedance points", measurement->numPoints);
        
    } else {
        LogWarning("Unexpected GEIS format: process %d, %d variables", 
                  processIndex, convData->numVariables);
        return ERR_OPERATION_FAILED;
    }
    
    return SUCCESS;
}

static int SaveEISMeasurementData(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s%seis_%03d_temp.txt", 
             ctx->experimentDirectory, PATH_SEPARATOR, 
             TEMP_RAMP_EIS_DIR, PATH_SEPARATOR,
             (int)(measurement->temperature + 0.5));
    
    strcpy(measurement->filename, filename);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create EIS file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    time_t now = time(NULL);
    char timeStr[64];
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    
    WriteINISection(file, "EIS_Measurement_Information");
    WriteINIValue(file, "Measurement_Index", "%d", measurement->measurementIndex);
    WriteINIValue(file, "Timestamp", "%s", timeStr);
    WriteINIDouble(file, "Elapsed_Time_s", measurement->timestamp, 1);
    WriteINIDouble(file, "Temperature_C", measurement->temperature, 1);
    WriteINIDouble(file, "OCV_Voltage_V", measurement->ocvVoltage, 4);
    WriteINIDouble(file, "DTB_Setpoint_C", measurement->tempData.dtbSetpoint, 1);  // Note: Not used during program mode
    WriteINIDouble(file, "TC0_Temperature_C", measurement->tempData.tc0Temperature, 1);
    WriteINIDouble(file, "TC1_Temperature_C", measurement->tempData.tc1Temperature, 1);
    WriteINIValue(file, "Retry_Count", "%d", measurement->retryCount);
    fprintf(file, "\n");
    
    WriteINISection(file, "EIS_Configuration");
    WriteINIDouble(file, "OCV_Duration_s", OCV_DURATION_S, 1);
    WriteINIDouble(file, "GEIS_Initial_Freq_Hz", GEIS_INITIAL_FREQ, 0);
    WriteINIDouble(file, "GEIS_Final_Freq_Hz", GEIS_FINAL_FREQ, 1);
    WriteINIValue(file, "GEIS_Freq_Points", "%d", GEIS_FREQ_NUMBER);
    WriteINIDouble(file, "GEIS_Amplitude_A", GEIS_AMPLITUDE_I, 3);
    WriteINIValue(file, "GEIS_Average_N", "%d", GEIS_AVERAGE_N);
    fprintf(file, "\n");
    
    WriteINISection(file, "Impedance_Data");
    if (measurement->numPoints > 0) {
        fprintf(file, "# Frequency_Hz,Z_Real_Ohm,Z_Imag_Ohm,Z_Mag_Ohm,Phase_Deg\n");
        
        for (int i = 0; i < measurement->numPoints; i++) {
            double magnitude = sqrt(measurement->zReal[i] * measurement->zReal[i] + 
                                  measurement->zImag[i] * measurement->zImag[i]);
            double phase = atan2(measurement->zImag[i], measurement->zReal[i]) * 180.0 / M_PI;
            
            fprintf(file, "%.3e,%.6e,%.6e,%.6e,%.3e\n",
                    measurement->frequencies[i],
                    measurement->zReal[i],
                    measurement->zImag[i],
                    magnitude,
                    phase);
        }
    } else {
        fprintf(file, "# No impedance data\n");
    }
    
    fclose(file);
    LogDebug("Saved EIS data: %s", filename);
    return SUCCESS;
}

/******************************************************************************
 * Device Control Functions
 ******************************************************************************/

static int SwitchToBioLogic(TempRampExperimentContext *ctx) {
    LogMessage("Switching to BioLogic...");
    
    int result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to disconnect PSB relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_CONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to connect BioLogic relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    LogMessage("Switched to BioLogic");
    return SUCCESS;
}

static int SafeDisconnectAllDevices(TempRampExperimentContext *ctx) {
    LogMessage("Disconnecting all devices...");

    BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_NORMAL);

    if (ENABLE_DTB) {
        DTB_SetRunStopAllQueued(0, DEVICE_PRIORITY_NORMAL);

        // Reset DTB control method back to PID mode for all devices
        // This ensures that even if the experiment is cancelled or encounters an error,
        // the DTB controllers are left in a state that allows subsequent experiments to run
        for (int i = 0; i < DTB_NUM_DEVICES; i++) {
            int slaveAddress = (i == 0) ? DTB1_SLAVE_ADDRESS : DTB2_SLAVE_ADDRESS;
            int result = DTB_SetControlMethodQueued(slaveAddress, CONTROL_METHOD_PID, DEVICE_PRIORITY_NORMAL);
            if (result != DTB_SUCCESS) {
                LogWarning("Failed to reset control method for DTB slave %d: %s",
                          slaveAddress, DTB_GetErrorString(result));
            }
        }
    }

    TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);

    LogMessage("Devices disconnected");
    return SUCCESS;
}

/******************************************************************************
 * Graph Functions
 ******************************************************************************/

static int ConfigureExperimentGraphs(TempRampExperimentContext *ctx) {
    ConfigureGraph(ctx->mainPanelHandle, ctx->graphTempHandle, 
                   "Temperature vs Time", "Time (min)", "Temperature (�C)", 
                   ctx->params.initialTemp - 5.0, 
                   ctx->params.finalTemp + 5.0);
    
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphNyquistHandle, ATTR_LABEL_TEXT, "Nyquist Plot");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphNyquistHandle, ATTR_XNAME, "Z' (Ohms)");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphNyquistHandle, ATTR_YNAME, "-Z'' (Ohms)");
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphNyquistHandle, VAL_BOTTOM_XAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphNyquistHandle, VAL_LEFT_YAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    
    ClearAllExperimentGraphs(ctx);
    return SUCCESS;
}

static void UpdateTemperaturePlot(TempRampExperimentContext *ctx, TempRampTempData *tempData) {
    double time_min = tempData->timestamp / 60.0;
    PlotPoint(ctx->mainPanelHandle, ctx->graphTempHandle, 
              time_min, tempData->dtbAverageTemperature, 
              VAL_SOLID_CIRCLE, VAL_RED);
}

static void UpdateNyquistPlot(TempRampExperimentContext *ctx, TempRampEISMeasurement *measurement) {
    if (measurement->numPoints == 0) return;
    
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graphNyquistHandle, -1, VAL_DELAYED_DRAW);
    
    double *negZImag = (double*)calloc(measurement->numPoints, sizeof(double));
    if (!negZImag) return;
    
    for (int i = 0; i < measurement->numPoints; i++) {
        negZImag[i] = -measurement->zImag[i];
    }
    
    PlotXY(ctx->mainPanelHandle, ctx->graphNyquistHandle,
           measurement->zReal, negZImag, measurement->numPoints,
           VAL_DOUBLE, VAL_DOUBLE, VAL_SCATTER,
           VAL_SOLID_CIRCLE, VAL_SOLID, 1, VAL_GREEN);
    
    char title[MEDIUM_BUFFER_SIZE];
    snprintf(title, sizeof(title), "Nyquist Plot - %.1f�C", measurement->temperature);
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphNyquistHandle, ATTR_LABEL_TEXT, title);
    
    free(negZImag);
}

static void ClearAllExperimentGraphs(TempRampExperimentContext *ctx) {
    int graphs[] = {ctx->graphTempHandle, ctx->graphNyquistHandle};
    ClearAllGraphs(ctx->mainPanelHandle, graphs, 2);
}

/******************************************************************************
 * Setup and File System Functions
 ******************************************************************************/

static int VerifyDevicesAndInitialize(TempRampExperimentContext *ctx) {
    if (!ENABLE_DTB) {
        MessagePopup("DTB Required", 
                     "Temperature control (DTB) is required for this experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
    if (!dtbQueueMgr) {
        MessagePopup("DTB Not Connected", 
                     "The DTB temperature controller is not connected.");
        return ERR_NOT_CONNECTED;
    }
    
    BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
    if (!bioQueueMgr) {
        MessagePopup("BioLogic Not Connected", 
                     "The BioLogic potentiostat is not connected.");
        return ERR_NOT_CONNECTED;
    }
    
    ctx->biologicID = BIO_QueueGetDeviceID(bioQueueMgr);
    if (ctx->biologicID < 0) {
        MessagePopup("BioLogic Not Connected", 
                     "The BioLogic potentiostat is not connected.");
        return ERR_NOT_CONNECTED;
    }
    
    TNYQueueManager *tnyQueueMgr = TNY_GetGlobalQueueManager();
    if (!tnyQueueMgr) {
        MessagePopup("Teensy Not Connected", 
                     "The Teensy relay controller is not connected.");
        return ERR_NOT_CONNECTED;
    }
    
    LogMessage("All required devices verified");
    return SUCCESS;
}

static int CreateExperimentFileSystem(TempRampExperimentContext *ctx) {
    char basePath[MAX_PATH_LENGTH];
    char dataPath[MAX_PATH_LENGTH];
    
    if (GetExecutableDirectory(basePath, sizeof(basePath)) != SUCCESS) {
        strcpy(basePath, ".");
    }
    
    snprintf(dataPath, sizeof(dataPath), "%s%s%s", 
             basePath, PATH_SEPARATOR, TEMP_RAMP_DATA_DIR);
    
    if (CreateDirectoryPath(dataPath) != SUCCESS) {
        LogError("Failed to create data directory: %s", dataPath);
        return ERR_BASE_FILE;
    }
    
    int result = CreateTimestampedDirectory(dataPath, "temp_ramp", 
                                          ctx->experimentDirectory, sizeof(ctx->experimentDirectory));
    if (result != SUCCESS) {
        LogError("Failed to create experiment directory");
        return result;
    }
    
    char logFile[MAX_PATH_LENGTH];
    snprintf(logFile, sizeof(logFile), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, TEMP_RAMP_LOG_FILE);
    ctx->experimentLogFile = fopen(logFile, "w");
    if (!ctx->experimentLogFile) {
        LogWarning("Failed to create experiment log");
    }
    
    char eisDir[MAX_PATH_LENGTH];
    snprintf(eisDir, sizeof(eisDir), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, TEMP_RAMP_EIS_DIR);
    if (CreateDirectoryPath(eisDir) != SUCCESS) {
        LogError("Failed to create EIS directory");
        return ERR_BASE_FILE;
    }
    
    char tempLogPath[MAX_PATH_LENGTH];
    snprintf(tempLogPath, sizeof(tempLogPath), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, TEMP_RAMP_TEMP_LOG_FILE);
    ctx->temperatureLogFile = fopen(tempLogPath, "w");
    if (!ctx->temperatureLogFile) {
        LogError("Failed to create temperature log");
        return ERR_BASE_FILE;
    }
    
    fprintf(ctx->temperatureLogFile, "Time_s,DTB_Avg_C,DTB_Setpoint_C,TC0_C,TC1_C\n");
    fflush(ctx->temperatureLogFile);
    
    LogMessage("Created experiment file system: %s", ctx->experimentDirectory);
    return SUCCESS;
}

static int SaveExperimentSettings(TempRampExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, TEMP_RAMP_SETTINGS_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create settings file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    time_t now = time(NULL);
    char timeStr[64];
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    
    fprintf(file, "# Temperature Ramp EIS Experiment Settings\n");
    fprintf(file, "# Created: %s\n", timeStr);
    fprintf(file, "# Battery Tester v%s\n\n", PROJECT_VERSION);
    
    WriteINISection(file, "Experiment_Parameters");
    WriteINIDouble(file, "Initial_Temperature_C", ctx->params.initialTemp, 1);
    WriteINIDouble(file, "Final_Temperature_C", ctx->params.finalTemp, 1);
    WriteINIDouble(file, "Ramp_Rate_C_per_min", ctx->params.rampRate, 1);
    WriteINIDouble(file, "EIS_Interval_min", ctx->params.eisInterval, 1);
    WriteINIValue(file, "Continue_Ramp_During_EIS", "%d (%s)",
                 ctx->params.continueRampDuringEIS,
                 ctx->params.continueRampDuringEIS ? "Continue" : "Pause");
    WriteINIValue(file, "Use_DTB_Ramp_Soak", "%d (%s)",
                 ctx->params.useRampSoak,
                 ctx->params.useRampSoak ? "New Implementation" : "Legacy Implementation");
    WriteINIValue(file, "Auto_Tune_Before_Ramp", "%d (%s)",
                 ctx->params.autoTuneBeforeRamp,
                 ctx->params.autoTuneBeforeRamp ? "Enabled" : "Disabled");
    fprintf(file, "\n");
    
    WriteINISection(file, "Device_Enable_Flags");
    WriteINIValue(file, "ENABLE_DTB", "%d", ENABLE_DTB);
    WriteINIValue(file, "ENABLE_BIOLOGIC", "%d", ENABLE_BIOLOGIC);
    WriteINIValue(file, "ENABLE_TNY", "%d", ENABLE_TNY);
    WriteINIValue(file, "ENABLE_CDAQ", "%d", ENABLE_CDAQ);
    fprintf(file, "\n");
    
    fclose(file);
    LogMessage("Settings saved: %s", filename);
    return SUCCESS;
}

/******************************************************************************
 * Results and Cleanup Functions
 ******************************************************************************/

static int WriteComprehensiveResults(TempRampExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, TEMP_RAMP_SUMMARY_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create summary file");
        return ERR_BASE_FILE;
    }
    
    time_t startTime = (time_t)ctx->experimentStartTime;
    time_t endTime = (time_t)ctx->experimentEndTime;
    char startTimeStr[64], endTimeStr[64];
    
    FormatTimestamp(startTime, startTimeStr, sizeof(startTimeStr));
    FormatTimestamp(endTime, endTimeStr, sizeof(endTimeStr));
    
    fprintf(file, "# TEMPERATURE RAMP EIS EXPERIMENT SUMMARY\n");
    fprintf(file, "# ========================================\n");
    fprintf(file, "# Generated by Battery Tester v%s\n\n", PROJECT_VERSION);
    
    WriteINISection(file, "Experiment_Overview");
    WriteINIValue(file, "Start_Time", "%s", startTimeStr);
    WriteINIValue(file, "End_Time", "%s", endTimeStr);
    WriteINIDouble(file, "Total_Duration_min", 
                  (ctx->experimentEndTime - ctx->experimentStartTime) / 60.0, 1);
    WriteINIDouble(file, "Initial_Temperature_C", ctx->params.initialTemp, 1);
    WriteINIDouble(file, "Final_Temperature_C", ctx->params.finalTemp, 1);
    WriteINIDouble(file, "Ramp_Rate_C_per_min", ctx->params.rampRate, 1);
    WriteINIDouble(file, "EIS_Interval_min", ctx->params.eisInterval, 1);
    fprintf(file, "\n");
    
    WriteINISection(file, "EIS_Measurements");
    WriteINIValue(file, "Total_Measurements", "%d", ctx->eisMeasurementCount);
    
    if (ctx->eisMeasurementCount > 0) {
        fprintf(file, "Temperatures=");
        for (int i = 0; i < ctx->eisMeasurementCount; i++) {
            fprintf(file, "%.1f", ctx->eisMeasurements[i].temperature);
            if (i < ctx->eisMeasurementCount - 1) fprintf(file, ",");
        }
        fprintf(file, "\n");
        
        fprintf(file, "OCV_Values=");
        for (int i = 0; i < ctx->eisMeasurementCount; i++) {
            fprintf(file, "%.6e", ctx->eisMeasurements[i].ocvVoltage);
            if (i < ctx->eisMeasurementCount - 1) fprintf(file, ",");
        }
        fprintf(file, "\n");
    }
    fprintf(file, "\n");
    
    fprintf(file, "# DATA FILES:\n");
    fprintf(file, "# Temperature Profile: %s\n", TEMP_RAMP_TEMP_LOG_FILE);
    fprintf(file, "# EIS Measurements: %s/\n", TEMP_RAMP_EIS_DIR);
    fprintf(file, "# Settings: %s\n", TEMP_RAMP_SETTINGS_FILE);
    
    fclose(file);
    LogMessage("Summary written: %s", filename);
    return SUCCESS;
}

static void CleanupExperiment(TempRampExperimentContext *ctx) {
    LogMessage("Cleaning up experiment...");
    
    SafeDisconnectAllDevices(ctx);
    
    ClearExternalLogFile();
    
    if (ctx->experimentLogFile) {
        fclose(ctx->experimentLogFile);
        ctx->experimentLogFile = NULL;
    }
    
    if (ctx->temperatureLogFile) {
        fclose(ctx->temperatureLogFile);
        ctx->temperatureLogFile = NULL;
    }
    
    if (ctx->eisMeasurements) {
        for (int i = 0; i < ctx->eisMeasurementCapacity; i++) {
            if (i < ctx->eisMeasurementCount || 
                ctx->eisMeasurements[i].ocvData || 
                ctx->eisMeasurements[i].geisData) {
                
                if (ctx->eisMeasurements[i].ocvData) {
                    BIO_FreeTechniqueData(ctx->eisMeasurements[i].ocvData);
                }
                if (ctx->eisMeasurements[i].geisData) {
                    BIO_FreeTechniqueData(ctx->eisMeasurements[i].geisData);
                }
                if (ctx->eisMeasurements[i].frequencies) {
                    free(ctx->eisMeasurements[i].frequencies);
                }
                if (ctx->eisMeasurements[i].zReal) {
                    free(ctx->eisMeasurements[i].zReal);
                }
                if (ctx->eisMeasurements[i].zImag) {
                    free(ctx->eisMeasurements[i].zImag);
                }
            }
        }
        free(ctx->eisMeasurements);
        ctx->eisMeasurements = NULL;
    }
    
    LogMessage("Cleanup completed");
}

static int CheckCancellation(TempRampExperimentContext *ctx) {
    return (ctx->cancelRequested || ctx->emergencyStop || 
            ctx->state == TEMP_RAMP_STATE_CANCELLED ||
            ctx->state == TEMP_RAMP_STATE_ERROR);
}
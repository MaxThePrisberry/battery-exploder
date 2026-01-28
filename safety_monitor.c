/******************************************************************************
 * safety_monitor.c
 *
 * Centralized Safety Monitor Module Implementation
 *
 * This module monitors sensor inputs (pressure, flow, temperature) and
 * evaluates safety conditions using boolean logic. All safety decisions
 * are made in a single function (EvaluateSafetyConditions) for easy
 * modification and verification.
 *
 * Architecture:
 *   1. Monitor thread runs at 2 Hz
 *   2. ReadSafetyInputs() - Read sensors via device queues
 *   3. EvaluateThresholds() - Compare to thresholds with debouncing
 *   4. EvaluateSafetyConditions() - ALL LOGIC IN ONE PLACE
 *   5. ExecuteSafetyActions() - Control valves + experiment
 ******************************************************************************/

#include "safety_monitor.h"
#include "logging.h"
#include "cdaq_utils.h"
#include "alicat_queue.h"
#include "dtb4848_queue.h"
#include "ni9472_queue.h"
#include <utility.h>

/******************************************************************************
 * Local Constants
 ******************************************************************************/

// Alarm configuration
#define SAFETY_ALARM_SOUND_BEEPS      5       // Number of beeps for alarm
#define SAFETY_ALARM_BEEP_FREQ_HZ     2000    // Beep frequency in Hz
#define SAFETY_ALARM_BEEP_DURATION_MS 500     // Duration of each beep in ms

/******************************************************************************
 * Module State
 ******************************************************************************/

// Thread control
static CmtThreadLockHandle g_safetyLock = 0;
static int g_monitorThreadId = 0;
static volatile int g_stopRequested = 0;
static volatile int g_forceCheckRequested = 0;

// Module state
static int g_initialized = 0;
static SafetyMonitorState g_state = {0};

// Registered experiment
static SafetyExperimentHandle g_experimentHandle = {0};
static int g_experimentRegistered = 0;

/******************************************************************************
 * Forward Declarations
 ******************************************************************************/

static int CVICALLBACK MonitorThreadFunc(void *functionData);
static int ReadSafetyInputs(SafetySensorInputs *inputs);
static void EvaluateThresholds(const SafetySensorInputs *inputs,
                               SafetyConditions *cond,
                               SafetyDebounceCounters *debounce);
static void EvaluateSafetyConditions(const SafetyConditions *cond,
                                     SafetyExperimentState expState,
                                     SafetyOutputs *out);
static int ExecuteSafetyActions(const SafetyOutputs *outputs);
static void SoundAlarm(void);

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

    // Create thread lock
    int result = CmtNewLock(NULL, 0, &g_safetyLock);
    if (result != 0) {
        LogErrorEx(LOG_DEVICE_SAFETY, "Failed to create thread lock: %d", result);
        return ERR_BASE_SAFETY - 1;
    }

    // Initialize state
    memset(&g_state, 0, sizeof(g_state));
    g_state.status = SAFETY_MONITOR_STOPPED;

    // Initialize debounce counters
    g_state.debounce.pcuBadCount = 0;
    g_state.debounce.scuBadCount = 0;
    g_state.debounce.flowBadCount = 0;
    g_state.debounce.tempBadCount = 0;

    // Initialize conditions to safe state
    g_state.conditions.pcuOK = 1;
    g_state.conditions.scuOK = 1;
    g_state.conditions.flowOK = 1;
    g_state.conditions.tempOK = 1;

    // Initialize outputs to safe state (valves closed, no action)
    g_state.outputs.experimentAction = SAFETY_ACTION_CONTINUE;
    g_state.outputs.valve1 = SAFETY_VALVE_CLOSED;
    g_state.outputs.valve2 = SAFETY_VALVE_CLOSED;
    g_state.outputs.violation = SAFETY_VIOLATION_NONE;
    g_state.outputs.violationMsg[0] = '\0';

    g_initialized = 1;
    g_stopRequested = 0;
    g_forceCheckRequested = 0;

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

    if (g_state.status == SAFETY_MONITOR_RUNNING) {
        CmtReleaseLock(g_safetyLock);
        LogWarningEx(LOG_DEVICE_SAFETY, "Monitor already running");
        return SUCCESS;
    }

    g_stopRequested = 0;

    // Start monitor thread
    int result = CmtScheduleThreadPoolFunction(g_threadPool, MonitorThreadFunc,
                                               NULL, &g_monitorThreadId);
    if (result != 0) {
        CmtReleaseLock(g_safetyLock);
        LogErrorEx(LOG_DEVICE_SAFETY, "Failed to start monitor thread: %d", result);
        return ERR_THREAD_CREATE;
    }

    g_state.status = SAFETY_MONITOR_RUNNING;
    CmtReleaseLock(g_safetyLock);

    LogMessageEx(LOG_DEVICE_SAFETY, "Safety monitor started (rate: %d Hz)",
                 SAFETY_MONITOR_RATE_HZ);
    return SUCCESS;
}

int SafetyMonitor_Stop(void)
{
    if (!g_initialized) {
        return SUCCESS;
    }

    CmtGetLock(g_safetyLock);

    if (g_state.status != SAFETY_MONITOR_RUNNING) {
        CmtReleaseLock(g_safetyLock);
        return SUCCESS;
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Stopping safety monitor...");

    g_stopRequested = 1;
    CmtReleaseLock(g_safetyLock);

    // Wait for thread to complete
    if (g_monitorThreadId != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_monitorThreadId,
                                               OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        g_monitorThreadId = 0;
    }

    CmtGetLock(g_safetyLock);
    g_state.status = SAFETY_MONITOR_STOPPED;
    CmtReleaseLock(g_safetyLock);

    LogMessageEx(LOG_DEVICE_SAFETY, "Safety monitor stopped");
    return SUCCESS;
}

void SafetyMonitor_Cleanup(void)
{
    if (!g_initialized) {
        return;
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Cleaning up safety monitor...");

    // Stop monitor thread if running
    SafetyMonitor_Stop();

    // Close valves to safe state
    if (ENABLE_NI9472 && NI9472_GetGlobalQueueManager() != NULL) {
        NI9472_SetChannelQueued(SAFETY_VALVE1_CHANNEL, NI9472_CHANNEL_LOW, DEVICE_PRIORITY_HIGH);
        NI9472_SetChannelQueued(SAFETY_VALVE2_CHANNEL, NI9472_CHANNEL_LOW, DEVICE_PRIORITY_HIGH);
    }

    // Discard lock
    if (g_safetyLock != 0) {
        CmtDiscardLock(g_safetyLock);
        g_safetyLock = 0;
    }

    // Clear state
    memset(&g_state, 0, sizeof(g_state));
    memset(&g_experimentHandle, 0, sizeof(g_experimentHandle));
    g_experimentRegistered = 0;
    g_initialized = 0;

    LogMessageEx(LOG_DEVICE_SAFETY, "Safety monitor cleanup complete");
}

/******************************************************************************
 * Public API - Experiment Registration
 ******************************************************************************/

int SafetyMonitor_RegisterExperiment(const SafetyExperimentHandle *handle)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    if (handle == NULL) {
        return ERR_NULL_POINTER;
    }

    CmtGetLock(g_safetyLock);

    if (g_experimentRegistered) {
        CmtReleaseLock(g_safetyLock);
        LogWarningEx(LOG_DEVICE_SAFETY, "Experiment already registered - unregister first");
        return ERR_INVALID_STATE;
    }

    // Copy handle
    g_experimentHandle.experimentName = handle->experimentName;
    g_experimentHandle.onStop = handle->onStop;
    g_experimentHandle.getState = handle->getState;
    g_experimentHandle.userData = handle->userData;
    g_experimentRegistered = 1;

    // Reset alarm state
    g_state.alarmActive = 0;
    g_state.alarmAcknowledged = 0;

    CmtReleaseLock(g_safetyLock);

    LogMessageEx(LOG_DEVICE_SAFETY, "Registered experiment: %s",
                 handle->experimentName ? handle->experimentName : "unnamed");
    return SUCCESS;
}

int SafetyMonitor_UnregisterExperiment(void)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    CmtGetLock(g_safetyLock);

    if (!g_experimentRegistered) {
        CmtReleaseLock(g_safetyLock);
        return SUCCESS;
    }

    const char *name = g_experimentHandle.experimentName;

    memset(&g_experimentHandle, 0, sizeof(g_experimentHandle));
    g_experimentRegistered = 0;

    // Reset to safe state
    g_state.experimentState = SAFETY_STATE_SAFE;

    CmtReleaseLock(g_safetyLock);

    LogMessageEx(LOG_DEVICE_SAFETY, "Unregistered experiment: %s",
                 name ? name : "unnamed");
    return SUCCESS;
}

int SafetyMonitor_HasExperiment(void)
{
    if (!g_initialized) {
        return 0;
    }

    CmtGetLock(g_safetyLock);
    int result = g_experimentRegistered;
    CmtReleaseLock(g_safetyLock);

    return result;
}

/******************************************************************************
 * Public API - Status and Control
 ******************************************************************************/

int SafetyMonitor_GetState(SafetyMonitorState *state)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    if (state == NULL) {
        return ERR_NULL_POINTER;
    }

    CmtGetLock(g_safetyLock);
    memcpy(state, &g_state, sizeof(SafetyMonitorState));
    CmtReleaseLock(g_safetyLock);

    return SUCCESS;
}

int SafetyMonitor_ForceCheck(void)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    g_forceCheckRequested = 1;
    return SUCCESS;
}

int SafetyMonitor_AcknowledgeAlarm(void)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    CmtGetLock(g_safetyLock);
    g_state.alarmAcknowledged = 1;
    CmtReleaseLock(g_safetyLock);

    LogMessageEx(LOG_DEVICE_SAFETY, "Alarm acknowledged");
    return SUCCESS;
}

int SafetyMonitor_EmergencyStop(void)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    LogErrorEx(LOG_DEVICE_SAFETY, "EMERGENCY STOP triggered!");

    CmtGetLock(g_safetyLock);

    // Set emergency state
    g_state.outputs.experimentAction = SAFETY_ACTION_EMERGENCY_STOP;
    g_state.outputs.valve1 = SAFETY_VALVE_CLOSED;
    g_state.outputs.valve2 = SAFETY_VALVE_CLOSED;
    g_state.outputs.violation = SAFETY_VIOLATION_MULTIPLE;
    snprintf(g_state.outputs.violationMsg, sizeof(g_state.outputs.violationMsg),
             "Manual emergency stop triggered");
    g_state.alarmActive = 1;

    // Execute actions
    ExecuteSafetyActions(&g_state.outputs);

    // Notify experiment
    if (g_experimentRegistered && g_experimentHandle.onStop) {
        g_experimentHandle.onStop(SAFETY_VIOLATION_MULTIPLE,
                                  g_state.outputs.violationMsg,
                                  g_experimentHandle.userData);
    }

    CmtReleaseLock(g_safetyLock);

    return SUCCESS;
}

int SafetyMonitor_SetValves(SafetyValveState valve1, SafetyValveState valve2)
{
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }

    if (!ENABLE_NI9472) {
        return ERR_NOT_SUPPORTED;
    }

    NI9472_QueueManager *mgr = NI9472_GetGlobalQueueManager();
    if (mgr == NULL) {
        return ERR_NOT_CONNECTED;
    }

    int result1 = NI9472_SetChannelQueued(SAFETY_VALVE1_CHANNEL,
                                          valve1 == SAFETY_VALVE_OPEN ? NI9472_CHANNEL_HIGH : NI9472_CHANNEL_LOW,
                                          DEVICE_PRIORITY_HIGH);
    int result2 = NI9472_SetChannelQueued(SAFETY_VALVE2_CHANNEL,
                                          valve2 == SAFETY_VALVE_OPEN ? NI9472_CHANNEL_HIGH : NI9472_CHANNEL_LOW,
                                          DEVICE_PRIORITY_HIGH);

    if (result1 != SUCCESS || result2 != SUCCESS) {
        LogErrorEx(LOG_DEVICE_SAFETY, "Failed to set valves: V1=%d, V2=%d", result1, result2);
        return ERR_OPERATION_FAILED;
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Valves set: V1=%s, V2=%s",
                 SafetyMonitor_ValveStateToString(valve1),
                 SafetyMonitor_ValveStateToString(valve2));
    return SUCCESS;
}

int SafetyMonitor_CheckStartConditions(SafetyMonitorState *state)
{
    if (!g_initialized) {
        return 0;
    }

    // Read current sensors
    SafetySensorInputs inputs = {0};
    int result = ReadSafetyInputs(&inputs);

    if (result != SUCCESS || !inputs.readSuccess) {
        LogWarningEx(LOG_DEVICE_SAFETY, "Cannot verify start conditions - sensor read failed");
        return 0;
    }

    // Check basic conditions (ignore experiment state since we're checking start)
    int pcuOK = (inputs.pcuPressure >= SAFETY_PCU_PRESSURE_MIN);
    int scuOK = (inputs.scuPressure >= SAFETY_SCU_PRESSURE_MIN);
    int flowOK = (inputs.massFlow >= SAFETY_FLOW_MIN);
    int tempOK = (inputs.temperature < SAFETY_TEMP_MAX_ABSOLUTE);

    // For start, we need PCU OK and flow OK
    int safeToStart = pcuOK && flowOK && tempOK;

    if (state != NULL) {
        SafetyMonitor_GetState(state);
    }

    if (!safeToStart) {
        LogWarningEx(LOG_DEVICE_SAFETY, "Not safe to start: PCU=%d SCU=%d Flow=%d Temp=%d",
                     pcuOK, scuOK, flowOK, tempOK);
    }

    return safeToStart;
}

/******************************************************************************
 * Public API - Direct Condition Access
 ******************************************************************************/

int SafetyMonitor_ReadSensors(SafetySensorInputs *inputs)
{
    if (inputs == NULL) {
        return ERR_NULL_POINTER;
    }

    return ReadSafetyInputs(inputs);
}

int SafetyMonitor_EvaluateConditions(const SafetySensorInputs *inputs,
                                     SafetyExperimentState expState,
                                     SafetyOutputs *outputs)
{
    if (inputs == NULL || outputs == NULL) {
        return ERR_NULL_POINTER;
    }

    // Convert inputs to conditions (no debouncing for direct evaluation)
    SafetyConditions cond;
    cond.pcuOK = (inputs->pcuPressure >= SAFETY_PCU_PRESSURE_MIN) ? 1 : 0;
    cond.scuOK = (inputs->scuPressure >= SAFETY_SCU_PRESSURE_MIN) ? 1 : 0;
    cond.flowOK = (inputs->massFlow >= SAFETY_FLOW_MIN) ? 1 : 0;
    cond.tempOK = (inputs->temperature < SAFETY_TEMP_MAX_ABSOLUTE) ? 1 : 0;

    // Evaluate using central function
    EvaluateSafetyConditions(&cond, expState, outputs);

    return SUCCESS;
}

/******************************************************************************
 * Monitor Thread
 ******************************************************************************/

static int CVICALLBACK MonitorThreadFunc(void *functionData)
{
    LogMessageEx(LOG_DEVICE_SAFETY, "Monitor thread started");

    double lastCheckTime = GetTimestamp();
    double checkInterval = 1.0 / SAFETY_MONITOR_RATE_HZ;

    while (!g_stopRequested) {
        double currentTime = GetTimestamp();

        // Check if it's time for a safety check
        int shouldCheck = g_forceCheckRequested ||
                         ((currentTime - lastCheckTime) >= checkInterval);

        if (shouldCheck) {
            g_forceCheckRequested = 0;
            lastCheckTime = currentTime;

            CmtGetLock(g_safetyLock);

            // Step 1: Read sensor inputs
            SafetySensorInputs inputs = {0};
            int readResult = ReadSafetyInputs(&inputs);
            g_state.lastInputs = inputs;

            if (readResult == SUCCESS && inputs.readSuccess) {
                // Step 2: Get current experiment state
                SafetyExperimentState expState = SAFETY_STATE_SAFE;
                if (g_experimentRegistered && g_experimentHandle.getState) {
                    expState = g_experimentHandle.getState(g_experimentHandle.userData);
                }
                g_state.experimentState = expState;

                // Step 3: Evaluate thresholds with debouncing
                EvaluateThresholds(&inputs, &g_state.conditions, &g_state.debounce);

                // Step 4: Evaluate safety conditions (THE CENTRAL LOGIC)
                SafetyOutputs newOutputs = {0};
                EvaluateSafetyConditions(&g_state.conditions, expState, &newOutputs);

                // Step 5: Check if action changed
                int actionChanged = (newOutputs.experimentAction != g_state.outputs.experimentAction) ||
                                   (newOutputs.valve1 != g_state.outputs.valve1) ||
                                   (newOutputs.valve2 != g_state.outputs.valve2);

                // Copy new outputs
                g_state.outputs = newOutputs;

                // Step 6: Execute actions if needed
                if (actionChanged || newOutputs.experimentAction != SAFETY_ACTION_CONTINUE) {
                    ExecuteSafetyActions(&newOutputs);

                    // Notify experiment if stop requested
                    if (newOutputs.experimentAction != SAFETY_ACTION_CONTINUE) {
                        g_state.totalViolations++;

                        if (!g_state.alarmActive) {
                            g_state.alarmActive = 1;
                            g_state.alarmAcknowledged = 0;

                            LogErrorEx(LOG_DEVICE_SAFETY, "Safety violation: %s",
                                       newOutputs.violationMsg);

                            // Sound alarm if not acknowledged
                            SoundAlarm();
                        }

                        // Notify experiment
                        if (g_experimentRegistered && g_experimentHandle.onStop) {
                            g_experimentHandle.onStop(newOutputs.violation,
                                                     newOutputs.violationMsg,
                                                     g_experimentHandle.userData);
                        }
                    }
                }

                // Clear alarm if conditions restored
                if (newOutputs.experimentAction == SAFETY_ACTION_CONTINUE && g_state.alarmActive) {
                    LogMessageEx(LOG_DEVICE_SAFETY, "Safety conditions restored");
                    g_state.alarmActive = 0;
                    g_state.alarmAcknowledged = 0;
                }
            } else {
                // Sensor read failed - log warning but don't trigger alarm
                LogWarningEx(LOG_DEVICE_SAFETY, "Sensor read failed - skipping check");
            }

            g_state.lastCheckTime = currentTime;
            g_state.totalChecks++;

            CmtReleaseLock(g_safetyLock);
        }

        // Sleep to avoid busy waiting
        Delay(0.05);  // 50ms sleep
    }

    LogMessageEx(LOG_DEVICE_SAFETY, "Monitor thread exiting");
    return 0;
}

/******************************************************************************
 * Sensor Reading
 ******************************************************************************/

static int ReadSafetyInputs(SafetySensorInputs *inputs)
{
    if (inputs == NULL) {
        return ERR_NULL_POINTER;
    }

    memset(inputs, 0, sizeof(SafetySensorInputs));
    inputs->readSuccess = 1;

    // Read PCU pressure (cDAQ slot 1, channel 0)
    if (ENABLE_CDAQ) {
        int result = CDAQ_ReadVoltage(SAFETY_PCU_CHANNEL, &inputs->pcuPressure);
        if (result != SUCCESS) {
            LogWarningEx(LOG_DEVICE_SAFETY, "Failed to read PCU pressure: %d", result);
            inputs->readSuccess = 0;
        }

        // Read SCU pressure (cDAQ slot 1, channel 1)
        result = CDAQ_ReadVoltage(SAFETY_SCU_CHANNEL, &inputs->scuPressure);
        if (result != SUCCESS) {
            LogWarningEx(LOG_DEVICE_SAFETY, "Failed to read SCU pressure: %d", result);
            inputs->readSuccess = 0;
        }
    } else {
        // If cDAQ disabled, assume OK
        inputs->pcuPressure = 5.0;
        inputs->scuPressure = 5.0;
    }

    // Read mass flow from ALICAT
    if (ENABLE_ALICAT && ALICAT_GetGlobalQueueManager() != NULL) {
        double flowRate = 0.0;
        int result = ALICAT_GetFlowRateQueued(ALICAT_MODBUS_ADDRESS, &flowRate, DEVICE_PRIORITY_LOW);
        if (result == SUCCESS) {
            inputs->massFlow = flowRate;
        } else {
            LogWarningEx(LOG_DEVICE_SAFETY, "Failed to read mass flow: %d", result);
            inputs->readSuccess = 0;
        }
    } else {
        // If ALICAT disabled, assume OK
        inputs->massFlow = 1000.0;
    }

    // Read temperature from DTB (get max of all devices)
    if (ENABLE_DTB && DTB_GetGlobalQueueManager() != NULL) {
        double maxTemp = -273.15;  // Absolute zero
        int anySuccess = 0;

        // Read from DTB devices
        int slaveAddresses[] = {DTB1_SLAVE_ADDRESS, DTB2_SLAVE_ADDRESS};
        for (int i = 0; i < DTB_NUM_DEVICES; i++) {
            double temp = 0.0;
            int result = DTB_GetProcessValueQueued(slaveAddresses[i], &temp, DEVICE_PRIORITY_LOW);
            if (result == SUCCESS) {
                if (temp > maxTemp) {
                    maxTemp = temp;
                }
                anySuccess = 1;
            }
        }

        if (anySuccess) {
            inputs->temperature = maxTemp;
        } else {
            LogWarningEx(LOG_DEVICE_SAFETY, "Failed to read any DTB temperature");
            inputs->readSuccess = 0;
        }
    } else {
        // If DTB disabled, assume safe temperature
        inputs->temperature = 25.0;
    }

    return SUCCESS;
}

/******************************************************************************
 * Threshold Evaluation with Debouncing
 ******************************************************************************/

static void EvaluateThresholds(const SafetySensorInputs *inputs,
                               SafetyConditions *cond,
                               SafetyDebounceCounters *debounce)
{
    // PCU pressure check with debouncing
    if (inputs->pcuPressure >= SAFETY_PCU_PRESSURE_MIN) {
        debounce->pcuBadCount = 0;
        cond->pcuOK = 1;
    } else {
        debounce->pcuBadCount++;
        if (debounce->pcuBadCount >= SAFETY_DEBOUNCE_PRESSURE) {
            cond->pcuOK = 0;
        }
    }

    // SCU pressure check with debouncing
    if (inputs->scuPressure >= SAFETY_SCU_PRESSURE_MIN) {
        debounce->scuBadCount = 0;
        cond->scuOK = 1;
    } else {
        debounce->scuBadCount++;
        if (debounce->scuBadCount >= SAFETY_DEBOUNCE_PRESSURE) {
            cond->scuOK = 0;
        }
    }

    // Flow check with debouncing
    if (inputs->massFlow >= SAFETY_FLOW_MIN) {
        debounce->flowBadCount = 0;
        cond->flowOK = 1;
    } else {
        debounce->flowBadCount++;
        if (debounce->flowBadCount >= SAFETY_DEBOUNCE_FLOW) {
            cond->flowOK = 0;
        }
    }

    // Temperature check with debouncing
    if (inputs->temperature < SAFETY_TEMP_MAX_ABSOLUTE) {
        debounce->tempBadCount = 0;
        cond->tempOK = 1;
    } else {
        debounce->tempBadCount++;
        if (debounce->tempBadCount >= SAFETY_DEBOUNCE_TEMP) {
            cond->tempOK = 0;
        }
    }
}

/******************************************************************************
 * CENTRAL SAFETY LOGIC - ALL DECISIONS MADE HERE
 ******************************************************************************/

/**
 * EvaluateSafetyConditions - THE HEART OF THE SAFETY SYSTEM
 *
 * All safety logic is in this ONE function. To modify safety behavior,
 * only change the logic here.
 *
 * Boolean Logic (from safety matrix spreadsheet):
 *   Inputs:
 *     PCU   = PCU Pressure OK (1=closed door, 0=open)
 *     SCU   = SCU Pressure OK (1=closed door, 0=open)
 *     Flow  = Mass Flow OK (1=flowing, 0=not flowing)
 *     State = Experiment State (1=dangerous, 0=safe)
 *
 *   Outputs:
 *     STOP = !PCU || !Flow || (State && !SCU)
 *     VALVE_OPEN = (State && (PCU || SCU)) || (!State && PCU && Flow)
 *
 * Temperature override: If temp exceeds absolute max, emergency stop regardless
 * of other conditions.
 */
static void EvaluateSafetyConditions(const SafetyConditions *cond,
                                     SafetyExperimentState expState,
                                     SafetyOutputs *out)
{
    // Initialize outputs
    memset(out, 0, sizeof(SafetyOutputs));
    out->experimentAction = SAFETY_ACTION_CONTINUE;
    out->valve1 = SAFETY_VALVE_CLOSED;
    out->valve2 = SAFETY_VALVE_CLOSED;
    out->violation = SAFETY_VIOLATION_NONE;
    out->violationMsg[0] = '\0';

    // Extract boolean inputs
    int PCU = cond->pcuOK;
    int SCU = cond->scuOK;
    int Flow = cond->flowOK;
    int State = (expState == SAFETY_STATE_DANGEROUS) ? 1 : 0;

    //=========================================================================
    // RULE 1: STOP = !PCU || !Flow || (State && !SCU)
    //=========================================================================
    int shouldStop = (!PCU) || (!Flow) || (State && !SCU);

    if (shouldStop) {
        out->experimentAction = SAFETY_ACTION_STOP;

        // Determine which condition caused the stop
        if (!PCU) {
            out->violation = SAFETY_VIOLATION_PCU_PRESSURE;
            snprintf(out->violationMsg, sizeof(out->violationMsg),
                     "PCU pressure below threshold - door may be open");
        } else if (!Flow) {
            out->violation = SAFETY_VIOLATION_FLOW;
            snprintf(out->violationMsg, sizeof(out->violationMsg),
                     "Mass flow below minimum threshold");
        } else if (State && !SCU) {
            out->violation = SAFETY_VIOLATION_SCU_PRESSURE;
            snprintf(out->violationMsg, sizeof(out->violationMsg),
                     "SCU pressure below threshold in dangerous state");
        }
    }

    //=========================================================================
    // RULE 2: VALVE_OPEN = (State && (PCU || SCU)) || (!State && PCU && Flow)
    //=========================================================================
    int valveOpen = (State && (PCU || SCU)) || (!State && PCU && Flow);

    out->valve1 = valveOpen ? SAFETY_VALVE_OPEN : SAFETY_VALVE_CLOSED;
    out->valve2 = valveOpen ? SAFETY_VALVE_OPEN : SAFETY_VALVE_CLOSED;

    //=========================================================================
    // RULE 3: Emergency over-temperature override
    //=========================================================================
    if (!cond->tempOK) {
        out->experimentAction = SAFETY_ACTION_EMERGENCY_STOP;
        out->violation = SAFETY_VIOLATION_TEMPERATURE;
        snprintf(out->violationMsg, sizeof(out->violationMsg),
                 "Temperature exceeds absolute maximum (%.1f deg C)",
                 SAFETY_TEMP_MAX_ABSOLUTE);

        // Close valves on emergency stop
        out->valve1 = SAFETY_VALVE_CLOSED;
        out->valve2 = SAFETY_VALVE_CLOSED;
    }

    //=========================================================================
    // Handle multiple violations
    //=========================================================================
    int violationCount = (!PCU ? 1 : 0) + (!Flow ? 1 : 0) +
                         ((State && !SCU) ? 1 : 0) + (!cond->tempOK ? 1 : 0);

    if (violationCount > 1) {
        out->violation = SAFETY_VIOLATION_MULTIPLE;
        snprintf(out->violationMsg, sizeof(out->violationMsg),
                 "Multiple safety violations: PCU=%d SCU=%d Flow=%d Temp=%d",
                 PCU, SCU, Flow, cond->tempOK);
    }
}

/******************************************************************************
 * Action Execution
 ******************************************************************************/

static int ExecuteSafetyActions(const SafetyOutputs *outputs)
{
    int result = SUCCESS;

    // Control valves via NI 9472
    if (ENABLE_NI9472 && NI9472_GetGlobalQueueManager() != NULL) {
        int v1State = (outputs->valve1 == SAFETY_VALVE_OPEN) ? NI9472_CHANNEL_HIGH : NI9472_CHANNEL_LOW;
        int v2State = (outputs->valve2 == SAFETY_VALVE_OPEN) ? NI9472_CHANNEL_HIGH : NI9472_CHANNEL_LOW;

        int r1 = NI9472_SetChannelQueued(SAFETY_VALVE1_CHANNEL, v1State, DEVICE_PRIORITY_HIGH);
        int r2 = NI9472_SetChannelQueued(SAFETY_VALVE2_CHANNEL, v2State, DEVICE_PRIORITY_HIGH);

        if (r1 != SUCCESS || r2 != SUCCESS) {
            LogErrorEx(LOG_DEVICE_SAFETY, "Failed to set valve states");
            result = ERR_OPERATION_FAILED;
        } else {
            LogDebugEx(LOG_DEVICE_SAFETY, "Valves set: V1=%s V2=%s",
                       SafetyMonitor_ValveStateToString(outputs->valve1),
                       SafetyMonitor_ValveStateToString(outputs->valve2));
        }
    }

    return result;
}

/******************************************************************************
 * Alarm
 ******************************************************************************/

static void SoundAlarm(void)
{
    // Use system beeps for alarm
    for (int i = 0; i < SAFETY_ALARM_SOUND_BEEPS; i++) {
        MessageBeep(MB_ICONEXCLAMATION);
        Delay(0.3);  // Brief delay between beeps
    }
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* SafetyMonitor_ViolationToString(SafetyViolationType violation)
{
    switch (violation) {
        case SAFETY_VIOLATION_NONE:         return "None";
        case SAFETY_VIOLATION_PCU_PRESSURE: return "PCU Pressure";
        case SAFETY_VIOLATION_SCU_PRESSURE: return "SCU Pressure";
        case SAFETY_VIOLATION_FLOW:         return "Mass Flow";
        case SAFETY_VIOLATION_TEMPERATURE:  return "Temperature";
        case SAFETY_VIOLATION_MULTIPLE:     return "Multiple";
        default:                            return "Unknown";
    }
}

const char* SafetyMonitor_ActionToString(SafetyAction action)
{
    switch (action) {
        case SAFETY_ACTION_CONTINUE:       return "Continue";
        case SAFETY_ACTION_STOP:           return "Stop";
        case SAFETY_ACTION_EMERGENCY_STOP: return "Emergency Stop";
        default:                           return "Unknown";
    }
}

const char* SafetyMonitor_ValveStateToString(SafetyValveState state)
{
    switch (state) {
        case SAFETY_VALVE_CLOSED: return "Closed";
        case SAFETY_VALVE_OPEN:   return "Open";
        default:                  return "Unknown";
    }
}

const char* SafetyMonitor_ExpStateToString(SafetyExperimentState state)
{
    switch (state) {
        case SAFETY_STATE_SAFE:      return "Safe";
        case SAFETY_STATE_DANGEROUS: return "Dangerous";
        default:                     return "Unknown";
    }
}

const char* SafetyMonitor_StatusToString(SafetyMonitorStatus status)
{
    switch (status) {
        case SAFETY_MONITOR_STOPPED: return "Stopped";
        case SAFETY_MONITOR_RUNNING: return "Running";
        case SAFETY_MONITOR_ERROR:   return "Error";
        default:                     return "Unknown";
    }
}

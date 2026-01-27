/******************************************************************************
 * safety_monitor.h
 *
 * Centralized Safety Monitor Module
 * Monitors sensor inputs and evaluates safety conditions in a single function.
 * Controls valves and experiment shutdown based on boolean safety logic.
 *
 * Boolean Logic (from safety matrix):
 *   Inputs:
 *     PCU = PCU Pressure OK (1=closed door, 0=open)
 *     SCU = SCU Pressure OK (1=closed door, 0=open)
 *     Flow = Mass Flow OK (1=flowing, 0=not flowing)
 *     State = Experiment State (1=dangerous, 0=safe)
 *
 *   Outputs:
 *     STOP = !PCU || !Flow || (State && !SCU)
 *     VALVE_OPEN = (State && (PCU || SCU)) || (!State && PCU && Flow)
 ******************************************************************************/
#ifndef SAFETY_MONITOR_H
#define SAFETY_MONITOR_H

#include "common.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Monitor thread timing
#define SAFETY_MONITOR_RATE_HZ          2       // Check sensors at 2 Hz (every 500ms)
#define SAFETY_MONITOR_INTERVAL_MS      500     // Milliseconds between checks

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Safety violation types
typedef enum {
    SAFETY_VIOLATION_NONE = 0,
    SAFETY_VIOLATION_PCU_PRESSURE,      // PCU pressure below threshold
    SAFETY_VIOLATION_SCU_PRESSURE,      // SCU pressure below threshold (in dangerous state)
    SAFETY_VIOLATION_FLOW,              // Mass flow below minimum
    SAFETY_VIOLATION_TEMPERATURE,       // Temperature above absolute maximum
    SAFETY_VIOLATION_MULTIPLE           // Multiple conditions violated
} SafetyViolationType;

// Safety action types
typedef enum {
    SAFETY_ACTION_CONTINUE = 0,         // No action needed
    SAFETY_ACTION_STOP,                 // Stop experiment normally
    SAFETY_ACTION_EMERGENCY_STOP        // Emergency stop (over-temperature)
} SafetyAction;

// Valve state
typedef enum {
    SAFETY_VALVE_CLOSED = 0,
    SAFETY_VALVE_OPEN = 1
} SafetyValveState;

// Experiment state for safety logic
typedef enum {
    SAFETY_STATE_SAFE = 0,              // Normal/safe state (T < dangerous threshold)
    SAFETY_STATE_DANGEROUS = 1          // Dangerous state (T >= dangerous threshold)
} SafetyExperimentState;

// Sensor inputs (raw readings)
typedef struct {
    double pcuPressure;                 // Volts from cDAQ slot 1 channel 0
    double scuPressure;                 // Volts from cDAQ slot 1 channel 1
    double massFlow;                    // SCCM from ALICAT
    double temperature;                 // Max temperature from DTB/cDAQ (deg C)
    int readSuccess;                    // 1 if all readings succeeded, 0 otherwise
} SafetySensorInputs;

// Boolean conditions (after threshold comparison and debouncing)
typedef struct {
    int pcuOK;                          // 1 = pressure above threshold
    int scuOK;                          // 1 = pressure above threshold
    int flowOK;                         // 1 = flow above minimum
    int tempOK;                         // 1 = temp below maximum
} SafetyConditions;

// Output actions from safety evaluation
typedef struct {
    SafetyAction experimentAction;      // CONTINUE, STOP, or EMERGENCY_STOP
    SafetyValveState valve1;            // OPEN or CLOSED (NI 9472 channel 0)
    SafetyValveState valve2;            // OPEN or CLOSED (NI 9472 channel 1)
    SafetyViolationType violation;      // Type of violation detected
    char violationMsg[256];             // Human-readable message
} SafetyOutputs;

// Monitor state
typedef enum {
    SAFETY_MONITOR_STOPPED = 0,
    SAFETY_MONITOR_RUNNING,
    SAFETY_MONITOR_ERROR
} SafetyMonitorStatus;

// Debounce counters
typedef struct {
    int pcuBadCount;                    // Consecutive bad PCU readings
    int scuBadCount;                    // Consecutive bad SCU readings
    int flowBadCount;                   // Consecutive bad flow readings
    int tempBadCount;                   // Consecutive bad temp readings
} SafetyDebounceCounters;

// Full monitor state (for status queries)
typedef struct {
    SafetyMonitorStatus status;
    SafetySensorInputs lastInputs;
    SafetyConditions conditions;
    SafetyOutputs outputs;
    SafetyExperimentState experimentState;
    SafetyDebounceCounters debounce;
    int alarmActive;
    int alarmAcknowledged;
    double lastCheckTime;
    int totalChecks;
    int totalViolations;
} SafetyMonitorState;

/******************************************************************************
 * Experiment Registration
 ******************************************************************************/

// Callback invoked when safety condition triggers experiment stop
// Parameters:
//   violation - Type of safety violation that triggered the stop
//   msg       - Human-readable description
//   userData  - User data passed during registration
typedef void (*SafetyStopCallback)(SafetyViolationType violation,
                                   const char *msg, void *userData);

// Callback to query current experiment state (safe vs dangerous)
// Parameters:
//   userData  - User data passed during registration
// Returns:
//   SAFETY_STATE_SAFE or SAFETY_STATE_DANGEROUS
typedef SafetyExperimentState (*SafetyStateCallback)(void *userData);

// Handle for experiment registration
typedef struct {
    const char *experimentName;         // Name for logging
    SafetyStopCallback onStop;          // Called when safety triggers stop
    SafetyStateCallback getState;       // Called to query experiment state
    void *userData;                     // User data passed to callbacks
} SafetyExperimentHandle;

/******************************************************************************
 * Public API - Lifecycle
 ******************************************************************************/

/**
 * Initialize the safety monitor module
 * Must be called before any other SafetyMonitor functions
 * @return SUCCESS or error code
 */
int SafetyMonitor_Initialize(void);

/**
 * Start the safety monitor thread
 * Begins continuous monitoring at SAFETY_MONITOR_RATE_HZ
 * @return SUCCESS or error code
 */
int SafetyMonitor_Start(void);

/**
 * Stop the safety monitor thread
 * Stops monitoring but keeps module initialized
 * @return SUCCESS or error code
 */
int SafetyMonitor_Stop(void);

/**
 * Cleanup and release all resources
 * Must be called during application shutdown
 */
void SafetyMonitor_Cleanup(void);

/******************************************************************************
 * Public API - Experiment Registration
 ******************************************************************************/

/**
 * Register an experiment with the safety monitor
 * Only one experiment can be registered at a time
 * @param handle - Experiment callbacks and info
 * @return SUCCESS or error code
 */
int SafetyMonitor_RegisterExperiment(const SafetyExperimentHandle *handle);

/**
 * Unregister the current experiment
 * Should be called when experiment completes or is cancelled
 * @return SUCCESS or error code
 */
int SafetyMonitor_UnregisterExperiment(void);

/**
 * Check if an experiment is currently registered
 * @return 1 if registered, 0 if not
 */
int SafetyMonitor_HasExperiment(void);

/******************************************************************************
 * Public API - Status and Control
 ******************************************************************************/

/**
 * Get current safety monitor state
 * @param state - Pointer to receive current state
 * @return SUCCESS or error code
 */
int SafetyMonitor_GetState(SafetyMonitorState *state);

/**
 * Force an immediate safety check
 * Bypasses the normal timing interval
 * @return SUCCESS or error code
 */
int SafetyMonitor_ForceCheck(void);

/**
 * Acknowledge an active safety alarm
 * Silences the alarm but does not clear the condition
 * @return SUCCESS or error code
 */
int SafetyMonitor_AcknowledgeAlarm(void);

/**
 * Trigger an emergency stop
 * Stops experiment and closes all valves
 * @return SUCCESS or error code
 */
int SafetyMonitor_EmergencyStop(void);

/**
 * Set valves to a specific state (for testing/override)
 * @param valve1 - State for valve 1
 * @param valve2 - State for valve 2
 * @return SUCCESS or error code
 */
int SafetyMonitor_SetValves(SafetyValveState valve1, SafetyValveState valve2);

/**
 * Check if it's safe to start an experiment
 * Verifies all safety conditions are met
 * @param state - Optional pointer to receive current state
 * @return 1 if safe to start, 0 if not
 */
int SafetyMonitor_CheckStartConditions(SafetyMonitorState *state);

/******************************************************************************
 * Public API - Direct Condition Access (for testing)
 ******************************************************************************/

/**
 * Read current sensor inputs (for testing/diagnostics)
 * @param inputs - Pointer to receive sensor readings
 * @return SUCCESS or error code
 */
int SafetyMonitor_ReadSensors(SafetySensorInputs *inputs);

/**
 * Evaluate safety conditions from inputs (for testing)
 * This exposes the central safety logic function
 * @param inputs - Sensor inputs
 * @param expState - Current experiment state
 * @param outputs - Pointer to receive outputs
 * @return SUCCESS or error code
 */
int SafetyMonitor_EvaluateConditions(const SafetySensorInputs *inputs,
                                     SafetyExperimentState expState,
                                     SafetyOutputs *outputs);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * Get string representation of violation type
 */
const char* SafetyMonitor_ViolationToString(SafetyViolationType violation);

/**
 * Get string representation of safety action
 */
const char* SafetyMonitor_ActionToString(SafetyAction action);

/**
 * Get string representation of valve state
 */
const char* SafetyMonitor_ValveStateToString(SafetyValveState state);

/**
 * Get string representation of experiment state
 */
const char* SafetyMonitor_ExpStateToString(SafetyExperimentState state);

/**
 * Get string representation of monitor status
 */
const char* SafetyMonitor_StatusToString(SafetyMonitorStatus status);

#endif // SAFETY_MONITOR_H

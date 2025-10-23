/******************************************************************************
 * pressure_safety.h
 *
 * Pressure Safety Monitoring Module Header
 * Monitors fume hood differential pressure for safe experiment conditions
 *
 * Safety Logic:
 *   SAFE PHASE (T < CRITICAL_TEMP):
 *     - Ventilation loss → STOP EXPERIMENT
 *   CRITICAL PHASE (T >= CRITICAL_TEMP):
 *     - Ventilation loss → ALARM + CONTINUE (controlled runaway safer)
 ******************************************************************************/
#ifndef PRESSURE_SAFETY_H
#define PRESSURE_SAFETY_H

#include "common.h"

/******************************************************************************
 * Configuration Constants - EDIT THESE AS NEEDED
 ******************************************************************************/

// Pressure differential thresholds (Volts from NI 9202 channel 0)
// TODO: Calibrate these values based on your differential pressure sensor
#define PRESSURE_THRESHOLD_OK_MIN       2.95    // Minimum voltage for "ventilation OK"
#define PRESSURE_THRESHOLD_LOST_MAX     2.85    // Maximum voltage for "ventilation LOST"
// Hysteresis: System needs to drop below LOST_MAX to trigger alarm,
//             but must rise above OK_MIN to clear alarm

// Temperature thresholds (degrees C)
#define PRESSURE_SAFE_TEMP_THRESHOLD    30.0   // Below this = SAFE phase
                                                // At/above this = CRITICAL phase

// Monitoring configuration
#define PRESSURE_MONITOR_RATE_HZ        1      // Check pressure every 1 second
#define PRESSURE_ALARM_DEBOUNCE_COUNT   3      // Require 3 consecutive bad readings
                                                // before triggering alarm (3 seconds)

// Alarm configuration
#define PRESSURE_ALARM_SOUND_BEEPS      5      // Number of beeps for alarm
#define PRESSURE_ALARM_BEEP_FREQ_HZ     1000   // Beep frequency in Hz
#define PRESSURE_ALARM_BEEP_DURATION_MS 200    // Duration of each beep

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Ventilation status
typedef enum {
    VENTILATION_UNKNOWN = 0,    // Status not yet determined
    VENTILATION_OK,             // Pressure differential indicates good ventilation
    VENTILATION_DEGRADED,       // In hysteresis zone
    VENTILATION_LOST            // Pressure differential too low
} VentilationStatus;

// Experiment phase for safety logic
typedef enum {
    PHASE_SAFE = 0,             // T < CRITICAL_TEMP (stop if ventilation lost)
    PHASE_CRITICAL              // T >= CRITICAL_TEMP (alarm if ventilation lost, but continue)
} ExperimentPhase;

// Pressure safety state
typedef struct {
    VentilationStatus ventStatus;
    ExperimentPhase phase;
    double currentPressureVoltage;
    double currentTemperature;
    int consecutiveBadReadings;
    int alarmActive;
    int monitoringEnabled;
    double lastCheckTime;
} PressureSafetyState;

// Callback function types
typedef void (*VentilationLostCallback)(ExperimentPhase phase, double temperature, double pressure);
typedef void (*VentilationRestoredCallback)(double pressure);

/******************************************************************************
 * Public Function Declarations
 ******************************************************************************/

/**
 * Initialize the pressure safety monitoring module
 * @return SUCCESS or error code
 */
int PressureSafety_Initialize(void);

/**
 * Start pressure monitoring for an experiment
 * @param initialTemp - Initial battery temperature (deg C)
 * @param onVentLost - Callback when ventilation is lost (can be NULL)
 * @param onVentRestored - Callback when ventilation restored (can be NULL)
 * @return SUCCESS or error code
 */
int PressureSafety_StartMonitoring(double initialTemp,
                                   VentilationLostCallback onVentLost,
                                   VentilationRestoredCallback onVentRestored);

/**
 * Stop pressure monitoring
 * @return SUCCESS or error code
 */
int PressureSafety_StopMonitoring(void);

/**
 * Update the current battery temperature (call during experiment)
 * This determines which phase the experiment is in
 * @param temperature - Current battery temperature (deg C)
 * @return SUCCESS or error code
 */
int PressureSafety_UpdateTemperature(double temperature);

/**
 * Get current ventilation status
 * @param status - Pointer to receive status
 * @return SUCCESS or error code
 */
int PressureSafety_GetStatus(VentilationStatus *status);

/**
 * Get current safety state
 * @param state - Pointer to receive full state structure
 * @return SUCCESS or error code
 */
int PressureSafety_GetState(PressureSafetyState *state);

/**
 * Check if it's safe to start an experiment (ventilation OK)
 * @param pressureVoltage - Pointer to receive current pressure reading
 * @return 1 if safe to start, 0 if not safe
 */
int PressureSafety_CheckStartConditions(double *pressureVoltage);

/**
 * Manually acknowledge alarm (for emergency override)
 * This silences the alarm but does not change ventilation status
 * @return SUCCESS or error code
 */
int PressureSafety_AcknowledgeAlarm(void);

/**
 * Clean up and release all resources
 */
void PressureSafety_Cleanup(void);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * Get string description of ventilation status
 */
const char* PressureSafety_StatusToString(VentilationStatus status);

/**
 * Get string description of experiment phase
 */
const char* PressureSafety_PhaseToString(ExperimentPhase phase);

#endif // PRESSURE_SAFETY_H

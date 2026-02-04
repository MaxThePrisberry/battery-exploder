/******************************************************************************
 * safety_monitor.h
 *
 * Simplified Safety Monitor Module
 * Monitors SCU pressure via cDAQ channel 0 and controls solenoid valves.
 *
 * Safety Logic:
 *   - Start experiment: open solenoid valves (nitrogen flows)
 *   - Stop experiment: close solenoid valves (nitrogen stops)
 *   - Single safety condition: SCU pressure (cDAQ channel 0) must be >= 3.42 V
 *   - If SCU drops below 3.42 V for SAFETY_DEBOUNCE_COUNT consecutive reads
 *     at 2 Hz, stop everything.
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

// Monitor status
typedef enum {
    SAFETY_MONITOR_STOPPED = 0,
    SAFETY_MONITOR_RUNNING
} SafetyMonitorStatus;

// Callback invoked when safety condition triggers experiment stop
// Parameters:
//   msg       - Human-readable description
//   userData  - User data passed during registration
typedef void (*SafetyStopCallback)(const char *msg, void *userData);

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
 * Start the safety monitor background thread
 * Begins continuous SCU pressure monitoring at SAFETY_MONITOR_RATE_HZ
 * @return SUCCESS or error code
 */
int SafetyMonitor_Start(void);

/**
 * Stop the safety monitor background thread
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
 * Register an experiment stop callback with the safety monitor
 * Only one experiment can be registered at a time
 * @param onStop    - Callback invoked when SCU pressure violation is detected
 * @param userData  - User data passed to the callback
 * @return SUCCESS or error code
 */
int SafetyMonitor_RegisterExperiment(SafetyStopCallback onStop, void *userData);

/**
 * Unregister the current experiment
 * Should be called when experiment completes or is cancelled
 * @return SUCCESS or error code
 */
int SafetyMonitor_UnregisterExperiment(void);

/******************************************************************************
 * Public API - Start Condition & Valve Control
 ******************************************************************************/

/**
 * Check if SCU pressure meets the start condition (>= SAFETY_SCU_PRESSURE_MIN)
 * Reads the current SCU voltage and returns whether it's safe to start.
 * @param scuVoltage - Optional pointer to receive the current SCU voltage reading
 * @return 1 if SCU voltage >= threshold, 0 if not (or read failed)
 */
int SafetyMonitor_CheckStartCondition(double *scuVoltage);

/**
 * Open both solenoid valves (NI 9472 channels 0 & 1 HIGH)
 * Call at experiment start to begin nitrogen flow.
 * @return SUCCESS or error code
 */
int SafetyMonitor_OpenValves(void);

/**
 * Close both solenoid valves (NI 9472 channels 0 & 1 LOW)
 * Call at experiment stop/cleanup to stop nitrogen flow.
 * @return SUCCESS or error code
 */
int SafetyMonitor_CloseValves(void);

#endif // SAFETY_MONITOR_H

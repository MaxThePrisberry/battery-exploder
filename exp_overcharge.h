/******************************************************************************
 * exp_overcharge.h
 *
 * Overcharge Thermal Runaway Experiment Module
 * Constant-current overcharge with periodic EIS measurements
 ******************************************************************************/

#ifndef EXP_OVERCHARGE_H
#define EXP_OVERCHARGE_H

#include "common.h"
#include "psb10000/psb10000_dll.h"
#include "psb10000/psb10000_queue.h"
#include "biologic/biologic_dll.h"
#include "biologic/biologic_queue.h"
#include "biologic/biologic_abstract.h"
#include "teensy/teensy_dll.h"
#include "teensy/teensy_queue.h"
#include "dtb4848/dtb4848_dll.h"
#include "dtb4848/dtb4848_queue.h"
#include "alicat basis 2/alicat_dll.h"
#include "alicat basis 2/alicat_queue.h"
#include "cdaq_utils.h"
#include "pressure_safety.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// File System Structure
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

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Experiment state
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

// Experiment parameters
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

// Temperature data point (reuse from exp_temp_ramp)
typedef struct {
    double timestamp;                        // Time since experiment start (s)
    double dtbTemperatures[DTB_NUM_DEVICES]; // All DTB temperatures (deg C)
    double dtbAverageTemperature;            // Average DTB temperature (deg C)
    int dtbDeviceCount;                      // Number of DTB devices
    double dtbSetpoint;                      // DTB setpoint (deg C)
    double tc0Temperature;                   // Thermocouple 0 (deg C)
    double tc1Temperature;                   // Thermocouple 1 (deg C)
    double actualRampRate;                   // Not used in overcharge
    char status[128];                        // Status message
} OverchargeTempData;

// EIS measurement data
typedef struct {
    int measurementIndex;        // Sequential measurement number
    double chargeDelivered_mAh;  // Charge at time of measurement
    double socPercent;           // SOC when measured (if nominal capacity provided)
    double timestamp;            // Time since experiment start (s)
    double ocvVoltage;          // Open circuit voltage (V)
    OverchargeTempData tempData; // Temperature readings
    BIO_TechniqueData *ocvData;  // Raw OCV data
    BIO_TechniqueData *geisData; // Raw GEIS data
    double *frequencies;         // Frequency array (Hz)
    double *zReal;              // Real impedance (Ohm)
    double *zImag;              // Imaginary impedance (Ohm)
    int numPoints;              // Number of impedance points
    int retryCount;             // Retry attempts
    char filename[MAX_PATH_LENGTH]; // Data filename
} OverchargeEISMeasurement;

// Experiment context
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
    OverchargeEISMeasurement *eisMeasurements;  // Dynamic array
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
    int modeControl;
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

} OverchargeExperimentContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

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
 * @return 1 if running, 0 if not
 */
int OverchargeExperiment_IsRunning(void);

/**
 * Abort running experiment
 * @return SUCCESS or error code
 */
int OverchargeExperiment_Abort(void);

/**
 * Emergency stop - immediate halt
 * @return SUCCESS or error code
 */
int OverchargeExperiment_EmergencyStop(void);

/**
 * Module cleanup
 */
void OverchargeExperiment_Cleanup(void);

#endif // EXP_OVERCHARGE_H

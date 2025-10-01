/******************************************************************************
 * exp_runaway.h
 * 
 * Thermal Ramp Runaway Battery Experiment Module
 * Measures EIS of the battery under temperature ramp
 ******************************************************************************/

#ifndef EXP_RUNAWAY_H
#define EXP_RUNAWAY_H

#include "common.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "teensy_dll.h"
#include "teensy_queue.h"
#include "dtb4848_dll.h"
#include "dtb4848_queue.h"
#include "cdaq_utils.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Battery Settling Time
#define RUNAWAY_SETTLING_TIME          10.0    // Seconds to wait for battery relaxation after opening the relay

// Temperature Control Constants
#define RUNAWAY_TEMP_TOLERANCE         2.0     // °C tolerance for temperature target
#define RUNAWAY_TEMP_CHECK_INTERVAL    10.0    // Seconds between temperature checks
#define RUNAWAY_MAX_TEMP			   150	   // °C maximum temperature for safety

// EIS Retry and Timeout
#define RUNAWAY_MAX_EIS_RETRY          2       // Retry failed measurements twice
#define RUNAWAY_EIS_RETRY_DELAY        5.0     // Seconds to wait between retries

// File System Structure
#define RUNAWAY_DATA_DIR               "data"
#define RUNAWAY_SUMMARY_FILE           "summary.txt"
#define RUNAWAY_SETTINGS_FILE          "experiment_settings.ini"
#define RUNAWAY_LOG_FILE               "experiment.log"

#define RUNAWAY_TEMP_OCV_FILE     "time_temp_ocv.csv"
#define RUNAWAY_EIS_DIR         "eis_measurements"

// Experiment Limits and Safety
#define RUNAWAY_MAX_EXPERIMENT_TIME    3600   // 1 hours maximum experiment time

#define RUNAWAY__HEADER "Time_s,Voltage_V,DTB1_Temp_C,DTB2_Temp_C,TC0_Temp_C,TC1_Temp_C"


/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Experiment state
typedef enum {
    RUNAWAY_STATE_IDLE = 0,
    RUNAWAY_STATE_PREPARING,
    RUNAWAY_STATE_PHASE1_DISCHARGE,
    RUNAWAY_STATE_PHASE1_TEMP_WAIT,
    RUNAWAY_STATE_PHASE1_TEMP_STABILIZE,
    RUNAWAY_STATE_PHASE2_CHARGE,
    RUNAWAY_STATE_PHASE2_DISCHARGE,
    RUNAWAY_STATE_PHASE3_SETUP,
    RUNAWAY_STATE_PHASE3_CHARGING,
    RUNAWAY_STATE_PHASE3_EIS_MEASUREMENT,
    RUNAWAY_STATE_PHASE4_DISCHARGE,
    RUNAWAY_STATE_COMPLETED,
    RUNAWAY_STATE_ERROR,
    RUNAWAY_STATE_CANCELLED
} BaselineExperimentState;

// Experiment phase identifier
typedef enum {
    RUNAWAY_PHASE_1 = 1,  // Initial discharge + temperature setup
    RUNAWAY_PHASE_2,      // Capacity experiment (charge ? discharge)
    RUNAWAY_PHASE_3,      // EIS measurements during charge
    RUNAWAY_PHASE_4       // Discharge to 50%
} BaselineExperimentPhase;

// Experiment parameters from UI
typedef struct {
    double targetTemperature;    // DTB target temperature (°C)
    double eisInterval;          // SOC percentage between EIS measurements
    double currentThreshold;     // Current threshold for operation completion (A)
    unsigned int logInterval;    // Data logging interval in seconds
    double chargeVoltage;        // Maximum charge voltage (V)
    double dischargeVoltage;     // Minimum discharge voltage (V)
    double chargeCurrent;        // Maximum charge current (A)
    double dischargeCurrent;     // Maximum discharge current (A)
} BaselineExperimentParams;

// Temperature data point
typedef struct {
    double timestamp;                    // Time since experiment start (s)
    double dtbTemperatures[DTB_NUM_DEVICES]; // All DTB measured temperatures (°C)
    double dtbAverageTemperature;        // Average DTB temperature (°C)
    int dtbDeviceCount;                  // Number of DTB devices that responded
    double tc0Temperature;               // Thermocouple 0 temperature (°C)
    double tc1Temperature;               // Thermocouple 1 temperature (°C)
    char status[128];                    // Temperature controller status
} TemperatureDataPoint;

// Generic data point for logging
typedef struct {
    double timestamp;            // Time since experiment start (s)
    double voltage;              // Battery voltage (V)
    double current;              // Battery current (A)
    double power;                // Power (W)
    double soc;                  // State of charge (%)
    TemperatureDataPoint tempData; // Temperature measurements
    BaselineExperimentPhase phase; // Current phase
    char phaseDescription[128];  // Human-readable phase description
} BaselineDataPoint;

// EIS measurement data
typedef struct {
    int measurementIndex;        // Sequential measurement number
    double targetSOC;            // Target SOC for this measurement
    double actualSOC;            // Actual SOC when measured
    double ocvVoltage;           // Open circuit voltage (V)
    double timestamp;            // Time since experiment start (s)
    TemperatureDataPoint tempData; // Temperature readings during measurement
    BIO_TechniqueData *ocvData;   // Raw OCV data
    BIO_TechniqueData *geisData;  // Raw GEIS data
    // Processed impedance data
    double *frequencies;         // Array of frequencies (Hz)
    double *zReal;              // Real impedance values (Ohm)
    double *zImag;              // Imaginary impedance values (Ohm)
    int numPoints;              // Number of impedance points
    int retryCount;             // Number of retries for this measurement
    char filename[MAX_PATH_LENGTH]; // Saved data filename
} BaselineEISMeasurement;

// Phase results tracking
typedef struct {
    BaselineExperimentPhase phase;
    double startTime;            // Phase start time (s since experiment start)
    double endTime;              // Phase end time (s since experiment start)
    double duration;             // Phase duration (s)
    double capacity_mAh;         // Capacity transferred in this phase (mAh)
    double energy_Wh;            // Energy transferred in this phase (Wh)
    double startVoltage;         // Starting voltage (V)
    double endVoltage;           // Ending voltage (V)
    double avgCurrent;           // Average current magnitude (A)
    double avgVoltage;           // Average voltage (V)
    double avgTemperature_dtb;   // Average DTB temperature (°C)
    double avgTemperature_tc0;   // Average TC0 temperature (°C)
    double avgTemperature_tc1;   // Average TC1 temperature (°C)
    int dataPointCount;          // Number of data points collected
    double peakCurrent;          // Peak current observed (A)
    char completionReason[128];  // Why the phase ended
    char phaseDirectory[MAX_PATH_LENGTH]; // Directory for this phase's data
} BaselinePhaseResults;

// Experiment context
typedef struct {
    BaselineExperimentState state;
    BaselineExperimentParams params;
    BaselineExperimentPhase currentPhase;
    
    // Cancellation handling (dual approach for redundancy)
    volatile int cancelRequested;     // Thread-safe cancellation flag
    volatile int emergencyStop;       // Emergency stop flag (device failures)
    
    // Timing and progress
    double experimentStartTime;       // Experiment start timestamp
    double experimentEndTime;         // Experiment end timestamp
    double phaseStartTime;           // Current phase start time
    double lastLogTime;              // Last data logging time
    double lastGraphUpdate;          // Last graph update time
    double lastTempCheck;            // Last temperature check time
    
    // Temperature management
    int dtbReady;                    // Flag indicating DTB reached target
    int temperatureStable;           // Flag indicating temperature is stable
    double temperatureStabilizationStart; // When temp reached target
    
    // Capacity tracking and SOC management
    double measuredChargeCapacity_mAh;    // From Phase 2 charge
    double measuredDischargeCapacity_mAh; // From Phase 2 discharge
    double currentSOC;               // Current state of charge (0-100%+)
    double accumulatedCapacity_mAh;  // For coulomb counting
    double lastCurrent;              // For trapezoidal integration
    double lastTime;                 // For time-based calculations
    double estimatedBatteryCapacity_mAh; // Dynamic capacity estimate
    
    // EIS measurements and dynamic SOC management
    BaselineEISMeasurement *eisMeasurements;    // Array of measurements
    int eisMeasurementCount;         // Number of completed measurements
    int eisMeasurementCapacity;      // Array capacity
    double *targetSOCs;              // Array of target SOC points
    int numTargetSOCs;              // Number of planned SOC targets
    int targetSOCCapacity;          // Array capacity for dynamic growth
    int dynamicTargetsAdded;        // Count of targets added beyond initial plan
    
    // Phase results
    BaselinePhaseResults phase1Results;
    BaselinePhaseResults phase2ChargeResults;
    BaselinePhaseResults phase2DischargeResults;
    BaselinePhaseResults phase3Results;
    BaselinePhaseResults phase4Results;
    
    // File system and logging
    char experimentDirectory[MAX_PATH_LENGTH];
    char currentPhaseDirectory[MAX_PATH_LENGTH];
    FILE *currentPhaseLogFile;      // Current phase data log
	FILE *baselineExperimentLog;    // Experiment log file
    
    // UI handles
    int mainPanelHandle;
    int tabPanelHandle;
    int buttonControl;
    int outputControl;              // RUNAWAY_NUM_OUTPUT
    int statusControl;              // RUNAWAY_STR_RUNAWAY_STATUS
    int graph1Handle;               // Current vs Time
    int graph2Handle;               // Voltage vs Time / OCV vs SOC
    int graphBiologicHandle;        // Nyquist plot
    
    // Device handles and configuration
    PSB_Handle *psbHandle;
    int biologicID;
    
    // Graph plot handles
    int currentPlotHandle;
    int voltagePlotHandle;
    int ocvPlotHandle;
    int nyquistPlotHandle;
    
} BaselineExperimentContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

/**
 * Main callback for starting/stopping baseline experiment
 */
int CVICALLBACK StartBaselineExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2);

/**
 * Check if a baseline experiment is running
 * @return 1 if running, 0 if not
 */
int BaselineExperiment_IsRunning(void);

/**
 * Abort a running baseline experiment
 * @return SUCCESS or error code
 */
int BaselineExperiment_Abort(void);

/**
 * Emergency stop - immediately halt all device operations
 * @return SUCCESS or error code
 */
int BaselineExperiment_EmergencyStop(void);

/**
 * Cleanup baseline experiment module
 */
void BaselineExperiment_Cleanup(void);

#endif // EXP_RUNAWAY_H
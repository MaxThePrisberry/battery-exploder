/******************************************************************************
 * exp_temp_ramp.h
 * 
 * Temperature Ramp EIS Experiment Module
 * EIS measurements during controlled temperature ramping
 ******************************************************************************/

#ifndef EXP_TEMP_RAMP_H
#define EXP_TEMP_RAMP_H

#include "common.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "teensy_dll.h"
#include "teensy_queue.h"
#include "dtb4848_dll.h"
#include "dtb4848_queue.h"
#include "alicat_dll.h"
#include "alicat_queue.h"
#include "cdaq_utils.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Temperature Control Constants
#define TEMP_RAMP_TOLERANCE             1.0     // °C tolerance for reaching target
#define TEMP_RAMP_CHECK_INTERVAL        5.0     // Seconds between temperature checks
#define TEMP_RAMP_STABILIZE_TIME        60     // Seconds to stabilize at initial temp
#define TEMP_RAMP_HOLD_TIME             60     // Seconds to hold at final temp
#define TEMP_RAMP_TIMEOUT_SEC           3600    // Max wait for initial temperature

// EIS Configuration
#define TEMP_RAMP_MAX_EIS_RETRY         2       // Retry failed measurements
#define TEMP_RAMP_EIS_RETRY_DELAY       5.0     // Seconds between retries

// File System Structure
#define TEMP_RAMP_DATA_DIR              "data"
#define TEMP_RAMP_SUMMARY_FILE          "summary.txt"
#define TEMP_RAMP_SETTINGS_FILE         "experiment_settings.ini"
#define TEMP_RAMP_LOG_FILE              "experiment.log"
#define TEMP_RAMP_TEMP_LOG_FILE         "temperature_profile.csv"
#define TEMP_RAMP_EIS_DIR               "eis_measurements"

// Experiment Limits
#define TEMP_RAMP_MAX_EXPERIMENT_TIME   28800   // 8 hours maximum

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Experiment state
typedef enum {
    TEMP_RAMP_STATE_IDLE = 0,
    TEMP_RAMP_STATE_PREPARING,
    TEMP_RAMP_STATE_REACHING_INITIAL_TEMP,
    TEMP_RAMP_STATE_STABILIZING_INITIAL,
    TEMP_RAMP_STATE_RAMPING,
    TEMP_RAMP_STATE_EIS_MEASUREMENT,
    TEMP_RAMP_STATE_HOLDING_FINAL,
    TEMP_RAMP_STATE_COMPLETED,
    TEMP_RAMP_STATE_ERROR,
    TEMP_RAMP_STATE_CANCELLED
} TempRampExperimentState;

// Experiment parameters
typedef struct {
    double initialTemp;          // Starting temperature (°C)
    double finalTemp;            // Final temperature (°C)
    double rampRate;             // Heating rate (°C/min)
    double eisInterval;          // Time between EIS measurements (minutes)
    int continueRampDuringEIS;   // 1 = continue ramping during EIS, 0 = pause ramp
} TempRampExperimentParams;

// Temperature data point
typedef struct {
    double timestamp;                        // Time since experiment start (s)
    double dtbTemperatures[DTB_NUM_DEVICES]; // All DTB temperatures (°C)
    double dtbAverageTemperature;            // Average DTB temperature (°C)
    int dtbDeviceCount;                      // Number of DTB devices
    double dtbSetpoint;                      // DTB setpoint (°C)
    double tc0Temperature;                   // Thermocouple 0 (°C)
    double tc1Temperature;                   // Thermocouple 1 (°C)
    char status[128];                        // Status message
} TempRampTempData;

// Gas flow data point
typedef struct {
    double timestamp;                        // Time since experiment start (s)
    double alicatMassFLow[ALICAT_NUM_DEVICES]; // All ALICAT mass flow rates (check headers for unit)
    double alicatTemperature[ALICAT_NUM_DEVICES]; // All ALICAT temperature (°C)
    int alicatDeviceCount;                      // Number of DTB devices
    double alicatSetpoint[ALICAT_NUM_DEVICES];  // ALICAT setpoint (check headers for unit)
    char status[128];                        // Status message
} MFlowData;

// EIS measurement data
typedef struct {
    int measurementIndex;        // Sequential measurement number
    double temperature;          // Temperature when measured (°C)
    double timestamp;            // Time since experiment start (s)
    double ocvVoltage;          // Open circuit voltage (V)
    TempRampTempData tempData;  // Temperature readings
    BIO_TechniqueData *ocvData;  // Raw OCV data
    BIO_TechniqueData *geisData; // Raw GEIS data
    double *frequencies;         // Frequency array (Hz)
    double *zReal;              // Real impedance (Ohm)
    double *zImag;              // Imaginary impedance (Ohm)
    int numPoints;              // Number of impedance points
    int retryCount;             // Retry attempts
    char filename[MAX_PATH_LENGTH]; // Data filename
} TempRampEISMeasurement;

// Experiment context
typedef struct {
    TempRampExperimentState state;
    TempRampExperimentParams params;
    
    // Cancellation flags
    volatile int cancelRequested;
    volatile int emergencyStop;
    
    // Timing
    double experimentStartTime;
    double experimentEndTime;
    double rampStartTime;
    double lastEISTime;
    double lastTempLogTime;
    double lastGraphUpdate;
    double totalEISTime;            // Cumulative time spent in EIS measurements
    
    // Temperature tracking
    double currentTemperature;
    double targetTemperature;
    int initialTempReached;
    int finalTempReached;
	
	// Mass flow tracking
	double currentMassFlow;
	double targetMassFlow;
    
    // EIS measurements
    TempRampEISMeasurement *eisMeasurements;
    int eisMeasurementCount;
    int eisMeasurementCapacity;
    
    // File system
    char experimentDirectory[MAX_PATH_LENGTH];
    FILE *temperatureLogFile;
    FILE *experimentLogFile;
    
    // UI handles
    int mainPanelHandle;
    int tabPanelHandle;
    int buttonControl;
    int outputControl;
    int statusControl;
    int graphTempHandle;        // Temperature vs Time
    int graphNyquistHandle;     // Nyquist plot
    
    // Device handles
    int biologicID;
    
} TempRampExperimentContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

/**
 * Main callback for starting/stopping temperature ramp experiment
 */
int CVICALLBACK StartTempRampExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2);

/**
 * Check if experiment is running
 */
int TempRampExperiment_IsRunning(void);

/**
 * Abort running experiment
 */
int TempRampExperiment_Abort(void);

/**
 * Emergency stop
 */
int TempRampExperiment_EmergencyStop(void);

/**
 * Cleanup module
 */
void TempRampExperiment_Cleanup(void);

#endif // EXP_TEMP_RAMP_H
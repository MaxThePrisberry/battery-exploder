/******************************************************************************
 * DTB4848 Temperature Controller Library
 * Header file for LabWindows/CVI
 * 
 * This library provides essential functions to control DTB4848
 * temperature controllers via Modbus ASCII communication protocol.
 * 
 * Configured for K-type thermocouple with PID control
 ******************************************************************************/

#ifndef DTB4848_DLL_H
#define DTB4848_DLL_H

#include "common.h"
#include <rs232.h>

/******************************************************************************
 * Constants and Definitions
 ******************************************************************************/

// DTB-specific error codes (using base from common.h)
#define DTB_SUCCESS                 SUCCESS
#define DTB_ERROR_COMM             (ERR_BASE_DTB - 1)
#define DTB_ERROR_CHECKSUM         (ERR_BASE_DTB - 2)
#define DTB_ERROR_TIMEOUT          (ERR_BASE_DTB - 3)
#define DTB_ERROR_INVALID_PARAM    (ERR_BASE_DTB - 4)
#define DTB_ERROR_BUSY             (ERR_BASE_DTB - 5)
#define DTB_ERROR_NOT_CONNECTED    (ERR_BASE_DTB - 6)
#define DTB_ERROR_RESPONSE         (ERR_BASE_DTB - 7)
#define DTB_ERROR_NOT_SUPPORTED    (ERR_BASE_DTB - 8)

// Modbus ASCII constants
#define MODBUS_ASCII_START          ':'
#define MODBUS_ASCII_CR             '\r'
#define MODBUS_ASCII_LF             '\n'
#define DEFAULT_SLAVE_ADDRESS       1
#define DEFAULT_TIMEOUT_MS          1000
#define DEFAULT_BAUD_RATE           9600

// Modbus function codes
#define MODBUS_READ_BITS            0x02
#define MODBUS_READ_REGISTERS       0x03
#define MODBUS_WRITE_BIT            0x05
#define MODBUS_WRITE_REGISTER       0x06

// DTB register addresses (hex values from manual)
#define REG_PROCESS_VALUE           0x1000
#define REG_SET_POINT               0x1001
#define REG_UPPER_LIMIT_TEMP        0x1002
#define REG_LOWER_LIMIT_TEMP        0x1003
#define REG_INPUT_SENSOR_TYPE       0x1004
#define REG_CONTROL_METHOD          0x1005
#define REG_HEATING_COOLING         0x1006
#define REG_CONTROL_CYCLE_1         0x1007
#define REG_CONTROL_CYCLE_2         0x1008
#define REG_PROPORTIONAL_BAND       0x1009
#define REG_INTEGRAL_TIME           0x100A
#define REG_DERIVATIVE_TIME         0x100B
#define REG_INTEGRAL_DEFAULT        0x100C
#define REG_PD_OFFSET               0x100D
#define REG_HYSTERESIS_HEAT         0x1010
#define REG_HYSTERESIS_COOL         0x1011
#define REG_TEMP_REGULATION         0x1016
#define REG_PID_SELECTION           0x101C
#define REG_ALARM1_TYPE             0x1020
#define REG_ALARM1_UPPER            0x1024
#define REG_ALARM1_LOWER            0x1025
#define REG_COMM_WRITE_ENABLE       0x102C
#define REG_LOCK_STATUS             0x102C
#define REG_SOFTWARE_VERSION        0x102F

// DTB bit register addresses
#define BIT_AT_STATUS               0x0800
#define BIT_OUTPUT1_STATUS          0x0801
#define BIT_OUTPUT2_STATUS          0x0802
#define BIT_ALARM1_STATUS           0x0803
#define BIT_COMM_WRITE_ENABLE       0x0810
#define BIT_TEMP_UNIT               0x0811
#define BIT_DECIMAL_POINT           0x0812
#define BIT_AUTO_TUNING             0x0813
#define BIT_RUN_STOP                0x0814
#define BIT_PROGRAM_STOP            0x0815
#define BIT_PROGRAM_HOLD            0x0816

// Factory reset registers
#define REG_FACTORY_RESET_1         0x472A
#define REG_FACTORY_RESET_2         0x474E
#define FACTORY_RESET_VALUE         0x1234

// Control parameters
#define CONTROL_METHOD_PID          0
#define CONTROL_METHOD_ONOFF        1
#define CONTROL_METHOD_MANUAL       2
#define CONTROL_METHOD_PID_PROG     3

// Sensor types
#define SENSOR_TYPE_K               0   // K-type thermocouple
#define SENSOR_TYPE_J               1   // J-type thermocouple
#define SENSOR_TYPE_T               2   // T-type thermocouple
#define SENSOR_TYPE_E               3   // E-type thermocouple
#define SENSOR_TYPE_N               4   // N-type thermocouple
#define SENSOR_TYPE_R               5   // R-type thermocouple
#define SENSOR_TYPE_S               6   // S-type thermocouple
#define SENSOR_TYPE_B               7   // B-type thermocouple
#define SENSOR_TYPE_L               8   // L-type thermocouple
#define SENSOR_TYPE_U               9   // U-type thermocouple
#define SENSOR_TYPE_TXK             10  // TXK-type thermocouple
#define SENSOR_TYPE_JPT100          11  // JPt100 RTD
#define SENSOR_TYPE_PT100           12  // Pt100 RTD

// PID selection modes
#define PID_MODE_0                  0
#define PID_MODE_1                  1
#define PID_MODE_2                  2
#define PID_MODE_3                  3
#define PID_MODE_AUTO               4   // Automatic PID selection

// Temperature ranges for K-type thermocouple
#define K_TYPE_MIN_TEMP             -199.9
#define K_TYPE_MAX_TEMP             999.9

// Alarm types
#define ALARM_DISABLED              0
#define ALARM_DEVIATION_HIGH_LOW    1
#define ALARM_DEVIATION_HIGH        2
#define ALARM_DEVIATION_LOW         3
#define ALARM_ABSOLUTE_HIGH_LOW     5
#define ALARM_ABSOLUTE_HIGH         6
#define ALARM_ABSOLUTE_LOW          7

// Front panel lock modes
#define FRONT_PANEL_UNLOCKED        0    // All settings unlocked
#define FRONT_PANEL_LOCK_ALL        1    // All settings locked
#define FRONT_PANEL_LOCK_EXCEPT_SV  11   // Lock everything except setpoint (SV)

// Heating/Cooling modes
#define HEATING_COOLING_HEATING         0   // Heating only
#define HEATING_COOLING_COOLING         1   // Cooling only
#define HEATING_COOLING_HEAT_COOL       2   // Heating/Cooling
#define HEATING_COOLING_COOL_HEAT       3   // Cooling/Heating

/******************************************************************************
 * Ramp-Soak (PID Program Control) Constants
 ******************************************************************************/

// Pattern and Step Limits
#define DTB_MAX_PATTERNS            8
#define DTB_MAX_STEPS_PER_PATTERN   8
#define DTB_LINK_PATTERN_END        8    // Indicates program end
#define DTB_MIN_STEP_TIME           0    // minutes
#define DTB_MAX_STEP_TIME           900  // minutes (15 hours)
#define DTB_MIN_CYCLE_COUNT         0
#define DTB_MAX_CYCLE_COUNT         99

// Ramp-Soak Register Addresses
#define REG_START_PATTERN           0x1030
#define REG_ACTUAL_STEP_BASE        0x1040  // +pattern number (0-7)
#define REG_CYCLE_COUNT_BASE        0x1050  // +pattern number (0-7)
#define REG_LINK_PATTERN_BASE       0x1060  // +pattern number (0-7)
#define REG_PATTERN_TEMP_BASE       0x2000  // +(pattern*8 + step)
#define REG_PATTERN_TIME_BASE       0x2080  // +(pattern*8 + step)

/******************************************************************************
 * Data Structures
 ******************************************************************************/

// DTB Handle structure
typedef struct {
    int comPort;
    int slaveAddress;
    int baudRate;
    int timeoutMs;
    int isConnected;
    char modelNumber[64];
    DeviceState state;
} DTB_Handle;

// Device status structure
typedef struct {
    double processValue;        // Current temperature
    double setPoint;            // Target temperature
    int outputEnabled;          // 1 = running, 0 = stopped
    int output1State;           // Output 1 relay state
    int output2State;           // Output 2 relay state (if available)
    int alarmState;             // Alarm output state
    int autoTuning;             // 1 = auto-tuning active
    int controlMethod;          // Current control method
    int pidMode;                // Current PID selection (0-4)
} DTB_Status;

// PID parameters structure
typedef struct {
    double proportionalBand;    // P value
    double integralTime;        // I value (seconds)
    double derivativeTime;      // D value (seconds)
    double integralDefault;     // Integral default value (%)
} DTB_PIDParams;

// Discovery result structure
typedef struct {
    char modelType[64];
    int comPort;
    int slaveAddress;
    int baudRate;
    double firmwareVersion;
} DTB_DiscoveryResult;

typedef struct {
    int sensorType;             // Sensor type (default: K-type)
    int controlMethod;          // Control method (default: PID)
    int pidMode;                // PID selection mode (default: AUTO)
    int heatingCoolingMode;     // Heating/cooling mode (default: Cooling/Heating)
    double upperTempLimit;      // Upper temperature limit
    double lowerTempLimit;      // Lower temperature limit
    int alarmType;              // Alarm configuration
    double alarmUpperLimit;     // Alarm upper threshold
    double alarmLowerLimit;     // Alarm lower threshold
} DTB_Configuration;

// Ramp-Soak Program Control Structures

// Program execution states
typedef enum {
    DTB_PROG_STATE_STOPPED = 0,
    DTB_PROG_STATE_RUNNING,
    DTB_PROG_STATE_PAUSED,
    DTB_PROG_STATE_COMPLETED
} DTB_ProgramState;

// Step definition (single ramp or soak segment)
typedef struct {
    double temperature;     // Target temperature (°C)
    int timeMinutes;       // Execution time (minutes, 0-900)
} DTB_Step;

// Pattern definition (sequence of up to 8 steps)
typedef struct {
    DTB_Step steps[DTB_MAX_STEPS_PER_PATTERN];
    int actualStepCount;   // 0-7 (limits which steps execute)
    int cycleCount;        // 0-99 (additional execution cycles beyond the first)
    int linkPattern;       // 0-7 (next pattern) or 8 (END)
} DTB_Pattern;

// Program status information
typedef struct {
    int isRunning;         // 1 if program is executing
    int isPaused;          // 1 if program is held
    int currentPattern;    // Current executing pattern (0-7, or -1 if not running)
    int currentStep;       // Current executing step (0-7, or -1 if not running)
    DTB_ProgramState state;
} DTB_ProgramStatus;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
// Connection Functions
int DTB_Initialize(DTB_Handle *handle, int comPort, int slaveAddress, int baudRate);
int DTB_TestConnection(DTB_Handle *handle);

// Configuration Functions
int DTB_FactoryReset(DTB_Handle *handle);
int DTB_Configure(DTB_Handle *handle, const DTB_Configuration *config);
int DTB_ConfigureDefault(DTB_Handle *handle);  // Simplified config for PID with K-type

// Basic Control Functions
int DTB_SetRunStop(DTB_Handle *handle, int run);
int DTB_SetSetPoint(DTB_Handle *handle, double temperature);
int DTB_StartAutoTuning(DTB_Handle *handle);
int DTB_StopAutoTuning(DTB_Handle *handle);

// Read Functions
int DTB_GetStatus(DTB_Handle *handle, DTB_Status *status);
int DTB_GetProcessValue(DTB_Handle *handle, double *temperature);
int DTB_GetSetPoint(DTB_Handle *handle, double *setPoint);
int DTB_GetPIDParams(DTB_Handle *handle, int pidNumber, DTB_PIDParams *params);
int DTB_SetPIDParams(DTB_Handle *handle, int pidNumber, const DTB_PIDParams *params);

// Alarm Functions
int DTB_GetAlarmStatus(DTB_Handle *handle, int *alarmActive);
int DTB_ClearAlarm(DTB_Handle *handle);
int DTB_SetAlarmLimits(DTB_Handle *handle, double upperLimit, double lowerLimit);

// Advanced Functions
int DTB_SetControlMethod(DTB_Handle *handle, int method);
int DTB_SetPIDMode(DTB_Handle *handle, int mode);
int DTB_SetSensorType(DTB_Handle *handle, int sensorType);
int DTB_SetTemperatureLimits(DTB_Handle *handle, double upperLimit, double lowerLimit);
int DTB_SetHeatingCooling(DTB_Handle *handle, int mode);

// Control Cycle Functions
int DTB_GetControlCycle(DTB_Handle *handle, int outputNumber, int *cycleTime);
int DTB_SetControlCycle(DTB_Handle *handle, int outputNumber, int cycleTime);

// Front Panel Lock Functions
int DTB_SetFrontPanelLock(DTB_Handle *handle, int lockMode);
int DTB_GetFrontPanelLock(DTB_Handle *handle, int *lockMode);
int DTB_UnlockFrontPanel(DTB_Handle *handle);
int DTB_LockFrontPanel(DTB_Handle *handle, int allowSetpointChange);

// Write Protection Functions
int DTB_EnableWriteAccess(DTB_Handle *handle);
int DTB_DisableWriteAccess(DTB_Handle *handle);
int DTB_GetWriteAccessStatus(DTB_Handle *handle, int *isEnabled);

// Utility Functions
const char* DTB_GetErrorString(int errorCode);
void DTB_EnableDebugOutput(int enable);
void DTB_PrintStatus(const DTB_Status *status);

// Low-level Modbus functions (usually not called directly)
int DTB_ReadRegister(DTB_Handle *handle, unsigned short address, unsigned short *value);
int DTB_WriteRegister(DTB_Handle *handle, unsigned short address, unsigned short value);
int DTB_ReadBit(DTB_Handle *handle, unsigned short address, int *value);
int DTB_WriteBit(DTB_Handle *handle, unsigned short address, int value);
int DTB_ReadMultipleRegisters(DTB_Handle *handle, unsigned short startAddress,
                              int numRegisters, unsigned short *values);

// Optimized read functions
int DTB_GetTemperatureQuick(DTB_Handle *handle, double *temperature, double *setpoint);

/******************************************************************************
 * Ramp-Soak (PID Program Control) Functions
 ******************************************************************************/

// Pattern/Step Configuration Functions

/**
 * Set a complete pattern with all steps and metadata
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param pattern - Complete pattern configuration
 * @return DTB_SUCCESS or error code
 */
int DTB_SetPattern(DTB_Handle *handle, int patternNumber, const DTB_Pattern *pattern);

/**
 * Get a complete pattern configuration
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param pattern - Pointer to receive pattern configuration
 * @return DTB_SUCCESS or error code
 */
int DTB_GetPattern(DTB_Handle *handle, int patternNumber, DTB_Pattern *pattern);

/**
 * Set a single step within a pattern
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param stepNumber - Step number (0-7)
 * @param temperature - Target temperature (°C)
 * @param timeMinutes - Execution time (minutes, 0-900)
 * @return DTB_SUCCESS or error code
 */
int DTB_SetStep(DTB_Handle *handle, int patternNumber, int stepNumber,
                double temperature, int timeMinutes);

/**
 * Get a single step configuration
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param stepNumber - Step number (0-7)
 * @param temperature - Pointer to receive temperature (°C)
 * @param timeMinutes - Pointer to receive time (minutes)
 * @return DTB_SUCCESS or error code
 */
int DTB_GetStep(DTB_Handle *handle, int patternNumber, int stepNumber,
                double *temperature, int *timeMinutes);

// Pattern Metadata Functions

/**
 * Set the actual number of steps to execute in a pattern
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param stepCount - Number of steps to execute (0-7)
 * @return DTB_SUCCESS or error code
 */
int DTB_SetActualStepCount(DTB_Handle *handle, int patternNumber, int stepCount);

/**
 * Get the actual number of steps configured for a pattern
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param stepCount - Pointer to receive step count
 * @return DTB_SUCCESS or error code
 */
int DTB_GetActualStepCount(DTB_Handle *handle, int patternNumber, int *stepCount);

/**
 * Set the cycle count (number of additional repetitions) for a pattern
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param cycleCount - Additional cycles (0-99)
 * @return DTB_SUCCESS or error code
 */
int DTB_SetCycleCount(DTB_Handle *handle, int patternNumber, int cycleCount);

/**
 * Get the cycle count for a pattern
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param cycleCount - Pointer to receive cycle count
 * @return DTB_SUCCESS or error code
 */
int DTB_GetCycleCount(DTB_Handle *handle, int patternNumber, int *cycleCount);

/**
 * Set the link pattern (next pattern to execute or END)
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param linkPattern - Next pattern (0-7) or DTB_LINK_PATTERN_END (8)
 * @return DTB_SUCCESS or error code
 */
int DTB_SetLinkPattern(DTB_Handle *handle, int patternNumber, int linkPattern);

/**
 * Get the link pattern setting
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param linkPattern - Pointer to receive link pattern
 * @return DTB_SUCCESS or error code
 */
int DTB_GetLinkPattern(DTB_Handle *handle, int patternNumber, int *linkPattern);

// Program Control Functions

/**
 * Set the starting pattern for program execution
 * @param handle - DTB device handle
 * @param patternNumber - Pattern to start from (0-7)
 * @return DTB_SUCCESS or error code
 */
int DTB_SetStartPattern(DTB_Handle *handle, int patternNumber);

/**
 * Get the currently configured starting pattern
 * @param handle - DTB device handle
 * @param patternNumber - Pointer to receive pattern number
 * @return DTB_SUCCESS or error code
 */
int DTB_GetStartPattern(DTB_Handle *handle, int *patternNumber);

/**
 * Start program execution from the configured start pattern
 * Note: Control method must be set to CONTROL_METHOD_PID_PROG (3)
 * @param handle - DTB device handle
 * @return DTB_SUCCESS or error code
 */
int DTB_StartProgram(DTB_Handle *handle);

/**
 * Stop program execution and disable output
 * @param handle - DTB device handle
 * @return DTB_SUCCESS or error code
 */
int DTB_StopProgram(DTB_Handle *handle);

/**
 * Pause (hold) program execution at current temperature
 * @param handle - DTB device handle
 * @return DTB_SUCCESS or error code
 */
int DTB_HoldProgram(DTB_Handle *handle);

/**
 * Resume program execution from paused state
 * @param handle - DTB device handle
 * @return DTB_SUCCESS or error code
 */
int DTB_ResumeProgram(DTB_Handle *handle);

/**
 * Get current program execution status
 * @param handle - DTB device handle
 * @param status - Pointer to receive program status
 * @return DTB_SUCCESS or error code
 */
int DTB_GetProgramStatus(DTB_Handle *handle, DTB_ProgramStatus *status);

// Convenience Functions

/**
 * Set up a simple ramp-soak profile with 2 steps
 * Step 0: Ramp from startTemp to endTemp over rampTimeMinutes
 * Step 1: Soak at endTemp for soakTimeMinutes
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @param startTemp - Starting temperature (°C)
 * @param endTemp - Ending temperature (°C)
 * @param rampTimeMinutes - Time to ramp (0-900 minutes)
 * @param soakTimeMinutes - Time to soak (0-900 minutes)
 * @return DTB_SUCCESS or error code
 */
int DTB_SetSimpleRamp(DTB_Handle *handle, int patternNumber,
                      double startTemp, double endTemp,
                      int rampTimeMinutes, int soakTimeMinutes);

/**
 * Clear a pattern (set all steps to 0, no link, no cycles)
 * @param handle - DTB device handle
 * @param patternNumber - Pattern number (0-7)
 * @return DTB_SUCCESS or error code
 */
int DTB_ClearPattern(DTB_Handle *handle, int patternNumber);

/**
 * Clear all patterns (reset entire program memory)
 * @param handle - DTB device handle
 * @return DTB_SUCCESS or error code
 */
int DTB_ClearAllPatterns(DTB_Handle *handle);

#endif // DTB4848_DLL_H
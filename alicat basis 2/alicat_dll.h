/******************************************************************************
 * ALICAT BASIS 2 Flow Controller Library
 * Header file for LabWindows/CVI
 * 
 * This library provides functions to control ALICAT BASIS 2
 * mass flow controllers via Modbus-RTU communication protocol.
 ******************************************************************************/

#ifndef ALICAT_DLL_H
#define ALICAT_DLL_H

#include "common.h"
#include <rs232.h>

/******************************************************************************
 * Constants and Definitions
 ******************************************************************************/

// ALICAT-specific error codes
#define ALICAT_SUCCESS              SUCCESS
#define ALICAT_ERROR_COMM          (ERR_BASE_ALICAT - 1)
#define ALICAT_ERROR_CHECKSUM      (ERR_BASE_ALICAT - 2)
#define ALICAT_ERROR_TIMEOUT       (ERR_BASE_ALICAT - 3)
#define ALICAT_ERROR_INVALID_PARAM (ERR_BASE_ALICAT - 4)
#define ALICAT_ERROR_BUSY          (ERR_BASE_ALICAT - 5)
#define ALICAT_ERROR_NOT_CONNECTED (ERR_BASE_ALICAT - 6)
#define ALICAT_ERROR_RESPONSE      (ERR_BASE_ALICAT - 7)
#define ALICAT_ERROR_NOT_SUPPORTED (ERR_BASE_ALICAT - 8)

// Modbus RTU constants
#define DEFAULT_MODBUS_ADDRESS      1
#define DEFAULT_TIMEOUT_MS          1000
#define DEFAULT_BAUD_RATE           38400

// Modbus function codes
#define MODBUS_READ_HOLDING         0x03
#define MODBUS_WRITE_SINGLE         0x06
#define MODBUS_WRITE_MULTIPLE       0x10

// ALICAT register addresses
#define REG_BAUD_RATE               21
#define REG_FIRMWARE_VERSION        25
#define REG_SERIAL_NUMBER_START     26
#define REG_MODBUS_ADDRESS          45
#define REG_UNIT_ID                 46
#define REG_FULL_SCALE_FLOW         47      // 2 registers (47-48)
#define REG_FLOW_UNITS              49
#define REG_TARE_SAMPLES            51
#define REG_REF_TEMPERATURE         52
#define REG_TOTALIZER_RESET         53
#define REG_TOTALIZER_CONFIG        54
#define REG_FLOW_AVERAGING          55
#define REG_PROTOCOL_MODE           56
#define REG_FACTORY_RESTORE         80

// Control registers
#define REG_PGAIN                   519
#define REG_IGAIN                   520
#define REG_BATCH_VOLUME            521     // 2 registers (521-522)
#define REG_MAX_SETPOINT_RAMP       524     // 2 registers (524-525)

// Setpoint source
#define REG_SETPOINT_SOURCE         516
#define REG_WATCHDOG_TIME           514
#define REG_AUTOTARE_ENABLE         515

// Data registers (instantaneous)
#define REG_SETPOINT                2053    // 2 registers (2053-2054) - Write
#define REG_SELECTED_GAS            2100
#define REG_STATUS_FLAGS            2101
#define REG_TEMPERATURE             2102
#define REG_FLOW                    2103
#define REG_TOTAL_VOLUME            2104    // 2 registers (2104-2105)
#define REG_CURRENT_SETPOINT        2106
#define REG_VALVE_DRIVE             2107
#define REG_BATCH_REMAINING         2108    // 2 registers (2108-2109)

// Tare command
#define REG_TARE_COMMAND            39
#define TARE_COMMAND_VALUE          0xAA55

// Gas types
#define GAS_AIR                     0
#define GAS_ARGON                   1
#define GAS_CO2                     2
#define GAS_NITROGEN                3
#define GAS_OXYGEN                  4
#define GAS_N2O                     5
#define GAS_HYDROGEN                6
#define GAS_HELIUM                  7
#define GAS_METHANE                 8

// Setpoint source types
#define SETPOINT_SOURCE_ANALOG      0
#define SETPOINT_SOURCE_DIGITAL_SAVED    1
#define SETPOINT_SOURCE_DIGITAL_UNSAVED  2

// Status flags (REG_STATUS_FLAGS)
#define STATUS_MASS_OVERRANGE       0x0001
#define STATUS_TEMP_OVERRANGE       0x0002
#define STATUS_TOTALIZER_OVERRANGE  0x0004
#define STATUS_VALVE_HOLD           0x0008
#define STATUS_VALVE_THERMAL_MGMT   0x0010

// Flow units
#define FLOW_UNIT_SCCM              0
#define FLOW_UNIT_NCCM              1
#define FLOW_UNIT_SLPM              2
#define FLOW_UNIT_NLPM              3
#define FLOW_UNIT_SML_S             4
#define FLOW_UNIT_NML_S             5
#define FLOW_UNIT_SML_M             6
#define FLOW_UNIT_NML_M             7
#define FLOW_UNIT_SL_H              8
#define FLOW_UNIT_NL_H              9
#define FLOW_UNIT_SCCS              10
#define FLOW_UNIT_NCCS              11
#define FLOW_UNIT_SM3_H             12
#define FLOW_UNIT_NM3_H             13
#define FLOW_UNIT_SM3_D             14
#define FLOW_UNIT_NM3_D             15
#define FLOW_UNIT_SCIM              16
#define FLOW_UNIT_SCFM              17
#define FLOW_UNIT_SCFH              18
#define FLOW_UNIT_SCFD              19

// Scaling factors
#define FLOW_SCALE_FACTOR           1000.0  // Flow values are * 1000
#define TEMP_SCALE_FACTOR           100.0   // Temperature values are * 100
#define VALVE_SCALE_FACTOR          100.0   // Valve drive is * 100

/******************************************************************************
 * Data Structures
 ******************************************************************************/

// ALICAT Handle structure
typedef struct {
    int comPort;
    int modbusAddress;
    int baudRate;
    int timeoutMs;
    int isConnected;
    char modelNumber[64];
    DeviceState state;
    char serialNumber[16];
} ALICAT_Handle;

// Device status structure
typedef struct {
    double flowRate;            // Current flow rate
    double setpoint;            // Current setpoint
    double temperature;         // Current temperature (°C)
    double totalVolume;         // Total volume since reset
    double valveDrive;          // Valve drive percentage
    int selectedGas;            // Currently selected gas
    int statusFlags;            // Status flags
    int massOverrange;          // Mass flow overrange
    int tempOverrange;          // Temperature overrange
    int totalizerOverrange;     // Totalizer overrange
    int valveHold;              // Valve hold active
    int valveThermalMgmt;       // Valve thermal management active
} ALICAT_Status;

// PID parameters structure
typedef struct {
    unsigned short pGain;       // Proportional gain (0-65535)
    unsigned short iGain;       // Integral gain (0-65535)
} ALICAT_PIDParams;

// Configuration structure
typedef struct {
    int gasType;                // Gas type (0-8)
    int setpointSource;         // Setpoint source
    double refTemperature;      // Reference temperature (°C)
    int flowAveraging;          // Flow averaging time (ms)
    int autotareEnable;         // Autotare enable (0/1)
    unsigned short pGain;       // Proportional gain
    unsigned short iGain;       // Integral gain
} ALICAT_Configuration;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

// Connection Functions
int ALICAT_Initialize(ALICAT_Handle *handle, int comPort, int modbusAddress, int baudRate);
int ALICAT_TestConnection(ALICAT_Handle *handle);
int ALICAT_Disconnect(ALICAT_Handle *handle);

// Configuration Functions
int ALICAT_FactoryReset(ALICAT_Handle *handle);
int ALICAT_Configure(ALICAT_Handle *handle, const ALICAT_Configuration *config);
int ALICAT_ConfigureDefault(ALICAT_Handle *handle);

// Basic Control Functions
int ALICAT_SetSetpoint(ALICAT_Handle *handle, double flowRate);
int ALICAT_SetGas(ALICAT_Handle *handle, int gasType);
int ALICAT_Tare(ALICAT_Handle *handle);

// Read Functions
int ALICAT_GetStatus(ALICAT_Handle *handle, ALICAT_Status *status);
int ALICAT_GetFlowRate(ALICAT_Handle *handle, double *flowRate);
int ALICAT_GetSetpoint(ALICAT_Handle *handle, double *setpoint);
int ALICAT_GetTemperature(ALICAT_Handle *handle, double *temperature);
int ALICAT_GetTotalVolume(ALICAT_Handle *handle, double *totalVolume);
int ALICAT_GetValveDrive(ALICAT_Handle *handle, double *valveDrive);
int ALICAT_GetPIDParams(ALICAT_Handle *handle, ALICAT_PIDParams *params);

// Configuration Functions
int ALICAT_SetSetpointSource(ALICAT_Handle *handle, int source);
int ALICAT_SetPIDParams(ALICAT_Handle *handle, const ALICAT_PIDParams *params);
int ALICAT_SetFlowAveraging(ALICAT_Handle *handle, int averagingMs);
int ALICAT_SetRefTemperature(ALICAT_Handle *handle, double tempC);
int ALICAT_SetWatchdog(ALICAT_Handle *handle, int timeoutMs);
int ALICAT_SetAutotare(ALICAT_Handle *handle, int enable);

// Totalizer Functions
int ALICAT_ResetTotalizer(ALICAT_Handle *handle);
int ALICAT_SetBatchVolume(ALICAT_Handle *handle, double volume);
int ALICAT_GetBatchRemaining(ALICAT_Handle *handle, double *remaining);

// Utility Functions
const char* ALICAT_GetErrorString(int errorCode);
const char* ALICAT_GetGasName(int gasType);
const char* ALICAT_GetFlowUnitName(int unitType);
void ALICAT_PrintStatus(const ALICAT_Status *status);

// Low-level Modbus functions
int ALICAT_ReadRegister(ALICAT_Handle *handle, unsigned short address, unsigned short *value);
int ALICAT_ReadRegisters(ALICAT_Handle *handle, unsigned short address, unsigned short *values, int count);
int ALICAT_WriteRegister(ALICAT_Handle *handle, unsigned short address, unsigned short value);
int ALICAT_WriteRegisters(ALICAT_Handle *handle, unsigned short address, unsigned short *values, int count);

#endif // ALICAT_DLL_H
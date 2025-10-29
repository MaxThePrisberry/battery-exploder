/******************************************************************************
 * alicat_queue.h
 * 
 * Thread-safe command queue implementation for ALICAT BASIS 2 Flow Controllers
 * Built on top of the generic device queue system
 * 
 * Supports multiple ALICAT devices on the same COM port with different Modbus addresses
 ******************************************************************************/

#ifndef ALICAT_QUEUE_H
#define ALICAT_QUEUE_H

#include "common.h"
#include "alicat_dll.h"
#include "device_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Maximum number of ALICAT devices supported on one COM port
#define MAX_ALICAT_DEVICES          16

// Command delays (milliseconds)
#define ALICAT_DELAY_AFTER_WRITE        10   // After write register
#define ALICAT_DELAY_AFTER_READ         10   // After read operations
#define ALICAT_DELAY_SETPOINT_CHANGE   100   // After setpoint change
#define ALICAT_DELAY_CONFIG_CHANGE     100   // After configuration changes
#define ALICAT_DELAY_TARE              100   // After tare command
#define ALICAT_DELAY_RECOVERY           20   // General recovery between commands

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Use generic types from device_queue.h
typedef DeviceQueueManager ALICAT_QueueManager;
typedef DeviceTransactionHandle TransactionHandle;
typedef DeviceCommandID CommandID;
typedef DeviceCommandCallback ALICAT_CommandCallback;
typedef DeviceTransactionCallback ALICAT_TransactionCallback;
typedef DeviceQueueStats ALICAT_QueueStats;

// Map transaction constants
#define ALICAT_MAX_TRANSACTION_COMMANDS  DEVICE_MAX_TRANSACTION_COMMANDS
#define ALICAT_QUEUE_COMMAND_TIMEOUT_MS  DEVICE_QUEUE_COMMAND_TIMEOUT_MS

// Command types
typedef enum {
    ALICAT_CMD_NONE = 0,
    
    // Control commands
    ALICAT_CMD_SET_SETPOINT,
    ALICAT_CMD_SET_GAS,
    ALICAT_CMD_TARE,
    
    // Configuration commands
    ALICAT_CMD_SET_SETPOINT_SOURCE,
    ALICAT_CMD_SET_PID_PARAMS,
    ALICAT_CMD_SET_FLOW_AVERAGING,
    ALICAT_CMD_SET_REF_TEMPERATURE,
    ALICAT_CMD_SET_WATCHDOG,
    ALICAT_CMD_SET_AUTOTARE,
    ALICAT_CMD_CONFIGURE,
    ALICAT_CMD_CONFIGURE_DEFAULT,
    ALICAT_CMD_FACTORY_RESET,
    
    // Query commands
    ALICAT_CMD_GET_STATUS,
    ALICAT_CMD_GET_FLOW_RATE,
    ALICAT_CMD_GET_SETPOINT,
    ALICAT_CMD_GET_TEMPERATURE,
    ALICAT_CMD_GET_TOTAL_VOLUME,
    ALICAT_CMD_GET_VALVE_DRIVE,
    ALICAT_CMD_GET_PID_PARAMS,
    
    // Totalizer commands
    ALICAT_CMD_RESET_TOTALIZER,
    ALICAT_CMD_SET_BATCH_VOLUME,
    ALICAT_CMD_GET_BATCH_REMAINING,
    
    ALICAT_CMD_TYPE_COUNT
} ALICAT_CommandType;

// Command parameters union
typedef union {
    struct { int modbusAddress; double flowRate; } setSetpoint;
    struct { int modbusAddress; int gasType; } setGas;
    struct { int modbusAddress; } tare;
    struct { int modbusAddress; int source; } setSetpointSource;
    struct { int modbusAddress; ALICAT_PIDParams params; } setPIDParams;
    struct { int modbusAddress; int averagingMs; } setFlowAveraging;
    struct { int modbusAddress; double tempC; } setRefTemperature;
    struct { int modbusAddress; int timeoutMs; } setWatchdog;
    struct { int modbusAddress; int enable; } setAutotare;
    struct { int modbusAddress; ALICAT_Configuration config; } configure;
    struct { int modbusAddress; } configureDefault;
    struct { int modbusAddress; } factoryReset;
    struct { int modbusAddress; } getStatus;
    struct { int modbusAddress; } getFlowRate;
    struct { int modbusAddress; } getSetpoint;
    struct { int modbusAddress; } getTemperature;
    struct { int modbusAddress; } getTotalVolume;
    struct { int modbusAddress; } getValveDrive;
    struct { int modbusAddress; } getPIDParams;
    struct { int modbusAddress; } resetTotalizer;
    struct { int modbusAddress; double volume; } setBatchVolume;
    struct { int modbusAddress; } getBatchRemaining;
} ALICAT_CommandParams;

// Command result structure
typedef struct {
    int errorCode;
    union {
        ALICAT_Status status;
        double flowRate;
        double setpoint;
        double temperature;
        double totalVolume;
        double valveDrive;
        ALICAT_PIDParams pidParams;
        double batchRemaining;
    } data;
} ALICAT_CommandResult;

typedef struct {
    ALICAT_Handle handles[MAX_ALICAT_DEVICES];
    int modbusAddresses[MAX_ALICAT_DEVICES];
    int numDevices;
    int comPort;
    int baudRate;
} ALICAT_DeviceContext;

typedef struct {
    int comPort;
    int baudRate;
    int *modbusAddresses;
    int numDevices;
} ALICAT_ConnectionParams;

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

/**
 * Initialize the queue manager with multiple devices on the same COM port
 * @param comPort - COM port number (1-16)
 * @param baudRate - Baud rate (default 38400)
 * @param modbusAddresses - Array of Modbus addresses
 * @param numDevices - Number of devices (1 to MAX_ALICAT_DEVICES)
 * @return Queue manager instance or NULL on failure
 */
ALICAT_QueueManager* ALICAT_QueueInit(int comPort, int baudRate, int *modbusAddresses, int numDevices);

/**
 * Get the ALICAT handle for a specific Modbus address
 * @param mgr - Queue manager instance
 * @param modbusAddress - Modbus address
 * @return ALICAT handle or NULL if address not found
 */
ALICAT_Handle* ALICAT_QueueGetHandle(ALICAT_QueueManager *mgr, int modbusAddress);

// Shutdown the queue manager
void ALICAT_QueueShutdown(ALICAT_QueueManager *mgr);

// Check if queue manager is running
bool ALICAT_QueueIsRunning(ALICAT_QueueManager *mgr);

// Get queue statistics
void ALICAT_QueueGetStats(ALICAT_QueueManager *mgr, ALICAT_QueueStats *stats);

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

// Cancel commands
int ALICAT_QueueCancelCommand(ALICAT_QueueManager *mgr, CommandID cmdId);
int ALICAT_QueueCancelByType(ALICAT_QueueManager *mgr, ALICAT_CommandType type);
int ALICAT_QueueCancelByAge(ALICAT_QueueManager *mgr, double ageSeconds);
int ALICAT_QueueCancelAll(ALICAT_QueueManager *mgr);

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

// Begin a transaction
TransactionHandle ALICAT_QueueBeginTransaction(ALICAT_QueueManager *mgr);

// Add command to transaction
int ALICAT_QueueAddToTransaction(ALICAT_QueueManager *mgr, TransactionHandle txn,
                                ALICAT_CommandType type, ALICAT_CommandParams *params);

// Commit transaction (async)
int ALICAT_QueueCommitTransaction(ALICAT_QueueManager *mgr, TransactionHandle txn,
                                 ALICAT_TransactionCallback callback, void *userData);

// Cancel transaction
int ALICAT_QueueCancelTransaction(ALICAT_QueueManager *mgr, TransactionHandle txn);

/******************************************************************************
 * Individual Device Functions (Global queue manager required)
 ******************************************************************************/

// Control functions
int ALICAT_SetSetpointQueued(int modbusAddress, double flowRate, DevicePriority priority);
int ALICAT_SetGasQueued(int modbusAddress, int gasType, DevicePriority priority);
int ALICAT_TareQueued(int modbusAddress, DevicePriority priority);

// Configuration functions
int ALICAT_SetSetpointSourceQueued(int modbusAddress, int source, DevicePriority priority);
int ALICAT_SetPIDParamsQueued(int modbusAddress, const ALICAT_PIDParams *params, DevicePriority priority);
int ALICAT_SetFlowAveragingQueued(int modbusAddress, int averagingMs, DevicePriority priority);
int ALICAT_SetRefTemperatureQueued(int modbusAddress, double tempC, DevicePriority priority);
int ALICAT_SetWatchdogQueued(int modbusAddress, int timeoutMs, DevicePriority priority);
int ALICAT_SetAutotareQueued(int modbusAddress, int enable, DevicePriority priority);
int ALICAT_ConfigureQueued(int modbusAddress, const ALICAT_Configuration *config, DevicePriority priority);
int ALICAT_ConfigureDefaultQueued(int modbusAddress, DevicePriority priority);
int ALICAT_FactoryResetQueued(int modbusAddress, DevicePriority priority);

// Read functions
int ALICAT_GetStatusQueued(int modbusAddress, ALICAT_Status *status, DevicePriority priority);
int ALICAT_GetFlowRateQueued(int modbusAddress, double *flowRate, DevicePriority priority);
int ALICAT_GetSetpointQueued(int modbusAddress, double *setpoint, DevicePriority priority);
int ALICAT_GetTemperatureQueued(int modbusAddress, double *temperature, DevicePriority priority);
int ALICAT_GetTotalVolumeQueued(int modbusAddress, double *totalVolume, DevicePriority priority);
int ALICAT_GetValveDriveQueued(int modbusAddress, double *valveDrive, DevicePriority priority);
int ALICAT_GetPIDParamsQueued(int modbusAddress, ALICAT_PIDParams *params, DevicePriority priority);

// Totalizer functions
int ALICAT_ResetTotalizerQueued(int modbusAddress, DevicePriority priority);
int ALICAT_SetBatchVolumeQueued(int modbusAddress, double volume, DevicePriority priority);
int ALICAT_GetBatchRemainingQueued(int modbusAddress, double *remaining, DevicePriority priority);

/******************************************************************************
 * "All Devices" Convenience Functions
 ******************************************************************************/

/**
 * Set setpoint for all initialized ALICAT devices
 */
int ALICAT_SetSetpointAllQueued(double flowRate, DevicePriority priority);

/**
 * Configure all initialized ALICAT devices with default configuration
 */
int ALICAT_ConfigureAllDefaultQueued(DevicePriority priority);

/**
 * Set gas type for all initialized ALICAT devices
 */
int ALICAT_SetGasAllQueued(int gasType, DevicePriority priority);

/**
 * Tare all initialized ALICAT devices
 */
int ALICAT_TareAllQueued(DevicePriority priority);

/**
 * Get status from all ALICAT devices
 */
int ALICAT_GetStatusAllQueued(ALICAT_Status *statuses, int *numDevices, DevicePriority priority);

/******************************************************************************
 * Async Command Functions
 ******************************************************************************/

CommandID ALICAT_GetStatusAsync(int modbusAddress, ALICAT_CommandCallback callback, void *userData, DevicePriority priority);
CommandID ALICAT_SetSetpointAsync(int modbusAddress, double flowRate, ALICAT_CommandCallback callback, void *userData, DevicePriority priority);
CommandID ALICAT_SetGasAsync(int modbusAddress, int gasType, ALICAT_CommandCallback callback, void *userData, DevicePriority priority);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

// Get command type name for logging
const char* ALICAT_QueueGetCommandTypeName(ALICAT_CommandType type);

// Get delay for command type
int ALICAT_QueueGetCommandDelay(ALICAT_CommandType type);

// Set/Get global queue manager
void ALICAT_SetGlobalQueueManager(ALICAT_QueueManager *mgr);
ALICAT_QueueManager* ALICAT_GetGlobalQueueManager(void);

#endif // ALICAT_QUEUE_H
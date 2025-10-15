/******************************************************************************
 * alicat_queue.c
 * 
 * Thread-safe command queue implementation for ALICAT BASIS 2 Flow Controllers
 * Built on top of the generic device queue system
 * 
 * Supports multiple ALICAT devices on the same COM port with different Modbus addresses
 ******************************************************************************/

#include "alicat_queue.h"
#include "logging.h"
#include <ansi_c.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char* g_commandTypeNames[] = {
    "NONE",
    "SET_SETPOINT",
    "SET_GAS",
    "TARE",
    "SET_SETPOINT_SOURCE",
    "SET_PID_PARAMS",
    "SET_FLOW_AVERAGING",
    "SET_REF_TEMPERATURE",
    "SET_WATCHDOG",
    "SET_AUTOTARE",
    "CONFIGURE",
    "CONFIGURE_DEFAULT",
    "FACTORY_RESET",
    "GET_STATUS",
    "GET_FLOW_RATE",
    "GET_SETPOINT",
    "GET_TEMPERATURE",
    "GET_TOTAL_VOLUME",
    "GET_VALVE_DRIVE",
    "GET_PID_PARAMS",
    "RESET_TOTALIZER",
    "SET_BATCH_VOLUME",
    "GET_BATCH_REMAINING"
};

// Global queue manager pointer
static ALICAT_QueueManager *g_alicatQueueManager = NULL;

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static int FindDeviceIndex(ALICAT_DeviceContext *ctx, int modbusAddress) {
    if (!ctx) return -1;
    
    for (int i = 0; i < ctx->numDevices; i++) {
        if (ctx->modbusAddresses[i] == modbusAddress) {
            return i;
        }
    }
    return -1;
}

static ALICAT_Handle* GetDeviceHandle(ALICAT_DeviceContext *ctx, int modbusAddress) {
    int index = FindDeviceIndex(ctx, modbusAddress);
    if (index < 0) return NULL;
    
    return &ctx->handles[index];
}

/******************************************************************************
 * Device Adapter Implementation
 ******************************************************************************/

// Forward declarations for adapter functions
static int ALICAT_AdapterConnect(void *deviceContext, void *connectionParams);
static int ALICAT_AdapterDisconnect(void *deviceContext);
static int ALICAT_AdapterTestConnection(void *deviceContext);
static bool ALICAT_AdapterIsConnected(void *deviceContext);
static int ALICAT_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result);
static void* ALICAT_AdapterCreateCommandParams(int commandType, void *sourceParams);
static void ALICAT_AdapterFreeCommandParams(int commandType, void *params);
static void* ALICAT_AdapterCreateCommandResult(int commandType);
static void ALICAT_AdapterFreeCommandResult(int commandType, void *result);
static void ALICAT_AdapterCopyCommandResult(int commandType, void *dest, void *src);

// ALICAT device adapter
static const DeviceAdapter g_alicatAdapter = {
    .deviceName = "ALICAT BASIS 2",
    
    // Connection management
    .connect = ALICAT_AdapterConnect,
    .disconnect = ALICAT_AdapterDisconnect,
    .testConnection = ALICAT_AdapterTestConnection,
    .isConnected = ALICAT_AdapterIsConnected,
    
    // Command execution
    .executeCommand = ALICAT_AdapterExecuteCommand,
    
    // Command management
    .createCommandParams = ALICAT_AdapterCreateCommandParams,
    .freeCommandParams = ALICAT_AdapterFreeCommandParams,
    .createCommandResult = ALICAT_AdapterCreateCommandResult,
    .freeCommandResult = ALICAT_AdapterFreeCommandResult,
    .copyCommandResult = ALICAT_AdapterCopyCommandResult,
    
    // Utility functions
    .getCommandTypeName = (const char* (*)(int))ALICAT_QueueGetCommandTypeName,
    .getCommandDelay = ALICAT_QueueGetCommandDelay,
    .getErrorString = GetErrorString
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int ALICAT_AdapterConnect(void *deviceContext, void *connectionParams) {
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)deviceContext;
    ALICAT_ConnectionParams *params = (ALICAT_ConnectionParams*)connectionParams;
    
    if (!ctx || !params || !params->modbusAddresses) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Invalid parameters for ALICAT adapter connect");
        return ALICAT_ERROR_INVALID_PARAM;
    }
    
    if (params->numDevices <= 0 || params->numDevices > MAX_ALICAT_DEVICES) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Invalid number of devices: %d (max %d)",
                   params->numDevices, MAX_ALICAT_DEVICES);
        return ALICAT_ERROR_INVALID_PARAM;
    }
    
    // Initialize context
    ctx->comPort = params->comPort;
    ctx->baudRate = params->baudRate;
    ctx->numDevices = params->numDevices;
    
    // Copy Modbus addresses
    for (int i = 0; i < params->numDevices; i++) {
        ctx->modbusAddresses[i] = params->modbusAddresses[i];
    }
    
    // Initialize each device
    int successCount = 0;
    for (int i = 0; i < ctx->numDevices; i++) {
        LogMessageEx(LOG_DEVICE_ALICAT, "Connecting to ALICAT address %d on COM%d...",
                     ctx->modbusAddresses[i], ctx->comPort);
        
        int result = ALICAT_Initialize(&ctx->handles[i], ctx->comPort,
                                      ctx->modbusAddresses[i], ctx->baudRate);
        
        if (result == ALICAT_SUCCESS) {
            LogMessageEx(LOG_DEVICE_ALICAT, "Successfully connected to ALICAT address %d",
                         ctx->modbusAddresses[i]);
            successCount++;
        } else {
            LogErrorEx(LOG_DEVICE_ALICAT, "Failed to connect to ALICAT address %d: %s",
                       ctx->modbusAddresses[i], ALICAT_GetErrorString(result));
            // Continue trying other devices
        }
    }
    
    if (successCount == 0) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to connect to any ALICAT devices");
        return ALICAT_ERROR_COMM;
    }
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Connected to %d of %d ALICAT devices",
                 successCount, ctx->numDevices);
    
    return ALICAT_SUCCESS;
}

static int ALICAT_AdapterDisconnect(void *deviceContext) {
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)deviceContext;
    
    if (!ctx) return ALICAT_SUCCESS;
    
    // Stop all devices first
    for (int i = 0; i < ctx->numDevices; i++) {
        if (ctx->handles[i].isConnected) {
            LogMessageEx(LOG_DEVICE_ALICAT, "Stopping ALICAT address %d...",
                         ctx->modbusAddresses[i]);
            
            // Set setpoint to zero before disconnecting
            ALICAT_SetSetpoint(&ctx->handles[i], 0.0);
            
            // Disconnect
            ALICAT_Disconnect(&ctx->handles[i]);
        }
    }
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Disconnected all ALICAT devices from COM%d", ctx->comPort);
    
    return ALICAT_SUCCESS;
}

static int ALICAT_AdapterTestConnection(void *deviceContext) {
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)deviceContext;
    
    if (!ctx) return ALICAT_ERROR_NOT_CONNECTED;
    
    // Test connection to all devices
    int connectedCount = 0;
    for (int i = 0; i < ctx->numDevices; i++) {
        if (ALICAT_TestConnection(&ctx->handles[i]) == ALICAT_SUCCESS) {
            connectedCount++;
        }
    }
    
    return (connectedCount > 0) ? ALICAT_SUCCESS : ALICAT_ERROR_NOT_CONNECTED;
}

static bool ALICAT_AdapterIsConnected(void *deviceContext) {
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)deviceContext;
    
    if (!ctx) return false;
    
    // Return true if any device is connected
    for (int i = 0; i < ctx->numDevices; i++) {
        if (ctx->handles[i].isConnected) {
            return true;
        }
    }
    
    return false;
}

static int ALICAT_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)deviceContext;
    ALICAT_CommandParams *cmdParams = (ALICAT_CommandParams*)params;
    ALICAT_CommandResult *cmdResult = (ALICAT_CommandResult*)result;
    
    if (!ctx || !cmdParams || !cmdResult) {
        return ALICAT_ERROR_INVALID_PARAM;
    }
    
    // Find the target device handle
    ALICAT_Handle *handle = GetDeviceHandle(ctx, cmdParams->setSetpoint.modbusAddress);
    if (!handle) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Device with address %d not found",
                   cmdParams->setSetpoint.modbusAddress);
        return ALICAT_ERROR_NOT_CONNECTED;
    }
    
    switch ((ALICAT_CommandType)commandType) {
        case ALICAT_CMD_SET_SETPOINT:
            cmdResult->errorCode = ALICAT_SetSetpoint(handle, cmdParams->setSetpoint.flowRate);
            break;
            
        case ALICAT_CMD_SET_GAS:
            cmdResult->errorCode = ALICAT_SetGas(handle, cmdParams->setGas.gasType);
            break;
            
        case ALICAT_CMD_TARE:
            cmdResult->errorCode = ALICAT_Tare(handle);
            break;
            
        case ALICAT_CMD_SET_SETPOINT_SOURCE:
            cmdResult->errorCode = ALICAT_SetSetpointSource(handle, cmdParams->setSetpointSource.source);
            break;
            
        case ALICAT_CMD_SET_PID_PARAMS:
            cmdResult->errorCode = ALICAT_SetPIDParams(handle, &cmdParams->setPIDParams.params);
            break;
            
        case ALICAT_CMD_SET_FLOW_AVERAGING:
            cmdResult->errorCode = ALICAT_SetFlowAveraging(handle, cmdParams->setFlowAveraging.averagingMs);
            break;
            
        case ALICAT_CMD_SET_REF_TEMPERATURE:
            cmdResult->errorCode = ALICAT_SetRefTemperature(handle, cmdParams->setRefTemperature.tempC);
            break;
            
        case ALICAT_CMD_SET_WATCHDOG:
            cmdResult->errorCode = ALICAT_SetWatchdog(handle, cmdParams->setWatchdog.timeoutMs);
            break;
            
        case ALICAT_CMD_SET_AUTOTARE:
            cmdResult->errorCode = ALICAT_SetAutotare(handle, cmdParams->setAutotare.enable);
            break;
            
        case ALICAT_CMD_CONFIGURE:
            cmdResult->errorCode = ALICAT_Configure(handle, &cmdParams->configure.config);
            break;
            
        case ALICAT_CMD_CONFIGURE_DEFAULT:
            cmdResult->errorCode = ALICAT_ConfigureDefault(handle);
            break;
            
        case ALICAT_CMD_FACTORY_RESET:
            cmdResult->errorCode = ALICAT_FactoryReset(handle);
            break;
            
        case ALICAT_CMD_GET_STATUS:
            cmdResult->errorCode = ALICAT_GetStatus(handle, &cmdResult->data.status);
            break;
            
        case ALICAT_CMD_GET_FLOW_RATE:
            cmdResult->errorCode = ALICAT_GetFlowRate(handle, &cmdResult->data.flowRate);
            break;
            
        case ALICAT_CMD_GET_SETPOINT:
            cmdResult->errorCode = ALICAT_GetSetpoint(handle, &cmdResult->data.setpoint);
            break;
            
        case ALICAT_CMD_GET_TEMPERATURE:
            cmdResult->errorCode = ALICAT_GetTemperature(handle, &cmdResult->data.temperature);
            break;
            
        case ALICAT_CMD_GET_TOTAL_VOLUME:
            cmdResult->errorCode = ALICAT_GetTotalVolume(handle, &cmdResult->data.totalVolume);
            break;
            
        case ALICAT_CMD_GET_VALVE_DRIVE:
            cmdResult->errorCode = ALICAT_GetValveDrive(handle, &cmdResult->data.valveDrive);
            break;
            
        case ALICAT_CMD_GET_PID_PARAMS:
            cmdResult->errorCode = ALICAT_GetPIDParams(handle, &cmdResult->data.pidParams);
            break;
            
        case ALICAT_CMD_RESET_TOTALIZER:
            cmdResult->errorCode = ALICAT_ResetTotalizer(handle);
            break;
            
        case ALICAT_CMD_SET_BATCH_VOLUME:
            cmdResult->errorCode = ALICAT_SetBatchVolume(handle, cmdParams->setBatchVolume.volume);
            break;
            
        case ALICAT_CMD_GET_BATCH_REMAINING:
            cmdResult->errorCode = ALICAT_GetBatchRemaining(handle, &cmdResult->data.batchRemaining);
            break;
            
        default:
            cmdResult->errorCode = ALICAT_ERROR_INVALID_PARAM;
            break;
    }
    
    // Log errors appropriately
    if (cmdResult->errorCode != ALICAT_SUCCESS) {
        switch (cmdResult->errorCode) {
            case ALICAT_ERROR_BUSY:
                LogWarningEx(LOG_DEVICE_ALICAT, "Device address %d busy: %s",
                           cmdParams->setSetpoint.modbusAddress,
                           ALICAT_GetErrorString(cmdResult->errorCode));
                break;
            case ALICAT_ERROR_TIMEOUT:
            case ALICAT_ERROR_COMM:
            case ALICAT_ERROR_NOT_CONNECTED:
                LogErrorEx(LOG_DEVICE_ALICAT, "Communication error with address %d: %s",
                         cmdParams->setSetpoint.modbusAddress,
                         ALICAT_GetErrorString(cmdResult->errorCode));
                break;
            default:
                LogErrorEx(LOG_DEVICE_ALICAT, "Command %s failed for address %d: %s",
                         ALICAT_QueueGetCommandTypeName(commandType),
                         cmdParams->setSetpoint.modbusAddress,
                         ALICAT_GetErrorString(cmdResult->errorCode));
                break;
        }
    }
    
    return cmdResult->errorCode;
}

static void* ALICAT_AdapterCreateCommandParams(int commandType, void *sourceParams) {
    if (!sourceParams) return NULL;
    
    ALICAT_CommandParams *params = malloc(sizeof(ALICAT_CommandParams));
    if (!params) return NULL;
    
    *params = *(ALICAT_CommandParams*)sourceParams;
    
    return params;
}

static void ALICAT_AdapterFreeCommandParams(int commandType, void *params) {
    if (!params) return;
    free(params);
}

static void* ALICAT_AdapterCreateCommandResult(int commandType) {
    ALICAT_CommandResult *result = calloc(1, sizeof(ALICAT_CommandResult));
    return result;
}

static void ALICAT_AdapterFreeCommandResult(int commandType, void *result) {
    if (!result) return;
    free(result);
}

static void ALICAT_AdapterCopyCommandResult(int commandType, void *dest, void *src) {
    if (!dest || !src) return;
    
    ALICAT_CommandResult *destResult = (ALICAT_CommandResult*)dest;
    ALICAT_CommandResult *srcResult = (ALICAT_CommandResult*)src;
    
    *destResult = *srcResult;
}

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

ALICAT_QueueManager* ALICAT_QueueInit(int comPort, int baudRate, int *modbusAddresses, int numDevices) {
    if (!modbusAddresses || numDevices <= 0 || numDevices > MAX_ALICAT_DEVICES) {
        LogErrorEx(LOG_DEVICE_ALICAT, "ALICAT_QueueInit: Invalid parameters (numDevices=%d, max=%d)",
                   numDevices, MAX_ALICAT_DEVICES);
        return NULL;
    }
    
    // Create device context
    ALICAT_DeviceContext *context = calloc(1, sizeof(ALICAT_DeviceContext));
    if (!context) {
        LogErrorEx(LOG_DEVICE_ALICAT, "ALICAT_QueueInit: Failed to allocate device context");
        return NULL;
    }
    
    // Create connection parameters
    ALICAT_ConnectionParams *connParams = calloc(1, sizeof(ALICAT_ConnectionParams));
    if (!connParams) {
        free(context);
        LogErrorEx(LOG_DEVICE_ALICAT, "ALICAT_QueueInit: Failed to allocate connection params");
        return NULL;
    }
    
    // Allocate and copy Modbus addresses
    connParams->modbusAddresses = malloc(sizeof(int) * numDevices);
    if (!connParams->modbusAddresses) {
        free(context);
        free(connParams);
        LogErrorEx(LOG_DEVICE_ALICAT, "ALICAT_QueueInit: Failed to allocate address array");
        return NULL;
    }
    
    connParams->comPort = comPort;
    connParams->baudRate = baudRate;
    connParams->numDevices = numDevices;
    
    for (int i = 0; i < numDevices; i++) {
        connParams->modbusAddresses[i] = modbusAddresses[i];
        LogMessageEx(LOG_DEVICE_ALICAT, "ALICAT_QueueInit: Will initialize address %d",
                     modbusAddresses[i]);
    }
    
    // Create the generic device queue
    ALICAT_QueueManager *mgr = DeviceQueue_Create(&g_alicatAdapter, context, connParams, 0);
    
    if (!mgr) {
        free(context);
        free(connParams->modbusAddresses);
        free(connParams);
        LogErrorEx(LOG_DEVICE_ALICAT, "ALICAT_QueueInit: Failed to create device queue");
        return NULL;
    }
    
    // Set logging device
    DeviceQueue_SetLogDevice(mgr, LOG_DEVICE_ALICAT);
    
    LogMessageEx(LOG_DEVICE_ALICAT, "ALICAT_QueueInit: Successfully created queue manager for %d devices",
                 numDevices);
    
    return mgr;
}

ALICAT_Handle* ALICAT_QueueGetHandle(ALICAT_QueueManager *mgr, int modbusAddress) {
    if (!mgr) return NULL;
    
    ALICAT_DeviceContext *context = (ALICAT_DeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    if (!context) return NULL;
    
    return GetDeviceHandle(context, modbusAddress);
}

void ALICAT_QueueShutdown(ALICAT_QueueManager *mgr) {
    if (!mgr) return;
    
    // Get and free the device context
    ALICAT_DeviceContext *context = (ALICAT_DeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    
    // Destroy the generic queue (this will call disconnect)
    DeviceQueue_Destroy(mgr);
    
    // Free our contexts
    if (context) free(context);
}

bool ALICAT_QueueIsRunning(ALICAT_QueueManager *mgr) {
    return DeviceQueue_IsRunning(mgr);
}

void ALICAT_QueueGetStats(ALICAT_QueueManager *mgr, ALICAT_QueueStats *stats) {
    DeviceQueue_GetStats(mgr, stats);
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

TransactionHandle ALICAT_QueueBeginTransaction(ALICAT_QueueManager *mgr) {
    return DeviceQueue_BeginTransaction(mgr);
}

int ALICAT_QueueAddToTransaction(ALICAT_QueueManager *mgr, TransactionHandle txn,
                                ALICAT_CommandType type, ALICAT_CommandParams *params) {
    return DeviceQueue_AddToTransaction(mgr, txn, type, params);
}

int ALICAT_QueueCommitTransaction(ALICAT_QueueManager *mgr, TransactionHandle txn,
                                 ALICAT_TransactionCallback callback, void *userData) {
    return DeviceQueue_CommitTransaction(mgr, txn, callback, userData);
}

int ALICAT_QueueCancelTransaction(ALICAT_QueueManager *mgr, TransactionHandle txn) {
    return DeviceQueue_CancelTransaction(mgr, txn);
}

/******************************************************************************
 * Global Queue Manager Functions
 ******************************************************************************/

void ALICAT_SetGlobalQueueManager(ALICAT_QueueManager *mgr) {
    g_alicatQueueManager = mgr;
}

ALICAT_QueueManager* ALICAT_GetGlobalQueueManager(void) {
    return g_alicatQueueManager;
}

/******************************************************************************
 * Individual Device Wrapper Functions
 ******************************************************************************/

int ALICAT_SetSetpointQueued(int modbusAddress, double flowRate, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.setSetpoint = {modbusAddress, flowRate}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_SETPOINT,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_SetGasQueued(int modbusAddress, int gasType, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.setGas = {modbusAddress, gasType}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_GAS,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_TareQueued(int modbusAddress, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.tare = {modbusAddress}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_TARE,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_SetSetpointSourceQueued(int modbusAddress, int source, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.setSetpointSource = {modbusAddress, source}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_SETPOINT_SOURCE,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_SetPIDParamsQueued(int modbusAddress, const ALICAT_PIDParams *pidParams, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!pidParams) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.setPIDParams = {modbusAddress, *pidParams}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_PID_PARAMS,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_SetFlowAveragingQueued(int modbusAddress, int averagingMs, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.setFlowAveraging = {modbusAddress, averagingMs}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_FLOW_AVERAGING,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_SetRefTemperatureQueued(int modbusAddress, double tempC, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.setRefTemperature = {modbusAddress, tempC}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_REF_TEMPERATURE,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_SetWatchdogQueued(int modbusAddress, int timeoutMs, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.setWatchdog = {modbusAddress, timeoutMs}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_WATCHDOG,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_SetAutotareQueued(int modbusAddress, int enable, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.setAutotare = {modbusAddress, enable}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_AUTOTARE,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_ConfigureQueued(int modbusAddress, const ALICAT_Configuration *config, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!config) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.configure = {modbusAddress, *config}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_CONFIGURE,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_ConfigureDefaultQueued(int modbusAddress, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.configureDefault = {modbusAddress}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_CONFIGURE_DEFAULT,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_FactoryResetQueued(int modbusAddress, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.factoryReset = {modbusAddress}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_FACTORY_RESET,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_GetStatusQueued(int modbusAddress, ALICAT_Status *status, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!status) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.getStatus = {modbusAddress}};
    ALICAT_CommandResult result;
    
    int error = DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_GET_STATUS,
                                          &params, priority, &result,
                                          ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == ALICAT_SUCCESS) {
        *status = result.data.status;
    }
    return error;
}

int ALICAT_GetFlowRateQueued(int modbusAddress, double *flowRate, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!flowRate) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.getFlowRate = {modbusAddress}};
    ALICAT_CommandResult result;
    
    int error = DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_GET_FLOW_RATE,
                                          &params, priority, &result,
                                          ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == ALICAT_SUCCESS) {
        *flowRate = result.data.flowRate;
    }
    return error;
}

int ALICAT_GetSetpointQueued(int modbusAddress, double *setpoint, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!setpoint) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.getSetpoint = {modbusAddress}};
    ALICAT_CommandResult result;
    
    int error = DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_GET_SETPOINT,
                                          &params, priority, &result,
                                          ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == ALICAT_SUCCESS) {
        *setpoint = result.data.setpoint;
    }
    return error;
}

int ALICAT_GetTemperatureQueued(int modbusAddress, double *temperature, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!temperature) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.getTemperature = {modbusAddress}};
    ALICAT_CommandResult result;
    
    int error = DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_GET_TEMPERATURE,
                                          &params, priority, &result,
                                          ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == ALICAT_SUCCESS) {
        *temperature = result.data.temperature;
    }
    return error;
}

int ALICAT_GetTotalVolumeQueued(int modbusAddress, double *totalVolume, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!totalVolume) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.getTotalVolume = {modbusAddress}};
    ALICAT_CommandResult result;
    
    int error = DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_GET_TOTAL_VOLUME,
                                          &params, priority, &result,
                                          ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == ALICAT_SUCCESS) {
        *totalVolume = result.data.totalVolume;
    }
    return error;
}

int ALICAT_GetValveDriveQueued(int modbusAddress, double *valveDrive, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!valveDrive) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.getValveDrive = {modbusAddress}};
    ALICAT_CommandResult result;
    
    int error = DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_GET_VALVE_DRIVE,
                                          &params, priority, &result,
                                          ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == ALICAT_SUCCESS) {
        *valveDrive = result.data.valveDrive;
    }
    return error;
}

int ALICAT_GetPIDParamsQueued(int modbusAddress, ALICAT_PIDParams *params, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!params) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams cmdParams = {.getPIDParams = {modbusAddress}};
    ALICAT_CommandResult result;
    
    int error = DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_GET_PID_PARAMS,
                                          &cmdParams, priority, &result,
                                          ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == ALICAT_SUCCESS) {
        *params = result.data.pidParams;
    }
    return error;
}

int ALICAT_ResetTotalizerQueued(int modbusAddress, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.resetTotalizer = {modbusAddress}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_RESET_TOTALIZER,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_SetBatchVolumeQueued(int modbusAddress, double volume, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_CommandParams params = {.setBatchVolume = {modbusAddress, volume}};
    ALICAT_CommandResult result;
    
    return DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_SET_BATCH_VOLUME,
                                      &params, priority, &result,
                                      ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
}

int ALICAT_GetBatchRemainingQueued(int modbusAddress, double *remaining, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!remaining) return ERR_NULL_POINTER;
    
    ALICAT_CommandParams params = {.getBatchRemaining = {modbusAddress}};
    ALICAT_CommandResult result;
    
    int error = DeviceQueue_CommandBlocking(g_alicatQueueManager, ALICAT_CMD_GET_BATCH_REMAINING,
                                          &params, priority, &result,
                                          ALICAT_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == ALICAT_SUCCESS) {
        *remaining = result.data.batchRemaining;
    }
    return error;
}

/******************************************************************************
 * "All Devices" Convenience Functions
 ******************************************************************************/

int ALICAT_SetSetpointAllQueued(double flowRate, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)DeviceQueue_GetDeviceContext(g_alicatQueueManager);
    if (!ctx) return ERR_QUEUE_NOT_INIT;
    
    int allSuccess = ALICAT_SUCCESS;
    int failureCount = 0;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting setpoint to %.3f for all %d ALICAT devices...",
                 flowRate, ctx->numDevices);
    
    for (int i = 0; i < ctx->numDevices; i++) {
        int result = ALICAT_SetSetpointQueued(ctx->modbusAddresses[i], flowRate, priority);
        if (result != ALICAT_SUCCESS) {
            LogErrorEx(LOG_DEVICE_ALICAT, "Failed to set setpoint for address %d: %s",
                       ctx->modbusAddresses[i], ALICAT_GetErrorString(result));
            if (allSuccess == ALICAT_SUCCESS) {
                allSuccess = result;  // Store first failure
            }
            failureCount++;
        }
    }
    
    if (failureCount == 0) {
        LogMessageEx(LOG_DEVICE_ALICAT, "Successfully set setpoint for all ALICAT devices");
    } else {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to set setpoint for %d of %d ALICAT devices",
                   failureCount, ctx->numDevices);
    }
    
    return allSuccess;
}

int ALICAT_ConfigureAllDefaultQueued(DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)DeviceQueue_GetDeviceContext(g_alicatQueueManager);
    if (!ctx) return ERR_QUEUE_NOT_INIT;
    
    int allSuccess = ALICAT_SUCCESS;
    int failureCount = 0;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Configuring all %d ALICAT devices...", ctx->numDevices);
    
    for (int i = 0; i < ctx->numDevices; i++) {
        int result = ALICAT_ConfigureDefaultQueued(ctx->modbusAddresses[i], priority);
        if (result != ALICAT_SUCCESS) {
            LogErrorEx(LOG_DEVICE_ALICAT, "Failed to configure address %d: %s",
                       ctx->modbusAddresses[i], ALICAT_GetErrorString(result));
            if (allSuccess == ALICAT_SUCCESS) {
                allSuccess = result;
            }
            failureCount++;
        }
    }
    
    if (failureCount == 0) {
        LogMessageEx(LOG_DEVICE_ALICAT, "Successfully configured all ALICAT devices");
    } else {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to configure %d of %d ALICAT devices",
                   failureCount, ctx->numDevices);
    }
    
    return allSuccess;
}

int ALICAT_SetGasAllQueued(int gasType, DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)DeviceQueue_GetDeviceContext(g_alicatQueueManager);
    if (!ctx) return ERR_QUEUE_NOT_INIT;
    
    int allSuccess = ALICAT_SUCCESS;
    int failureCount = 0;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting gas to %s for all %d ALICAT devices...",
                 ALICAT_GetGasName(gasType), ctx->numDevices);
    
    for (int i = 0; i < ctx->numDevices; i++) {
        int result = ALICAT_SetGasQueued(ctx->modbusAddresses[i], gasType, priority);
        if (result != ALICAT_SUCCESS) {
            LogErrorEx(LOG_DEVICE_ALICAT, "Failed to set gas for address %d: %s",
                       ctx->modbusAddresses[i], ALICAT_GetErrorString(result));
            if (allSuccess == ALICAT_SUCCESS) {
                allSuccess = result;
            }
            failureCount++;
        }
    }
    
    if (failureCount == 0) {
        LogMessageEx(LOG_DEVICE_ALICAT, "Successfully set gas for all ALICAT devices");
    } else {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to set gas for %d of %d ALICAT devices",
                   failureCount, ctx->numDevices);
    }
    
    return allSuccess;
}

int ALICAT_TareAllQueued(DevicePriority priority) {
    if (!g_alicatQueueManager) return ERR_QUEUE_NOT_INIT;
    
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)DeviceQueue_GetDeviceContext(g_alicatQueueManager);
    if (!ctx) return ERR_QUEUE_NOT_INIT;
    
    int allSuccess = ALICAT_SUCCESS;
    int failureCount = 0;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Taring all %d ALICAT devices...", ctx->numDevices);
    
    for (int i = 0; i < ctx->numDevices; i++) {
        int result = ALICAT_TareQueued(ctx->modbusAddresses[i], priority);
        if (result != ALICAT_SUCCESS) {
            LogErrorEx(LOG_DEVICE_ALICAT, "Failed to tare address %d: %s",
                       ctx->modbusAddresses[i], ALICAT_GetErrorString(result));
            if (allSuccess == ALICAT_SUCCESS) {
                allSuccess = result;
            }
            failureCount++;
        }
    }
    
    if (failureCount == 0) {
        LogMessageEx(LOG_DEVICE_ALICAT, "Successfully tared all ALICAT devices");
    } else {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to tare %d of %d ALICAT devices",
                   failureCount, ctx->numDevices);
    }
    
    return allSuccess;
}

int ALICAT_GetStatusAllQueued(ALICAT_Status *statuses, int *numDevices, DevicePriority priority) {
    ALICAT_QueueManager *queueMgr = ALICAT_GetGlobalQueueManager();
    if (!queueMgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    if (!statuses || !numDevices) {
        return ERR_NULL_POINTER;
    }
    
    ALICAT_DeviceContext *ctx = (ALICAT_DeviceContext*)DeviceQueue_GetDeviceContext(queueMgr);
    if (!ctx) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    *numDevices = 0;
    int allSuccess = ALICAT_SUCCESS;
    int successCount = 0;
    
    for (int i = 0; i < ctx->numDevices; i++) {
        int result = ALICAT_GetStatusQueued(ctx->modbusAddresses[i], &statuses[i], priority);
        if (result == ALICAT_SUCCESS) {
            successCount++;
        } else {
            LogErrorEx(LOG_DEVICE_ALICAT, "Failed to get status from ALICAT address %d: %s",
                       ctx->modbusAddresses[i], ALICAT_GetErrorString(result));
            if (allSuccess == ALICAT_SUCCESS) {
                allSuccess = result;  // Store first failure
            }
        }
    }
    
    *numDevices = ctx->numDevices;
    
    if (successCount == 0) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to get status from any ALICAT devices");
        return allSuccess;
    } else if (successCount < ctx->numDevices) {
        LogWarningEx(LOG_DEVICE_ALICAT, "Got status from %d of %d ALICAT devices",
                     successCount, ctx->numDevices);
        return allSuccess;  // Return the first error encountered
    }
    
    LogDebugEx(LOG_DEVICE_ALICAT, "Successfully got status from all %d ALICAT devices",
               ctx->numDevices);
    return ALICAT_SUCCESS;
}

/******************************************************************************
 * Async Command Function Implementations
 ******************************************************************************/

CommandID ALICAT_GetStatusAsync(int modbusAddress, ALICAT_CommandCallback callback, void *userData, DevicePriority priority) {
    ALICAT_QueueManager *mgr = ALICAT_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    ALICAT_CommandParams params = {.getStatus = {modbusAddress}};
    
    return DeviceQueue_CommandAsync(mgr, ALICAT_CMD_GET_STATUS, &params,
                                   priority, callback, userData);
}

CommandID ALICAT_SetSetpointAsync(int modbusAddress, double flowRate, ALICAT_CommandCallback callback, void *userData, DevicePriority priority) {
    ALICAT_QueueManager *mgr = ALICAT_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    ALICAT_CommandParams params = {.setSetpoint = {modbusAddress, flowRate}};
    
    return DeviceQueue_CommandAsync(mgr, ALICAT_CMD_SET_SETPOINT, &params,
                                   priority, callback, userData);
}

CommandID ALICAT_SetGasAsync(int modbusAddress, int gasType, ALICAT_CommandCallback callback, void *userData, DevicePriority priority) {
    ALICAT_QueueManager *mgr = ALICAT_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    ALICAT_CommandParams params = {.setGas = {modbusAddress, gasType}};
    
    return DeviceQueue_CommandAsync(mgr, ALICAT_CMD_SET_GAS, &params,
                                   priority, callback, userData);
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* ALICAT_QueueGetCommandTypeName(ALICAT_CommandType type) {
    if (type >= 0 && type < ALICAT_CMD_TYPE_COUNT) {
        return g_commandTypeNames[type];
    }
    return "UNKNOWN";
}

int ALICAT_QueueGetCommandDelay(ALICAT_CommandType type) {
    switch (type) {
        case ALICAT_CMD_SET_SETPOINT:
            return ALICAT_DELAY_SETPOINT_CHANGE;
            
        case ALICAT_CMD_SET_GAS:
            return ALICAT_DELAY_CONFIG_CHANGE;
            
        case ALICAT_CMD_TARE:
            return ALICAT_DELAY_TARE;
            
        case ALICAT_CMD_SET_SETPOINT_SOURCE:
        case ALICAT_CMD_SET_PID_PARAMS:
        case ALICAT_CMD_SET_FLOW_AVERAGING:
        case ALICAT_CMD_SET_REF_TEMPERATURE:
        case ALICAT_CMD_SET_WATCHDOG:
        case ALICAT_CMD_SET_AUTOTARE:
        case ALICAT_CMD_CONFIGURE:
        case ALICAT_CMD_CONFIGURE_DEFAULT:
            return ALICAT_DELAY_CONFIG_CHANGE;
            
        case ALICAT_CMD_FACTORY_RESET:
            return 1000; // 1 second after factory reset
            
        case ALICAT_CMD_GET_STATUS:
        case ALICAT_CMD_GET_FLOW_RATE:
        case ALICAT_CMD_GET_SETPOINT:
        case ALICAT_CMD_GET_TEMPERATURE:
        case ALICAT_CMD_GET_TOTAL_VOLUME:
        case ALICAT_CMD_GET_VALVE_DRIVE:
        case ALICAT_CMD_GET_PID_PARAMS:
        case ALICAT_CMD_GET_BATCH_REMAINING:
            return ALICAT_DELAY_AFTER_READ;
            
        case ALICAT_CMD_RESET_TOTALIZER:
        case ALICAT_CMD_SET_BATCH_VOLUME:
            return ALICAT_DELAY_AFTER_WRITE;
            
        default:
            return ALICAT_DELAY_RECOVERY;
    }
}

/******************************************************************************
 * Cancel Functions (delegate to generic queue)
 ******************************************************************************/

int ALICAT_QueueCancelCommand(ALICAT_QueueManager *mgr, CommandID cmdId) {
    return DeviceQueue_CancelCommand(mgr, cmdId);
}

int ALICAT_QueueCancelByType(ALICAT_QueueManager *mgr, ALICAT_CommandType type) {
    return DeviceQueue_CancelByType(mgr, type);
}

int ALICAT_QueueCancelByAge(ALICAT_QueueManager *mgr, double ageSeconds) {
    return DeviceQueue_CancelByAge(mgr, ageSeconds);
}

int ALICAT_QueueCancelAll(ALICAT_QueueManager *mgr) {
    return DeviceQueue_CancelAll(mgr);
}
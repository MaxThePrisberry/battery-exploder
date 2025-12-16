/******************************************************************************
 * ni9472_queue.c
 *
 * Thread-safe command queue implementation for NI 9472 Digital Output Module
 * Built on top of the generic device queue system
 ******************************************************************************/

#include "ni9472_queue.h"
#include "logging.h"
#include <ansi_c.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char* g_commandTypeNames[] = {
    "NONE",
    "SET_CHANNEL",
    "GET_CHANNEL_STATE",
    "SET_MULTIPLE_CHANNELS",
    "SET_ALL_CHANNELS",
    "GET_ALL_CHANNELS",
    "TEST_CONNECTION"
};

// Global queue manager pointer
static NI9472_QueueManager *g_ni9472QueueManager = NULL;

// Queue a command (blocking)
static int NI9472_QueueCommandBlocking(NI9472_QueueManager *mgr, NI9472_CommandType type,
                                      NI9472_CommandParams *params, DevicePriority priority,
                                      NI9472_CommandResult *result, int timeoutMs);

// Queue a command (async with callback)
static CommandID NI9472_QueueCommandAsync(NI9472_QueueManager *mgr, NI9472_CommandType type,
                                         NI9472_CommandParams *params, DevicePriority priority,
                                         NI9472_CommandCallback callback, void *userData);

/******************************************************************************
 * NI9472 Device Context Structure
 ******************************************************************************/

typedef struct {
    NI9472_Handle handle;
    int slot;
} NI9472_DeviceContext;

/******************************************************************************
 * NI9472 Connection Parameters
 ******************************************************************************/

typedef struct {
    int slot;
} NI9472_ConnectionParams;

/******************************************************************************
 * Device Adapter Implementation
 ******************************************************************************/

// Forward declarations for adapter functions
static int NI9472_AdapterConnect(void *deviceContext, void *connectionParams);
static int NI9472_AdapterDisconnect(void *deviceContext);
static int NI9472_AdapterTestConnection(void *deviceContext);
static bool NI9472_AdapterIsConnected(void *deviceContext);
static int NI9472_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result);
static void* NI9472_AdapterCreateCommandParams(int commandType, void *sourceParams);
static void NI9472_AdapterFreeCommandParams(int commandType, void *params);
static void* NI9472_AdapterCreateCommandResult(int commandType);
static void NI9472_AdapterFreeCommandResult(int commandType, void *result);
static void NI9472_AdapterCopyCommandResult(int commandType, void *dest, void *src);

// NI9472 device adapter
static const DeviceAdapter g_ni9472Adapter = {
    .deviceName = "NI9472",

    // Connection management
    .connect = NI9472_AdapterConnect,
    .disconnect = NI9472_AdapterDisconnect,
    .testConnection = NI9472_AdapterTestConnection,
    .isConnected = NI9472_AdapterIsConnected,

    // Command execution
    .executeCommand = NI9472_AdapterExecuteCommand,

    // Command management
    .createCommandParams = NI9472_AdapterCreateCommandParams,
    .freeCommandParams = NI9472_AdapterFreeCommandParams,
    .createCommandResult = NI9472_AdapterCreateCommandResult,
    .freeCommandResult = NI9472_AdapterFreeCommandResult,
    .copyCommandResult = NI9472_AdapterCopyCommandResult,

    // Utility functions
    .getCommandTypeName = (const char* (*)(int))NI9472_QueueGetCommandTypeName,
    .getCommandDelay = NI9472_QueueGetCommandDelay,
    .getErrorString = GetErrorString
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int NI9472_AdapterConnect(void *deviceContext, void *connectionParams) {
    NI9472_DeviceContext *ctx = (NI9472_DeviceContext*)deviceContext;
    NI9472_ConnectionParams *params = (NI9472_ConnectionParams*)connectionParams;
    int result;

    LogMessageEx(LOG_DEVICE_NI9472, "Connecting to NI 9472 on slot %d...", params->slot);
    result = NI9472_Initialize(&ctx->handle, params->slot);

    if (result == NI9472_SUCCESS) {
        ctx->slot = params->slot;
    }

    return result;
}

static int NI9472_AdapterDisconnect(void *deviceContext) {
    NI9472_DeviceContext *ctx = (NI9472_DeviceContext*)deviceContext;

    if (ctx->handle.isConnected) {
        NI9472_Close(&ctx->handle);
    }

    return NI9472_SUCCESS;
}

static int NI9472_AdapterTestConnection(void *deviceContext) {
    NI9472_DeviceContext *ctx = (NI9472_DeviceContext*)deviceContext;
    return NI9472_TestConnection(&ctx->handle);
}

static bool NI9472_AdapterIsConnected(void *deviceContext) {
    NI9472_DeviceContext *ctx = (NI9472_DeviceContext*)deviceContext;
    return ctx->handle.isConnected;
}

static int NI9472_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    NI9472_DeviceContext *ctx = (NI9472_DeviceContext*)deviceContext;
    NI9472_CommandParams *cmdParams = (NI9472_CommandParams*)params;
    NI9472_CommandResult *cmdResult = (NI9472_CommandResult*)result;

    switch ((NI9472_CommandType)commandType) {
        case NI9472_CMD_SET_CHANNEL:
            cmdResult->errorCode = NI9472_SetChannel(&ctx->handle,
                                                    cmdParams->setChannel.channel,
                                                    cmdParams->setChannel.state);
            break;

        case NI9472_CMD_GET_CHANNEL_STATE:
            cmdResult->errorCode = NI9472_GetChannelState(&ctx->handle,
                                                         cmdParams->getChannelState.channel,
                                                         &cmdResult->data.channelState);
            // Copy to output parameter if provided
            if (cmdParams->getChannelState.state) {
                *cmdParams->getChannelState.state = cmdResult->data.channelState;
            }
            break;

        case NI9472_CMD_SET_MULTIPLE_CHANNELS:
            cmdResult->errorCode = NI9472_SetMultipleChannels(&ctx->handle,
                                                             cmdParams->setMultipleChannels.channels,
                                                             cmdParams->setMultipleChannels.states,
                                                             cmdParams->setMultipleChannels.count);
            break;

        case NI9472_CMD_SET_ALL_CHANNELS:
            cmdResult->errorCode = NI9472_SetAllChannels(&ctx->handle,
                                                        cmdParams->setAllChannels.pattern);
            break;

        case NI9472_CMD_GET_ALL_CHANNELS:
            cmdResult->errorCode = NI9472_GetAllChannels(&ctx->handle,
                                                        &cmdResult->data.pattern);
            // Copy to output parameter if provided
            if (cmdParams->getAllChannels.pattern) {
                *cmdParams->getAllChannels.pattern = cmdResult->data.pattern;
            }
            break;

        case NI9472_CMD_TEST_CONNECTION:
            cmdResult->errorCode = NI9472_TestConnection(&ctx->handle);
            cmdResult->data.testResult = (cmdResult->errorCode == NI9472_SUCCESS) ? 1 : 0;
            break;

        default:
            cmdResult->errorCode = NI9472_ERROR_INVALID_PARAM;
            break;
    }

    // Log errors appropriately
    if (cmdResult->errorCode != NI9472_SUCCESS) {
        switch (cmdResult->errorCode) {
            case NI9472_ERROR_DAQMX:
            case NI9472_ERROR_NOT_CONNECTED:
            case NI9472_ERROR_WRITE_FAILED:
                LogErrorEx(LOG_DEVICE_NI9472, "Communication error: %s",
                         NI9472_GetErrorString(cmdResult->errorCode));
                break;
            default:
                LogErrorEx(LOG_DEVICE_NI9472, "Command %s failed: %s",
                         NI9472_QueueGetCommandTypeName(commandType),
                         NI9472_GetErrorString(cmdResult->errorCode));
                break;
        }
    }

    return cmdResult->errorCode;
}

static void* NI9472_AdapterCreateCommandParams(int commandType, void *sourceParams) {
    if (!sourceParams) return NULL;

    NI9472_CommandParams *params = malloc(sizeof(NI9472_CommandParams));
    if (!params) return NULL;

    *params = *(NI9472_CommandParams*)sourceParams;

    // Handle special cases that need deep copying
    if (commandType == NI9472_CMD_SET_MULTIPLE_CHANNELS && sourceParams) {
        NI9472_CommandParams *src = (NI9472_CommandParams*)sourceParams;
        if (src->setMultipleChannels.count > 0) {
            // Allocate arrays for channels and states
            int size = src->setMultipleChannels.count * sizeof(int);
            params->setMultipleChannels.channels = malloc(size);
            params->setMultipleChannels.states = malloc(size);

            if (params->setMultipleChannels.channels && params->setMultipleChannels.states) {
                memcpy(params->setMultipleChannels.channels, src->setMultipleChannels.channels, size);
                memcpy(params->setMultipleChannels.states, src->setMultipleChannels.states, size);
            } else {
                // Allocation failed - clean up
                if (params->setMultipleChannels.channels) free(params->setMultipleChannels.channels);
                if (params->setMultipleChannels.states) free(params->setMultipleChannels.states);
                free(params);
                return NULL;
            }
        }
    }

    return params;
}

static void NI9472_AdapterFreeCommandParams(int commandType, void *params) {
    if (!params) return;

    NI9472_CommandParams *cmdParams = (NI9472_CommandParams*)params;

    // Free arrays for multiple channels command
    if (commandType == NI9472_CMD_SET_MULTIPLE_CHANNELS) {
        if (cmdParams->setMultipleChannels.channels) free(cmdParams->setMultipleChannels.channels);
        if (cmdParams->setMultipleChannels.states) free(cmdParams->setMultipleChannels.states);
    }

    free(params);
}

static void* NI9472_AdapterCreateCommandResult(int commandType) {
    NI9472_CommandResult *result = calloc(1, sizeof(NI9472_CommandResult));
    return result;
}

static void NI9472_AdapterFreeCommandResult(int commandType, void *result) {
    if (!result) return;
    free(result);
}

static void NI9472_AdapterCopyCommandResult(int commandType, void *dest, void *src) {
    if (!dest || !src) return;

    NI9472_CommandResult *destResult = (NI9472_CommandResult*)dest;
    NI9472_CommandResult *srcResult = (NI9472_CommandResult*)src;

    *destResult = *srcResult;
}

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

NI9472_QueueManager* NI9472_QueueInit(int slot) {
    // Create device context
    NI9472_DeviceContext *context = calloc(1, sizeof(NI9472_DeviceContext));
    if (!context) {
        LogErrorEx(LOG_DEVICE_NI9472, "NI9472_QueueInit: Failed to allocate device context");
        return NULL;
    }

    // Create connection parameters
    NI9472_ConnectionParams *connParams = calloc(1, sizeof(NI9472_ConnectionParams));
    if (!connParams) {
        free(context);
        LogErrorEx(LOG_DEVICE_NI9472, "NI9472_QueueInit: Failed to allocate connection params");
        return NULL;
    }

    connParams->slot = slot;

    // Create the generic device queue
    NI9472_QueueManager *mgr = DeviceQueue_Create(&g_ni9472Adapter, context, connParams, 0);

    if (!mgr) {
        free(context);
        free(connParams);
        return NULL;
    }

    // Set logging device
    DeviceQueue_SetLogDevice(mgr, LOG_DEVICE_NI9472);

    return mgr;
}

NI9472_Handle* NI9472_QueueGetHandle(NI9472_QueueManager *mgr) {
    NI9472_DeviceContext *context = (NI9472_DeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    if (!context) return NULL;

    return &context->handle;
}

void NI9472_QueueShutdown(NI9472_QueueManager *mgr) {
    if (!mgr) return;

    // Get and free the device context
    NI9472_DeviceContext *context = (NI9472_DeviceContext*)DeviceQueue_GetDeviceContext(mgr);

    // Destroy the generic queue (this will call disconnect)
    DeviceQueue_Destroy(mgr);

    // Free our contexts
    if (context) free(context);
    // Note: Connection params are freed by the generic queue
}

bool NI9472_QueueIsRunning(NI9472_QueueManager *mgr) {
    return DeviceQueue_IsRunning(mgr);
}

void NI9472_QueueGetStats(NI9472_QueueManager *mgr, NI9472_QueueStats *stats) {
    DeviceQueue_GetStats(mgr, stats);
}

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

static int NI9472_QueueCommandBlocking(NI9472_QueueManager *mgr, NI9472_CommandType type,
                                      NI9472_CommandParams *params, DevicePriority priority,
                                      NI9472_CommandResult *result, int timeoutMs) {
    return DeviceQueue_CommandBlocking(mgr, type, params, priority, result, timeoutMs, NULL, NULL);
}

static CommandID NI9472_QueueCommandAsync(NI9472_QueueManager *mgr, NI9472_CommandType type,
                                         NI9472_CommandParams *params, DevicePriority priority,
                                         NI9472_CommandCallback callback, void *userData) {
    return DeviceQueue_CommandAsync(mgr, type, params, priority, callback, userData);
}

int NI9472_QueueCancelAll(NI9472_QueueManager *mgr) {
    return DeviceQueue_CancelAll(mgr);
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

TransactionHandle NI9472_QueueBeginTransaction(NI9472_QueueManager *mgr) {
    return DeviceQueue_BeginTransaction(mgr);
}

int NI9472_QueueAddToTransaction(NI9472_QueueManager *mgr, TransactionHandle txn,
                                NI9472_CommandType type, NI9472_CommandParams *params) {
    return DeviceQueue_AddToTransaction(mgr, txn, type, params);
}

int NI9472_QueueCommitTransaction(NI9472_QueueManager *mgr, TransactionHandle txn,
                                 NI9472_TransactionCallback callback, void *userData) {
    return DeviceQueue_CommitTransaction(mgr, txn, callback, userData);
}

/******************************************************************************
 * Wrapper Functions - No fallback behavior, require queue to be initialized
 ******************************************************************************/

void NI9472_SetGlobalQueueManager(NI9472_QueueManager *mgr) {
    g_ni9472QueueManager = mgr;
}

NI9472_QueueManager* NI9472_GetGlobalQueueManager(void) {
    return g_ni9472QueueManager;
}

int NI9472_SetChannelQueued(int channel, int state, DevicePriority priority) {
    if (!g_ni9472QueueManager) return ERR_QUEUE_NOT_INIT;

    NI9472_CommandParams params = {.setChannel = {channel, state}};
    NI9472_CommandResult result;

    return NI9472_QueueCommandBlocking(g_ni9472QueueManager, NI9472_CMD_SET_CHANNEL,
                                      &params, priority, &result,
                                      NI9472_QUEUE_COMMAND_TIMEOUT_MS);
}

int NI9472_GetChannelStateQueued(int channel, int *state, DevicePriority priority) {
    if (!g_ni9472QueueManager) return ERR_QUEUE_NOT_INIT;

    NI9472_CommandParams params = {.getChannelState = {channel, state}};
    NI9472_CommandResult result;

    return NI9472_QueueCommandBlocking(g_ni9472QueueManager, NI9472_CMD_GET_CHANNEL_STATE,
                                      &params, priority, &result,
                                      NI9472_QUEUE_COMMAND_TIMEOUT_MS);
}

int NI9472_SetMultipleChannelsQueued(const int *channels, const int *states, int count, DevicePriority priority) {
    if (!g_ni9472QueueManager) return ERR_QUEUE_NOT_INIT;

    NI9472_CommandParams params = {.setMultipleChannels = {(int*)channels, (int*)states, count}};
    NI9472_CommandResult result;

    return NI9472_QueueCommandBlocking(g_ni9472QueueManager, NI9472_CMD_SET_MULTIPLE_CHANNELS,
                                      &params, priority, &result,
                                      NI9472_QUEUE_COMMAND_TIMEOUT_MS);
}

int NI9472_SetAllChannelsQueued(uInt8 pattern, DevicePriority priority) {
    if (!g_ni9472QueueManager) return ERR_QUEUE_NOT_INIT;

    NI9472_CommandParams params = {.setAllChannels = {pattern}};
    NI9472_CommandResult result;

    return NI9472_QueueCommandBlocking(g_ni9472QueueManager, NI9472_CMD_SET_ALL_CHANNELS,
                                      &params, priority, &result,
                                      NI9472_QUEUE_COMMAND_TIMEOUT_MS);
}

int NI9472_GetAllChannelsQueued(uInt8 *pattern, DevicePriority priority) {
    if (!g_ni9472QueueManager) return ERR_QUEUE_NOT_INIT;

    NI9472_CommandParams params = {.getAllChannels = {pattern}};
    NI9472_CommandResult result;

    return NI9472_QueueCommandBlocking(g_ni9472QueueManager, NI9472_CMD_GET_ALL_CHANNELS,
                                      &params, priority, &result,
                                      NI9472_QUEUE_COMMAND_TIMEOUT_MS);
}

int NI9472_TestConnectionQueued(DevicePriority priority) {
    if (!g_ni9472QueueManager) return ERR_QUEUE_NOT_INIT;

    NI9472_CommandParams params = {0};
    NI9472_CommandResult result;

    return NI9472_QueueCommandBlocking(g_ni9472QueueManager, NI9472_CMD_TEST_CONNECTION,
                                      &params, priority, &result,
                                      NI9472_QUEUE_COMMAND_TIMEOUT_MS);
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* NI9472_QueueGetCommandTypeName(NI9472_CommandType type) {
    if (type >= 0 && type < NI9472_CMD_TYPE_COUNT) {
        return g_commandTypeNames[type];
    }
    return "UNKNOWN";
}

int NI9472_QueueGetCommandDelay(NI9472_CommandType type) {
    switch (type) {
        case NI9472_CMD_SET_CHANNEL:
        case NI9472_CMD_SET_MULTIPLE_CHANNELS:
        case NI9472_CMD_SET_ALL_CHANNELS:
            return NI9472_DELAY_AFTER_WRITE;

        case NI9472_CMD_GET_CHANNEL_STATE:
        case NI9472_CMD_GET_ALL_CHANNELS:
        case NI9472_CMD_TEST_CONNECTION:
            return NI9472_DELAY_RECOVERY;

        default:
            return NI9472_DELAY_RECOVERY;
    }
}

/******************************************************************************
 * Cancel Functions (delegate to generic queue)
 ******************************************************************************/

int NI9472_QueueCancelCommand(NI9472_QueueManager *mgr, CommandID cmdId) {
    return DeviceQueue_CancelCommand(mgr, cmdId);
}

int NI9472_QueueCancelByType(NI9472_QueueManager *mgr, NI9472_CommandType type) {
    return DeviceQueue_CancelByType(mgr, type);
}

int NI9472_QueueCancelByAge(NI9472_QueueManager *mgr, double ageSeconds) {
    return DeviceQueue_CancelByAge(mgr, ageSeconds);
}

int NI9472_QueueCancelTransaction(NI9472_QueueManager *mgr, TransactionHandle txn) {
    return DeviceQueue_CancelTransaction(mgr, txn);
}

/******************************************************************************
 * Advanced Transaction-Based Functions
 ******************************************************************************/

int NI9472_SetChannelsAtomic(const NI9472_ChannelState *channelStates, int count,
                            DevicePriority priority, NI9472_TransactionCallback callback,
                            void *userData) {
    if (!g_ni9472QueueManager) return ERR_QUEUE_NOT_INIT;

    if (!channelStates || count <= 0) {
        return NI9472_ERROR_INVALID_PARAM;
    }

    // Create transaction
    TransactionHandle txn = NI9472_QueueBeginTransaction(g_ni9472QueueManager);
    if (txn == 0) {
        LogErrorEx(LOG_DEVICE_NI9472, "Failed to begin atomic channel set transaction");
        return ERR_QUEUE_NOT_INIT;
    }

    // Set transaction priority
    DeviceQueue_SetTransactionPriority(g_ni9472QueueManager, txn, priority);

    NI9472_CommandParams params;
    int result = SUCCESS;

    // Add all channel commands to transaction
    for (int i = 0; i < count; i++) {
        params.setChannel.channel = channelStates[i].channel;
        params.setChannel.state = channelStates[i].state;

        result = NI9472_QueueAddToTransaction(g_ni9472QueueManager, txn, NI9472_CMD_SET_CHANNEL, &params);
        if (result != SUCCESS) {
            LogErrorEx(LOG_DEVICE_NI9472, "Failed to add channel %d to transaction",
                      channelStates[i].channel);
            goto cleanup;
        }
    }

    // Commit transaction
    result = NI9472_QueueCommitTransaction(g_ni9472QueueManager, txn, callback, userData);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_NI9472, "Atomic channel set transaction committed (%d channels)", count);
        return SUCCESS;
    }

cleanup:
    NI9472_QueueCancelTransaction(g_ni9472QueueManager, txn);
    LogErrorEx(LOG_DEVICE_NI9472, "Failed to create atomic channel set transaction");
    return result;
}

int NI9472_InitializeChannels(const int *lowChannels, int lowCount,
                             const int *highChannels, int highCount,
                             DevicePriority priority) {
    if (!g_ni9472QueueManager) return ERR_QUEUE_NOT_INIT;

    int totalChannels = (lowChannels ? lowCount : 0) + (highChannels ? highCount : 0);
    if (totalChannels == 0) {
        return SUCCESS;
    }

    LogMessageEx(LOG_DEVICE_NI9472, "Initializing %d channels (%d low, %d high)",
                totalChannels, lowCount, highCount);

    // Create transaction
    TransactionHandle txn = NI9472_QueueBeginTransaction(g_ni9472QueueManager);
    if (txn == 0) {
        LogErrorEx(LOG_DEVICE_NI9472, "Failed to begin channel initialization transaction");
        return ERR_QUEUE_NOT_INIT;
    }

    // Set transaction priority
    DeviceQueue_SetTransactionPriority(g_ni9472QueueManager, txn, priority);

    NI9472_CommandParams params;
    int result = SUCCESS;

    // Add commands to set channels low
    if (lowChannels && lowCount > 0) {
        for (int i = 0; i < lowCount; i++) {
            params.setChannel.channel = lowChannels[i];
            params.setChannel.state = NI9472_CHANNEL_LOW;

            result = NI9472_QueueAddToTransaction(g_ni9472QueueManager, txn, NI9472_CMD_SET_CHANNEL, &params);
            if (result != SUCCESS) goto cleanup;
        }
    }

    // Add commands to set channels high
    if (highChannels && highCount > 0) {
        for (int i = 0; i < highCount; i++) {
            params.setChannel.channel = highChannels[i];
            params.setChannel.state = NI9472_CHANNEL_HIGH;

            result = NI9472_QueueAddToTransaction(g_ni9472QueueManager, txn, NI9472_CMD_SET_CHANNEL, &params);
            if (result != SUCCESS) goto cleanup;
        }
    }

    // Commit transaction
    result = NI9472_QueueCommitTransaction(g_ni9472QueueManager, txn, NULL, NULL);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_NI9472, "Channel initialization transaction committed");
        return SUCCESS;
    }

cleanup:
    NI9472_QueueCancelTransaction(g_ni9472QueueManager, txn);
    LogErrorEx(LOG_DEVICE_NI9472, "Failed to initialize channels");
    return result;
}

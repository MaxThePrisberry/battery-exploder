/******************************************************************************
 * ni9472_queue.h
 *
 * Thread-safe command queue implementation for NI 9472 Digital Output Module
 * Built on top of the generic device queue system
 ******************************************************************************/

#ifndef NI9472_QUEUE_H
#define NI9472_QUEUE_H

#include "common.h"
#include "ni9472_dll.h"
#include "device_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Command delays (milliseconds)
#define NI9472_DELAY_AFTER_WRITE      10    // After writing channels
#define NI9472_DELAY_RECOVERY         5     // General recovery between commands

#define NI9472_CHANNEL_LOW              0
#define NI9472_CHANNEL_HIGH             1

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Use generic types from device_queue.h
typedef DeviceQueueManager NI9472_QueueManager;
typedef DeviceTransactionHandle TransactionHandle;
typedef DeviceCommandID CommandID;
typedef DeviceCommandCallback NI9472_CommandCallback;
typedef DeviceTransactionCallback NI9472_TransactionCallback;
typedef DeviceQueueStats NI9472_QueueStats;

// Map transaction constants
#define NI9472_MAX_TRANSACTION_COMMANDS  DEVICE_MAX_TRANSACTION_COMMANDS
#define NI9472_QUEUE_COMMAND_TIMEOUT_MS  DEVICE_QUEUE_COMMAND_TIMEOUT_MS

// Command types
typedef enum {
    NI9472_CMD_NONE = 0,

    // Channel control commands
    NI9472_CMD_SET_CHANNEL,
    NI9472_CMD_GET_CHANNEL_STATE,
    NI9472_CMD_SET_MULTIPLE_CHANNELS,
    NI9472_CMD_SET_ALL_CHANNELS,
    NI9472_CMD_GET_ALL_CHANNELS,

    // Test command
    NI9472_CMD_TEST_CONNECTION,

    NI9472_CMD_TYPE_COUNT
} NI9472_CommandType;

// Command parameters union
typedef union {
    struct {
        int channel;
        int state;
    } setChannel;

    struct {
        int channel;
        int *state;  // Output parameter
    } getChannelState;

    struct {
        int *channels;
        int *states;
        int count;
    } setMultipleChannels;

    struct {
        uInt8 pattern;
    } setAllChannels;

    struct {
        uInt8 *pattern;  // Output parameter
    } getAllChannels;
} NI9472_CommandParams;

// Command result structure
typedef struct {
    int errorCode;
    union {
        int channelState;  // For get channel state
        uInt8 pattern;     // For get all channels
        int testResult;    // For test connection
    } data;
} NI9472_CommandResult;

// Channel state structure for batch operations
typedef struct {
    int channel;
    int state;
} NI9472_ChannelState;

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

/**
 * Initialize the queue manager with specific slot number
 * @param slot - cDAQ slot number where NI 9472 is installed
 * @return Queue manager instance or NULL on failure
 */
NI9472_QueueManager* NI9472_QueueInit(int slot);

/**
 * Get the NI 9472 handle from the queue manager
 * @param mgr - Queue manager instance
 * @return NI 9472 handle or NULL if not connected
 */
NI9472_Handle* NI9472_QueueGetHandle(NI9472_QueueManager *mgr);

// Shutdown the queue manager
void NI9472_QueueShutdown(NI9472_QueueManager *mgr);

// Check if queue manager is running
bool NI9472_QueueIsRunning(NI9472_QueueManager *mgr);

// Get queue statistics
void NI9472_QueueGetStats(NI9472_QueueManager *mgr, NI9472_QueueStats *stats);

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

// Cancel commands
int NI9472_QueueCancelCommand(NI9472_QueueManager *mgr, CommandID cmdId);
int NI9472_QueueCancelByType(NI9472_QueueManager *mgr, NI9472_CommandType type);
int NI9472_QueueCancelByAge(NI9472_QueueManager *mgr, double ageSeconds);
int NI9472_QueueCancelAll(NI9472_QueueManager *mgr);

// Check if a command type is already queued
bool NI9472_QueueHasCommandType(NI9472_QueueManager *mgr, NI9472_CommandType type);

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

// Begin a transaction
TransactionHandle NI9472_QueueBeginTransaction(NI9472_QueueManager *mgr);

// Add command to transaction
int NI9472_QueueAddToTransaction(NI9472_QueueManager *mgr, TransactionHandle txn,
                                NI9472_CommandType type, NI9472_CommandParams *params);

// Commit transaction (async)
int NI9472_QueueCommitTransaction(NI9472_QueueManager *mgr, TransactionHandle txn,
                                 NI9472_TransactionCallback callback, void *userData);

// Cancel transaction
int NI9472_QueueCancelTransaction(NI9472_QueueManager *mgr, TransactionHandle txn);

/******************************************************************************
 * Wrapper Functions
 * All functions require the global queue manager to be initialized.
 * If not initialized, they return ERR_QUEUE_NOT_INIT.
 ******************************************************************************/

// Channel control functions
int NI9472_SetChannelQueued(int channel, int state, DevicePriority priority);
int NI9472_GetChannelStateQueued(int channel, int *state, DevicePriority priority);
int NI9472_SetMultipleChannelsQueued(const int *channels, const int *states, int count, DevicePriority priority);
int NI9472_SetAllChannelsQueued(uInt8 pattern, DevicePriority priority);
int NI9472_GetAllChannelsQueued(uInt8 *pattern, DevicePriority priority);

// Test function
int NI9472_TestConnectionQueued(DevicePriority priority);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

// Get command type name for logging
const char* NI9472_QueueGetCommandTypeName(NI9472_CommandType type);

// Get delay for command type
int NI9472_QueueGetCommandDelay(NI9472_CommandType type);

// Set/Get global queue manager
void NI9472_SetGlobalQueueManager(NI9472_QueueManager *mgr);
NI9472_QueueManager* NI9472_GetGlobalQueueManager(void);

/******************************************************************************
 * Advanced Functions
 ******************************************************************************/

/**
 * Set multiple channels atomically using a transaction
 * All channels are set in sequence without interruption
 *
 * @param channelStates - Array of channel/state pairs
 * @param count - Number of channels to set
 * @param priority - Priority for all commands in the transaction
 * @param callback - Optional callback for transaction completion
 * @param userData - User data for callback
 * @return SUCCESS or error code
 */
int NI9472_SetChannelsAtomic(const NI9472_ChannelState *channelStates, int count,
                            DevicePriority priority, NI9472_TransactionCallback callback,
                            void *userData);

/**
 * Initialize channels to a known state using a transaction
 *
 * @param lowChannels - Array of channels to set LOW
 * @param lowCount - Number of channels to set LOW
 * @param highChannels - Array of channels to set HIGH
 * @param highCount - Number of channels to set HIGH
 * @param priority - Priority for all commands in the transaction
 * @return SUCCESS or error code
 */
int NI9472_InitializeChannels(const int *lowChannels, int lowCount,
                             const int *highChannels, int highCount,
                             DevicePriority priority);

#endif // NI9472_QUEUE_H

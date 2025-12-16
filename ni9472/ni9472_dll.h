/******************************************************************************
 * ni9472_dll.h
 *
 * NI 9472 Digital Output Module Control Library
 * Header file for LabWindows/CVI
 *
 * This library provides functions to control the NI 9472 8-channel sourcing
 * digital output module via NI-DAQmx. The NI 9472 provides 8 digital output
 * channels capable of sourcing up to 24V.
 *
 * Typical use: Solenoid valve control, relay activation, indicator LEDs
 ******************************************************************************/

#ifndef NI9472_DLL_H
#define NI9472_DLL_H

#include "common.h"
#include <NIDAQmx.h>

/******************************************************************************
 * Constants and Definitions
 ******************************************************************************/

// NI9472-specific error codes (using base from common.h)
#define NI9472_SUCCESS                 SUCCESS
#define NI9472_ERROR_DAQMX            (ERR_BASE_NI9472 - 1)
#define NI9472_ERROR_INVALID_CHANNEL  (ERR_BASE_NI9472 - 2)
#define NI9472_ERROR_NOT_CONNECTED    (ERR_BASE_NI9472 - 3)
#define NI9472_ERROR_TASK_FAILED      (ERR_BASE_NI9472 - 4)
#define NI9472_ERROR_INVALID_PARAM    (ERR_BASE_NI9472 - 5)
#define NI9472_ERROR_WRITE_FAILED     (ERR_BASE_NI9472 - 6)

// Channel constants
#define NI9472_NUM_CHANNELS           8
#define NI9472_MIN_CHANNEL            0
#define NI9472_MAX_CHANNEL            7

// State constants
#define NI9472_STATE_LOW              0
#define NI9472_STATE_HIGH             1

// DAQmx constants
#define NI9472_WRITE_TIMEOUT          10.0  // seconds

/******************************************************************************
 * Data Structures
 ******************************************************************************/

// NI 9472 Handle structure
typedef struct {
    int slot;                    // cDAQ slot number
    TaskHandle taskHandle;       // NI-DAQmx task handle
    int isConnected;             // Connection status
    DeviceState state;           // Device state
    uInt8 channelStates[NI9472_NUM_CHANNELS];  // Current state cache
} NI9472_Handle;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

// Connection Functions
/**
 * Initialize connection to NI 9472 module
 * Creates a DAQmx task for all 8 digital output channels
 * @param handle - NI 9472 handle structure to initialize
 * @param slot - cDAQ slot number where NI 9472 is installed
 * @return NI9472_SUCCESS or error code
 */
int NI9472_Initialize(NI9472_Handle *handle, int slot);

/**
 * Close connection to NI 9472 module
 * Stops and clears the DAQmx task
 * @param handle - NI 9472 handle
 * @return NI9472_SUCCESS or error code
 */
int NI9472_Close(NI9472_Handle *handle);

/**
 * Test connection by toggling channel 0
 * @param handle - NI 9472 handle
 * @return NI9472_SUCCESS or error code
 */
int NI9472_TestConnection(NI9472_Handle *handle);

// Channel Control Functions
/**
 * Set digital channel state
 * @param handle - NI 9472 handle
 * @param channel - Channel number (0-7)
 * @param state - Channel state (0=low, 1=high)
 * @return NI9472_SUCCESS or error code
 */
int NI9472_SetChannel(NI9472_Handle *handle, int channel, int state);

/**
 * Get current digital channel state from cache
 * @param handle - NI 9472 handle
 * @param channel - Channel number (0-7)
 * @param state - Pointer to receive current state
 * @return NI9472_SUCCESS or error code
 */
int NI9472_GetChannelState(NI9472_Handle *handle, int channel, int *state);

/**
 * Set multiple channels at once
 * More efficient than individual SetChannel calls
 * @param handle - NI 9472 handle
 * @param channels - Array of channel numbers
 * @param states - Array of channel states
 * @param count - Number of channels to set
 * @return NI9472_SUCCESS or error code
 */
int NI9472_SetMultipleChannels(NI9472_Handle *handle, const int *channels, const int *states, int count);

/**
 * Set all 8 channels at once using a bit pattern
 * @param handle - NI 9472 handle
 * @param pattern - 8-bit pattern (bit 0 = channel 0, bit 7 = channel 7)
 * @return NI9472_SUCCESS or error code
 */
int NI9472_SetAllChannels(NI9472_Handle *handle, uInt8 pattern);

/**
 * Get all channel states as an 8-bit pattern
 * @param handle - NI 9472 handle
 * @param pattern - Pointer to receive 8-bit pattern
 * @return NI9472_SUCCESS or error code
 */
int NI9472_GetAllChannels(NI9472_Handle *handle, uInt8 *pattern);

// Utility Functions
/**
 * Get error string for error code
 * @param errorCode - Error code
 * @return Error description string
 */
const char* NI9472_GetErrorString(int errorCode);

/**
 * Get DAQmx error details
 * @param daqmxError - DAQmx error code
 * @param buffer - Buffer for error string
 * @param bufferSize - Size of buffer
 * @return NI9472_SUCCESS or error code
 */
int NI9472_GetDAQmxErrorString(int32 daqmxError, char *buffer, int bufferSize);

/**
 * Enable/disable debug output
 * @param enable - 1 to enable, 0 to disable
 */
void NI9472_EnableDebugOutput(int enable);

/**
 * Get library version string
 * @return Version string
 */
const char* NI9472_GetVersion(void);

/**
 * Check if handle is connected
 * @param handle - NI 9472 handle
 * @return 1 if connected, 0 otherwise
 */
int NI9472_IsConnected(const NI9472_Handle *handle);

#endif // NI9472_DLL_H

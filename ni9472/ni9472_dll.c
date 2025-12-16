/******************************************************************************
 * ni9472_dll.c
 *
 * NI 9472 Digital Output Module Control Library
 * Implementation file for LabWindows/CVI
 *
 * The NI 9472 provides 8 sourcing digital output channels (24V logic)
 ******************************************************************************/

#include "ni9472_dll.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char* libraryVersion = "1.0.0";

static const char* errorStrings[] = {
    "Success",
    "DAQmx error",
    "Invalid channel number",
    "Not connected",
    "Task operation failed",
    "Invalid parameter",
    "Write operation failed"
};

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static void PrintDebug(const char *format, ...) {
    if (!g_debugMode) return;

    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    LogDebugEx(LOG_DEVICE_NI9472, "%s", buffer);
}

static int ValidateChannel(int channel) {
    if (channel < NI9472_MIN_CHANNEL || channel > NI9472_MAX_CHANNEL) {
        LogErrorEx(LOG_DEVICE_NI9472, "Channel %d out of valid range (%d-%d)",
                   channel, NI9472_MIN_CHANNEL, NI9472_MAX_CHANNEL);
        return NI9472_ERROR_INVALID_CHANNEL;
    }
    return NI9472_SUCCESS;
}

/******************************************************************************
 * Connection Functions
 ******************************************************************************/

int NI9472_Initialize(NI9472_Handle *handle, int slot) {
    if (!handle) return NI9472_ERROR_INVALID_PARAM;

    // Initialize handle structure
    memset(handle, 0, sizeof(NI9472_Handle));
    handle->slot = slot;
    handle->state = DEVICE_STATE_CONNECTING;

    LogMessageEx(LOG_DEVICE_NI9472, "Initializing NI 9472 on cDAQ slot %d", slot);

    // Create task name
    char taskName[64];
    snprintf(taskName, sizeof(taskName), "NI9472_Slot_%d", slot);

    // Create DAQmx task
    int32 result = DAQmxCreateTask(taskName, &handle->taskHandle);
    if (result != 0) {
        char errBuff[2048];
        DAQmxGetExtendedErrorInfo(errBuff, sizeof(errBuff));
        LogErrorEx(LOG_DEVICE_NI9472, "Failed to create DAQmx task: %s", errBuff);
        handle->state = DEVICE_STATE_ERROR;
        return NI9472_ERROR_TASK_FAILED;
    }

    // Add all 8 digital output lines to the task
    // NI 9472 uses port0 with 8 lines (line0-line7)
    char channelName[128];
    snprintf(channelName, sizeof(channelName), "cDAQ1Mod%d/port0/line0:7", slot);

    result = DAQmxCreateDOChan(handle->taskHandle, channelName, "",
                               DAQmx_Val_ChanForAllLines);
    if (result != 0) {
        char errBuff[2048];
        DAQmxGetExtendedErrorInfo(errBuff, sizeof(errBuff));
        LogErrorEx(LOG_DEVICE_NI9472, "Failed to create digital output channels: %s", errBuff);
        DAQmxClearTask(handle->taskHandle);
        handle->taskHandle = 0;
        handle->state = DEVICE_STATE_ERROR;
        return NI9472_ERROR_TASK_FAILED;
    }

    // Start the task
    result = DAQmxStartTask(handle->taskHandle);
    if (result != 0) {
        char errBuff[2048];
        DAQmxGetExtendedErrorInfo(errBuff, sizeof(errBuff));
        LogErrorEx(LOG_DEVICE_NI9472, "Failed to start DAQmx task: %s", errBuff);
        DAQmxClearTask(handle->taskHandle);
        handle->taskHandle = 0;
        handle->state = DEVICE_STATE_ERROR;
        return NI9472_ERROR_TASK_FAILED;
    }

    // Initialize all channels to LOW
    memset(handle->channelStates, 0, sizeof(handle->channelStates));
    result = NI9472_SetAllChannels(handle, 0x00);
    if (result != NI9472_SUCCESS) {
        LogWarningEx(LOG_DEVICE_NI9472, "Failed to initialize channels to LOW");
    }

    handle->isConnected = 1;
    handle->state = DEVICE_STATE_READY;

    LogMessageEx(LOG_DEVICE_NI9472, "Successfully initialized NI 9472 on slot %d (%d channels)",
                 slot, NI9472_NUM_CHANNELS);

    return NI9472_SUCCESS;
}

int NI9472_Close(NI9472_Handle *handle) {
    if (!handle) return NI9472_ERROR_INVALID_PARAM;

    if (!handle->isConnected) {
        return NI9472_ERROR_NOT_CONNECTED;
    }

    LogMessageEx(LOG_DEVICE_NI9472, "Closing NI 9472 on slot %d", handle->slot);

    // Set all channels to LOW before closing
    NI9472_SetAllChannels(handle, 0x00);

    // Stop and clear the task
    if (handle->taskHandle != 0) {
        DAQmxStopTask(handle->taskHandle);
        DAQmxClearTask(handle->taskHandle);
        handle->taskHandle = 0;
    }

    handle->isConnected = 0;
    handle->state = DEVICE_STATE_DISCONNECTED;

    LogMessageEx(LOG_DEVICE_NI9472, "NI 9472 closed successfully");

    return NI9472_SUCCESS;
}

int NI9472_TestConnection(NI9472_Handle *handle) {
    if (!handle || !handle->isConnected) return NI9472_ERROR_NOT_CONNECTED;

    PrintDebug("Testing connection by toggling channel 0");

    // Toggle channel 0
    int result = NI9472_SetChannel(handle, 0, NI9472_STATE_LOW);
    if (result != NI9472_SUCCESS) return result;

    Delay(0.1);  // 100ms delay

    result = NI9472_SetChannel(handle, 0, NI9472_STATE_HIGH);
    if (result != NI9472_SUCCESS) return result;

    Delay(0.1);

    result = NI9472_SetChannel(handle, 0, NI9472_STATE_LOW);

    return result;
}

/******************************************************************************
 * Channel Control Functions
 ******************************************************************************/

int NI9472_SetChannel(NI9472_Handle *handle, int channel, int state) {
    if (!handle || !handle->isConnected) return NI9472_ERROR_NOT_CONNECTED;

    // Validate channel
    int result = ValidateChannel(channel);
    if (result != NI9472_SUCCESS) return result;

    // Validate state
    if (state != NI9472_STATE_LOW && state != NI9472_STATE_HIGH) {
        LogErrorEx(LOG_DEVICE_NI9472, "Invalid channel state: %d", state);
        return NI9472_ERROR_INVALID_PARAM;
    }

    // Update the channel in our state array
    handle->channelStates[channel] = (uInt8)state;

    // Write all channels (NI-DAQmx requires writing all lines in the port)
    int32 daqResult = DAQmxWriteDigitalLines(handle->taskHandle,
                                             1,                    // numSampsPerChan
                                             1,                    // autoStart
                                             NI9472_WRITE_TIMEOUT, // timeout
                                             DAQmx_Val_GroupByChannel,
                                             handle->channelStates,
                                             NULL, NULL);

    if (daqResult != 0) {
        char errBuff[2048];
        DAQmxGetExtendedErrorInfo(errBuff, sizeof(errBuff));
        LogErrorEx(LOG_DEVICE_NI9472, "Failed to write channel %d: %s", channel, errBuff);
        return NI9472_ERROR_WRITE_FAILED;
    }

    PrintDebug("Set channel %d to %s", channel, state ? "HIGH" : "LOW");

    return NI9472_SUCCESS;
}

int NI9472_GetChannelState(NI9472_Handle *handle, int channel, int *state) {
    if (!handle || !handle->isConnected) return NI9472_ERROR_NOT_CONNECTED;
    if (!state) return NI9472_ERROR_INVALID_PARAM;

    // Validate channel
    int result = ValidateChannel(channel);
    if (result != NI9472_SUCCESS) return result;

    // Return cached state
    *state = handle->channelStates[channel];

    return NI9472_SUCCESS;
}

int NI9472_SetMultipleChannels(NI9472_Handle *handle, const int *channels,
                               const int *states, int count) {
    if (!handle || !channels || !states || count <= 0) {
        return NI9472_ERROR_INVALID_PARAM;
    }

    if (!handle->isConnected) return NI9472_ERROR_NOT_CONNECTED;

    LogMessageEx(LOG_DEVICE_NI9472, "Setting %d channels", count);

    // Update all channels in the state array
    for (int i = 0; i < count; i++) {
        // Validate channel
        int result = ValidateChannel(channels[i]);
        if (result != NI9472_SUCCESS) {
            LogErrorEx(LOG_DEVICE_NI9472, "Invalid channel %d in array", channels[i]);
            return result;
        }

        // Validate state
        if (states[i] != NI9472_STATE_LOW && states[i] != NI9472_STATE_HIGH) {
            LogErrorEx(LOG_DEVICE_NI9472, "Invalid state %d for channel %d",
                      states[i], channels[i]);
            return NI9472_ERROR_INVALID_PARAM;
        }

        handle->channelStates[channels[i]] = (uInt8)states[i];
    }

    // Write all channels at once
    int32 daqResult = DAQmxWriteDigitalLines(handle->taskHandle,
                                             1,                    // numSampsPerChan
                                             1,                    // autoStart
                                             NI9472_WRITE_TIMEOUT, // timeout
                                             DAQmx_Val_GroupByChannel,
                                             handle->channelStates,
                                             NULL, NULL);

    if (daqResult != 0) {
        char errBuff[2048];
        DAQmxGetExtendedErrorInfo(errBuff, sizeof(errBuff));
        LogErrorEx(LOG_DEVICE_NI9472, "Failed to write multiple channels: %s", errBuff);
        return NI9472_ERROR_WRITE_FAILED;
    }

    PrintDebug("Successfully set %d channels", count);

    return NI9472_SUCCESS;
}

int NI9472_SetAllChannels(NI9472_Handle *handle, uInt8 pattern) {
    if (!handle || !handle->isConnected) return NI9472_ERROR_NOT_CONNECTED;

    // Update all channel states from the bit pattern
    for (int i = 0; i < NI9472_NUM_CHANNELS; i++) {
        handle->channelStates[i] = (pattern & (1 << i)) ? 1 : 0;
    }

    // Write all channels
    int32 daqResult = DAQmxWriteDigitalLines(handle->taskHandle,
                                             1,                    // numSampsPerChan
                                             1,                    // autoStart
                                             NI9472_WRITE_TIMEOUT, // timeout
                                             DAQmx_Val_GroupByChannel,
                                             handle->channelStates,
                                             NULL, NULL);

    if (daqResult != 0) {
        char errBuff[2048];
        DAQmxGetExtendedErrorInfo(errBuff, sizeof(errBuff));
        LogErrorEx(LOG_DEVICE_NI9472, "Failed to write all channels (pattern 0x%02X): %s",
                   pattern, errBuff);
        return NI9472_ERROR_WRITE_FAILED;
    }

    PrintDebug("Set all channels to pattern 0x%02X", pattern);

    return NI9472_SUCCESS;
}

int NI9472_GetAllChannels(NI9472_Handle *handle, uInt8 *pattern) {
    if (!handle || !handle->isConnected) return NI9472_ERROR_NOT_CONNECTED;
    if (!pattern) return NI9472_ERROR_INVALID_PARAM;

    // Build bit pattern from cached states
    *pattern = 0;
    for (int i = 0; i < NI9472_NUM_CHANNELS; i++) {
        if (handle->channelStates[i]) {
            *pattern |= (1 << i);
        }
    }

    return NI9472_SUCCESS;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* NI9472_GetErrorString(int errorCode) {
    if (errorCode == NI9472_SUCCESS) {
        return errorStrings[0];
    }

    int index = 0;
    switch (errorCode) {
        case NI9472_ERROR_DAQMX:           index = 1; break;
        case NI9472_ERROR_INVALID_CHANNEL: index = 2; break;
        case NI9472_ERROR_NOT_CONNECTED:   index = 3; break;
        case NI9472_ERROR_TASK_FAILED:     index = 4; break;
        case NI9472_ERROR_INVALID_PARAM:   index = 5; break;
        case NI9472_ERROR_WRITE_FAILED:    index = 6; break;
        default:
            return "Unknown NI 9472 error";
    }

    if (index < sizeof(errorStrings) / sizeof(errorStrings[0])) {
        return errorStrings[index];
    }
    return "Unknown error";
}

int NI9472_GetDAQmxErrorString(int32 daqmxError, char *buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return NI9472_ERROR_INVALID_PARAM;

    if (daqmxError == 0) {
        snprintf(buffer, bufferSize, "No error");
        return NI9472_SUCCESS;
    }

    DAQmxGetExtendedErrorInfo(buffer, bufferSize);
    return NI9472_SUCCESS;
}

const char* NI9472_GetVersion(void) {
    return libraryVersion;
}

int NI9472_IsConnected(const NI9472_Handle *handle) {
    return (handle && handle->isConnected) ? 1 : 0;
}

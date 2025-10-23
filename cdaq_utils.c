/******************************************************************************
 * cdaq_utils.c
 *
 * cDAQ Utilities Module Implementation
 * Handles NI cDAQ slots:
 *   - Slot 1: NI 9202 for 4-20mA current sensors (voltage measurement)
 *   - Slot 2: NI 9213 for thermocouples
 *   - Slot 3: NI 9213 for thermocouples
 ******************************************************************************/

#include "cdaq_utils.h"
#include "logging.h"

/******************************************************************************
 * Module State
 ******************************************************************************/
static struct {
    TaskHandle slot1TaskHandle;  // NI 9202 voltage/current inputs
    TaskHandle slot2TaskHandle;  // NI 9213 thermocouples
    TaskHandle slot3TaskHandle;  // NI 9213 thermocouples
    int initialized;             // Slots 2 & 3 (thermocouples)
    int currentSlotInitialized;  // Slot 1 (4-20mA)
} g_cdaq = {0};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/
static int CDAQ_CreateSlotTask(int slot, TaskHandle *taskHandle);
static int CDAQ_CreateCurrentSlotTask(TaskHandle *taskHandle);

/******************************************************************************
 * Public Function Implementation
 ******************************************************************************/

int CDAQ_Initialize(void) {
    if (g_cdaq.initialized) {
        LogWarning("cDAQ module already initialized");
        return SUCCESS;
    }
    
    LogMessage("Initializing cDAQ thermocouple slots 2 and 3...");
    
    // Initialize slot 2
    int result = CDAQ_CreateSlotTask(2, &g_cdaq.slot2TaskHandle);
    if (result != SUCCESS) {
        LogError("Failed to initialize cDAQ slot 2");
        return result;
    }
    LogMessage("cDAQ slot 2 initialized with %d thermocouples", CDAQ_CHANNELS_PER_SLOT);
    
    // Initialize slot 3
    result = CDAQ_CreateSlotTask(3, &g_cdaq.slot3TaskHandle);
    if (result != SUCCESS) {
        LogError("Failed to initialize cDAQ slot 3");
        CDAQ_Cleanup();
        return result;
    }
    LogMessage("cDAQ slot 3 initialized with %d thermocouples", CDAQ_CHANNELS_PER_SLOT);
    
    g_cdaq.initialized = 1;
    LogMessage("cDAQ module initialized successfully");
    return SUCCESS;
}

void CDAQ_Cleanup(void) {
    LogMessage("Cleaning up cDAQ module...");

    if (g_cdaq.slot2TaskHandle != 0) {
        DAQmxStopTask(g_cdaq.slot2TaskHandle);
        DAQmxClearTask(g_cdaq.slot2TaskHandle);
        g_cdaq.slot2TaskHandle = 0;
        LogMessage("Cleaned up cDAQ slot 2 task");
    }

    if (g_cdaq.slot3TaskHandle != 0) {
        DAQmxStopTask(g_cdaq.slot3TaskHandle);
        DAQmxClearTask(g_cdaq.slot3TaskHandle);
        g_cdaq.slot3TaskHandle = 0;
        LogMessage("Cleaned up cDAQ slot 3 task");
    }

    g_cdaq.initialized = 0;
    LogMessage("cDAQ module cleaned up");
}

int CDAQ_InitializeCurrentSlot(void) {
    if (g_cdaq.currentSlotInitialized) {
        LogWarning("cDAQ current slot (slot 1) already initialized");
        return SUCCESS;
    }

    LogMessage("Initializing cDAQ slot 1 for 4-20mA current sensors (NI 9202)...");

    int result = CDAQ_CreateCurrentSlotTask(&g_cdaq.slot1TaskHandle);
    if (result != SUCCESS) {
        LogError("Failed to initialize cDAQ current slot (slot 1)");
        return result;
    }

    LogMessage("cDAQ slot 1 initialized with %d voltage channels (4-20mA mode)", CDAQ_CHANNELS_PER_SLOT);
    LogMessage("Shunt resistor: %.1f ohms, Range: %.1f-%.1fV (4-20mA)",
               CDAQ_CURRENT_SHUNT_RESISTOR, CDAQ_CURRENT_MIN_V, CDAQ_CURRENT_MAX_V);

    g_cdaq.currentSlotInitialized = 1;
    return SUCCESS;
}

void CDAQ_CleanupCurrentSlot(void) {
    LogMessage("Cleaning up cDAQ current slot...");

    if (g_cdaq.slot1TaskHandle != 0) {
        // Stop task if running, then clear it
        DAQmxStopTask(g_cdaq.slot1TaskHandle);
        DAQmxClearTask(g_cdaq.slot1TaskHandle);
        g_cdaq.slot1TaskHandle = 0;
        LogMessage("Cleaned up cDAQ slot 1 task");
    }

    g_cdaq.currentSlotInitialized = 0;
    LogMessage("cDAQ current slot cleaned up");
}

int CDAQ_ReadTC(int slot, int tc_number, double *temperature) {
    if (!g_cdaq.initialized) {
        LogError("cDAQ module not initialized");
        return ERR_NOT_INITIALIZED;
    }
    
    if (!temperature) {
        return ERR_NULL_POINTER;
    }
    
    if (slot != 2 && slot != 3) {
        LogError("Invalid slot %d (only slots 2 and 3 supported)", slot);
        return ERR_INVALID_PARAMETER;
    }
    
    if (tc_number < 0 || tc_number >= CDAQ_CHANNELS_PER_SLOT) {
        LogError("Thermocouple number %d out of range (0-%d)", 
                tc_number, CDAQ_CHANNELS_PER_SLOT - 1);
        return ERR_INVALID_PARAMETER;
    }
    
    // Select the appropriate task handle
    TaskHandle taskHandle = (slot == 2) ? g_cdaq.slot2TaskHandle : g_cdaq.slot3TaskHandle;
    
    // Read all channels and return the requested one
    float64 data[CDAQ_CHANNELS_PER_SLOT];
    int32 result = DAQmxReadAnalogF64(taskHandle, 1, CDAQ_READ_TIMEOUT,
                                     DAQmx_Val_GroupByChannel, data, CDAQ_CHANNELS_PER_SLOT, 
                                     NULL, NULL);
    if (result != 0) {
        LogError("Failed to read thermocouple data from slot %d: %d", slot, result);
        return ERR_OPERATION_FAILED;
    }
    
    *temperature = data[tc_number];
    return SUCCESS;
}

int CDAQ_ReadTCArray(int slot, double *temperatures, int *num_read) {
    if (!g_cdaq.initialized) {
        LogError("cDAQ module not initialized");
        return ERR_NOT_INITIALIZED;
    }
    
    if (!temperatures || !num_read) {
        return ERR_NULL_POINTER;
    }
    
    if (slot != 2 && slot != 3) {
        LogError("Invalid slot %d (only slots 2 and 3 supported)", slot);
        return ERR_INVALID_PARAMETER;
    }
    
    // Select the appropriate task handle
    TaskHandle taskHandle = (slot == 2) ? g_cdaq.slot2TaskHandle : g_cdaq.slot3TaskHandle;
    
    // Read all channels
    float64 data[CDAQ_CHANNELS_PER_SLOT];
    int32 result = DAQmxReadAnalogF64(taskHandle, 1, CDAQ_READ_TIMEOUT,
                                     DAQmx_Val_GroupByChannel, data, CDAQ_CHANNELS_PER_SLOT, 
                                     NULL, NULL);
    if (result != 0) {
        LogError("Failed to read thermocouple array from slot %d: %d", slot, result);
        return ERR_OPERATION_FAILED;
    }
    
    // Copy data to output array
    for (int i = 0; i < CDAQ_CHANNELS_PER_SLOT; i++) {
        temperatures[i] = data[i];
    }
    
    *num_read = CDAQ_CHANNELS_PER_SLOT;
    return SUCCESS;
}

int CDAQ_ReadCurrent(int channel, double *current_mA) {
    if (!g_cdaq.currentSlotInitialized) {
        LogError("cDAQ current slot not initialized");
        return ERR_NOT_INITIALIZED;
    }

    if (!current_mA) {
        return ERR_NULL_POINTER;
    }

    if (channel < 0 || channel >= CDAQ_CHANNELS_PER_SLOT) {
        LogError("Channel number %d out of range (0-%d)",
                channel, CDAQ_CHANNELS_PER_SLOT - 1);
        return ERR_INVALID_PARAMETER;
    }

    // Read voltage from the channel
    double voltage;
    int result = CDAQ_ReadVoltage(channel, &voltage);
    if (result != SUCCESS) {
        return result;
    }

    // Convert voltage to current: I = V / R, then to milliamps
    double current_A = voltage / CDAQ_CURRENT_SHUNT_RESISTOR;
    *current_mA = current_A * 1000.0;

    return SUCCESS;
}

int CDAQ_ReadCurrentArray(double *currents_mA, int *num_read) {
    if (!g_cdaq.currentSlotInitialized) {
        LogError("cDAQ current slot not initialized");
        return ERR_NOT_INITIALIZED;
    }

    if (!currents_mA || !num_read) {
        return ERR_NULL_POINTER;
    }

    // Finite sampling mode requires stopping and starting the task for each read
    int32 result = DAQmxStopTask(g_cdaq.slot1TaskHandle);
    if (result != 0) {
        LogError("Failed to stop task before read (slot 1): %d", result);
        return ERR_OPERATION_FAILED;
    }

    result = DAQmxStartTask(g_cdaq.slot1TaskHandle);
    if (result != 0) {
        LogError("Failed to restart task for read (slot 1): %d", result);
        return ERR_OPERATION_FAILED;
    }

    // Read 100 samples per channel for averaging (finite sampling mode)
    // Data organized as: [ch0_s0, ch1_s0, ..., ch15_s0, ch0_s1, ch1_s1, ..., ch15_s1, ...]
    #define SAMPLES_TO_READ 100
    float64 data[CDAQ_CHANNELS_PER_SLOT * SAMPLES_TO_READ];
    int32 samplesRead = 0;
    result = DAQmxReadAnalogF64(g_cdaq.slot1TaskHandle, SAMPLES_TO_READ, CDAQ_READ_TIMEOUT,
                                DAQmx_Val_GroupByScanNumber, data,
                                CDAQ_CHANNELS_PER_SLOT * SAMPLES_TO_READ,
                                &samplesRead, NULL);
    if (result != 0) {
        LogError("Failed to read voltage array from slot 1: %d", result);
        return ERR_OPERATION_FAILED;
    }

    // Average all samples for each channel
    // Data is [ch0_s0, ch1_s0, ..., ch15_s0, ch0_s1, ch1_s1, ..., ch15_s1, ...]
    for (int ch = 0; ch < CDAQ_CHANNELS_PER_SLOT; ch++) {
        double sum = 0.0;
        for (int s = 0; s < samplesRead; s++) {
            sum += data[s * CDAQ_CHANNELS_PER_SLOT + ch];
        }
        double avgVoltage = sum / samplesRead;
        double current_A = avgVoltage / CDAQ_CURRENT_SHUNT_RESISTOR;
        currents_mA[ch] = current_A * 1000.0;
    }

    *num_read = CDAQ_CHANNELS_PER_SLOT;
    return SUCCESS;
    #undef SAMPLES_TO_READ
}

int CDAQ_ReadVoltage(int channel, double *voltage) {
    if (!g_cdaq.currentSlotInitialized) {
        LogError("cDAQ current slot not initialized");
        return ERR_NOT_INITIALIZED;
    }

    if (!voltage) {
        return ERR_NULL_POINTER;
    }

    if (channel < 0 || channel >= CDAQ_CHANNELS_PER_SLOT) {
        LogError("Channel number %d out of range (0-%d)",
                channel, CDAQ_CHANNELS_PER_SLOT - 1);
        return ERR_INVALID_PARAMETER;
    }

    // Finite sampling mode: start task, read samples, task auto-stops
    // Read 10 samples for averaging to reduce noise
    // Data organized as: [ch0_s0, ch1_s0, ..., ch15_s0, ch0_s1, ch1_s1, ..., ch15_s1, ...]
    #define SAMPLES_TO_AVERAGE 10  // Average 10 samples for noise reduction
    float64 data[CDAQ_CHANNELS_PER_SLOT * SAMPLES_TO_AVERAGE];
    int32 samplesRead = 0;

    // Ensure task is stopped before starting (prevents error -200479)
    // Safe to call even if task is already stopped
    DAQmxStopTask(g_cdaq.slot1TaskHandle);

    // Start task (will acquire finite samples and auto-stop)
    int32 result = DAQmxStartTask(g_cdaq.slot1TaskHandle);
    if (result != 0) {
        LogError("Failed to start task for slot 1 read: %d", result);
        return ERR_OPERATION_FAILED;
    }

    // Read the samples (acquisition happens now)
    result = DAQmxReadAnalogF64(g_cdaq.slot1TaskHandle,
                                SAMPLES_TO_AVERAGE,           // Number of samples to acquire
                                CDAQ_READ_TIMEOUT,
                                DAQmx_Val_GroupByScanNumber,
                                data,
                                CDAQ_CHANNELS_PER_SLOT * SAMPLES_TO_AVERAGE,
                                &samplesRead,
                                NULL);
    if (result != 0) {
        LogError("Failed to read voltage data from slot 1: %d", result);
        DAQmxStopTask(g_cdaq.slot1TaskHandle);  // Clean up on error
        return ERR_OPERATION_FAILED;
    }

    // Task automatically stops after finite samples acquired
    // Explicitly stop to ensure clean state for next read
    DAQmxStopTask(g_cdaq.slot1TaskHandle);

    // Check that we got the expected number of samples
    if (samplesRead != SAMPLES_TO_AVERAGE) {
        LogWarning("Expected %d samples but got %d from slot 1", SAMPLES_TO_AVERAGE, samplesRead);
        if (samplesRead == 0) {
            return ERR_OPERATION_FAILED;
        }
    }

    // Average all samples for the requested channel
    // Data is [ch0_s0, ch1_s0, ..., ch15_s0, ch0_s1, ch1_s1, ..., ch15_s1, ...]
    double sum = 0.0;
    for (int s = 0; s < samplesRead; s++) {
        sum += data[s * CDAQ_CHANNELS_PER_SLOT + channel];
    }
    *voltage = sum / samplesRead;

    return SUCCESS;
    #undef SAMPLES_TO_AVERAGE
}

/******************************************************************************
 * Internal Function Implementation
 ******************************************************************************/

static int CDAQ_CreateSlotTask(int slot, TaskHandle *taskHandle) {
    // Create task name
    char taskName[64];
    snprintf(taskName, sizeof(taskName), "TC_Slot_%d", slot);
    
    // Create DAQmx task
    int32 result = DAQmxCreateTask(taskName, taskHandle);
    if (result != 0) {
        LogError("Failed to create cDAQ task for slot %d: %d", slot, result);
        return ERR_OPERATION_FAILED;
    }
    
    // Add thermocouple channels (0-15)
    for (int i = 0; i < CDAQ_CHANNELS_PER_SLOT; i++) {
        char channelName[64];
        snprintf(channelName, sizeof(channelName), "cDAQ1Mod%d/ai%d", slot, i);
        
        result = DAQmxCreateAIThrmcplChan(*taskHandle, channelName, "", 
                                         CDAQ_TC_MIN_TEMP, CDAQ_TC_MAX_TEMP, 
                                         DAQmx_Val_DegC, DAQmx_Val_K_Type_TC, 
                                         DAQmx_Val_BuiltIn, CDAQ_CJC_TEMP, NULL);
        if (result != 0) {
            LogError("Failed to create thermocouple channel %s: %d", channelName, result);
            DAQmxClearTask(*taskHandle);
            *taskHandle = 0;
            return ERR_OPERATION_FAILED;
        }
    }
    
    // Start the task
    result = DAQmxStartTask(*taskHandle);
    if (result != 0) {
        LogError("Failed to start cDAQ task for slot %d: %d", slot, result);
        DAQmxClearTask(*taskHandle);
        *taskHandle = 0;
        return ERR_OPERATION_FAILED;
    }

    return SUCCESS;
}

static int CDAQ_CreateCurrentSlotTask(TaskHandle *taskHandle) {
    // Create task name
    char taskName[64];
    snprintf(taskName, sizeof(taskName), "Current_Slot_1");

    // Create DAQmx task
    int32 result = DAQmxCreateTask(taskName, taskHandle);
    if (result != 0) {
        LogError("Failed to create cDAQ task for slot 1 (current): %d", result);
        return ERR_OPERATION_FAILED;
    }

    // Add voltage input channels (0-15) for NI 9202
    // These will measure voltage from 4-20mA sensors with shunt resistors
    for (int i = 0; i < CDAQ_CHANNELS_PER_SLOT; i++) {
        char channelName[64];
        snprintf(channelName, sizeof(channelName), "cDAQ1Mod1/ai%d", i);

        // Create voltage channel with ±10V range (NI 9202 range)
        // Actual signal will be 1-5V for 4-20mA with 250Ω shunt
        // NI 9202 only supports Differential mode (measures AI+ minus AI-)
        result = DAQmxCreateAIVoltageChan(*taskHandle,
                                         channelName,
                                         "",
                                         DAQmx_Val_Diff,          // Differential mode (AI+ to AI-)
                                         CDAQ_VOLTAGE_RANGE_MIN,  // -10V
                                         CDAQ_VOLTAGE_RANGE_MAX,  // +10V
                                         DAQmx_Val_Volts,
                                         NULL);
        if (result != 0) {
            LogError("Failed to create voltage channel %s: %d", channelName, result);
            DAQmxClearTask(*taskHandle);
            *taskHandle = 0;
            return ERR_OPERATION_FAILED;
        }
    }

    // Configure finite sampling mode for averaging reads
    // This acquires fresh samples on each read call
    // Using a moderate sample rate suitable for slow measurements
    result = DAQmxCfgSampClkTiming(*taskHandle,
                                  "",                          // Use onboard clock
                                  1000.0,                      // Sample rate (Hz) - moderate rate
                                  DAQmx_Val_Rising,            // Active edge
                                  DAQmx_Val_FiniteSamps,       // Finite samples mode
                                  10);                         // Samples per channel per read
    if (result != 0) {
        LogError("Failed to configure timing for slot 1 (current): %d", result);
        DAQmxClearTask(*taskHandle);
        *taskHandle = 0;
        return ERR_OPERATION_FAILED;
    }

    return SUCCESS;
}
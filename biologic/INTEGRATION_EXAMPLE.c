/******************************************************************************
 * INTEGRATION_EXAMPLE.c
 *
 * Example code showing how to initialize and use the BioLogic abstraction
 * layer with both Direct DLL and EC-Lab OLE COM modes.
 *
 * This file is for reference only - not compiled into the project.
 ******************************************************************************/

#include "biologic_abstract.h"
#include "logging.h"
#include "common.h"

/******************************************************************************
 * Example 1: Initialization in main()
 *
 * This shows how to modify BatteryExploder.c main() to use the abstraction
 * layer instead of directly initializing biologic_queue.
 ******************************************************************************/

int main_example(int argc, char *argv[]) {
    // ... other initialization code ...

    // OLD CODE (Direct DLL only):
    // g_bioQueueMgr = BIO_QueueInit(BIOLOGIC_DEFAULT_ADDRESS);
    // if (!g_bioQueueMgr) {
    //     LogError("Failed to initialize BioLogic queue manager");
    //     return -1;
    // }

    // NEW CODE (Supports both modes):
    BIO_Config bioConfig = {0};

#if BIOLOGIC_CONTROL_MODE == 0
    // Direct DLL mode configuration
    bioConfig.mode = BIO_MODE_DIRECT_DLL;
    strncpy(bioConfig.dll.deviceAddress, BIOLOGIC_DEFAULT_ADDRESS, sizeof(bioConfig.dll.deviceAddress) - 1);
    bioConfig.dll.timeout = BIOLOGIC_CONNECTION_TIMEOUT;

    LogMessage("Initializing BioLogic in Direct DLL mode");

#elif BIOLOGIC_CONTROL_MODE == 1
    // EC-Lab OLE COM mode configuration
    bioConfig.mode = BIO_MODE_ECLAB_OLECOM;
    strncpy(bioConfig.eclab.settingsDir, ECLAB_SETTINGS_DIR, MAX_PATH - 1);
    strncpy(bioConfig.eclab.dataDir, ECLAB_DATA_DIR, MAX_PATH - 1);
    bioConfig.eclab.deviceNumber = ECLAB_DEVICE_NUMBER;
    bioConfig.eclab.channelNumber = ECLAB_CHANNEL_NUMBER;
    strncpy(bioConfig.eclab.ocvTemplate, ECLAB_OCV_TEMPLATE, MAX_PATH - 1);
    strncpy(bioConfig.eclab.peisTemplate, ECLAB_PEIS_TEMPLATE, MAX_PATH - 1);
    strncpy(bioConfig.eclab.geisTemplate, ECLAB_GEIS_TEMPLATE, MAX_PATH - 1);

    LogMessage("Initializing BioLogic in EC-Lab OLE COM mode");

#else
    #error "Invalid BIOLOGIC_CONTROL_MODE - must be 0 or 1"
#endif

    // Initialize abstraction layer (works for both modes)
    int result = BIO_InitializeAbstract(&bioConfig);
    if (result != SUCCESS) {
        LogError("Failed to initialize BioLogic: %s", GetErrorString(result));
        return -1;
    }

    LogMessage("BioLogic initialized successfully in %s mode",
              BIO_GetModeName(bioConfig.mode));

    // ... rest of main() ...

    return 0;
}

/******************************************************************************
 * Example 2: Running OCV Measurement
 *
 * This shows how to use BIO_Abstract_RunOCV() which works with both modes.
 ******************************************************************************/

void example_run_ocv(void) {
    LogMessage("Starting OCV measurement example");

    BIO_TechniqueData *ocvData = NULL;
    volatile int cancelled = 0;

    // Run OCV measurement
    // Parameters are used in Direct DLL mode, ignored in EC-Lab mode
    int result = BIO_Abstract_RunOCV(
        0,          // channel
        300.0,      // duration_s (5 minutes)
        1.0,        // sample_interval_s
        5.0,        // record_every_dE (5 mV)
        30.0,       // record_every_dT (30 seconds)
        3,          // e_range (Auto)
        &ocvData,   // result
        600000,     // timeout_ms (10 minutes)
        NULL,       // progressCallback
        NULL,       // userData
        &cancelled  // cancellation flag
    );

    if (result == SUCCESS) {
        LogMessage("OCV measurement completed successfully");
        LogMessage("  Data points: %d", ocvData->rawData->numPoints);

        // Use the data...

        // Free when done
        BIO_FreeTechniqueData(ocvData);
    } else {
        LogError("OCV measurement failed: %s", GetErrorString(result));
    }
}

/******************************************************************************
 * Example 3: Running PEIS Measurement with Progress Callback
 *
 * This shows how to use BIO_Abstract_RunPEIS() with a progress callback.
 ******************************************************************************/

// Progress callback
void peis_progress_callback(double elapsedTime, int pointsCollected, void *userData) {
    LogDebug("PEIS progress: %.1f s, %d points collected", elapsedTime, pointsCollected);

    // Update UI if needed
    // SetCtrlVal(panelHandle, PANEL_PROGRESS, elapsedTime);
}

void example_run_peis(void) {
    LogMessage("Starting PEIS measurement example");

    BIO_TechniqueData *peisData = NULL;
    volatile int cancelled = 0;

    // Run PEIS measurement with progress callback
    int result = BIO_Abstract_RunPEIS(
        0,              // channel
        true,           // vs_initial
        0.0,            // initial_voltage_step (V)
        300.0,          // duration_step (s)
        0.1,            // record_every_dT (s)
        0.0,            // record_every_dI (A)
        100000.0,       // initial_freq (Hz) - 100 kHz
        0.1,            // final_freq (Hz)
        false,          // sweep_linear (logarithmic)
        0.01,           // amplitude_voltage (10 mV)
        50,             // frequency_number
        3,              // average_n_times
        false,          // correction
        2.0,            // wait_for_steady (periods)
        &peisData,      // result
        1800000,        // timeout_ms (30 minutes)
        peis_progress_callback,  // Progress callback
        NULL,           // userData
        &cancelled      // cancellation flag
    );

    if (result == SUCCESS) {
        LogMessage("PEIS measurement completed successfully");
        LogMessage("  Data points: %d", peisData->rawData->numPoints);

        // Process impedance data...

        BIO_FreeTechniqueData(peisData);
    } else {
        LogError("PEIS measurement failed: %s", GetErrorString(result));
    }
}

/******************************************************************************
 * Example 4: Running GEIS Measurement
 *
 * This shows how to use BIO_Abstract_RunGEIS() for galvanostatic EIS.
 ******************************************************************************/

void example_run_geis(void) {
    LogMessage("Starting GEIS measurement example");

    BIO_TechniqueData *geisData = NULL;
    volatile int cancelled = 0;

    // Run GEIS measurement
    int result = BIO_Abstract_RunGEIS(
        0,              // channel
        true,           // vs_initial
        0.0,            // initial_current_step (A)
        300.0,          // duration_step (s)
        0.1,            // record_every_dT (s)
        0.005,          // record_every_dE (5 mV)
        100000.0,       // initial_freq (Hz)
        0.1,            // final_freq (Hz)
        false,          // sweep_linear (logarithmic)
        0.001,          // amplitude_current (1 mA)
        50,             // frequency_number
        3,              // average_n_times
        false,          // correction
        2.0,            // wait_for_steady (periods)
        5,              // i_range (appropriate for 1 mA)
        &geisData,      // result
        1800000,        // timeout_ms (30 minutes)
        NULL,           // progressCallback
        NULL,           // userData
        &cancelled      // cancellation flag
    );

    if (result == SUCCESS) {
        LogMessage("GEIS measurement completed successfully");

        // Process impedance data...

        BIO_FreeTechniqueData(geisData);
    } else {
        LogError("GEIS measurement failed: %s", GetErrorString(result));
    }
}

/******************************************************************************
 * Example 5: Integration with Temperature Ramp Experiment
 *
 * This shows how the existing exp_temp_ramp.c can use the abstraction layer
 * without any modifications to the experiment logic.
 ******************************************************************************/

// From exp_temp_ramp.c - PerformEISMeasurement()
// No changes needed! This code works with both modes transparently.

int PerformEISMeasurement_example(void) {
    LogMessage("Performing EIS measurement during temperature ramp");

    BIO_TechniqueData *eisData = NULL;
    volatile int cancelled = 0;

    // Use abstraction layer - mode is transparent
    int result = BIO_Abstract_RunPEIS(
        0,              // channel
        true,           // vs_initial
        0.0,            // initial_voltage_step
        300.0,          // duration_step
        0.1,            // record_every_dT
        0.0,            // record_every_dI
        100000.0,       // initial_freq
        0.1,            // final_freq
        false,          // sweep_linear
        0.01,           // amplitude_voltage
        50,             // frequency_number
        3,              // average_n_times
        false,          // correction
        2.0,            // wait_for_steady
        &eisData,       // result
        1800000,        // timeout_ms
        NULL,           // progressCallback
        NULL,           // userData
        &cancelled      // cancellation flag
    );

    if (result == SUCCESS) {
        // Save EIS data to file...
        // Log temperature and impedance...

        BIO_FreeTechniqueData(eisData);
        return SUCCESS;
    }

    return result;
}

/******************************************************************************
 * Example 6: Utility Functions
 *
 * This shows how to use utility functions to check status and mode.
 ******************************************************************************/

void example_utilities(void) {
    // Check if initialized
    if (BIO_IsAbstractInitialized()) {
        LogMessage("BioLogic abstraction layer is initialized");
    }

    // Get current mode
    BIO_ControlMode mode = BIO_GetCurrentMode();
    LogMessage("Current mode: %s", BIO_GetModeName(mode));

    // Test connection
    int result = BIO_Abstract_TestConnection();
    if (result == SUCCESS) {
        LogMessage("Connection test successful");
    } else {
        LogError("Connection test failed: %s", GetErrorString(result));
    }

    // Get device ID
    int deviceID = BIO_Abstract_GetDeviceID();
    LogMessage("Device ID: %d", deviceID);

    // Get configuration
    BIO_Config config;
    if (BIO_GetAbstractConfig(&config) == SUCCESS) {
        LogMessage("Configuration retrieved:");
        LogMessage("  Mode: %s", BIO_GetModeName(config.mode));

        if (config.mode == BIO_MODE_DIRECT_DLL) {
            LogMessage("  Address: %s", config.dll.deviceAddress);
        } else {
            LogMessage("  Settings dir: %s", config.eclab.settingsDir);
            LogMessage("  Data dir: %s", config.eclab.dataDir);
        }
    }
}

/******************************************************************************
 * Example 7: Cleanup in PanelCallback
 *
 * This shows how to modify cleanup code to use the abstraction layer.
 ******************************************************************************/

int PanelCallback_example(int panel, int event, void *callbackData,
                         int eventData1, int eventData2) {
    if (event == EVENT_CLOSE) {
        LogMessage("Shutting down application");

        // OLD CODE (Direct DLL only):
        // if (g_bioQueueMgr) {
        //     BIO_QueueShutdown(g_bioQueueMgr);
        //     g_bioQueueMgr = NULL;
        // }

        // NEW CODE (Works for both modes):
        BIO_ShutdownAbstract();

        // ... other cleanup ...

        QuitUserInterface(0);
    }

    return 0;
}

/******************************************************************************
 * Example 8: Mode Comparison Test
 *
 * This shows how to run the same measurement in both modes for validation.
 ******************************************************************************/

void example_mode_comparison(void) {
    LogMessage("Running mode comparison test");

    BIO_TechniqueData *dllData = NULL;
    BIO_TechniqueData *eclabData = NULL;

    // Test parameters
    const double initial_freq = 100000.0;
    const double final_freq = 0.1;
    const int num_points = 50;

    // Run in Direct DLL mode
    LogMessage("Testing Direct DLL mode...");
    BIO_Config dllConfig = {0};
    dllConfig.mode = BIO_MODE_DIRECT_DLL;
    strncpy(dllConfig.dll.deviceAddress, BIOLOGIC_DEFAULT_ADDRESS, 63);
    dllConfig.dll.timeout = 5;

    BIO_InitializeAbstract(&dllConfig);
    int dll_result = BIO_Abstract_RunPEIS(
        0, true, 0.0, 300.0, 0.1, 0.0,
        initial_freq, final_freq, false, 0.01,
        num_points, 3, false, 2.0,
        &dllData, 1800000,
        NULL, NULL, NULL
    );
    BIO_ShutdownAbstract();

    // Run in EC-Lab OLE COM mode
    LogMessage("Testing EC-Lab OLE COM mode...");
    BIO_Config eclabConfig = {0};
    eclabConfig.mode = BIO_MODE_ECLAB_OLECOM;
    strncpy(eclabConfig.eclab.settingsDir, ECLAB_SETTINGS_DIR, MAX_PATH - 1);
    strncpy(eclabConfig.eclab.dataDir, ECLAB_DATA_DIR, MAX_PATH - 1);
    eclabConfig.eclab.deviceNumber = 0;
    eclabConfig.eclab.channelNumber = 0;
    strncpy(eclabConfig.eclab.peisTemplate, ECLAB_PEIS_TEMPLATE, MAX_PATH - 1);

    BIO_InitializeAbstract(&eclabConfig);
    int eclab_result = BIO_Abstract_RunPEIS(
        0, true, 0.0, 300.0, 0.1, 0.0,
        initial_freq, final_freq, false, 0.01,
        num_points, 3, false, 2.0,
        &eclabData, 1800000,
        NULL, NULL, NULL
    );
    BIO_ShutdownAbstract();

    // Compare results
    if (dll_result == SUCCESS && eclab_result == SUCCESS) {
        LogMessage("Both modes completed successfully");
        LogMessage("DLL mode points: %d", dllData->rawData->numPoints);
        LogMessage("EC-Lab mode points: %d", eclabData->rawData->numPoints);

        // Compare impedance data...
        // (Implementation depends on data structure)

        BIO_FreeTechniqueData(dllData);
        BIO_FreeTechniqueData(eclabData);
    } else {
        LogError("Mode comparison failed");
        if (dll_result != SUCCESS) {
            LogError("DLL mode error: %s", GetErrorString(dll_result));
        }
        if (eclab_result != SUCCESS) {
            LogError("EC-Lab mode error: %s", GetErrorString(eclab_result));
        }
    }
}

/******************************************************************************
 * Summary
 *
 * Key Points:
 *
 * 1. Use BIO_InitializeAbstract() instead of BIO_QueueInit()
 * 2. Use BIO_Abstract_RunXXX() functions instead of BIO_RunXXXQueued()
 * 3. Use BIO_ShutdownAbstract() instead of BIO_QueueShutdown()
 * 4. Existing experiment code requires NO changes
 * 5. Mode selection is via BIOLOGIC_CONTROL_MODE in common.h
 * 6. The abstraction layer handles all dispatching automatically
 *
 * Benefits:
 *
 * - Single codebase supports both modes
 * - Easy switching via configuration
 * - Experiments remain unchanged
 * - Can validate one mode against the other
 * - Development in EC-Lab mode, production in Direct DLL mode
 *
 ******************************************************************************/

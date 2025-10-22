/******************************************************************************
 * cdaq_current_test.c
 *
 * Simple command-line test for NI 9202 4-20mA current sensor measurements
 *
 * Usage:
 *   cdaq_current_test [options]
 *
 * Options:
 *   -c <channel>    Test single channel (0-15)
 *   -a              Test all channels (array read)
 *   -m              Continuous monitoring mode (press Ctrl+C to exit)
 *   -v              Verbose mode (show voltages and currents)
 *   -h              Show help
 *
 * Examples:
 *   cdaq_current_test -c 0              Read channel 0 once
 *   cdaq_current_test -a                Read all channels once
 *   cdaq_current_test -c 5 -m           Monitor channel 5 continuously
 *   cdaq_current_test -a -m -v          Monitor all channels with voltages
 *
 * Authors: Maxwell Prisbrey, Nicolas Rasmont, Gabriel Meier
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "../cdaq_utils.h"
#include "../common.h"
#include "../logging.h"

/******************************************************************************
 * Configuration
 ******************************************************************************/
#define MONITOR_INTERVAL_MS 1000  // Update every second in monitoring mode

/******************************************************************************
 * Global Variables
 ******************************************************************************/
static int g_running = 1;  // Flag for continuous monitoring

/******************************************************************************
 * Signal Handler
 ******************************************************************************/
void signal_handler(int sig) {
    printf("\n\nReceived signal %d, exiting...\n", sig);
    g_running = 0;
}

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

void print_usage(const char *program_name) {
    printf("\n");
    printf("NI 9202 4-20mA Current Sensor Test\n");
    printf("===================================\n\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -c <channel>    Test single channel (0-15)\n");
    printf("  -a              Test all channels (array read)\n");
    printf("  -m              Continuous monitoring mode (press Ctrl+C to exit)\n");
    printf("  -v              Verbose mode (show voltages and currents)\n");
    printf("  -h              Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s -c 0              Read channel 0 once\n", program_name);
    printf("  %s -a                Read all channels once\n", program_name);
    printf("  %s -c 5 -m           Monitor channel 5 continuously\n", program_name);
    printf("  %s -a -m -v          Monitor all channels with voltages\n\n", program_name);
    printf("Hardware Setup:\n");
    printf("  - NI 9202 in slot 1 of cDAQ-9178\n");
    printf("  - 4-20mA sensors with 250 ohm shunt resistors\n");
    printf("  - Expected voltage range: 1V (4mA) to 5V (20mA)\n\n");
}

void print_header_single(int channel, int verbose) {
    printf("\n");
    printf("================================================================================\n");
    printf("  Channel %d - 4-20mA Current Sensor Reading\n", channel);
    printf("================================================================================\n");
    if (verbose) {
        printf("  Time        Voltage (V)    Current (mA)    Status\n");
        printf("--------------------------------------------------------------------------------\n");
    } else {
        printf("  Time        Current (mA)    Status\n");
        printf("--------------------------------------------------------------------------------\n");
    }
}

void print_header_all(int verbose) {
    printf("\n");
    printf("================================================================================\n");
    printf("  All Channels - 4-20mA Current Sensor Readings\n");
    printf("================================================================================\n");
}

int test_single_channel(int channel, int verbose, int monitor_mode) {
    int result;
    double voltage, current_mA;
    int iteration = 0;

    print_header_single(channel, verbose);

    do {
        // Read voltage (if verbose mode)
        if (verbose) {
            result = CDAQ_ReadVoltage(channel, &voltage);
            if (result != SUCCESS) {
                printf("  ERROR: Failed to read voltage from channel %d (error %d)\n", channel, result);
                return result;
            }
        }

        // Read current
        result = CDAQ_ReadCurrent(channel, &current_mA);
        if (result != SUCCESS) {
            printf("  ERROR: Failed to read current from channel %d (error %d)\n", channel, result);
            return result;
        }

        // Display results
        char time_str[32];
        time_t now = time(NULL);
        strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&now));

        if (verbose) {
            printf("  %s    %8.4f       %8.3f        ", time_str, voltage, current_mA);
        } else {
            printf("  %s    %8.3f        ", time_str, current_mA);
        }

        // Status indication
        if (current_mA < 3.5) {
            printf("FAULT (< 4mA)\n");
        } else if (current_mA > 20.5) {
            printf("OVER-RANGE (> 20mA)\n");
        } else if (current_mA < 4.5) {
            printf("LOW\n");
        } else if (current_mA > 19.5) {
            printf("HIGH\n");
        } else {
            printf("OK\n");
        }

        if (monitor_mode) {
            // Sleep for interval
            Sleep(MONITOR_INTERVAL_MS);
            iteration++;
        }

    } while (monitor_mode && g_running);

    if (monitor_mode && iteration > 0) {
        printf("--------------------------------------------------------------------------------\n");
        printf("  Monitoring stopped after %d readings\n", iteration);
        printf("================================================================================\n");
    }

    return SUCCESS;
}

int test_all_channels(int verbose, int monitor_mode) {
    int result;
    double currents[CDAQ_CHANNELS_PER_SLOT];
    int num_read;
    int iteration = 0;

    print_header_all(verbose);

    do {
        // Read all channels
        result = CDAQ_ReadCurrentArray(currents, &num_read);
        if (result != SUCCESS) {
            printf("  ERROR: Failed to read current array (error %d)\n", result);
            return result;
        }

        // Display timestamp
        char time_str[32];
        time_t now = time(NULL);
        strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&now));

        printf("\n  Time: %s\n", time_str);
        printf("  %-8s %-12s %-10s\n", "Channel", "Current (mA)", "Status");
        printf("  -------------------------------\n");

        // Display all channels
        for (int i = 0; i < num_read; i++) {
            const char *status;
            if (currents[i] < 3.5) {
                status = "FAULT";
            } else if (currents[i] > 20.5) {
                status = "OVER";
            } else if (currents[i] < 4.5) {
                status = "LOW";
            } else if (currents[i] > 19.5) {
                status = "HIGH";
            } else {
                status = "OK";
            }

            printf("  %-8d %-12.3f %-10s\n", i, currents[i], status);
        }

        if (monitor_mode) {
            printf("\n  Press Ctrl+C to stop monitoring...\n");
            Sleep(MONITOR_INTERVAL_MS);
            iteration++;
        }

    } while (monitor_mode && g_running);

    if (monitor_mode && iteration > 0) {
        printf("\n  Monitoring stopped after %d readings\n", iteration);
    }

    printf("================================================================================\n\n");

    return SUCCESS;
}

/******************************************************************************
 * Main Function
 ******************************************************************************/
int main(int argc, char *argv[]) {
    int result;
    int test_channel = -1;
    int test_all = 0;
    int monitor_mode = 0;
    int verbose = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            test_channel = atoi(argv[++i]);
            if (test_channel < 0 || test_channel >= CDAQ_CHANNELS_PER_SLOT) {
                fprintf(stderr, "Error: Invalid channel %d (must be 0-15)\n", test_channel);
                return 1;
            }
        } else if (strcmp(argv[i], "-a") == 0) {
            test_all = 1;
        } else if (strcmp(argv[i], "-m") == 0) {
            monitor_mode = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate arguments
    if (test_channel < 0 && !test_all) {
        fprintf(stderr, "Error: Must specify either -c <channel> or -a\n");
        print_usage(argv[0]);
        return 1;
    }

    if (test_channel >= 0 && test_all) {
        fprintf(stderr, "Error: Cannot use both -c and -a options\n");
        print_usage(argv[0]);
        return 1;
    }

    // Setup signal handler for Ctrl+C
    signal(SIGINT, signal_handler);

    // Print configuration
    printf("\n");
    printf("NI 9202 4-20mA Current Sensor Test\n");
    printf("===================================\n");
    printf("Configuration:\n");
    printf("  Shunt Resistor: %.1f ohms\n", CDAQ_CURRENT_SHUNT_RESISTOR);
    printf("  Voltage Range:  %.1fV (4mA) to %.1fV (20mA)\n",
           CDAQ_CURRENT_MIN_V, CDAQ_CURRENT_MAX_V);
    printf("  Mode:           %s\n", monitor_mode ? "Continuous Monitoring" : "Single Read");
    if (test_channel >= 0) {
        printf("  Channel:        %d\n", test_channel);
    } else {
        printf("  Channels:       All (0-15)\n");
    }
    printf("\n");

    // Initialize cDAQ current slot
    printf("Initializing NI 9202 on slot 1...\n");
    result = CDAQ_InitializeCurrentSlot();
    if (result != SUCCESS) {
        fprintf(stderr, "ERROR: Failed to initialize cDAQ current slot (error %d)\n", result);
        fprintf(stderr, "Check that:\n");
        fprintf(stderr, "  1. NI 9202 is installed in slot 1 of cDAQ-9178\n");
        fprintf(stderr, "  2. cDAQ chassis is powered on and connected\n");
        fprintf(stderr, "  3. NI-DAQmx drivers are installed\n");
        return 1;
    }
    printf("Initialization successful!\n");

    // Run test
    if (test_channel >= 0) {
        result = test_single_channel(test_channel, verbose, monitor_mode);
    } else {
        result = test_all_channels(verbose, monitor_mode);
    }

    // Cleanup
    printf("\nCleaning up...\n");
    CDAQ_CleanupCurrentSlot();
    printf("Done!\n\n");

    return (result == SUCCESS) ? 0 : 1;
}

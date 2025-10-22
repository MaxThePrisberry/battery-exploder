/******************************************************************************
 * cdaq_current_test.h
 *
 * Header for NI 9202 4-20mA current sensor test program
 *
 * Authors: Maxwell Prisbrey, Nicolas Rasmont, Gabriel Meier
 ******************************************************************************/
#ifndef CDAQ_CURRENT_TEST_H
#define CDAQ_CURRENT_TEST_H

#include "../cdaq_utils.h"
#include "../common.h"

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

/**
 * Print usage information
 */
void print_usage(const char *program_name);

/**
 * Test a single channel
 * @param channel - Channel number (0-15)
 * @param verbose - Show voltage and current
 * @param monitor_mode - Continuous monitoring
 * @return SUCCESS or error code
 */
int test_single_channel(int channel, int verbose, int monitor_mode);

/**
 * Test all channels
 * @param verbose - Show voltage and current
 * @param monitor_mode - Continuous monitoring
 * @return SUCCESS or error code
 */
int test_all_channels(int verbose, int monitor_mode);

#endif // CDAQ_CURRENT_TEST_H

/******************************************************************************
 * cdaq_utils.h
 *
 * cDAQ Utilities Module Header
 * Handles NI cDAQ slots:
 *   - Slot 1: NI 9202 for 4-20mA current sensors (voltage measurement)
 *   - Slot 2: NI 9213 for thermocouples
 *   - Slot 3: NI 9213 for thermocouples
 ******************************************************************************/
#ifndef CDAQ_UTILS_H
#define CDAQ_UTILS_H

#include "common.h"
#include <NIDAQmx.h>

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/
#define CDAQ_CHANNELS_PER_SLOT 16
#define CDAQ_TC_MIN_TEMP 0.0
#define CDAQ_TC_MAX_TEMP 400.0
#define CDAQ_CJC_TEMP 25.0
#define CDAQ_READ_TIMEOUT 10.0

// 4-20mA current sensor configuration (for NI 9202 on slot 1)
// Using external shunt resistor to convert current to voltage
#define CDAQ_CURRENT_SHUNT_RESISTOR 250.0  // Ohms (standard for 4-20mA)
#define CDAQ_CURRENT_MIN_V 1.0             // 4mA * 250Ω = 1V
#define CDAQ_CURRENT_MAX_V 5.0             // 20mA * 250Ω = 5V
#define CDAQ_VOLTAGE_RANGE_MIN -10.0       // NI 9202 range
#define CDAQ_VOLTAGE_RANGE_MAX 10.0        // NI 9202 range

/******************************************************************************
 * Public Function Declarations
 ******************************************************************************/

/**
 * Initialize the cDAQ module
 * Creates DAQmx tasks for slots 2 and 3
 * @return SUCCESS or error code
 */
int CDAQ_Initialize(void);

/**
 * Clean up and release all cDAQ resources
 */
void CDAQ_Cleanup(void);

/**
 * Read a specific thermocouple channel
 * @param slot - cDAQ module slot number (2 or 3 only)
 * @param tc_number - Thermocouple channel number (0-15)
 * @param temperature - Pointer to receive temperature value in degrees C
 * @return SUCCESS or error code
 */
int CDAQ_ReadTC(int slot, int tc_number, double *temperature);

/**
 * Read all thermocouple channels for a slot
 * @param slot - cDAQ module slot number (2 or 3 only)
 * @param temperatures - Array to receive temperature values (must be size 16)
 * @param num_read - Pointer to receive number of channels read
 * @return SUCCESS or error code
 */
int CDAQ_ReadTCArray(int slot, double *temperatures, int *num_read);

/**
 * Initialize slot 1 for 4-20mA current sensors (NI 9202)
 * Must be called separately from CDAQ_Initialize()
 * @return SUCCESS or error code
 */
int CDAQ_InitializeCurrentSlot(void);

/**
 * Clean up slot 1 current sensor resources
 */
void CDAQ_CleanupCurrentSlot(void);

/**
 * Read a 4-20mA current sensor channel (slot 1 only)
 * Automatically converts voltage to current using shunt resistor value
 * @param channel - Channel number (0-15)
 * @param current_mA - Pointer to receive current value in milliamps
 * @return SUCCESS or error code
 */
int CDAQ_ReadCurrent(int channel, double *current_mA);

/**
 * Read all 4-20mA current sensor channels (slot 1 only)
 * @param currents_mA - Array to receive current values in mA (must be size 16)
 * @param num_read - Pointer to receive number of channels read
 * @return SUCCESS or error code
 */
int CDAQ_ReadCurrentArray(double *currents_mA, int *num_read);

/**
 * Read raw voltage from a channel on slot 1
 * Useful for diagnostics or non-standard sensors
 * @param channel - Channel number (0-15)
 * @param voltage - Pointer to receive voltage value in volts
 * @return SUCCESS or error code
 */
int CDAQ_ReadVoltage(int channel, double *voltage);

#endif // CDAQ_UTILS_H
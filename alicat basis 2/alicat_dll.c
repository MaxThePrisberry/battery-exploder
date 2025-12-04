/******************************************************************************
 * ALICAT BASIS 2 Flow Controller Library
 * Implementation file for LabWindows/CVI
 * 
 * Implements Modbus-RTU communication for ALICAT flow controllers
 ******************************************************************************/

#include "alicat_dll.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <utility.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char* errorStrings[] = {
    "Success",
    "Communication error",
    "Checksum error",
    "Timeout error",
    "Invalid parameter",
    "Device busy",
    "Not connected",
    "Invalid response",
    "Not supported"
};

static const char* gasNames[] = {
    "Air", "Argon", "CO2", "Nitrogen", "Oxygen",
    "N2O", "Hydrogen", "Helium", "Methane"
};

static const char* flowUnitNames[] = {
    "SCCM", "NCCM", "SLPM", "NLPM", "SmL/s", "NmL/s",
    "SmL/m", "NmL/m", "SL/h", "NL/h", "SCCS", "NCCS",
    "Sm�/h", "Nm�/h", "Sm�/d", "Nm�/d", "SCIM",
    "SCFM", "SCFH", "SCFD"
};

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static unsigned short CalculateCRC(unsigned char *buffer, int length) {
    unsigned short crc = 0xFFFF;
    
    for (int i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

static int SendModbusRTU(ALICAT_Handle *handle, unsigned char functionCode,
                        unsigned short startAddress, unsigned short *data,
                        int dataCount, unsigned short *response, int maxResponseLen) {
    
							
	if (!handle || !handle->isConnected) {
        return ALICAT_ERROR_NOT_CONNECTED;
    }
	
	unsigned char txBuffer[256] = {0};
    unsigned char rxBuffer[256] = {0};
    int txLength = 0;
    
    // Build request frame
    txBuffer[txLength++] = (unsigned char)handle->modbusAddress;
    txBuffer[txLength++] = functionCode;
    txBuffer[txLength++] = (unsigned char)((startAddress >> 8) & 0xFF);
    txBuffer[txLength++] = (unsigned char)(startAddress & 0xFF);
    
    if (functionCode == MODBUS_READ_HOLDING) {
        // Read: add register count
        txBuffer[txLength++] = (unsigned char)((dataCount >> 8) & 0xFF);
        txBuffer[txLength++] = (unsigned char)(dataCount & 0xFF);
    } else if (functionCode == MODBUS_WRITE_SINGLE) {
        // Write single: add value
        txBuffer[txLength++] = (unsigned char)((data[0] >> 8) & 0xFF);
        txBuffer[txLength++] = (unsigned char)(data[0] & 0xFF);
    } else if (functionCode == MODBUS_WRITE_MULTIPLE) {
        // Write multiple: add count, byte count, and values
        txBuffer[txLength++] = (unsigned char)((dataCount >> 8) & 0xFF);
        txBuffer[txLength++] = (unsigned char)(dataCount & 0xFF);
        txBuffer[txLength++] = (unsigned char)(dataCount * 2);
        
        for (int i = 0; i < dataCount; i++) {
            txBuffer[txLength++] = (unsigned char)((data[i] >> 8) & 0xFF);
            txBuffer[txLength++] = (unsigned char)(data[i] & 0xFF);
        }
    }
    
    // Calculate and append CRC
    unsigned short crc = CalculateCRC(txBuffer, txLength);
    txBuffer[txLength++] = (unsigned char)(crc & 0xFF);
    txBuffer[txLength++] = (unsigned char)((crc >> 8) & 0xFF);
    
    // Clear input queue
    FlushInQ(handle->comPort);
    
    // Send command
    int bytesWritten = ComWrt(handle->comPort, (char*)txBuffer, txLength);
    if (bytesWritten != txLength) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to write to COM port: wrote %d of %d bytes",
                   bytesWritten, txLength);
        return ALICAT_ERROR_COMM;
    }
    
    LogDebugEx(LOG_DEVICE_ALICAT, "Sent Modbus frame: %d bytes", bytesWritten);
    
    // Wait for response
    Delay(0.05); // 50ms for device processing
    
    double startTime = Timer();
    int totalRead = 0;
    int expectedLength = 0;
    
    while (totalRead < 256) {
        int available = GetInQLen(handle->comPort);
        
        if (available > 0) {
            int toRead = (available < (256 - totalRead)) ? available : (256 - totalRead);
            int bytesRead = ComRd(handle->comPort, (char*)&rxBuffer[totalRead], toRead);
            
            if (bytesRead > 0) {
                totalRead += bytesRead;
                
                // Determine expected response length
                if (totalRead >= 3) {
                    if (functionCode == MODBUS_READ_HOLDING && totalRead >= 3) {
                        expectedLength = 5 + rxBuffer[2]; // Addr + Func + ByteCount + Data + CRC
                    } else if (functionCode == MODBUS_WRITE_SINGLE) {
                        expectedLength = 8; // Addr + Func + Addr + Value + CRC
                    } else if (functionCode == MODBUS_WRITE_MULTIPLE) {
                        expectedLength = 8; // Addr + Func + Addr + Count + CRC
                    }
                }
                
                if (expectedLength > 0 && totalRead >= expectedLength) {
                    break;
                }
            }
        }
        
        if ((Timer() - startTime) > (handle->timeoutMs / 1000.0)) {
            LogErrorEx(LOG_DEVICE_ALICAT, "Timeout reading response (got %d bytes)", totalRead);
            return ALICAT_ERROR_TIMEOUT;
        }
        
        Delay(0.01);
    }
    
    LogDebugEx(LOG_DEVICE_ALICAT, "Received response: %d bytes", totalRead);
    
    // Validate response
    if (totalRead < 5) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Response too short: %d bytes", totalRead);
        return ALICAT_ERROR_RESPONSE;
    }
    
    // Check CRC
    unsigned short receivedCRC = (rxBuffer[totalRead-1] << 8) | rxBuffer[totalRead-2];
    unsigned short calculatedCRC = CalculateCRC(rxBuffer, totalRead - 2);
    
    if (receivedCRC != calculatedCRC) {
        LogErrorEx(LOG_DEVICE_ALICAT, "CRC mismatch: expected 0x%04X, got 0x%04X",
                   calculatedCRC, receivedCRC);
        return ALICAT_ERROR_CHECKSUM;
    }
    
    // Check address
    if (rxBuffer[0] != handle->modbusAddress) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Wrong Modbus address: expected %d, got %d",
                   handle->modbusAddress, rxBuffer[0]);
        return ALICAT_ERROR_RESPONSE;
    }
    
    // Check for exception
    if (rxBuffer[1] & 0x80) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Modbus exception: code 0x%02X", rxBuffer[2]);
        return ALICAT_ERROR_RESPONSE;
    }
    
    // Check function code
    if (rxBuffer[1] != functionCode) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Wrong function code: expected 0x%02X, got 0x%02X",
                   functionCode, rxBuffer[1]);
        return ALICAT_ERROR_RESPONSE;
    }
    
    // Extract data
    if (response && maxResponseLen > 0) {
        if (functionCode == MODBUS_READ_HOLDING) {
            int byteCount = rxBuffer[2];
            int registerCount = byteCount / 2;
            if (registerCount > maxResponseLen) registerCount = maxResponseLen;
            
            for (int i = 0; i < registerCount; i++) {
                response[i] = (rxBuffer[3 + i*2] << 8) | rxBuffer[4 + i*2];
            }
        } else if (functionCode == MODBUS_WRITE_SINGLE || functionCode == MODBUS_WRITE_MULTIPLE) {
            // For write, verify echo
            unsigned short respAddr = (rxBuffer[2] << 8) | rxBuffer[3];
            if (respAddr != startAddress) {
                LogErrorEx(LOG_DEVICE_ALICAT, "Address mismatch in write response");
                return ALICAT_ERROR_RESPONSE;
            }
        }
    }
    
    LogDebugEx(LOG_DEVICE_ALICAT, "Transaction completed successfully");
    Delay(0.02); // 20ms recovery time
    
    return ALICAT_SUCCESS;
}

/******************************************************************************
 * Connection Functions
 ******************************************************************************/

int ALICAT_Initialize(ALICAT_Handle *handle, int comPort, int modbusAddress, int baudRate) {
    if (!handle) return ALICAT_ERROR_INVALID_PARAM;
    
    memset(handle, 0, sizeof(ALICAT_Handle));
    handle->comPort = comPort;
    handle->modbusAddress = modbusAddress;
    handle->baudRate = baudRate;
    handle->timeoutMs = DEFAULT_TIMEOUT_MS;
    handle->state = DEVICE_STATE_CONNECTING;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Initializing on COM%d, address %d, %d baud",
                 comPort, modbusAddress, baudRate);
    
    if (OpenComConfig(comPort, "", baudRate, 0, 8, 1, 512, 512) < 0) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to open COM%d", comPort);
        handle->state = DEVICE_STATE_ERROR;
        return ALICAT_ERROR_COMM;
    }
    
    SetComTime(comPort, handle->timeoutMs / 1000.0);
    SetXMode(comPort, 0); // No XON/XOFF
    SetCTSMode(comPort, 0); // No CTS checking
    
    handle->isConnected = 1;
    handle->state = DEVICE_STATE_CONNECTED;
    
    // Read serial number
    unsigned short serialRegs[6];
    if (ALICAT_ReadRegisters(handle, REG_SERIAL_NUMBER_START, serialRegs, 6) == ALICAT_SUCCESS) {
        for (int i = 0; i < 6; i++) {
            handle->serialNumber[i*2] = (char)(serialRegs[i] >> 8);
            handle->serialNumber[i*2 + 1] = (char)(serialRegs[i] & 0xFF);
        }
        handle->serialNumber[12] = '\0';
        LogMessageEx(LOG_DEVICE_ALICAT, "Serial Number: %s", handle->serialNumber);
    }
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Successfully initialized");
    return ALICAT_SUCCESS;
}

int ALICAT_TestConnection(ALICAT_Handle *handle) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    // Try to read firmware version
    unsigned short version;
    return ALICAT_ReadRegister(handle, REG_FIRMWARE_VERSION, &version);
}

int ALICAT_Disconnect(ALICAT_Handle *handle) {
    if (!handle) return ALICAT_SUCCESS;
    
    if (handle->isConnected) {
        LogMessageEx(LOG_DEVICE_ALICAT, "Disconnecting from COM%d", handle->comPort);
        CloseCom(handle->comPort);
        handle->isConnected = 0;
        handle->state = DEVICE_STATE_DISCONNECTED;
    }
    
    return ALICAT_SUCCESS;
}

/******************************************************************************
 * Configuration Functions
 ******************************************************************************/

int ALICAT_FactoryReset(ALICAT_Handle *handle) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Performing factory reset...");
    
    int result = ALICAT_WriteRegister(handle, REG_FACTORY_RESTORE, 0x5214);
    if (result != ALICAT_SUCCESS) {
        LogErrorEx(LOG_DEVICE_ALICAT, "Failed to write factory reset register");
        return result;
    }
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Factory reset command sent - power cycle required");
    return ALICAT_SUCCESS;
}

int ALICAT_Configure(ALICAT_Handle *handle, const ALICAT_Configuration *config) {
    if (!handle || !handle->isConnected || !config) return ALICAT_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Configuring ALICAT...");
    
    int result;
    
    // Set gas type
    result = ALICAT_SetGas(handle, config->gasType);
    if (result != ALICAT_SUCCESS) return result;
    
    // Set setpoint source
    result = ALICAT_SetSetpointSource(handle, config->setpointSource);
    if (result != ALICAT_SUCCESS) return result;
    
    // Set reference temperature
    result = ALICAT_SetRefTemperature(handle, config->refTemperature);
    if (result != ALICAT_SUCCESS) return result;
    
    // Set flow averaging
    result = ALICAT_SetFlowAveraging(handle, config->flowAveraging);
    if (result != ALICAT_SUCCESS) return result;
    
    // Set autotare
    result = ALICAT_SetAutotare(handle, config->autotareEnable);
    if (result != ALICAT_SUCCESS) return result;
    
    // Set PID parameters
    ALICAT_PIDParams pidParams = {config->pGain, config->iGain};
    result = ALICAT_SetPIDParams(handle, &pidParams);
    if (result != ALICAT_SUCCESS) return result;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Configuration complete");
    return ALICAT_SUCCESS;
}

int ALICAT_ConfigureDefault(ALICAT_Handle *handle) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    ALICAT_Configuration config = {
        .gasType = GAS_AIR,
        .setpointSource = SETPOINT_SOURCE_DIGITAL_UNSAVED,
        .refTemperature = 25.0,
        .flowAveraging = 100,
        .autotareEnable = 1,
        .pGain = 500,
        .iGain = 5000
    };
    
    return ALICAT_Configure(handle, &config);
}

/******************************************************************************
 * Basic Control Functions
 ******************************************************************************/

int ALICAT_SetSetpoint(ALICAT_Handle *handle, double flowRate) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;

    LogMessageEx(LOG_DEVICE_ALICAT, "Setting setpoint: %.3f", flowRate);

    // Convert to scaled integer (value * 1000 per manual specification)
    // Manual page 19: "Setpoint (flow units) = value / 1000"
    // Example: 500 SCCM requires writing 500,000
    int scaledValue = (int)(flowRate * FLOW_SCALE_FACTOR);

    LogMessageEx(LOG_DEVICE_ALICAT, "Scaled setpoint value: %d (0x%08X)", scaledValue, scaledValue);

    // Split into two 16-bit registers for 32-bit write (big-endian)
    // Register 2053 = high word, Register 2054 = low word
    unsigned short values[2];
    values[0] = (unsigned short)((scaledValue >> 16) & 0xFFFF);   // High word
    values[1] = (unsigned short)(scaledValue & 0xFFFF);           // Low word

    LogMessageEx(LOG_DEVICE_ALICAT, "Writing registers: [0]=0x%04X [1]=0x%04X", values[0], values[1]);

    // Write to both registers 2053-2054
    return ALICAT_WriteRegisters(handle, REG_SETPOINT, values, 2);
}

int ALICAT_SetGas(ALICAT_Handle *handle, int gasType) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    if (gasType < 0 || gasType > 8) return ALICAT_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting gas type: %d (%s)", gasType, ALICAT_GetGasName(gasType));
    
    return ALICAT_WriteRegister(handle, REG_SELECTED_GAS, (unsigned short)gasType);
}

int ALICAT_Tare(ALICAT_Handle *handle) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Performing tare...");
    
    return ALICAT_WriteRegister(handle, REG_TARE_COMMAND, TARE_COMMAND_VALUE);
}

/******************************************************************************
 * Read Functions
 ******************************************************************************/

int ALICAT_GetStatus(ALICAT_Handle *handle, ALICAT_Status *status) {
    if (!handle || !handle->isConnected || !status) return ALICAT_ERROR_INVALID_PARAM;
    
    memset(status, 0, sizeof(ALICAT_Status));
    
    int result;
    unsigned short values[10];
    
    // Read multiple registers at once for efficiency
    result = ALICAT_ReadRegisters(handle, REG_SELECTED_GAS, values, 8);
    if (result != ALICAT_SUCCESS) return result;
    
    // Parse values
    status->selectedGas = values[0];
    status->statusFlags = values[1];
    status->temperature = (short)values[2] / TEMP_SCALE_FACTOR;
    status->flowRate = (short)values[3] / FLOW_SCALE_FACTOR;
    
    unsigned int totalVol = ((unsigned int)values[4] << 16) | values[5];
    status->totalVolume = totalVol / FLOW_SCALE_FACTOR;
    
    status->setpoint = (short)values[6] / FLOW_SCALE_FACTOR;
    status->valveDrive = values[7] / VALVE_SCALE_FACTOR;
    
    // Parse status flags
    status->massOverrange = (status->statusFlags & STATUS_MASS_OVERRANGE) ? 1 : 0;
    status->tempOverrange = (status->statusFlags & STATUS_TEMP_OVERRANGE) ? 1 : 0;
    status->totalizerOverrange = (status->statusFlags & STATUS_TOTALIZER_OVERRANGE) ? 1 : 0;
    status->valveHold = (status->statusFlags & STATUS_VALVE_HOLD) ? 1 : 0;
    status->valveThermalMgmt = (status->statusFlags & STATUS_VALVE_THERMAL_MGMT) ? 1 : 0;
    
    return ALICAT_SUCCESS;
}

int ALICAT_GetFlowRate(ALICAT_Handle *handle, double *flowRate) {
    if (!handle || !handle->isConnected || !flowRate) return ALICAT_ERROR_INVALID_PARAM;
    
    unsigned short value;
    int result = ALICAT_ReadRegister(handle, REG_FLOW, &value);
    
    if (result == ALICAT_SUCCESS) {
        *flowRate = (short)value / FLOW_SCALE_FACTOR;
    }
    
    return result;
}

int ALICAT_GetSetpoint(ALICAT_Handle *handle, double *setpoint) {
    if (!handle || !handle->isConnected || !setpoint) return ALICAT_ERROR_INVALID_PARAM;
    
    unsigned short value;
    int result = ALICAT_ReadRegister(handle, REG_CURRENT_SETPOINT, &value);
    
    if (result == ALICAT_SUCCESS) {
        *setpoint = (short)value / FLOW_SCALE_FACTOR;
    }
    
    return result;
}

int ALICAT_GetTemperature(ALICAT_Handle *handle, double *temperature) {
    if (!handle || !handle->isConnected || !temperature) return ALICAT_ERROR_INVALID_PARAM;
    
    unsigned short value;
    int result = ALICAT_ReadRegister(handle, REG_TEMPERATURE, &value);
    
    if (result == ALICAT_SUCCESS) {
        *temperature = (short)value / TEMP_SCALE_FACTOR;
    }
    
    return result;
}

int ALICAT_GetTotalVolume(ALICAT_Handle *handle, double *totalVolume) {
    if (!handle || !handle->isConnected || !totalVolume) return ALICAT_ERROR_INVALID_PARAM;
    
    unsigned short values[2];
    int result = ALICAT_ReadRegisters(handle, REG_TOTAL_VOLUME, values, 2);
    
    if (result == ALICAT_SUCCESS) {
        unsigned int total = ((unsigned int)values[0] << 16) | values[1];
        *totalVolume = total / FLOW_SCALE_FACTOR;
    }
    
    return result;
}

int ALICAT_GetValveDrive(ALICAT_Handle *handle, double *valveDrive) {
    if (!handle || !handle->isConnected || !valveDrive) return ALICAT_ERROR_INVALID_PARAM;
    
    unsigned short value;
    int result = ALICAT_ReadRegister(handle, REG_VALVE_DRIVE, &value);
    
    if (result == ALICAT_SUCCESS) {
        *valveDrive = value / VALVE_SCALE_FACTOR;
    }
    
    return result;
}

int ALICAT_GetPIDParams(ALICAT_Handle *handle, ALICAT_PIDParams *params) {
    if (!handle || !handle->isConnected || !params) return ALICAT_ERROR_INVALID_PARAM;
    
    unsigned short values[2];
    int result = ALICAT_ReadRegisters(handle, REG_PGAIN, values, 2);
    
    if (result == ALICAT_SUCCESS) {
        params->pGain = values[0];
        params->iGain = values[1];
    }
    
    return result;
}

/******************************************************************************
 * Configuration Functions
 ******************************************************************************/

int ALICAT_SetSetpointSource(ALICAT_Handle *handle, int source) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    if (source < 0 || source > 2) return ALICAT_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting setpoint source: %d", source);
    
    return ALICAT_WriteRegister(handle, REG_SETPOINT_SOURCE, (unsigned short)source);
}

int ALICAT_SetPIDParams(ALICAT_Handle *handle, const ALICAT_PIDParams *params) {
    if (!handle || !handle->isConnected || !params) return ALICAT_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting PID parameters: P %u, I %u",
                 params->pGain, params->iGain);
    
    unsigned short values[2] = {params->pGain, params->iGain};
    return ALICAT_WriteRegisters(handle, REG_PGAIN, values, 2);
}

int ALICAT_SetFlowAveraging(ALICAT_Handle *handle, int averagingMs) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    if (averagingMs < 0 || averagingMs > 2500) return ALICAT_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting flow averaging: %d ms", averagingMs);
    
    return ALICAT_WriteRegister(handle, REG_FLOW_AVERAGING, (unsigned short)averagingMs);
}

int ALICAT_SetRefTemperature(ALICAT_Handle *handle, double tempC) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting reference temperature: %.1f deg C", tempC);
    
    unsigned short value = (unsigned short)(tempC * TEMP_SCALE_FACTOR);
    return ALICAT_WriteRegister(handle, REG_REF_TEMPERATURE, value);
}

int ALICAT_SetWatchdog(ALICAT_Handle *handle, int timeoutMs) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    if (timeoutMs < 0 || timeoutMs > 5000) return ALICAT_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting watchdog: %d ms", timeoutMs);
    
    return ALICAT_WriteRegister(handle, REG_WATCHDOG_TIME, (unsigned short)timeoutMs);
}

int ALICAT_SetAutotare(ALICAT_Handle *handle, int enable) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting autotare: %s", enable ? "ENABLED" : "DISABLED");
    
    return ALICAT_WriteRegister(handle, REG_AUTOTARE_ENABLE, enable ? 1 : 0);
}

/******************************************************************************
 * Totalizer Functions
 ******************************************************************************/

int ALICAT_ResetTotalizer(ALICAT_Handle *handle) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Resetting totalizer...");
    
    return ALICAT_WriteRegister(handle, REG_TOTALIZER_RESET, 0xAA55);
}

int ALICAT_SetBatchVolume(ALICAT_Handle *handle, double volume) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "Setting batch volume: %.3f", volume);
    
    int scaledValue = (int)(volume * FLOW_SCALE_FACTOR);
    unsigned short values[2];
    values[0] = (unsigned short)((scaledValue >> 16) & 0xFFFF);
    values[1] = (unsigned short)(scaledValue & 0xFFFF);
    
    return ALICAT_WriteRegisters(handle, REG_BATCH_VOLUME, values, 2);
}

int ALICAT_GetBatchRemaining(ALICAT_Handle *handle, double *remaining) {
    if (!handle || !handle->isConnected || !remaining) return ALICAT_ERROR_INVALID_PARAM;
    
    unsigned short values[2];
    int result = ALICAT_ReadRegisters(handle, REG_BATCH_REMAINING, values, 2);
    
    if (result == ALICAT_SUCCESS) {
        unsigned int rem = ((unsigned int)values[0] << 16) | values[1];
        *remaining = rem / FLOW_SCALE_FACTOR;
    }
    
    return result;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* ALICAT_GetErrorString(int errorCode) {
    if (errorCode == ALICAT_SUCCESS) {
        return errorStrings[0];
    }
    
    int index = 0;
    switch (errorCode) {
        case ALICAT_ERROR_COMM:         index = 1; break;
        case ALICAT_ERROR_CHECKSUM:     index = 2; break;
        case ALICAT_ERROR_TIMEOUT:      index = 3; break;
        case ALICAT_ERROR_INVALID_PARAM: index = 4; break;
        case ALICAT_ERROR_BUSY:         index = 5; break;
        case ALICAT_ERROR_NOT_CONNECTED: index = 6; break;
        case ALICAT_ERROR_RESPONSE:     index = 7; break;
        case ALICAT_ERROR_NOT_SUPPORTED: index = 8; break;
        default:
            return "Unknown error";
    }
    
    if (index < sizeof(errorStrings) / sizeof(errorStrings[0])) {
        return errorStrings[index];
    }
    return "Unknown error";
}

const char* ALICAT_GetGasName(int gasType) {
    if (gasType >= 0 && gasType < sizeof(gasNames) / sizeof(gasNames[0])) {
        return gasNames[gasType];
    }
    return "Unknown";
}

const char* ALICAT_GetFlowUnitName(int unitType) {
    if (unitType >= 0 && unitType < sizeof(flowUnitNames) / sizeof(flowUnitNames[0])) {
        return flowUnitNames[unitType];
    }
    return "Unknown";
}

void ALICAT_PrintStatus(const ALICAT_Status *status) {
    if (!status) return;
    
    LogMessageEx(LOG_DEVICE_ALICAT, "=== ALICAT Status ===");
    LogMessageEx(LOG_DEVICE_ALICAT, "Flow Rate: %.3f", status->flowRate);
    LogMessageEx(LOG_DEVICE_ALICAT, "Setpoint: %.3f", status->setpoint);
    LogMessageEx(LOG_DEVICE_ALICAT, "Temperature: %.1f deg C", status->temperature);
    LogMessageEx(LOG_DEVICE_ALICAT, "Total Volume: %.3f", status->totalVolume);
    LogMessageEx(LOG_DEVICE_ALICAT, "Valve Drive: %.1f%%", status->valveDrive);
    LogMessageEx(LOG_DEVICE_ALICAT, "Selected Gas: %s", ALICAT_GetGasName(status->selectedGas));
    LogMessageEx(LOG_DEVICE_ALICAT, "Mass Overrange: %s", status->massOverrange ? "YES" : "NO");
    LogMessageEx(LOG_DEVICE_ALICAT, "Temp Overrange: %s", status->tempOverrange ? "YES" : "NO");
    LogMessageEx(LOG_DEVICE_ALICAT, "Totalizer Overrange: %s", status->totalizerOverrange ? "YES" : "NO");
    LogMessageEx(LOG_DEVICE_ALICAT, "Valve Hold: %s", status->valveHold ? "YES" : "NO");
    LogMessageEx(LOG_DEVICE_ALICAT, "Valve Thermal Mgmt: %s", status->valveThermalMgmt ? "YES" : "NO");
    LogMessageEx(LOG_DEVICE_ALICAT, "====================");
}

/******************************************************************************
 * Low-level Modbus Functions
 ******************************************************************************/

int ALICAT_ReadRegister(ALICAT_Handle *handle, unsigned short address, unsigned short *value) {
    if (!value) return ALICAT_ERROR_INVALID_PARAM;
    
    return ALICAT_ReadRegisters(handle, address, value, 1);
}

int ALICAT_ReadRegisters(ALICAT_Handle *handle, unsigned short address, unsigned short *values, int count) {
    if (!handle || !handle->isConnected || !values || count <= 0) {
        return ALICAT_ERROR_INVALID_PARAM;
    }
    
    return SendModbusRTU(handle, MODBUS_READ_HOLDING, address, NULL, count, values, count);
}

int ALICAT_WriteRegister(ALICAT_Handle *handle, unsigned short address, unsigned short value) {
    if (!handle || !handle->isConnected) return ALICAT_ERROR_NOT_CONNECTED;
    
    return SendModbusRTU(handle, MODBUS_WRITE_SINGLE, address, &value, 1, NULL, 0);
}

int ALICAT_WriteRegisters(ALICAT_Handle *handle, unsigned short address, unsigned short *values, int count) {
    if (!handle || !handle->isConnected || !values || count <= 0) {
        return ALICAT_ERROR_INVALID_PARAM;
    }
    
    return SendModbusRTU(handle, MODBUS_WRITE_MULTIPLE, address, values, count, NULL, 0);
}
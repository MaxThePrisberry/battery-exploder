# ALICAT Flow Controller - Command Prompt Reference

This document describes the command prompt interface for the ALICAT BASIS 2 Mass Flow Controller in the Battery Exploder system.

## Overview

The ALICAT command prompt interface provides interactive control and monitoring of ALICAT flow controllers through the Battery Exploder UI command prompt. All commands use the device queue system for thread-safe operation and execute at HIGH priority for responsive interaction.

## Command Format

All ALICAT commands follow this format:

```
ALI<addr><command>[parameters]
```

Where:
- `ALI` - Device prefix (identifies ALICAT device)
- `<addr>` - 2-digit hexadecimal Modbus address (01-FF)
- `<command>` - Command name
- `[parameters]` - Optional command parameters

### Modbus Address Format

The address must be specified as a **2-digit hexadecimal** value:
- `01` - Modbus address 1 (most common)
- `0A` - Modbus address 10
- `FF` - Modbus address 255

## Available Commands

### Status & Monitoring

#### `ALI<addr>STAT` - Get Full Status

Retrieves complete device status including flow rate, setpoint, temperature, valve position, gas type, totalizer, and status flags.

**Example:**
```
ALI01STAT
```

**Output:**
```
[--->>] Flow: 5.234, Setpoint: 5.000, Temp: 24.3 C
[--->>] Valve: 45.2%, Gas: Air, Total: 123.456
```

#### `ALI<addr>FLOW` - Get Flow Rate Only

Reads just the current flow rate (faster than full status).

**Example:**
```
ALI01FLOW
```

**Output:**
```
[--->>] Flow Rate: 5.234
```

---

### Control Commands

#### `ALI<addr>SET<value>` - Set Flow Setpoint

Sets the flow rate setpoint. Value is in the flow controller's configured units (typically SLPM or SCCM).

**Examples:**
```
ALI01SET10.5    - Set flow to 10.5
ALI01SET0       - Set flow to 0 (stop flow)
ALI01SET100.25  - Set flow to 100.25
```

**Notes:**
- Setpoint range depends on flow controller full scale
- Negative values may be rejected depending on controller configuration
- Setpoint change takes effect immediately

#### `ALI<addr>GAS<type>` - Set Gas Type

Selects the gas type for flow compensation. Each gas has different calibration factors.

**Gas Type Values:**
- `0` - Air (default)
- `1` - Argon
- `2` - CO2 (Carbon Dioxide)
- `3` - N2 (Nitrogen)
- `4` - O2 (Oxygen)
- `5` - N2O (Nitrous Oxide)
- `6` - H2 (Hydrogen)
- `7` - He (Helium)
- `8` - CH4 (Methane)

**Examples:**
```
ALI01GAS0    - Set gas to Air
ALI01GAS3    - Set gas to Nitrogen
ALI01GAS7    - Set gas to Helium
```

**Output:**
```
[--->>] Gas type set to 3 (Nitrogen)
```

#### `ALI<addr>TARE` - Tare Flow Controller

Zeros the flow reading at the current flow condition. Use when no flow is present to eliminate offset errors.

**Example:**
```
ALI01TARE
```

**Output:**
```
[--->>] Tare command success
```

**Usage Notes:**
- Ensure flow is actually at zero before taring
- Wait for flow to stabilize (2-3 seconds) before taring
- Tare is performed automatically at power-on

---

### Configuration Commands

#### `ALI<addr>PID` - Get PID Parameters

Reads the current PID controller gains.

**Example:**
```
ALI01PID
```

**Output:**
```
[--->>] PID Parameters: P=500, I=5000
```

#### `ALI<addr>PIDP<value>` - Set Proportional Gain

Sets the proportional gain for the PID controller.

**Range:** 0 - 65535 (default: 500)

**Example:**
```
ALI01PIDP750    - Set P gain to 750
```

**Notes:**
- Higher P gain = faster response but more overshoot
- Lower P gain = slower response but more stable
- Factory default is usually optimal

#### `ALI<addr>PIDI<value>` - Set Integral Gain

Sets the integral gain for the PID controller.

**Range:** 0 - 65535 (default: 5000)

**Example:**
```
ALI01PIDI6000    - Set I gain to 6000
```

**Notes:**
- Higher I gain = faster elimination of steady-state error
- Too high can cause oscillation
- Set to 0 to disable integral action (not recommended)

#### `ALI<addr>AVG<ms>` - Set Flow Averaging Time

Sets the time window for flow measurement averaging.

**Range:** 0 - 2500 milliseconds (default: 100 ms)

**Examples:**
```
ALI01AVG100     - 100 ms averaging (default)
ALI01AVG500     - 500 ms averaging (smoother, slower response)
ALI01AVG0       - No averaging (fastest response, noisy)
```

**Recommendations:**
- Use 100-200 ms for most applications
- Use 500-1000 ms for very stable readings (slow processes)
- Use 0-50 ms for fast response (rapid transients)

#### `ALI<addr>TEMP<degC>` - Set Reference Temperature

Sets the reference temperature for flow rate calculations (standard conditions).

**Range:** Typically -50 to +150 deg C (default: 25.0)

**Examples:**
```
ALI01TEMP25.0    - Set reference to 25°C (standard)
ALI01TEMP20.0    - Set reference to 20°C
ALI01TEMP0       - Set reference to 0°C
```

**Notes:**
- Affects flow rate calculation and gas density compensation
- Standard temperature is typically 25°C (NIST) or 0°C (STP)
- Controller automatically measures actual gas temperature

#### `ALI<addr>RESET` - Reset Totalizer

Resets the totalizer (total volume) counter to zero.

**Example:**
```
ALI01RESET
```

**Output:**
```
[--->>] Totalizer reset success
```

**Notes:**
- Totalizer accumulates total volume of gas that has passed through
- Useful for batch operations or consumption tracking
- Does not affect flow rate or setpoint

---

### Help

#### `ALI<addr>HELP` - Show Command Help

Displays a summary of all available ALICAT commands with syntax.

**Example:**
```
ALI01HELP
```

**Output:**
```
[--->>] ALICAT Flow Controller Commands (address in hex):
[--->>]   ALI<addr>STAT          - Get full status
[--->>]   ALI<addr>FLOW          - Get flow rate only
[--->>]   ALI<addr>SET<value>    - Set flow setpoint
[--->>]   ...
```

---

## Common Usage Examples

### Initial Setup and Testing

```bash
# 1. Check connection and get status
ALI01STAT

# 2. Verify gas type is correct
ALI01STAT
# (Look at the Gas field in output)

# 3. Set gas type if needed (e.g., Nitrogen)
ALI01GAS3

# 4. Tare the controller (with zero flow)
ALI01TARE

# 5. Test flow control
ALI01SET5.0
ALI01FLOW
```

### Monitoring Flow During Experiment

```bash
# Quick flow check
ALI01FLOW

# Full status with valve position and flags
ALI01STAT
```

### Changing Flow Setpoint

```bash
# Ramp to new setpoint
ALI01SET10.0    # Set to 10
# Wait...
ALI01SET15.0    # Increase to 15
# Wait...
ALI01SET0       # Stop flow
```

### PID Tuning (Advanced)

```bash
# 1. Read current PID parameters
ALI01PID

# 2. If response is too slow, increase P gain
ALI01PIDP750

# 3. Test response
ALI01SET5.0
# Observe response time

# 4. If oscillating, decrease P gain
ALI01PIDP400

# 5. Adjust I gain if steady-state error exists
ALI01PIDI6000
```

### Troubleshooting

#### Flow Not Reaching Setpoint

```bash
# 1. Check current status
ALI01STAT
# Look for valve position (should be <100%)
# Check for VALVE_HOLD flag

# 2. Verify gas supply pressure is adequate
# (Check physical gauge - should be >50 psi)

# 3. Check valve drive
ALI01STAT
# If valve = 100% but flow low, insufficient supply pressure
```

#### Noisy Flow Reading

```bash
# Increase averaging time
ALI01AVG500

# Or reduce P gain if oscillating
ALI01PIDP300
```

#### Zero Drift

```bash
# Tare with zero flow
# 1. Set setpoint to zero
ALI01SET0

# 2. Wait for flow to settle (5-10 seconds)

# 3. Tare
ALI01TARE

# 4. Verify
ALI01FLOW
# Should read close to 0.000
```

---

## Status Flags

When using `ALI<addr>STAT`, the following flags may appear:

- **MASS_OVERRANGE** - Flow rate exceeds full scale (check supply pressure)
- **TEMP_OVERRANGE** - Gas temperature out of range
- **VALVE_HOLD** - Valve control is being held (external control active)
- **OVER-RANGE** (in totalizer) - Totalizer has exceeded maximum count

---

## Error Messages

### Common Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `Invalid hex Modbus address given` | Address contains non-hex characters | Use 01-FF (hex) |
| `Failed to get status: Queue not initialized` | ALICAT queue manager not running | Check device initialization |
| `Failed to set setpoint: Communication error` | No response from device | Check COM port, cable, power |
| `Invalid gas type. Valid range: 0-8` | Gas type out of range | Use 0-8 for gas selection |
| `Averaging time must be 0-2500 ms` | Value out of range | Use 0-2500 for averaging |

---

## Configuration Reference

### Default Configuration (common.h)

```c
#define ALICAT_COM_PORT         7       // COM7
#define ALICAT_BAUD_RATE        38400   // 38400 baud
#define ALICAT_NUM_DEVICES      1       // 1 device
#define ALICAT_MODBUS_ADDRESS   1       // Address 1
```

### Device Specifications

- **Communication:** Modbus RTU over RS232
- **Baud Rate:** 38400 (default)
- **Data Format:** 8 data bits, no parity, 1 stop bit
- **Timeout:** 1000 ms (default)
- **Update Rate:** 10 Hz (100 ms per sample)

---

## Multiple Device Support

The system supports up to 16 ALICAT devices on the same COM port using different Modbus addresses.

### Example: Two Flow Controllers

```bash
# Device 1 (Modbus address 1)
ALI01STAT
ALI01SET10.0

# Device 2 (Modbus address 2)
ALI02STAT
ALI02SET5.0
```

### Setting Modbus Address on Device

Refer to ALICAT manual for programming device Modbus address:
1. Access configuration menu (hold SET button)
2. Navigate to "Modbus Address"
3. Set unique address (1-247)
4. Save and power cycle

---

## Integration with Experiments

The command prompt is useful for:

1. **Pre-experiment testing** - Verify flow controller operation
2. **Manual control** - Quick adjustments during setup
3. **Troubleshooting** - Diagnose issues during experiments
4. **Configuration** - Set gas type, tune PID, adjust averaging

For automated experiments, use the queue functions directly:
```c
ALICAT_SetSetpointQueued(modbusAddress, flowRate, DEVICE_PRIORITY_NORMAL);
ALICAT_GetStatusQueued(modbusAddress, &status, DEVICE_PRIORITY_LOW);
```

---

## See Also

- **ALICAT Hardware Manual:** `manuals/alicat_basis2_manual.pdf` (if available)
- **Queue System Documentation:** `device_queue.h`
- **ALICAT DLL Reference:** `alicat basis 2/alicat_dll.h`
- **ALICAT Queue API:** `alicat basis 2/alicat_queue.h`
- **System Configuration:** `common.h`

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025 | Initial command prompt interface |

# NI 9202 4-20mA Current Sensor Test

Simple command-line test program for testing 4-20mA current sensors connected to the NI 9202 module.

## Hardware Requirements

- **NI cDAQ-9178** chassis
- **NI 9202** voltage input module installed in **slot 1**
- **4-20mA current sensors** with **250Ω shunt resistors**
- NI-DAQmx drivers installed

## Hardware Setup

### Wiring Diagram

```
4-20mA Sensor (+) ──┬──> NI 9202 AI+ (channel X)
                    │
                  250Ω Shunt Resistor
                    │
4-20mA Sensor (−) ──┴──> NI 9202 AI− (channel X)
```

**Expected Voltage Range:**
- 4mA × 250Ω = **1.0V** (minimum)
- 20mA × 250Ω = **5.0V** (maximum)

### Shunt Resistor

- **Value:** 250Ω (standard for 4-20mA with 1-5V output)
- **Precision:** ±1% or better recommended
- **Power rating:** Minimum 0.125W (0.25W recommended)
  - Max power: (20mA)² × 250Ω = 0.1W

### Alternative Shunt Values

If using a different shunt resistor, update `CDAQ_CURRENT_SHUNT_RESISTOR` in `cdaq_utils.h`:

| Shunt (Ω) | 4mA (V) | 20mA (V) | Range |
|-----------|---------|----------|-------|
| 250       | 1.0     | 5.0      | Standard |
| 500       | 2.0     | 10.0     | Full scale |

## Building the Test

### Using LabWindows/CVI

1. Open **BatteryExploder.prj** in LabWindows/CVI 2020
2. Add the test files to the project:
   - `tests/cdaq_current_test.c`
   - `tests/cdaq_current_test.h`
3. Build the project
4. Run from the LabWindows/CVI IDE or command line

### Manual Build (if building standalone)

```bash
# Requires NI-DAQmx SDK and LabWindows/CVI SDK
gcc -o cdaq_current_test.exe \
    tests/cdaq_current_test.c \
    cdaq_utils.c \
    logging.c \
    -I"C:\Program Files\National Instruments\CVI2020\sdk\include" \
    -L"C:\Program Files\National Instruments\CVI2020\sdk\lib\msvc64" \
    -lNIDAQmx
```

## Usage

### Basic Syntax

```bash
cdaq_current_test [options]
```

### Options

| Option | Description |
|--------|-------------|
| `-c <channel>` | Test single channel (0-15) |
| `-a` | Test all channels (array read) |
| `-m` | Continuous monitoring mode |
| `-v` | Verbose mode (show voltages) |
| `-h` | Show help |

### Examples

**Read channel 0 once:**
```bash
cdaq_current_test -c 0
```

**Read all channels once:**
```bash
cdaq_current_test -a
```

**Monitor channel 5 continuously:**
```bash
cdaq_current_test -c 5 -m
```
Press `Ctrl+C` to stop monitoring.

**Monitor all channels with voltage display:**
```bash
cdaq_current_test -a -m -v
```

**Verbose single channel test:**
```bash
cdaq_current_test -c 3 -v
```

## Output Examples

### Single Channel Output

```
================================================================================
  Channel 0 - 4-20mA Current Sensor Reading
================================================================================
  Time        Current (mA)    Status
--------------------------------------------------------------------------------
  14:23:45    12.345          OK
================================================================================
```

### Single Channel (Verbose)

```
================================================================================
  Channel 0 - 4-20mA Current Sensor Reading
================================================================================
  Time        Voltage (V)    Current (mA)    Status
--------------------------------------------------------------------------------
  14:23:45    3.0862         12.345          OK
================================================================================
```

### All Channels Output

```
================================================================================
  All Channels - 4-20mA Current Sensor Readings
================================================================================

  Time: 14:23:45
  Channel  Current (mA)  Status
  -------------------------------
  0        12.345        OK
  1        4.123         LOW
  2        19.876        HIGH
  3        0.234         FAULT
  4        8.765         OK
  ...
  15       16.543        OK
================================================================================
```

### Continuous Monitoring

```
================================================================================
  Channel 5 - 4-20mA Current Sensor Reading
================================================================================
  Time        Current (mA)    Status
--------------------------------------------------------------------------------
  14:23:45    12.345          OK
  14:23:46    12.346          OK
  14:23:47    12.344          OK
  14:23:48    12.347          OK
  ^C

  Monitoring stopped after 4 readings
================================================================================
```

## Status Indicators

| Status | Condition | Description |
|--------|-----------|-------------|
| **OK** | 4.5mA - 19.5mA | Normal operating range |
| **LOW** | 4.0mA - 4.5mA | At lower threshold |
| **HIGH** | 19.5mA - 20.0mA | At upper threshold |
| **FAULT** | < 3.5mA | Open circuit or sensor fault |
| **OVER-RANGE** | > 20.5mA | Sensor over-range or wiring fault |

## Troubleshooting

### Error: Failed to initialize cDAQ current slot

**Check:**
1. NI 9202 is installed in slot 1
2. cDAQ chassis is powered on
3. USB/network connection to cDAQ is working
4. NI-DAQmx drivers are installed
5. NI MAX can see the device (run `NI MAX` and check Devices and Interfaces)

### Readings show FAULT (< 4mA)

**Possible causes:**
1. Sensor not connected or powered
2. Open circuit in wiring
3. Shunt resistor not connected
4. Sensor malfunction

### Readings show OVER-RANGE (> 20mA)

**Possible causes:**
1. Incorrect shunt resistor value
2. Sensor malfunction
3. Wiring short circuit

### Voltage seems incorrect

**Verify:**
1. Shunt resistor value matches `CDAQ_CURRENT_SHUNT_RESISTOR` (250Ω)
2. Shunt resistor is in series with current loop
3. Use multimeter to verify voltage across shunt resistor

## Process Variable Conversion

If your 4-20mA sensor represents a process variable (e.g., temperature, pressure), convert using:

```c
// For 0-100% scale:
double percent = ((current_mA - 4.0) / 16.0) * 100.0;

// For custom range (e.g., 0-500°C):
double min_value = 0.0;
double max_value = 500.0;
double process_value = ((current_mA - 4.0) / 16.0) * (max_value - min_value) + min_value;
```

## Integration with Main Application

To use current sensors in your main Battery Exploder application:

```c
// In initialization:
if (CDAQ_InitializeCurrentSlot() != SUCCESS) {
    LogError("Failed to initialize current sensors");
}

// Read current:
double current_mA;
if (CDAQ_ReadCurrent(0, &current_mA) == SUCCESS) {
    LogMessage("Channel 0: %.2f mA", current_mA);
}

// In cleanup:
CDAQ_CleanupCurrentSlot();
```

## Authors

Maxwell Prisbrey, Nicolas Rasmont, Gabriel Meier

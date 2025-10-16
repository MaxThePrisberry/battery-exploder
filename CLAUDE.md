# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Battery Exploder** is a comprehensive battery testing system built in **LabWindows/CVI 2020 (C99)** for automated battery characterization and thermal runaway analysis. The system integrates multiple hardware instruments to provide battery testing capabilities including charge/discharge cycles, electrochemical impedance spectroscopy (EIS), temperature control, mass flow control, and comprehensive data logging.

**Developed by:** Maxwell Prisbrey, Nicolas Rasmont, and Gabriel Meier

## Build System

This is a LabWindows/CVI project:
- **Project File:** `BatteryExploder.prj`
- **UI File:** `BatteryExploder.uir`
- **Build:** Open project in LabWindows/CVI 2020 and use the IDE's build system
- **Target:** Windows executable (C99 standard)

There are no standalone build/test/lint commands - the project must be built within the LabWindows/CVI 2020 IDE.

## Hardware Architecture

The system interfaces with 6 main hardware devices:

1. **PSB 10000 Power Supply** - EA Elektro-Automatik bidirectional power supply (60V/60A derated)
   - Communication: Modbus RTU over RS232 (COM3, 9600 baud, Slave Address 1)
   - Files: `psb10000/psb10000_dll.h/c`, `psb10000/psb10000_queue.h/c`

2. **Bio-Logic SP-150e Potentiostat** - For electrochemical measurements
   - Communication: USB using Bio-Logic Development Package API
   - Files: `biologic/biologic_dll.h/c`, `biologic/biologic_queue.h/c`

3. **DTB4848 Temperature Controllers** - K-type thermocouple PID controllers
   - Communication: Modbus ASCII over RS232 (COM5, 9600 baud)
   - Supports multiple devices (default: 2 devices at slave addresses 2 and 3)
   - Files: `dtb4848/dtb4848_dll.h/c`, `dtb4848/dtb4848_queue.h/c`

4. **ALICAT Basis 2 Mass Flow Controller** - Gas flow control
   - Communication: Modbus RTU over RS232 (COM7, 38400 baud, Modbus Address 1)
   - Files: `alicat basis 2/alicat_dll.h/c`, `alicat basis 2/alicat_queue.h/c`

5. **Teensy Microcontroller** - Digital I/O control via Arduino-compatible board
   - Communication: Serial over USB (COM6, 9600 baud)
   - Files: `teensy/teensy_dll.h/c`, `teensy/teensy_queue.h/c`

6. **cDAQ-9178 System** - National Instruments data acquisition for temperature monitoring
   - Hardware: NI 9213 thermocouple input modules in slots 2 & 3 (32 channels total)
   - Files: `cdaq_utils.h/c`

## Core Architecture: Device Queue System

The heart of the system is a **thread-safe command queue system** that ensures reliable, sequential device communication. This is the most critical architectural pattern:

### Key Concepts

1. **Never call device DLL functions directly from UI callbacks** - Always use the queue system
2. **Priority-based queuing:**
   - `DEVICE_PRIORITY_HIGH = 0` - User commands (immediate)
   - `DEVICE_PRIORITY_NORMAL = 1` - Experiment operations
   - `DEVICE_PRIORITY_LOW = 2` - Status monitoring

3. **Two execution modes:**
   - **Blocking:** `*Queued()` functions - wait for completion
   - **Asynchronous:** `*Async()` functions - return immediately with callback

4. **Transaction support** - Atomic operations where all commands succeed or all fail

### Device Adapter Pattern

Each device implements a standardized `DeviceAdapter` interface with:
- Connection management (`connect`, `disconnect`, `testConnection`, `isConnected`)
- Command execution (`executeCommand`)
- Memory management (`createCommandParams`, `freeCommandParams`)

Generic queue: `device_queue.h/c`
Device-specific wrappers: `<device>_queue.h/c`

## Code Organization

```
battery_exploder/
├── BatteryExploder.c/h/uir    # Main application and UI
├── common.h/c                 # Shared definitions, configuration constants
├── device_queue.h/c           # Generic thread-safe queue system
├── battery_utils.h/c          # Battery calculations (capacity, efficiency)
├── logging.h/c                # Thread-safe logging with device prefixes
├── status.h/c                 # Continuous device status monitoring (1 Hz)
├── controls.h/c               # UI control management
├── cdaq_utils.h/c             # NI-DAQmx interface
├── cmd_prompt.h/c             # Debug console
│
├── biologic/                  # Bio-Logic SP-150e module
│   ├── biologic_dll.h/c       # Core API wrapper
│   ├── biologic_queue.h/c     # Queue management
│   └── BLStructs.h            # Bio-Logic data structures
│
├── psb10000/                  # PSB 10000 Power Supply module
│   ├── psb10000_dll.h/c       # Modbus RTU interface
│   └── psb10000_queue.h/c     # Queue management
│
├── dtb4848/                   # DTB4848 Temperature Controllers module
│   ├── dtb4848_dll.h/c        # Modbus ASCII interface
│   └── dtb4848_queue.h/c      # Multi-device queue management
│
├── alicat basis 2/            # ALICAT mass flow controller module
│   ├── alicat_dll.h/c         # Modbus RTU interface
│   └── alicat_queue.h/c       # Queue management
│
├── teensy/                    # Teensy microcontroller module
│   ├── teensy_dll.h/c         # Serial interface
│   └── teensy_queue.h/c       # Pin control queue
│
├── tests/                     # Test suites
│   ├── biologic_test.h/c      # Bio-Logic validation
│   ├── psb10000_test.h/c      # PSB validation
│   ├── device_queue_test.h/c  # Queue system tests
│   └── dtb_ramp_soak_test.h/c # DTB ramp-soak functionality tests
│
└── exp_temp_ramp.h/c          # Temperature ramp EIS experiment
```

## Current Experiments

### Temperature Ramp EIS Experiment (`exp_temp_ramp.h/c`)

Performs EIS measurements during controlled temperature ramping for thermal runaway analysis.

**Workflow:**
1. Reach initial temperature using DTB controllers
2. Stabilize for specified duration
3. Begin temperature ramping at controlled rate
4. Perform periodic EIS measurements using Bio-Logic
5. Hold at final temperature
6. Log all data (temperature profile, EIS measurements, mass flow)

**Key Features:**
- Option to continue or pause ramping during EIS measurements
- Multi-device temperature monitoring (DTB controllers + cDAQ thermocouples)
- ALICAT mass flow monitoring and control
- Automatic relay switching via Teensy for device isolation
- Comprehensive data logging with timestamped directories

## Configuration Constants

All located in `common.h`:

### Device Enable Flags
```c
#define ENABLE_PSB         1    // Enable PSB 10000
#define ENABLE_BIOLOGIC    1    // Enable Bio-Logic SP-150e
#define ENABLE_DTB         1    // Enable DTB4848
#define ENABLE_ALICAT      1    // Enable ALICAT flow controller
#define ENABLE_TNY         1    // Enable Teensy
#define ENABLE_CDAQ        1    // Enable cDAQ 9178
```

### COM Port Assignments
```c
#define PSB_COM_PORT       3
#define DTB_COM_PORT       5
#define ALICAT_COM_PORT    7
#define TNY_COM_PORT       6
```

### PSB Safety Limits (60V/60A derated version)
```c
#define PSB_NOMINAL_VOLTAGE    60.0   // V
#define PSB_NOMINAL_CURRENT    60.0   // A
#define PSB_NOMINAL_POWER      1200.0 // W
#define PSB_SAFE_VOLTAGE_MAX   61.2   // V (102% of nominal)
#define PSB_SAFE_CURRENT_MAX   61.2   // A (102% of nominal)
```

### DTB Configuration
```c
#define DTB_NUM_DEVICES     2
#define DTB1_SLAVE_ADDRESS  2
#define DTB2_SLAVE_ADDRESS  3
```

## DTB4848 Ramp-Soak (PID Program) Capabilities

The DTB4848 temperature controllers support **PID Program Control** (also known as Ramp-Soak) for automated temperature profiles. This feature has been fully implemented in the Battery Exploder system.

### Overview

The ramp-soak system allows complex, multi-step temperature sequences to be programmed into the DTB controllers. This is particularly useful for thermal characterization, aging tests, and thermal runaway experiments.

**Key Features:**
- **8 Patterns** × **8 Steps per pattern** = 64 total programmable temperature points
- **Pattern Chaining** - Link patterns together for complex sequences
- **Cycle Control** - Repeat patterns 0-99 times
- **Program Control** - Start, Stop, Hold, Resume capabilities
- **Status Monitoring** - Real-time program state and current step tracking

### Ramp-Soak Architecture

#### Pattern-Based Programming

Each **pattern** contains:
- Up to 8 **steps** (temperature + duration pairs)
- **Actual step count** (1-8)
- **Cycle count** (0-99 repetitions)
- **Link pattern** (0-7 to chain, 8 for end)

Each **step** contains:
- **Temperature setpoint** (in °C)
- **Time duration** (0-900 minutes)

#### Example Usage

**Simple 2-step ramp:**
```c
// Create a simple ramp from 25°C to 100°C over 60 minutes
int result = DTB_SetSimpleRamp(&handle, 25.0, 100.0, 60);
```

**Complex multi-step pattern:**
```c
// Create a pattern with 3 steps
DTB_Pattern pattern = {0};
pattern.actualStepCount = 3;
pattern.cycleCount = 2;  // Repeat twice
pattern.linkPattern = DTB_LINK_PATTERN_END;  // No chaining

pattern.steps[0].temperature = 25.0;
pattern.steps[0].timeMinutes = 10;

pattern.steps[1].temperature = 50.0;
pattern.steps[1].timeMinutes = 30;

pattern.steps[2].temperature = 75.0;
pattern.steps[2].timeMinutes = 20;

// Set the pattern
DTB_SetPattern(&handle, 0, &pattern);

// Set as start pattern and begin
DTB_SetStartPattern(&handle, 0);
DTB_StartProgram(&handle);
```

**Pattern chaining:**
```c
// Pattern 0: Heat to 50°C
DTB_Pattern pattern0 = {0};
pattern0.actualStepCount = 1;
pattern0.cycleCount = 1;
pattern0.linkPattern = 1;  // Link to pattern 1
pattern0.steps[0].temperature = 50.0;
pattern0.steps[0].timeMinutes = 30;

// Pattern 1: Hold at 50°C, then cool to 25°C
DTB_Pattern pattern1 = {0};
pattern1.actualStepCount = 2;
pattern1.cycleCount = 1;
pattern1.linkPattern = DTB_LINK_PATTERN_END;  // End of sequence
pattern1.steps[0].temperature = 50.0;
pattern1.steps[0].timeMinutes = 60;
pattern1.steps[1].temperature = 25.0;
pattern1.steps[1].timeMinutes = 45;

// Configure both patterns
DTB_SetPattern(&handle, 0, &pattern0);
DTB_SetPattern(&handle, 1, &pattern1);
DTB_SetStartPattern(&handle, 0);
```

### API Functions

#### Low-Level DLL Functions (dtb4848_dll.h/c)

**Pattern Management:**
```c
int DTB_SetPattern(DTB_Handle *handle, int patternNumber, const DTB_Pattern *pattern);
int DTB_GetPattern(DTB_Handle *handle, int patternNumber, DTB_Pattern *pattern);
int DTB_ClearPattern(DTB_Handle *handle, int patternNumber);
int DTB_ClearAllPatterns(DTB_Handle *handle);
```

**Step Management:**
```c
int DTB_SetStep(DTB_Handle *handle, int patternNumber, int stepNumber, const DTB_Step *step);
int DTB_GetStep(DTB_Handle *handle, int patternNumber, int stepNumber, DTB_Step *step);
```

**Pattern Configuration:**
```c
int DTB_SetActualStepCount(DTB_Handle *handle, int patternNumber, int stepCount);
int DTB_GetActualStepCount(DTB_Handle *handle, int patternNumber, int *stepCount);
int DTB_SetCycleCount(DTB_Handle *handle, int patternNumber, int cycleCount);
int DTB_GetCycleCount(DTB_Handle *handle, int patternNumber, int *cycleCount);
int DTB_SetLinkPattern(DTB_Handle *handle, int patternNumber, int linkPattern);
int DTB_GetLinkPattern(DTB_Handle *handle, int patternNumber, int *linkPattern);
```

**Program Control:**
```c
int DTB_SetStartPattern(DTB_Handle *handle, int patternNumber);
int DTB_GetStartPattern(DTB_Handle *handle, int *startPattern);
int DTB_StartProgram(DTB_Handle *handle);
int DTB_StopProgram(DTB_Handle *handle);
int DTB_HoldProgram(DTB_Handle *handle);
int DTB_ResumeProgram(DTB_Handle *handle);
int DTB_GetProgramStatus(DTB_Handle *handle, DTB_ProgramStatus *status);
```

**Convenience Functions:**
```c
int DTB_SetSimpleRamp(DTB_Handle *handle, double startTemp, double endTemp, int durationMinutes);
```

#### Queued Wrapper Functions (dtb4848_queue.h/c)

All functions have queued equivalents with `Queued` suffix that use the queue system:

```c
int DTB_SetPatternQueued(int slaveAddress, int patternNumber,
                        const DTB_Pattern *pattern, DevicePriority priority);
int DTB_GetPatternQueued(int slaveAddress, int patternNumber,
                        DTB_Pattern *pattern, DevicePriority priority);
int DTB_StartProgramQueued(int slaveAddress, DevicePriority priority);
int DTB_GetProgramStatusQueued(int slaveAddress, DTB_ProgramStatus *status,
                              DevicePriority priority);
// ... and all other functions
```

#### Atomic Transactions

For complex pattern configuration, use atomic transactions to ensure all settings are applied together:

```c
DTB_Pattern pattern = /* configure pattern */;

// Atomically configure all pattern settings
int result = DTB_ConfigurePatternAtomic(
    slaveAddress,
    patternNumber,
    &pattern,
    NULL,           // callback (optional)
    NULL,           // userData (optional)
    PRIORITY_NORMAL
);
```

This ensures that all steps, cycle count, and link pattern are set together in a single atomic operation.

### Data Structures

```c
// Individual temperature/time step
typedef struct {
    double temperature;    // Temperature setpoint (°C)
    int timeMinutes;      // Duration at this temperature (0-900 min)
} DTB_Step;

// Complete pattern definition
typedef struct {
    DTB_Step steps[DTB_MAX_STEPS_PER_PATTERN];  // Up to 8 steps
    int actualStepCount;                         // Number of active steps (1-8)
    int cycleCount;                              // Repeat cycles (0-99)
    int linkPattern;                             // Next pattern (0-7) or 8 for end
} DTB_Pattern;

// Program execution state
typedef enum {
    DTB_PROG_STATE_STOPPED = 0,
    DTB_PROG_STATE_RUNNING,
    DTB_PROG_STATE_PAUSED,
    DTB_PROG_STATE_COMPLETED
} DTB_ProgramState;

// Current program status
typedef struct {
    int isRunning;           // Program is executing
    int isPaused;            // Program is paused (hold)
    int currentPattern;      // Current pattern being executed
    int currentStep;         // Current step within pattern
    DTB_ProgramState state;  // Overall program state
} DTB_ProgramStatus;
```

### Constants and Limits

```c
#define DTB_MAX_PATTERNS              8    // Maximum number of patterns
#define DTB_MAX_STEPS_PER_PATTERN     8    // Maximum steps per pattern
#define DTB_LINK_PATTERN_END          8    // Value to end pattern chain
#define DTB_MIN_STEP_TIME             0    // Minimum step time (minutes)
#define DTB_MAX_STEP_TIME             900  // Maximum step time (minutes)
#define DTB_MIN_CYCLE_COUNT           0    // Minimum cycle count
#define DTB_MAX_CYCLE_COUNT           99   // Maximum cycle count
```

### Register Addresses (Modbus)

The implementation uses these DTB4848 Modbus registers:

```c
#define REG_START_PATTERN           0x1030  // Start pattern register
#define REG_ACTUAL_STEP_BASE        0x1040  // Base for step count (+ pattern number)
#define REG_CYCLE_COUNT_BASE        0x1050  // Base for cycle count (+ pattern number)
#define REG_LINK_PATTERN_BASE       0x1060  // Base for link pattern (+ pattern number)
#define REG_PATTERN_TEMP_BASE       0x2000  // Base for temperature values
#define REG_PATTERN_TIME_BASE       0x2080  // Base for time values

#define BIT_PROGRAM_STOP            0x0815  // Program stop bit
#define BIT_PROGRAM_HOLD            0x0816  // Program hold bit
```

### Testing

A comprehensive test suite is available in `tests/dtb_ramp_soak_test.h/c`:

```c
// Run all ramp-soak tests with hardware
DTBRampSoakTest_RunAll(1);

// Run validation tests only (no hardware required)
DTBRampSoakTest_RunAll(0);
```

**Test coverage:**
- Pattern set/get operations
- Step set/get operations
- Cycle and link pattern configuration
- Program control (start/stop/hold/resume)
- Status monitoring
- Simple ramp creation
- Pattern clearing
- Validation and error handling
- Queued commands
- Atomic transactions
- Multi-pattern chaining
- Edge cases

### Integration with Temperature Ramp Experiment

The ramp-soak capabilities can be used in the Temperature Ramp EIS experiment (`exp_temp_ramp.h/c`) to automate complex thermal profiles. The experiment module can:
1. Configure DTB patterns for the desired temperature profile
2. Start the ramp-soak program
3. Monitor program status during execution
4. Perform EIS measurements at programmed temperature points
5. Log the complete temperature profile

This provides more precise and repeatable temperature control compared to manual setpoint changes.

### ALICAT Configuration
```c
#define ALICAT_BAUD_RATE        38400
#define ALICAT_NUM_DEVICES      1
#define ALICAT_MODBUS_ADDRESS   1
```

## Error Handling

Error codes are organized by module:

```c
#define SUCCESS                 0

// Base error codes
#define ERR_BASE_SYSTEM        -1000   // System errors
#define ERR_BASE_BIOLOGIC      -2000   // Bio-Logic errors
#define ERR_BASE_PSB           -3000   // PSB errors
#define ERR_BASE_DTB           -8000   // DTB errors
#define ERR_BASE_TNY           -9000   // Teensy errors
#define ERR_BASE_ALICAT        -9500   // ALICAT errors
```

Use `GetErrorString(errorCode)` for human-readable error messages.

## Threading Model

**Critical threads:**
1. **UI Thread** - LabWindows/CVI main thread
2. **Device Queue Threads** - One per device for sequential command processing
3. **Experiment Threads** - Background threads for long-running experiments
4. **Status Monitor Thread** - Continuous device status checking (1 Hz)

**Thread Safety Rules:**
- Use queue system for all device communication
- Never call device DLL functions directly from UI callbacks
- Use `PostDeferredCall()` for UI updates from background threads
- Protect shared data with mutexes (`g_busyLock`, `g_systemBusy`)

## Logging System

Thread-safe logging with device-specific prefixes:

```c
// Device-specific logging
LogMessageEx(LOG_DEVICE_PSB, "Setting voltage to %.2f V", voltage);
LogErrorEx(LOG_DEVICE_BIO, "Failed to load technique: %s", errorStr);

// General logging
LogMessage("Experiment started successfully");
LogWarning("Battery temperature elevated: %.1f°C", temp);
```

## Adding New Devices

1. Create device DLL module: `<device>/<device>_dll.h/c`
2. Implement device adapter: `<device>/<device>_queue.h/c`
3. Add to `common.h`: enable flag, COM port, configuration constants
4. Add to `BatteryExploder.c`: initialize queue manager in `main()`
5. Add to `status.c`: status monitoring
6. Add cleanup in `PanelCallback()`

## Coding Style

**Naming conventions:**
- Functions: `ModuleName_FunctionName()` (e.g., `PSB_SetVoltage()`)
- Types: `ModuleName_TypeName` (e.g., `PSB_Status`)
- Constants: `MODULE_CONSTANT_NAME` (e.g., `PSB_NOMINAL_VOLTAGE`)
- Global variables: `g_variableName` (e.g., `g_mainPanelHandle`)

**Memory management:**
- Always check return values from device functions
- Free dynamically allocated memory
- Initialize structures with `memset()`
- Bio-Logic technique data must be freed: `BIO_FreeTechniqueData()`

**Thread safety:**
- Use device queues for all device operations
- Use `PostDeferredCall()` for UI updates from background threads
- Never mix direct device calls with queue operations

## Global Queue Managers

These are initialized in `main()` and available throughout the application:

```c
extern PSBQueueManager *g_psbQueueMgr;
extern BioQueueManager *g_bioQueueMgr;
extern DTBQueueManager *g_dtbQueueMgr;
extern ALICAT_QueueManager *g_alicatQueueMgr;
extern TNYQueueManager *g_tnyQueueMgr;
```

Access via global setters:
```c
PSB_SetGlobalQueueManager(g_psbQueueMgr);
BIO_SetGlobalQueueManager(g_bioQueueMgr);
// etc.
```

## Key Dependencies

**External Libraries:**
- LabWindows/CVI 2020 Runtime
- NIDAQmx (National Instruments)
- Bio-Logic Development Package
- Windows RS232 Libraries

**Internal Dependencies:**
- All device modules depend on `device_queue.h/c`
- All modules include `common.h` for shared definitions
- Experiment modules depend on device queue managers
- UI updates depend on `logging.h` for thread-safe operations

## Important Notes

- This system controls high-power equipment (60V/60A/1200W power supply)
- Safety limits are enforced in `common.h` - modify with extreme caution
- The Teensy relay system (pins 0 and 1) isolates PSB and Bio-Logic from battery
- Temperature monitoring is redundant (DTB controllers + cDAQ thermocouples)
- All device initialization includes safety checks and safe default states
- Emergency stop capabilities are built into experiments

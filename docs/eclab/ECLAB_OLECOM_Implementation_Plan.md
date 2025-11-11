# EC-Lab OLE COM Implementation Plan

**Date:** 2025-01-27
**Author:** Claude Code Planning Session
**Purpose:** Add EC-Lab OLE COM control as alternative to direct DLL control for BioLogic SP-150e

---

## Executive Summary

This document outlines a plan to integrate EC-Lab OLE COM automation as an alternative control method for the BioLogic SP-150e potentiostat. The current system uses direct DLL calls to the hardware; the new system will control the hardware through the EC-Lab software GUI via OLE COM automation.

**Key Decision:** We will use **pre-configured .mps settings files** (created in EC-Lab GUI) rather than generating them programmatically. This significantly simplifies implementation.

---

## Architecture Overview

### Current System (Direct DLL)
- Direct USB communication with SP-150e via ECLib DLL
- Low-level control: firmware loading, technique parameters, data buffers
- Thread-safe queue system (`biologic_queue.h/c`)
- Direct memory access to technique data structures

### New System (EC-Lab OLE COM)
- Software-mediated control through EC-Lab application
- File-based workflow: settings files (.mps) → run → data files (.mpr)
- Polling-based status monitoring (32-value array from MeasureStatus)
- Higher-level abstraction, validated by EC-Lab GUI

### Dual-Mode Architecture

```
┌─────────────────────────────────────────────────────┐
│         Experiment Layer (exp_temp_ramp.c)          │
│              (No changes required)                  │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│      BioLogic Abstraction Layer (NEW)               │
│      biologic_abstract.h/c                          │
│  - BIO_Abstract_RunOCV()                            │
│  - BIO_Abstract_RunPEIS()                           │
│  - BIO_Abstract_RunGEIS()                           │
└─────────────────────────────────────────────────────┘
                         │
          ┌──────────────┴──────────────┐
          ▼                             ▼
┌──────────────────────┐    ┌──────────────────────┐
│   Direct DLL Mode    │    │   EC-Lab OLE COM     │
│   (Current System)   │    │      Mode (NEW)      │
│                      │    │                      │
│  biologic_dll.c      │    │  biologic_eclab.c    │
│  biologic_queue.c    │    │  eclab_olecom.c      │
│         │            │    │         │            │
│         ▼            │    │         ▼            │
│   ECLib64.dll        │    │   EC-Lab.exe         │
│         │            │    │   (OLE COM Server)   │
│         ▼            │    │         │            │
│   SP-150e Hardware   │    │         ▼            │
│      via USB         │    │   SP-150e Hardware   │
└──────────────────────┘    └──────────────────────┘
```

---

## Implementation Phases

### Phase 1: Core OLE COM Infrastructure (2-3 days)
**Files:** `biologic/eclab_olecom.h`, `biologic/eclab_olecom.c`

Create low-level wrapper for EC-Lab OLE COM automation:

```c
// eclab_olecom.h - OLE COM wrapper

typedef struct {
    IDispatch *pECLab;           // OLE COM interface
    int deviceNumber;             // EC-Lab device index (0-based)
    int channelNumber;            // EC-Lab channel index (0-based)
    bool isConnected;
    char workingDir[MAX_PATH];   // Directory for data files
} ECLabConnection;

// Core OLE COM functions
int ECLAB_Initialize(ECLabConnection **conn, const char *workingDir);
int ECLAB_Shutdown(ECLabConnection *conn);
int ECLAB_RegisterServer(const char *eclabPath);

// Device management
int ECLAB_ConnectDevice(ECLabConnection *conn, int deviceNumber);
int ECLAB_DisconnectDevice(ECLabConnection *conn);
int ECLAB_TestConnection(ECLabConnection *conn);

// Experiment control - uses PRE-EXISTING .mps files
int ECLAB_LoadSettings(ECLabConnection *conn, int device, int channel,
                       const char *mpsFilePath);
int ECLAB_RunChannel(ECLabConnection *conn, int device, int channel,
                     const char *outputMprPath);
int ECLAB_StopChannel(ECLabConnection *conn, int device, int channel);

// Status monitoring
typedef struct {
    int status;              // 0=Stop, 1=Run, 2=Pause, 3=Sync, etc.
    int techniqueCode;       // Technique code (see manual annex 4.1)
    double time;             // Elapsed time (s)
    double ewe;              // Working electrode potential (V)
    double current;          // Current (A)
    double frequency;        // For EIS techniques (Hz)
    double zReal;            // Real impedance (Ohm)
    double zImag;            // Imaginary impedance (Ohm)
    int currentPointIndex;   // Current data point
    int totalPointIndex;     // Total points collected
    // Map all 32 status array values from MeasureStatus
} ECLAB_Status;

int ECLAB_MeasureStatus(ECLabConnection *conn, int device, int channel,
                        ECLAB_Status *status);

// Data retrieval from .mpr files
int ECLAB_MeasureNumberOfPoints(const char *mprPath, int *numPoints);
int ECLAB_MeasureDcValue(const char *mprPath, int dataIndex,
                         double *time, double *voltage, double *current);
int ECLAB_MeasureEisValue(const char *mprPath, int dataIndex,
                          double *time, double *freq, double *zReal, double *zImag);
int ECLAB_MeasureValueByCode(const char *mprPath, int varCode, int dataIndex,
                             double *value);
```

**Key implementation details:**
- Use Windows COM API (`CoCreateInstance`, `IDispatch::Invoke`)
- Map OLE COM VARIANTs to C data types
- Handle BSTR string conversions (wide strings)
- Implement proper COM reference counting and cleanup
- Error handling for COM HRESULTs

**OLE COM Functions to Wrap:**
(From manual section 3.2)
- `ConnectDevice(DeviceNumber: integer): Integer`
- `DisconnectDevice(DeviceNumber: integer): Integer`
- `TestConnection(DeviceNumber: integer): integer`
- `LoadSettings(Device, Channel, FileName: string): Integer`
- `RunChannel(Device, Channel, OutputFile: string): Integer`
- `StopChannel(Device, Channel): Integer`
- `MeasureStatus(Device, Channel, out StatusVariant): Integer`
- `MeasureNumberOfPoints(FileName: string): integer`
- `MeasureDcValue(FileName, DataIndex, out ArrayValue): Integer`
- `MeasureEisValue(FileName, DataIndex, out ArrayValue): Integer`
- `MeasureValueByCode(FileName, VarCode, DataIndex, out Data): Integer`

---

### Phase 2: High-Level EC-Lab Technique Interface (3-4 days)
**Files:** `biologic/biologic_eclab.h`, `biologic/biologic_eclab.c`

Mirror the existing `biologic_queue.h` API but using EC-Lab backend with **pre-configured .mps templates**:

```c
// biologic_eclab.h - EC-Lab implementation of BioLogic techniques

// Configuration for EC-Lab mode
typedef struct {
    char settingsDir[MAX_PATH];    // Directory containing .mps templates
    char dataDir[MAX_PATH];        // Directory for output .mpr files
    int deviceNumber;              // EC-Lab device number
    int channelNumber;             // EC-Lab channel number
} ECLAB_Config;

// Initialize EC-Lab backend
int BIO_ECLAB_Init(const ECLAB_Config *config);
void BIO_ECLAB_Shutdown(void);

// Run OCV using pre-configured .mps file
int BIO_ECLAB_RunOCV(const char *mpsFilePath,      // e.g., "settings/ocv_default.mps"
                     const char *outputMprPath,      // e.g., "data/ocv_20250127.mpr"
                     BIO_TechniqueData **result,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData,
                     volatile int *cancelled);

// Run PEIS using pre-configured .mps file
int BIO_ECLAB_RunPEIS(const char *mpsFilePath,
                      const char *outputMprPath,
                      BIO_TechniqueData **result,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData,
                      volatile int *cancelled);

// Run GEIS using pre-configured .mps file
int BIO_ECLAB_RunGEIS(const char *mpsFilePath,
                      const char *outputMprPath,
                      BIO_TechniqueData **result,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData,
                      volatile int *cancelled);

// Helper: Convert .mpr file to BIO_TechniqueData structure
int BIO_ECLAB_ConvertMprToTechniqueData(const char *mprPath,
                                        BIO_TechniqueType type,
                                        BIO_TechniqueData **data);
```

**Workflow for each technique:**

1. **Load Settings**: Call `ECLAB_LoadSettings()` with pre-configured .mps file path
2. **Start Measurement**: Call `ECLAB_RunChannel()` with output .mpr path
3. **Monitor Progress**: Poll `ECLAB_MeasureStatus()` every ~500ms
   - Call `progressCallback` with elapsed time and progress percentage
   - Check `*cancelled` flag for user abort request
   - Check timeout
4. **Wait for Completion**: When `status.status == 0` (stopped), measurement is complete
5. **Retrieve Data**: Read data from output .mpr file using `ECLAB_MeasureDcValue()` or `ECLAB_MeasureEisValue()`
6. **Convert Format**: Convert .mpr data to `BIO_TechniqueData` structure for compatibility
7. **Return**: Return data to caller

**Example Implementation:**

```c
int BIO_ECLAB_RunOCV(const char *mpsFilePath,
                     const char *outputMprPath,
                     BIO_TechniqueData **result,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData,
                     volatile int *cancelled)
{
    // 1. Load pre-configured settings
    int ret = ECLAB_LoadSettings(g_eclabConn, g_config.deviceNumber,
                                  g_config.channelNumber, mpsFilePath);
    if (ret != SUCCESS) return ret;

    // 2. Start measurement
    ret = ECLAB_RunChannel(g_eclabConn, g_config.deviceNumber,
                          g_config.channelNumber, outputMprPath);
    if (ret != SUCCESS) return ret;

    // 3. Monitor progress
    ECLAB_Status status;
    double startTime = Timer();

    while (1) {
        // Check cancellation
        if (cancelled && *cancelled) {
            ECLAB_StopChannel(g_eclabConn, g_config.deviceNumber,
                             g_config.channelNumber);
            return ERR_CANCELLED;
        }

        // Check timeout
        if ((Timer() - startTime) * 1000 > timeout_ms) {
            ECLAB_StopChannel(g_eclabConn, g_config.deviceNumber,
                             g_config.channelNumber);
            return ERR_TIMEOUT;
        }

        // Get status
        ret = ECLAB_MeasureStatus(g_eclabConn, g_config.deviceNumber,
                                 g_config.channelNumber, &status);
        if (ret != SUCCESS) return ret;

        // Call progress callback
        if (progressCallback) {
            progressCallback(status.time, status.totalPointIndex, userData);
        }

        // Check if complete (status == 0 means stopped)
        if (status.status == 0) {
            break;
        }

        Delay(0.5);  // Poll every 500ms
    }

    // 4. Convert .mpr data to BIO_TechniqueData
    ret = BIO_ECLAB_ConvertMprToTechniqueData(outputMprPath,
                                               BIO_TECHNIQUE_OCV, result);

    return ret;
}
```

---

### Phase 3: Abstraction Layer (1-2 days)
**Files:** `biologic/biologic_abstract.h`, `biologic/biologic_abstract.c`

Create unified interface that can route to either direct DLL or EC-Lab mode:

```c
// biologic_abstract.h - Unified BioLogic interface

typedef enum {
    BIO_MODE_DIRECT_DLL = 0,    // Current system (biologic_dll + queue)
    BIO_MODE_ECLAB_OLECOM = 1   // New EC-Lab OLE COM mode
} BIO_ControlMode;

// Configuration
typedef struct {
    BIO_ControlMode mode;

    // Direct DLL mode settings
    char deviceAddress[64];      // e.g., "USB0"
    uint8_t timeout;

    // EC-Lab mode settings
    char eclabSettingsDir[MAX_PATH];  // Directory with .mps templates
    char eclabDataDir[MAX_PATH];      // Directory for output .mpr files
    int eclabDeviceNumber;
    int eclabChannelNumber;

} BIO_Config;

// Initialization
int BIO_InitializeAbstract(const BIO_Config *config);
void BIO_ShutdownAbstract(void);
BIO_ControlMode BIO_GetCurrentMode(void);

// Unified technique functions (dispatch to appropriate backend)
int BIO_Abstract_RunOCV(uint8_t channel,
                       double duration_s,          // Ignored in EC-Lab mode
                       double sample_interval_s,   // Ignored in EC-Lab mode
                       double record_every_dE,     // Ignored in EC-Lab mode
                       double record_every_dT,     // Ignored in EC-Lab mode
                       int e_range,                // Ignored in EC-Lab mode
                       BIO_TechniqueData **result,
                       int timeout_ms,
                       BioTechniqueProgressCallback progressCallback,
                       void *userData,
                       volatile int *cancelled);

int BIO_Abstract_RunPEIS(uint8_t channel, /* params */,
                        BIO_TechniqueData **result, /* ... */);

int BIO_Abstract_RunGEIS(uint8_t channel, /* params */,
                        BIO_TechniqueData **result, /* ... */);
```

**Implementation Notes:**
- Check `config->mode` and dispatch to appropriate backend
- Both backends return same `BIO_TechniqueData` structure
- Transparent to calling code (exp_temp_ramp.c requires no changes)
- **In EC-Lab mode, technique parameters are ignored** - all settings come from .mps file

---

### Phase 4: Integration and Testing (1-2 days)

#### Configuration (common.h additions)

```c
// BioLogic control mode selection
#define BIOLOGIC_CONTROL_MODE  BIO_MODE_DIRECT_DLL  // or BIO_MODE_ECLAB_OLECOM

// EC-Lab OLE COM settings (only used if mode == BIO_MODE_ECLAB_OLECOM)
#define ECLAB_SETTINGS_DIR     "C:\\BatteryExploder\\eclab_settings"
#define ECLAB_DATA_DIR         "C:\\BatteryExploder\\eclab_data"
#define ECLAB_DEVICE_NUMBER    0
#define ECLAB_CHANNEL_NUMBER   0
#define ECLAB_EXECUTABLE_PATH  "C:\\Program Files (x86)\\EC-Lab\\ECLab.exe"

// .mps template filenames (place these in ECLAB_SETTINGS_DIR)
#define ECLAB_OCV_TEMPLATE     "ocv_default.mps"
#define ECLAB_PEIS_TEMPLATE    "peis_default.mps"
#define ECLAB_GEIS_TEMPLATE    "geis_default.mps"
```

#### Directory Structure

```
C:\BatteryExploder\
├── eclab_settings\           # Pre-configured .mps files
│   ├── ocv_default.mps       # Created in EC-Lab GUI
│   ├── peis_default.mps      # Created in EC-Lab GUI
│   └── geis_default.mps      # Created in EC-Lab GUI
│
└── eclab_data\               # Runtime output directory
    ├── exp_20250127_100129\  # Organized by experiment
    │   ├── ocv_001.mpr
    │   ├── eis_001.mpr
    │   └── eis_002.mpr
    └── ...
```

#### Initialization (main() in BatteryExploder.c)

```c
// Configure BioLogic control mode
BIO_Config bioConfig = {0};
bioConfig.mode = BIOLOGIC_CONTROL_MODE;

if (bioConfig.mode == BIO_MODE_DIRECT_DLL) {
    // Direct DLL mode (current system)
    strcpy(bioConfig.deviceAddress, BIOLOGIC_DEFAULT_ADDRESS);
    bioConfig.timeout = 5;
    g_bioQueueMgr = BIO_QueueInit(bioConfig.deviceAddress);
}
else if (bioConfig.mode == BIO_MODE_ECLAB_OLECOM) {
    // EC-Lab OLE COM mode
    strcpy(bioConfig.eclabSettingsDir, ECLAB_SETTINGS_DIR);
    strcpy(bioConfig.eclabDataDir, ECLAB_DATA_DIR);
    bioConfig.eclabDeviceNumber = ECLAB_DEVICE_NUMBER;
    bioConfig.eclabChannelNumber = ECLAB_CHANNEL_NUMBER;

    // Register EC-Lab as OLE COM server (one-time setup, needs admin rights)
    ECLAB_RegisterServer(ECLAB_EXECUTABLE_PATH);

    // Initialize EC-Lab control
    ECLAB_Config eclabCfg;
    strcpy(eclabCfg.settingsDir, ECLAB_SETTINGS_DIR);
    strcpy(eclabCfg.dataDir, ECLAB_DATA_DIR);
    eclabCfg.deviceNumber = ECLAB_DEVICE_NUMBER;
    eclabCfg.channelNumber = ECLAB_CHANNEL_NUMBER;

    BIO_ECLAB_Init(&eclabCfg);
}

BIO_InitializeAbstract(&bioConfig);
```

---

## User Workflow

### Setup (One-Time)

1. **Register EC-Lab as OLE COM Server:**
   - Open Command Prompt as Administrator
   - Navigate to EC-Lab installation directory
   - Run: `ECLab.exe /regserver`
   - No confirmation message (silent operation)

2. **Create .mps Template Files:**
   - Open EC-Lab GUI
   - Create OCV technique with desired parameters:
     - Duration, record interval, voltage range, etc.
   - Save as `ocv_default.mps` in `C:\BatteryExploder\eclab_settings\`
   - Repeat for PEIS technique → save as `peis_default.mps`
   - Repeat for GEIS technique → save as `geis_default.mps`

3. **Configure Battery Exploder:**
   - Edit `common.h`
   - Set `BIOLOGIC_CONTROL_MODE` to `BIO_MODE_ECLAB_OLECOM`
   - Verify `ECLAB_SETTINGS_DIR` and `ECLAB_DATA_DIR` paths
   - Rebuild project

### Runtime

1. **Start EC-Lab** (must be running for OLE COM)
2. **Run Battery Exploder** as normal
3. Battery Exploder will:
   - Connect to EC-Lab via OLE COM
   - Load appropriate .mps file for each technique
   - Run experiments through EC-Lab
   - Save output to .mpr files
   - Convert data to internal format
4. **Monitor in EC-Lab GUI** (optional)
   - Can watch experiments in real-time
   - See live graphs, status updates

---

## Technical Details

### EC-Lab OLE COM Status Array
(From manual section 3.2.9)

The `MeasureStatus` function returns an array of 32 values:

| Index | Variable | Description |
|-------|----------|-------------|
| 0 | Status | 0=Stop, 1=Run, 2=Pause, 3=Sync, 4=Stop_rec1, 5=Stop_rec2, 6=Pause_rec |
| 1 | Ox/Red | 0=Oxidation, 1=Reduction |
| 2 | OCV | 0=OCV, 1=Other |
| 3 | EIS | 0=EIS, 1=No EIS |
| 4 | Technique number | Index 0-19 |
| 5 | Technique code | See annex 4.1 (e.g., 11=OCV, 29=PEIS, 30=GEIS) |
| 6 | Sequence Number | Current sequence |
| 7 | Current loop iteration | See annex 4.3 |
| 8 | Current sequence within loop | See annex 4.3 |
| 9 | Loop experiment iteration | |
| 10 | Cycle number | For CV, EIS, VASP, CASP |
| 11-13 | Counter 1-3 | For CV, ECN, SPFC, PR |
| 14 | Buffer Size | |
| 15 | Time | Seconds |
| 16 | Ewe | Working electrode voltage (V) |
| 17 | Ece | Counter electrode voltage (V) |
| 18 | Eoc | Open circuit voltage (V) |
| 19 | I | Current (A) |
| 20 | Q-Q0 | Charge (A.h) |
| 21-22 | Aux1, Aux2 | Auxiliary inputs |
| 23 | Irange | Current range (A) |
| 24 | R Compensation | Ohm |
| 25 | Frequency | Hz |
| 26 | \|Z\| | Impedance magnitude (Ohm) |
| 27 | Current point index | |
| 28 | Total point index | |
| 29 | T° | Temperature (°C) |
| 30 | Safety limit | 0=OK, 1=Emax, 2=Emin, 3=I, 4=Q-Q0, 7-10=Stack limits |
| 31 | Connection | 0=OK, 1=Disconnected |

### Technique Codes
(From manual annex 4.1)

| Code | Name | Description |
|------|------|-------------|
| 11 | OCV | Open Circuit Voltage |
| 29 | PEIS | Potentio Electrochemical Impedance Spectroscopy |
| 30 | GEIS | Galvano Electrochemical Impedance Spectroscopy |

### Variable Codes for MeasureValueByCode
(From manual annex 4.4)

Common variable codes for data retrieval:
- 4: time/s
- 6: Ewe/V
- 8: I/mA
- 32: freq/Hz
- 36: |Z|/Ohm
- 37: Re(Z)/Ohm
- 38: -Im(Z)/Ohm

---

## Files to Create

```
biologic/
├── eclab_olecom.h           # Low-level OLE COM wrapper
├── eclab_olecom.c           # Windows COM implementation
├── biologic_eclab.h         # High-level EC-Lab techniques (uses .mps)
├── biologic_eclab.c         # Technique execution and data conversion
├── biologic_abstract.h      # Unified abstraction layer
└── biologic_abstract.c      # Mode switching and dispatch
```

---

## Development Roadmap

| Phase | Description | Duration | Files |
|-------|-------------|----------|-------|
| 1 | Core OLE COM Infrastructure | 2-3 days | eclab_olecom.h/c |
| 2 | High-Level Technique Interface | 3-4 days | biologic_eclab.h/c |
| 3 | Abstraction Layer | 1-2 days | biologic_abstract.h/c |
| 4 | Integration and Testing | 1-2 days | common.h, main() mods |
| **Total** | | **7-11 days** | |

---

## Testing Strategy

### Unit Tests
1. **OLE COM Connection**: Test connect/disconnect to EC-Lab
2. **Settings Loading**: Verify .mps files load correctly
3. **Channel Control**: Test run/stop channel operations
4. **Status Polling**: Verify status array parsing
5. **Data Retrieval**: Test reading .mpr files

### Integration Tests
1. **Single Technique Tests**:
   - Run OCV measurement in both modes, compare results
   - Run PEIS measurement in both modes, compare results
   - Run GEIS measurement in both modes, compare results

2. **Full Experiment Test**:
   - Run complete temperature ramp EIS experiment in both modes
   - Compare data quality, timing, reliability

3. **Stress Testing**:
   - Long-duration experiments (>1 hour)
   - Cancellation during measurement
   - Error handling (EC-Lab crash, disconnect, etc.)

### Validation
- Compare impedance spectra from both modes (should be identical)
- Verify data file formats are correctly converted
- Check memory leaks (COM object cleanup)

---

## Advantages and Trade-offs

### Advantages of EC-Lab OLE COM Mode

1. **Validated Settings**: EC-Lab GUI validates all parameters before running
2. **Debugging**: Can manually test techniques in EC-Lab before automation
3. **Simplified Setup**: No firmware loading, channel detection handled by EC-Lab
4. **Persistent Logs**: EC-Lab maintains experiment history
5. **GUI Monitoring**: Users can watch experiments in EC-Lab interface in real-time
6. **Settings Reuse**: Save/load validated experiment configurations
7. **No Binary Format Parsing**: .mpr files accessed via OLE COM functions
8. **Template-Based**: Easy to modify experiment parameters in GUI without code changes

### Disadvantages

1. **External Dependency**: Requires EC-Lab installed, running, and registered as OLE COM server
2. **File System Overhead**: Must manage .mps and .mpr files on disk
3. **Less Direct Control**: Cannot modify technique parameters on-the-fly during measurement
4. **Polling Overhead**: Must poll MeasureStatus() instead of direct hardware callbacks (more latency)
5. **Process Coupling**: EC-Lab crash or closure disrupts Battery Exploder operation
6. **Performance**: Slightly slower response time due to polling and inter-process communication
7. **Template Limitations**: Technique parameters fixed at .mps creation time (cannot be changed programmatically)

### Recommended Usage

- **Direct DLL (Default)**: For production testing, maximum performance, automated testing
- **EC-Lab OLE COM**: For debugging, validation, user-visible demonstrations, and when GUI monitoring is desired

---

## Key References

### Manual Sections
- **Section 2**: OLE COM Activation (registration process)
- **Section 3.1**: Functions List (all available OLE COM functions)
- **Section 3.2.9**: MeasureStatus (32-value status array)
- **Section 3.2.10-3.2.12**: Data retrieval functions
- **Annex 4.1**: Technique codes (11=OCV, 29=PEIS, 30=GEIS)
- **Annex 4.4**: Variable codes for MeasureValueByCode

### Current Implementation
- `biologic/biologic_dll.h/c`: Direct DLL implementation (reference for API design)
- `biologic/biologic_queue.h/c`: Queue-based wrapper (reference for thread safety)
- `exp_temp_ramp.h/c`: Temperature ramp experiment (will use abstraction layer)

---

## Notes and Considerations

### .mps File Format
- Format is unknown (likely XML or binary)
- **Solution**: Create templates in EC-Lab GUI, use as-is
- No need to parse or generate programmatically
- Users can easily modify in EC-Lab GUI

### .mpr File Access
- Binary format documented by Bio-Logic
- Access via OLE COM functions (MeasureDcValue, MeasureEisValue)
- Must convert to `BIO_TechniqueData` format for compatibility
- Handle both DC techniques (OCV, CA, CP) and EIS techniques (PEIS, GEIS)

### Thread Safety
- OLE COM calls should be made from same thread that initialized COM
- Use mutex protection if calling from multiple threads
- EC-Lab status polling can be done in background thread

### Error Handling
- COM HRESULT codes must be checked and converted to internal error codes
- EC-Lab may display error dialogs - can be suppressed with `EnableMessagesWindows(0)`
- Handle case where EC-Lab is not running or not registered

### Memory Management
- COM objects must be properly released (IDispatch->Release())
- BSTR strings must be freed with SysFreeString()
- `.mpr` data arrays must be freed after conversion

---

## Future Enhancements

1. **Automatic .mps Selection**: Based on technique parameters, automatically select best-matching template
2. **Dynamic Settings**: Generate .mps files on-the-fly (requires reverse-engineering format)
3. **Real-Time Modifications**: Use EC-Lab's advanced OLE COM functions to modify running experiments
4. **Multi-Channel Support**: Extend to support multi-channel experiments
5. **Data Streaming**: Retrieve data during measurement instead of waiting until complete
6. **Template Library**: Create library of common experiment configurations

---

## Conclusion

This implementation provides a **clean, maintainable dual-mode architecture** that preserves the existing direct DLL system while adding EC-Lab OLE COM as a powerful alternative. The abstraction layer ensures experiments work identically regardless of backend, and the use of pre-configured .mps templates significantly reduces implementation complexity while providing maximum flexibility for users.

The system can be easily switched between modes via configuration, allowing developers to choose the best approach for each use case: direct DLL for performance and automation, or EC-Lab OLE COM for validation, debugging, and user-visible demonstrations.

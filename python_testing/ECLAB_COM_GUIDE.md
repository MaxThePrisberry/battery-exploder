# EC-Lab OLE COM Communication Guide

**A Practical Guide to EC-Lab COM Interface Implementation**

This guide supplements the official "BT-Lab and EC-Lab OLE COM User Manual v7" with practical insights gained from testing and implementation. It documents critical findings, common pitfalls, and best practices for implementing EC-Lab COM communication in any language (Python, C, C++, etc.).

**Authors:** Battery Exploder Team
**Date:** 2025-11-06
**Based on:** EC-Lab OLE COM Interface testing with Bio-Logic SP-150e

---

## Table of Contents

1. [Critical Findings](#critical-findings)
2. [COM Interface Basics](#com-interface-basics)
3. [Connection Workflow](#connection-workflow)
4. [Channel Management](#channel-management)
5. [Loading Settings](#loading-settings)
6. [Running Measurements](#running-measurements)
7. [Status Monitoring](#status-monitoring)
8. [Data Retrieval](#data-retrieval)
9. [Common Pitfalls](#common-pitfalls)
10. [Complete EIS Workflow Example](#complete-eis-workflow-example)
11. [Implementation Notes for C](#implementation-notes-for-c)

---

## Critical Findings

### 🔴 **1. Channels Use 0-Based Indexing**

**Most Important Discovery:** EC-Lab channels use **0-based indexing**, not 1-based.

- ✅ **Channel 0** = First physical channel
- ❌ **Channel 1** = Second physical channel (may not exist on single-channel devices)

**Impact:**
- `LoadSettings(device=0, channel=1, file)` will **fail** on single-channel devices
- `LoadSettings(device=0, channel=0, file)` will **succeed**

**Test Results:**
```
LoadSettings(0, 1, "simple_eis.mps") → returned: 0 (FAILED, 0.2ms)
LoadSettings(0, 0, "simple_eis.mps") → returned: 1 (SUCCESS, 212.1ms)
```

**Recommendation:** Always start with channel 0 unless you have a multi-channel device.

---

### ⏱️ **2. Initialization Delay Required**

After calling `ConnectDevice()`, EC-Lab needs time to initialize the channel hardware before accepting `LoadSettings()`.

**Required Delay:** Minimum 2 seconds
**Recommended Delay:** 2-5 seconds for reliability

**Why:** EC-Lab must initialize:
- Hardware communication
- Channel amplifier configuration
- Device capability detection
- Internal state setup

**Without delay:**
- `LoadSettings()` returns 0 (failure)
- No error message or diagnostic information
- Channel may appear connected but not ready

**Implementation:**
```c
// After ConnectDevice succeeds
ConnectDevice(device_number);
Sleep(2000);  // Wait 2 seconds for channel initialization
LoadSettings(device_number, channel, settings_file);
```

---

### 🔍 **3. LoadSettings Provides No Error Diagnostics**

`LoadSettings()` only returns:
- **1** = Success
- **0** = Failure (no details about why)

**Possible failure causes:**
- Channel not fully initialized (timing issue)
- Invalid channel number (wrong channel for device)
- Settings file incompatible with hardware (wrong Irange, Bandwidth, etc.)
- Device/channel combination invalid
- File not found or corrupted
- Hardware not responding

**Workaround:** Use pre-flight diagnostics before `LoadSettings()`:

```c
// 1. Check channel availability
GetDeviceChannelList(device, &channel_array);
if (!channel_array[channel]) {
    // Channel not available
}

// 2. Get channel hardware info
GetChannelInfos(device, channel, &infos);
// Verify serial number, amplifier ID, options

// 3. Explicitly select channel
SelectDevice(device);
SelectChannel(device, channel);

// 4. NOW try LoadSettings
LoadSettings(device, channel, file);
```

---

### 📊 **4. Status Monitoring Array Format**

`MeasureStatus()` returns an array with the measurement state:

**Array Format:** `[state, technique_index, ...]`

**State Values:**
- **0 = STOP** - Measurement complete or not running
- **1 = RUN** - Measurement actively running
- **2 = PAUSE** - Measurement paused

**Important:** State 0 means the measurement has completed. Use this to detect when `RunChannel()` finishes.

**Polling Strategy:**
- Poll every 500ms (2 Hz) for responsive status updates
- Implement timeout (5-10 minutes) for safety
- Log state transitions for debugging

---

## COM Interface Basics

### CLSID and IID

```c
// EC-Lab COM Server
CLSID_EClabExe = {77FE5C93-42EE-4127-944B-5BA14FD33447}
IID_IEClabExe  = {642C68D2-85BD-494B-93EB-583CCBB11794}
```

### Interface Methods

The IEClabExe interface has 16 methods (vtable positions):

| Method # | Name | Purpose |
|----------|------|---------|
| 1 | ConnectDevice | Connect to a device |
| 2 | DisconnectDevice | Disconnect from device |
| 3 | MeasureDcValue | Read DC measurement data |
| 4 | MeasureEisValue | Read EIS measurement data |
| 5 | MeasureNumberOfPoints | Get data point count |
| 6 | GetDeviceChannelList | Check channel availability |
| 7 | LoadSettings | Load .mps settings file |
| 8 | RunChannel | Start measurement |
| 9 | StopChannel | Stop measurement |
| 10 | GetDataFileName | Get output file path |
| 11 | MeasureStatus | Check measurement status |
| 12 | TestConnection | Verify device connection |
| 13 | ConnectDeviceByIP | Connect via IP address |
| 14 | SelectDevice | Select device |
| 15 | SelectChannel | Select channel |
| 16 | GetChannelInfos | Get channel hardware info |

---

## Connection Workflow

### 1. Basic Connection Sequence

```c
// Step 1: Create COM interface to EC-Lab
// (EC-Lab must be running first!)
IEClabExe* interface = CreateCOMObject(CLSID_EClabExe, IID_IEClabExe);

// Step 2: Connect to device
int result = ConnectDevice(interface, device_number);
// Returns: 1 = success, 0 = failure

// Step 3: Wait for initialization (CRITICAL!)
Sleep(2000);  // 2 seconds minimum

// Step 4: Verify connection (optional)
int connected = TestConnection(interface, device_number);
```

### 2. Connection Return Values

All connection methods return:
- **1** = Success
- **0** = Failure

No detailed error codes are provided.

### 3. Prerequisites

Before connecting:
1. **EC-Lab must be running** - Start EC-Lab GUI first
2. **EC-Lab registered** - Run `ECLab.exe /regserver` as Administrator (one-time)
3. **Device powered on** - Physical device must be connected
4. **Device not in use** - Only one application can connect at a time

---

## Channel Management

### Channel Numbering

**Critical:** Channels use **0-based indexing**

```
Device with 1 channel:  Use channel 0
Device with 2 channels: Use channels 0 and 1
Device with 4 channels: Use channels 0, 1, 2, 3
```

### Check Channel Availability

```c
// GetDeviceChannelList returns 128-element boolean array
VARIANT channel_array;
GetDeviceChannelList(interface, device_number, &channel_array);

// Check if specific channel is available
bool channel_0_available = channel_array[0];  // TRUE/FALSE
bool channel_1_available = channel_array[1];  // TRUE/FALSE
```

**Use this before LoadSettings** to avoid "channel not available" failures.

### Get Channel Hardware Info

```c
// GetChannelInfos returns array: [serial_number, amplifier_id, options]
VARIANT infos;
GetChannelInfos(interface, device_number, channel, &infos);

char* serial_number = infos[0];
int amplifier_id = infos[1];
int options = infos[2];
```

**Use this to:**
- Verify hardware capabilities
- Check compatibility with settings file
- Log device information

### Explicit Channel Selection

```c
// Explicitly select device and channel before operations
SelectDevice(interface, device_number);
SelectChannel(interface, device_number, channel);

// Now perform operations
LoadSettings(interface, device_number, channel, file);
```

**When to use:** If LoadSettings fails, try explicit selection first.

---

## Loading Settings

### Basic Usage

```c
int result = LoadSettings(interface, device_number, channel, settings_file_path);
// Returns: 1 = success, 0 = failure
```

### Critical Success Factors

1. **Correct channel number** - Use 0 for first channel
2. **Initialization delay** - Wait 2+ seconds after ConnectDevice
3. **Valid .mps file** - File must exist and be compatible with hardware
4. **Channel available** - Verify with GetDeviceChannelList first

### LoadSettings Timing

**Observed timings:**
- **Failure (wrong channel):** 0.2ms - Instant rejection
- **Success (correct channel):** 200-300ms - Hardware configuration time

The timing difference is a strong indicator:
- < 1ms = Immediate failure (wrong channel, not initialized)
- 100-300ms = Normal hardware configuration time

### Recommended Implementation

```c
int LoadSettingsWithDiagnostics(IEClabExe* interface, int device, int channel, char* file) {
    // 1. Check channel availability
    VARIANT channels;
    GetDeviceChannelList(interface, device, &channels);
    if (!channels[channel]) {
        Log("ERROR: Channel %d not available", channel);
        return 0;
    }

    // 2. Explicitly select channel
    SelectDevice(interface, device);
    SelectChannel(interface, device, channel);

    // 3. Try loading settings
    int result = LoadSettings(interface, device, channel, file);
    if (result != 1) {
        Log("ERROR: LoadSettings failed");

        // 4. Fallback to channel 0 if not already tried
        if (channel != 0) {
            Log("Trying fallback to channel 0...");
            result = LoadSettings(interface, device, 0, file);
            if (result == 1) {
                Log("SUCCESS with channel 0");
                // Update channel for subsequent operations
                channel = 0;
            }
        }
    }

    return result;
}
```

---

## Running Measurements

### Start Measurement

```c
// RunChannel starts the measurement defined in loaded settings
char* output_file = "C:\\path\\to\\output.mpt";
int result = RunChannel(interface, device_number, channel, output_file);
// Returns: 1 = success, 0 = failure
```

**Important:**
- Settings must be loaded first with `LoadSettings()`
- Output file will be created by EC-Lab
- File format: `.mpt` (Bio-Logic's text data format)
- RunChannel returns immediately - measurement runs asynchronously

### Stop Measurement

```c
int result = StopChannel(interface, device_number, channel);
// Returns: 1 = success, 0 = failure
```

**Use cases:**
- User-requested abort
- Timeout condition
- Error detected during measurement
- Emergency stop

---

## Status Monitoring

### Polling Measurement Status

```c
VARIANT status;
int result = MeasureStatus(interface, device_number, channel, &status);

// Status array format: [state, technique_index, ...]
int state = status[0];

switch (state) {
    case 0:  // STOP - Measurement complete
        Log("Measurement complete");
        break;
    case 1:  // RUN - Measurement active
        Log("Measurement running...");
        break;
    case 2:  // PAUSE - Measurement paused
        Log("Measurement paused");
        break;
}
```

### Recommended Monitoring Loop

```c
void MonitorMeasurement(IEClabExe* interface, int device, int channel, int timeout_seconds) {
    time_t start_time = time(NULL);
    int last_state = -1;
    bool complete = false;

    while (!complete) {
        // Poll every 500ms
        Sleep(500);

        VARIANT status;
        MeasureStatus(interface, device, channel, &status);
        int state = status[0];

        // Log state changes
        if (state != last_state) {
            switch (state) {
                case 0:
                    Log("Status: STOPPED - Measurement complete");
                    complete = true;
                    break;
                case 1:
                    Log("Status: RUNNING...");
                    break;
                case 2:
                    Log("Status: PAUSED");
                    break;
            }
            last_state = state;
        }

        // Timeout protection
        if (time(NULL) - start_time > timeout_seconds) {
            Log("WARNING: Measurement timeout - stopping channel");
            StopChannel(interface, device, channel);
            break;
        }
    }
}
```

**Polling frequency:**
- **500ms (2 Hz):** Good balance of responsiveness and CPU usage
- **1000ms (1 Hz):** Conservative, lower overhead
- **< 100ms:** Not recommended, excessive overhead

---

## Data Retrieval

### Check Data Point Count

```c
char* data_file = "C:\\path\\to\\output.mpt";
int num_points = MeasureNumberOfPoints(interface, data_file);

Log("Measurement contains %d data points", num_points);
```

### Read EIS Data Points

```c
// For EIS measurements, use MeasureEisValue
for (int i = 0; i < num_points; i++) {
    VARIANT data;
    int result = MeasureEisValue(interface, data_file, i, &data);

    if (result == 1) {
        // EIS data format: [frequency, Z_real, Z_imag, ...]
        double frequency = data[0];   // Hz
        double z_real = data[1];      // Ohms (real impedance)
        double z_imag = data[2];      // Ohms (imaginary impedance)

        Log("Point %d: f=%.3f Hz, Z_re=%.6f Ω, Z_im=%.6f Ω",
            i, frequency, z_real, z_imag);
    }
}
```

### Read DC Data Points

```c
// For DC measurements (OCV, CV, etc.), use MeasureDcValue
for (int i = 0; i < num_points; i++) {
    VARIANT data;
    int result = MeasureDcValue(interface, data_file, i, &data);

    if (result == 1) {
        // DC data format varies by technique
        // Typical: [time, voltage, current, ...]
        double time = data[0];      // seconds
        double voltage = data[1];   // V
        double current = data[2];   // A

        Log("Point %d: t=%.3f s, V=%.6f V, I=%.6f A",
            i, time, voltage, current);
    }
}
```

### Data File Formats

**Output files (.mpt):**
- Text-based format
- Tab or comma separated
- Header with metadata
- Column definitions
- Data rows

**Can also parse directly** instead of using MeasureEisValue/MeasureDcValue if preferred.

---

## Common Pitfalls

### ❌ Pitfall 1: Wrong Channel Number

**Problem:**
```c
LoadSettings(interface, 0, 1, "settings.mps");  // FAILS on single-channel device
```

**Solution:**
```c
LoadSettings(interface, 0, 0, "settings.mps");  // Use channel 0
```

**Symptom:** LoadSettings returns 0 instantly (< 1ms)

---

### ❌ Pitfall 2: No Initialization Delay

**Problem:**
```c
ConnectDevice(interface, 0);
LoadSettings(interface, 0, 0, "settings.mps");  // Fails - too fast!
```

**Solution:**
```c
ConnectDevice(interface, 0);
Sleep(2000);  // REQUIRED delay
LoadSettings(interface, 0, 0, "settings.mps");
```

**Symptom:** LoadSettings returns 0, but works after waiting

---

### ❌ Pitfall 3: EC-Lab Not Running

**Problem:**
```c
IEClabExe* interface = CreateCOMObject(...);  // FAILS
```

**Solution:**
1. Start EC-Lab GUI manually first
2. Or programmatically launch: `system("start ECLab.exe")`
3. Wait for EC-Lab to initialize (~5 seconds)
4. Then connect COM interface

**Symptom:** COM object creation fails with HRESULT error

---

### ❌ Pitfall 4: Assuming LoadSettings Errors Explain Themselves

**Problem:**
```c
int result = LoadSettings(...);
if (result == 0) {
    // Why did it fail? No information available!
}
```

**Solution:** Use diagnostic checks BEFORE LoadSettings
```c
// Check channel availability first
VARIANT channels;
GetDeviceChannelList(interface, device, &channels);
if (!channels[desired_channel]) {
    Log("Channel %d not available!", desired_channel);
}

// Check channel hardware
VARIANT infos;
GetChannelInfos(interface, device, desired_channel, &infos);
Log("Channel info: SN=%s, Amp=%d", infos[0], infos[1]);

// NOW try LoadSettings with better context
```

---

### ❌ Pitfall 5: Not Checking Measurement Completion

**Problem:**
```c
RunChannel(interface, device, channel, "output.mpt");
// Immediately try to read data - file may be incomplete!
int num_points = MeasureNumberOfPoints(interface, "output.mpt");
```

**Solution:**
```c
RunChannel(interface, device, channel, "output.mpt");

// Monitor until complete
VARIANT status;
while (true) {
    Sleep(500);
    MeasureStatus(interface, device, channel, &status);
    if (status[0] == 0) {  // STOP state
        break;  // Measurement complete
    }
}

// NOW safe to read data
int num_points = MeasureNumberOfPoints(interface, "output.mpt");
```

---

### ❌ Pitfall 6: Forgetting to Disconnect

**Problem:**
```c
// Program crashes or exits without disconnect
// Device left in connected state
// Next run may fail to connect
```

**Solution:**
```c
// Always disconnect in cleanup
void Cleanup(IEClabExe* interface, int device) {
    DisconnectDevice(interface, device);
    // Release COM interface
    interface->Release();
}

// Use try/finally or atexit() to ensure cleanup
```

---

## Complete EIS Workflow Example

### Full Implementation in Pseudocode

```c
int RunEISMeasurement(char* settings_file, char* output_file) {
    IEClabExe* interface = NULL;
    int device = 0;
    int channel = 0;  // Use channel 0 (0-based indexing)

    try {
        // ================================================================
        // Step 1: Create COM connection to EC-Lab
        // ================================================================
        Log("Connecting to EC-Lab COM server...");
        interface = CreateCOMObject(CLSID_EClabExe, IID_IEClabExe);
        if (!interface) {
            Log("ERROR: EC-Lab not running or not registered");
            return -1;
        }
        Log("SUCCESS: COM connection established");

        // ================================================================
        // Step 2: Connect to device
        // ================================================================
        Log("Connecting to device %d...", device);
        int result = ConnectDevice(interface, device);
        if (result != 1) {
            Log("ERROR: ConnectDevice failed");
            return -1;
        }
        Log("SUCCESS: Device connected");

        // ================================================================
        // Step 2.5: CRITICAL - Wait for channel initialization
        // ================================================================
        Log("Waiting for channel initialization...");
        Sleep(2000);  // 2 seconds minimum
        Log("Initialization complete");

        // ================================================================
        // Step 3: Diagnostic checks (optional but recommended)
        // ================================================================
        Log("Running diagnostic checks...");

        // Check channel availability
        VARIANT channels;
        GetDeviceChannelList(interface, device, &channels);
        if (!channels[channel]) {
            Log("ERROR: Channel %d not available", channel);
            return -1;
        }
        Log("Channel %d is available", channel);

        // Get channel hardware info
        VARIANT infos;
        GetChannelInfos(interface, device, channel, &infos);
        Log("Channel info: SN=%s, Amplifier=%d", infos[0], infos[1]);

        // ================================================================
        // Step 4: Load settings
        // ================================================================
        Log("Loading settings: %s", settings_file);
        result = LoadSettings(interface, device, channel, settings_file);
        if (result != 1) {
            Log("ERROR: LoadSettings failed");
            return -1;
        }
        Log("SUCCESS: Settings loaded");

        // ================================================================
        // Step 5: Run measurement
        // ================================================================
        Log("Starting EIS measurement...");
        Log("Output file: %s", output_file);
        result = RunChannel(interface, device, channel, output_file);
        if (result != 1) {
            Log("ERROR: RunChannel failed");
            return -1;
        }
        Log("SUCCESS: Measurement started");

        // ================================================================
        // Step 6: Monitor measurement status
        // ================================================================
        Log("Monitoring measurement progress...");
        time_t start_time = time(NULL);
        int last_state = -1;
        bool complete = false;
        int timeout = 300;  // 5 minutes

        while (!complete) {
            Sleep(500);  // Poll every 500ms

            VARIANT status;
            MeasureStatus(interface, device, channel, &status);
            int state = status[0];

            if (state != last_state) {
                switch (state) {
                    case 0:
                        Log("Status: STOPPED - Measurement complete");
                        complete = true;
                        break;
                    case 1:
                        Log("Status: RUNNING...");
                        break;
                    case 2:
                        Log("Status: PAUSED");
                        break;
                }
                last_state = state;
            }

            // Timeout protection
            if (time(NULL) - start_time > timeout) {
                Log("WARNING: Measurement timeout - stopping");
                StopChannel(interface, device, channel);
                return -1;
            }
        }

        double elapsed = difftime(time(NULL), start_time);
        Log("Measurement completed in %.1f seconds", elapsed);

        // ================================================================
        // Step 7: Read and verify data
        // ================================================================
        Log("Reading measurement data...");
        int num_points = MeasureNumberOfPoints(interface, output_file);
        Log("Total data points: %d", num_points);

        if (num_points > 0) {
            Log("Reading first 3 data points:");
            for (int i = 0; i < min(3, num_points); i++) {
                VARIANT data;
                result = MeasureEisValue(interface, output_file, i, &data);
                if (result == 1) {
                    double freq = data[0];
                    double z_real = data[1];
                    double z_imag = data[2];
                    Log("  Point %d: f=%.3f Hz, Z_re=%.6f Ω, Z_im=%.6f Ω",
                        i, freq, z_real, z_imag);
                }
            }
        }

        Log("SUCCESS: EIS data saved to %s", output_file);

        // ================================================================
        // Step 8: Disconnect
        // ================================================================
        Log("Disconnecting device...");
        DisconnectDevice(interface, device);
        Log("SUCCESS: Device disconnected");

        return 0;  // Success

    } catch (...) {
        Log("ERROR: Exception occurred");
        return -1;

    } finally {
        // Cleanup
        if (interface) {
            DisconnectDevice(interface, device);
            interface->Release();
        }
    }
}
```

---

## Implementation Notes for C

### Output Parameters in C

In C, output parameters are passed as pointers:

```c
// Python (comtypes - automatic return):
channel_array = interface.GetDeviceChannelList(device)

// C (explicit pointer parameter):
VARIANT channel_array;
GetDeviceChannelList(interface, device, &channel_array);
```

### VARIANT Handling

```c
// Declare VARIANT
VARIANT result;
VariantInit(&result);

// Call method with out parameter
int ret = SomeMethod(interface, param1, &result);

// Access data (depends on type)
if (result.vt == VT_I4) {
    int value = result.lVal;
}
else if (result.vt == (VT_ARRAY | VT_VARIANT)) {
    SAFEARRAY* array = result.parray;
    // Access array elements
}

// Always clear VARIANT when done
VariantClear(&result);
```

### COM Interface Creation

```c
// Initialize COM
CoInitialize(NULL);

// Create COM object
IEClabExe* interface = NULL;
HRESULT hr = CoCreateInstance(
    &CLSID_EClabExe,
    NULL,
    CLSCTX_LOCAL_SERVER,
    &IID_IEClabExe,
    (void**)&interface
);

if (FAILED(hr)) {
    // EC-Lab not running or not registered
    return -1;
}

// Use interface...

// Release and cleanup
interface->lpVtbl->Release(interface);
CoUninitialize();
```

### Method Calls via VTable

```c
// Method calls go through vtable
int result = interface->lpVtbl->ConnectDevice(interface, device_number);
int result = interface->lpVtbl->LoadSettings(interface, device, channel, file);
int result = interface->lpVtbl->RunChannel(interface, device, channel, output);

// Always pass interface pointer as first parameter
```

### String Parameters

```c
// EC-Lab expects BSTR (wide character strings)
BSTR settings_file = SysAllocString(L"C:\\path\\to\\settings.mps");
int result = LoadSettings(interface, device, channel, settings_file);
SysFreeString(settings_file);
```

### Error Handling

```c
// Check return values
int result = ConnectDevice(interface, device);
if (result != 1) {
    // All methods return 1 for success, 0 for failure
    fprintf(stderr, "ConnectDevice failed\n");
    return -1;
}

// Check HRESULT for COM errors
HRESULT hr = CoCreateInstance(...);
if (FAILED(hr)) {
    fprintf(stderr, "COM error: 0x%08X\n", hr);
    return -1;
}
```

---

## Testing Recommendations

### Incremental Testing Strategy

1. **Test 1: COM Connection**
   - Create COM object
   - Verify EC-Lab is accessible
   - Release object

2. **Test 2: Device Connection**
   - Connect to device
   - Verify TestConnection returns 1
   - Disconnect

3. **Test 3: Channel Diagnostics**
   - Get channel list
   - Get channel infos
   - Log all available channels

4. **Test 4: Load Settings**
   - Try with initialization delay
   - Test with channel 0 and channel 1
   - Compare timing of success vs failure

5. **Test 5: Status Monitoring**
   - Load settings
   - Start measurement
   - Monitor status until complete

6. **Test 6: Data Reading**
   - Run complete measurement
   - Wait for completion
   - Read and verify data points

7. **Test 7: Full Workflow**
   - Run complete end-to-end test
   - Verify all steps succeed
   - Check data integrity

### Logging Best Practices

```c
// Log all COM operations with timestamps
void LogOperation(const char* operation, int result) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", t);

    fprintf(log_file, "[%s] %s: %s\n",
            timestamp,
            operation,
            result == 1 ? "SUCCESS" : "FAILED");
}

// Usage:
int result = ConnectDevice(interface, device);
LogOperation("ConnectDevice", result);
```

---

## Summary Checklist

Before implementing EC-Lab COM communication, ensure you:

- [ ] **Use channel 0** for first channel (0-based indexing)
- [ ] **Wait 2+ seconds** after ConnectDevice before LoadSettings
- [ ] **Check channel availability** with GetDeviceChannelList before LoadSettings
- [ ] **Use diagnostic functions** (GetChannelInfos, SelectChannel) for troubleshooting
- [ ] **Monitor measurement status** with MeasureStatus (poll every 500ms)
- [ ] **Check state = 0** to detect measurement completion
- [ ] **Implement timeout protection** (5-10 minutes recommended)
- [ ] **Always disconnect** in cleanup/finally blocks
- [ ] **Log all operations** with timestamps for debugging
- [ ] **Handle VARIANT types** correctly (arrays, integers, strings)
- [ ] **Use BSTR strings** for file paths
- [ ] **Check return values** (1 = success, 0 = failure)
- [ ] **Start EC-Lab first** before creating COM objects
- [ ] **Register EC-Lab** with /regserver if first time

---

## Additional Resources

- **EC-Lab OLE COM User Manual v7** - Official documentation
- **Python Test Implementation** - `eclab_simple_load_test.py` (reference implementation)
- **Test Results** - `results/simple_load_test_*.log` (actual test data)
- **Bio-Logic Development Package** - Native C/C++ API (alternative to COM)

---

## Document Change History

| Date | Author | Changes |
|------|--------|---------|
| 2025-11-06 | Battery Exploder Team | Initial version based on Python testing |

---

**End of Guide**

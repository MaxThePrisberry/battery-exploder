# NI 9472 Digital Output Module - Usage Guide

## Overview

The NI 9472 is an 8-channel sourcing digital output module for the NI cDAQ-9178 chassis. This implementation provides thread-safe control of solenoid valves and other digital actuators using the queue/adapter pattern consistent with other devices in the Battery Exploder system.

**Module Specifications:**
- **Channels:** 8 sourcing digital outputs (line0-line7)
- **Output Voltage:** 5-30V DC (typical 24V for solenoids)
- **Output Current:** Up to 1A per channel
- **Switching Speed:** High-speed digital switching
- **Isolation:** Channel-to-earth isolation

## Architecture

The NI 9472 module follows the standard device queue architecture:

```
┌─────────────────────────────────────┐
│     Application / UI / Experiments   │
└────────────────┬────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────┐
│  NI9472_*Queued() Wrapper Functions │  ◄── Thread-safe interface
│  (ni9472_queue.h)                   │
└────────────────┬────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────┐
│  Generic Device Queue Manager       │  ◄── Priority queuing
│  (device_queue.h)                   │      Transaction support
└────────────────┬────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────┐
│  NI9472 Device Adapter              │  ◄── Command execution
│  (ni9472_queue.c)                   │
└────────────────┬────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────┐
│  NI9472 DLL Functions               │  ◄── Low-level NI-DAQmx
│  (ni9472_dll.h/c)                   │
└────────────────┬────────────────────┘
                 │
                 ▼
         ┌───────────────┐
         │   NI-DAQmx    │
         │   Hardware    │
         └───────────────┘
```

## Files

- **ni9472/ni9472_dll.h** - Low-level DLL interface declarations
- **ni9472/ni9472_dll.c** - Low-level NI-DAQmx implementation
- **ni9472/ni9472_queue.h** - Queue manager interface declarations
- **ni9472/ni9472_queue.c** - Queue manager and adapter implementation

## Configuration

### 1. Enable the Module (common.h)

```c
#define ENABLE_NI9472      1    // Enable NI 9472 digital output module
#define NI9472_SLOT        4    // cDAQ slot number (change as needed)
```

**Important:** Set `NI9472_SLOT` to match the physical slot where your NI 9472 is installed in the cDAQ-9178 chassis.

### 2. Initialize in Your Application

```c
#include "ni9472/ni9472_queue.h"

// Global queue manager pointer
NI9472_QueueManager *g_ni9472QueueMgr = NULL;

// In your main() or initialization function:
int InitializeNI9472(void) {
    #if ENABLE_NI9472
    LogMessage("Initializing NI 9472 Digital Output Module...");

    // Create queue manager for the configured slot
    g_ni9472QueueMgr = NI9472_QueueInit(NI9472_SLOT);
    if (!g_ni9472QueueMgr) {
        LogError("Failed to initialize NI 9472 queue manager");
        return ERR_NOT_INITIALIZED;
    }

    // Set as global queue manager
    NI9472_SetGlobalQueueManager(g_ni9472QueueMgr);

    LogMessage("NI 9472 initialized successfully on slot %d", NI9472_SLOT);
    #endif

    return SUCCESS;
}

// In your cleanup function:
void CleanupNI9472(void) {
    #if ENABLE_NI9472
    if (g_ni9472QueueMgr) {
        NI9472_QueueShutdown(g_ni9472QueueMgr);
        g_ni9472QueueMgr = NULL;
    }
    #endif
}
```

## Basic Usage

### Single Channel Control

```c
// Open solenoid valve on channel 3 (HIGH priority for safety)
int result = NI9472_SetChannelQueued(3, NI9472_STATE_HIGH, DEVICE_PRIORITY_HIGH);
if (result != SUCCESS) {
    LogError("Failed to open valve on channel 3: %s", GetErrorString(result));
}

// Close solenoid valve
result = NI9472_SetChannelQueued(3, NI9472_STATE_LOW, DEVICE_PRIORITY_NORMAL);
```

### Multiple Channel Control

```c
// Open valves on channels 0, 2, and 5 simultaneously
int channels[] = {0, 2, 5};
int states[] = {NI9472_STATE_HIGH, NI9472_STATE_HIGH, NI9472_STATE_HIGH};
int count = 3;

int result = NI9472_SetMultipleChannelsQueued(channels, states, count,
                                               DEVICE_PRIORITY_NORMAL);
```

### Bit Pattern Control

```c
// Set all channels using an 8-bit pattern
// Bit 0 = channel 0, Bit 7 = channel 7
// Example: 0b00101001 = channels 0, 3, 5 HIGH, others LOW
uInt8 pattern = 0x29;  // Binary: 00101001

int result = NI9472_SetAllChannelsQueued(pattern, DEVICE_PRIORITY_NORMAL);
```

### Read Channel State

```c
// Get current state of channel 2
int state;
int result = NI9472_GetChannelStateQueued(2, &state, DEVICE_PRIORITY_NORMAL);
if (result == SUCCESS) {
    LogMessage("Channel 2 is %s", state ? "HIGH" : "LOW");
}

// Get all channel states as a bit pattern
uInt8 pattern;
result = NI9472_GetAllChannelsQueued(&pattern, DEVICE_PRIORITY_NORMAL);
if (result == SUCCESS) {
    LogMessage("Channel states: 0x%02X", pattern);
}
```

## Advanced Usage

### Atomic Transactions

Use transactions when you need multiple channels to change atomically (all succeed or all fail):

```c
// Example: Open inlet valve, close outlet valve atomically
NI9472_ChannelState sequence[] = {
    {.channel = 0, .state = NI9472_STATE_HIGH},  // Open inlet
    {.channel = 1, .state = NI9472_STATE_LOW},   // Close outlet
};

int result = NI9472_SetChannelsAtomic(sequence, 2, DEVICE_PRIORITY_HIGH, NULL, NULL);
if (result == SUCCESS) {
    LogMessage("Gas flow switched successfully");
} else {
    LogError("Failed to switch gas flow: %s", GetErrorString(result));
}
```

### Transaction with Callback

```c
// Callback function executed when transaction completes
void OnValveSequenceComplete(void *userData, int success, int errorCode) {
    if (success) {
        LogMessage("Valve sequence completed successfully");
    } else {
        LogError("Valve sequence failed: %s", GetErrorString(errorCode));
    }
}

// Set up a complex valve sequence with callback
NI9472_ChannelState purgeSequence[] = {
    {.channel = 0, .state = NI9472_STATE_LOW},   // Close inlet
    {.channel = 3, .state = NI9472_STATE_HIGH},  // Open purge
    {.channel = 4, .state = NI9472_STATE_HIGH},  // Open exhaust
};

result = NI9472_SetChannelsAtomic(purgeSequence, 3, DEVICE_PRIORITY_NORMAL,
                                  OnValveSequenceComplete, NULL);
```

### Manual Transactions

For more complex control sequences:

```c
// Begin transaction
TransactionHandle txn = NI9472_QueueBeginTransaction(g_ni9472QueueMgr);
if (txn == 0) {
    LogError("Failed to begin transaction");
    return ERR_OPERATION_FAILED;
}

// Set transaction priority
DeviceQueue_SetTransactionPriority(g_ni9472QueueMgr, txn, DEVICE_PRIORITY_HIGH);

// Add commands to transaction
NI9472_CommandParams params;

params.setChannel.channel = 0;
params.setChannel.state = NI9472_STATE_HIGH;
NI9472_QueueAddToTransaction(g_ni9472QueueMgr, txn, NI9472_CMD_SET_CHANNEL, &params);

params.setChannel.channel = 1;
params.setChannel.state = NI9472_STATE_LOW;
NI9472_QueueAddToTransaction(g_ni9472QueueMgr, txn, NI9472_CMD_SET_CHANNEL, &params);

// Commit transaction
int result = NI9472_QueueCommitTransaction(g_ni9472QueueMgr, txn, NULL, NULL);
if (result != SUCCESS) {
    LogError("Transaction failed: %s", GetErrorString(result));
}
```

## Priority Levels

Commands can be queued with different priorities:

```c
// Emergency shutdown - executes immediately
NI9472_SetChannelQueued(0, NI9472_STATE_LOW, DEVICE_PRIORITY_HIGH);

// Normal experiment operation
NI9472_SetChannelQueued(1, NI9472_STATE_HIGH, DEVICE_PRIORITY_NORMAL);

// Background monitoring/status
NI9472_SetChannelQueued(2, NI9472_STATE_LOW, DEVICE_PRIORITY_LOW);
```

**Priority Levels:**
- `DEVICE_PRIORITY_HIGH (0)` - User commands, emergency operations
- `DEVICE_PRIORITY_NORMAL (1)` - Experiment operations
- `DEVICE_PRIORITY_LOW (2)` - Status monitoring, background tasks

## Integration with Experiments

### Temperature Ramp Experiment Example

```c
// In exp_temp_ramp.c

// Open gas flow valves before starting temperature ramp
int StartGasFlow(void) {
    LogMessage("Opening gas flow valves...");

    // Open inlet valve (channel 0)
    // Open bypass valve (channel 2)
    int channels[] = {0, 2};
    int states[] = {NI9472_STATE_HIGH, NI9472_STATE_HIGH};

    int result = NI9472_SetMultipleChannelsQueued(channels, states, 2,
                                                   DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to open gas flow: %s", GetErrorString(result));
        return result;
    }

    return SUCCESS;
}

// Close all valves at end of experiment
int StopGasFlow(void) {
    LogMessage("Closing all gas valves...");

    // Close all channels (all LOW)
    int result = NI9472_SetAllChannelsQueued(0x00, DEVICE_PRIORITY_HIGH);
    if (result != SUCCESS) {
        LogError("Failed to close gas flow: %s", GetErrorString(result));
        return result;
    }

    return SUCCESS;
}
```

## Error Handling

```c
int result = NI9472_SetChannelQueued(5, NI9472_STATE_HIGH, DEVICE_PRIORITY_NORMAL);

switch (result) {
    case SUCCESS:
        LogMessage("Valve opened successfully");
        break;

    case NI9472_ERROR_INVALID_CHANNEL:
        LogError("Invalid channel number (must be 0-7)");
        break;

    case NI9472_ERROR_NOT_CONNECTED:
        LogError("NI 9472 not connected - check hardware");
        break;

    case NI9472_ERROR_WRITE_FAILED:
        LogError("Failed to write to DAQmx - check connections");
        break;

    case ERR_QUEUE_NOT_INIT:
        LogError("Queue manager not initialized");
        break;

    default:
        LogError("Unknown error: %s", GetErrorString(result));
        break;
}
```

## Testing

### Connection Test

```c
// Test NI 9472 connection
int result = NI9472_TestConnectionQueued(DEVICE_PRIORITY_NORMAL);
if (result == SUCCESS) {
    LogMessage("NI 9472 connection test PASSED");
} else {
    LogError("NI 9472 connection test FAILED: %s", GetErrorString(result));
}
```

### Channel Verification

```c
// Verify a channel by toggling it
void TestChannel(int channel) {
    LogMessage("Testing channel %d...", channel);

    // Set HIGH
    NI9472_SetChannelQueued(channel, NI9472_STATE_HIGH, DEVICE_PRIORITY_NORMAL);
    Delay(0.5);  // 500ms delay

    // Verify state
    int state;
    NI9472_GetChannelStateQueued(channel, &state, DEVICE_PRIORITY_NORMAL);
    LogMessage("Channel %d state after HIGH: %s", channel, state ? "HIGH" : "LOW");

    // Set LOW
    NI9472_SetChannelQueued(channel, NI9472_STATE_LOW, DEVICE_PRIORITY_NORMAL);
    Delay(0.5);

    // Verify state
    NI9472_GetChannelStateQueued(channel, &state, DEVICE_PRIORITY_NORMAL);
    LogMessage("Channel %d state after LOW: %s", channel, state ? "HIGH" : "LOW");
}
```

## Typical Solenoid Valve Application

### Channel Mapping Example

```c
// Define valve channels for clarity
#define VALVE_INLET         0  // Main gas inlet
#define VALVE_OUTLET        1  // Main gas outlet
#define VALVE_BYPASS        2  // Bypass line
#define VALVE_PURGE         3  // Purge line
#define VALVE_EXHAUST       4  // Exhaust vent
#define VALVE_SAMPLE        5  // Sample collection
#define VALVE_SAFETY_1      6  // Safety shutoff 1
#define VALVE_SAFETY_2      7  // Safety shutoff 2

// Open inlet flow path
void OpenInletPath(void) {
    NI9472_ChannelState valves[] = {
        {.channel = VALVE_INLET, .state = NI9472_STATE_HIGH},
        {.channel = VALVE_OUTLET, .state = NI9472_STATE_HIGH},
        {.channel = VALVE_SAFETY_1, .state = NI9472_STATE_HIGH},
        {.channel = VALVE_SAFETY_2, .state = NI9472_STATE_HIGH},
    };

    NI9472_SetChannelsAtomic(valves, 4, DEVICE_PRIORITY_HIGH, NULL, NULL);
}

// Emergency shutdown - close all valves
void EmergencyShutdown(void) {
    LogWarning("EMERGENCY SHUTDOWN - Closing all valves");
    NI9472_SetAllChannelsQueued(0x00, DEVICE_PRIORITY_HIGH);
}
```

## Best Practices

1. **Always use queued functions** - Never call DLL functions directly from UI callbacks
2. **Use appropriate priorities** - HIGH for safety, NORMAL for experiments, LOW for monitoring
3. **Use transactions for sequences** - Ensures atomic operations
4. **Check return codes** - Always verify operations succeeded
5. **Initialize at startup** - Create queue manager during application initialization
6. **Cleanup at shutdown** - Always call `NI9472_QueueShutdown()` before exit
7. **Define channel constants** - Use named constants instead of magic numbers
8. **Log valve operations** - Track all valve state changes for debugging

## Troubleshooting

### Module Not Found
- Verify NI 9472 is installed in correct slot
- Check `NI9472_SLOT` in common.h matches physical slot
- Use NI MAX to verify module is detected

### Write Errors
- Check DAQmx driver is installed (NI-DAQmx)
- Verify cDAQ-9178 chassis is powered and connected
- Check USB connection to cDAQ chassis
- Use NI MAX to test module independently

### Queue Not Initialized
- Ensure `NI9472_QueueInit()` was called during startup
- Verify `NI9472_SetGlobalQueueManager()` was called
- Check `ENABLE_NI9472` is set to 1 in common.h

### Channels Not Switching
- Verify solenoid power supply is connected (24V DC typical)
- Check load current is within module specs (<1A per channel)
- Use NI MAX to test individual channels
- Check solenoid valve wiring and polarity

## Debugging

Enable debug output to see detailed command execution:

```c
// In common.h or your initialization code
extern int g_debugMode;
g_debugMode = 1;  // Enable debug logging

// All NI9472 operations will now log detailed information
```

## Thread Safety

The queue system ensures thread-safe operation:
- ✓ Safe to call from UI callbacks
- ✓ Safe to call from experiment threads
- ✓ Safe to call from multiple threads simultaneously
- ✓ Commands execute sequentially in priority order

## Performance

- **Command latency:** ~10-20ms typical
- **Channel switching time:** Module-dependent (typically <1ms)
- **Queue throughput:** ~100 commands/second
- **Transaction overhead:** Minimal (atomic execution)

---

**For more information, refer to:**
- NI 9472 hardware manual
- NI-DAQmx documentation
- device_queue.h for generic queue system details
- CLAUDE.md for overall system architecture

# CLI and Scripting Development Plan

**Date**: 2025-10-21
**Status**: Proposed Future Development
**Context**: Discussion about bypassing LabWindows/CVI GUI for automation and scripting

---

## Motivation

Current system requires LabWindows/CVI GUI for all operations. For research automation, batch experiments, and reproducible workflows, a scriptable interface would provide significant benefits:

- **Automation**: Run parametric studies overnight
- **Reproducibility**: Experiments defined as versioned scripts
- **Integration**: Control from Python/MATLAB/Lua analysis pipelines
- **Batch processing**: Multiple experiments without manual intervention
- **Remote control**: Headless operation on dedicated test hardware

---

## Architecture Analysis

### Portable Components (No CVI Dependencies)

✅ **Device communication layers**:
- DTB4848: Modbus ASCII over RS232
- PSB10000: Modbus RTU over RS232
- ALICAT: Modbus RTU over RS232
- Teensy: Serial protocol
- cDAQ: NIDAQmx library
- Bio-Logic: Bio-Logic SDK

✅ **Device queue system** (device_queue.c):
- Thread-safe command queuing
- Priority-based execution
- Could port from Windows Cmt to pthreads

✅ **Experiment logic**:
- Temperature ramp algorithms
- EIS sequencing
- Safety interlocks

✅ **Logging system** (with minor modifications)

### CVI-Specific Components

❌ UI controls: `SetCtrlVal()`, `GetCtrlVal()`
❌ Graphics: `PlotLine()`, chart updates
❌ Threading: `PostDeferredCall()`
❌ Event callbacks: CVI event system

---

## Implementation Options

### Option 1: Network Command Server ⭐ *Quickest Win*

**Description**: Add TCP/IP server to existing CVI application

**Architecture**:
```
BatteryExploder.exe (CVI GUI + TCP Server)
    ↓ localhost:9000
Python/Lua/MATLAB scripts
```

**Implementation**:
```c
// Add to BatteryExploder.c
void StartTCPCommandServer() {
    // Listen on localhost:9000
    // Parse text commands (same format as cmd_prompt)
    // Return JSON responses
}
```

**Client Example (Python)**:
```python
import socket

def dtb_command(cmd):
    s = socket.socket()
    s.connect(('localhost', 9000))
    s.send(f"DTB02{cmd}\n".encode())
    response = s.recv(4096).decode()
    s.close()
    return response

# Use it
dtb_command("SETUP")
dtb_command("PIDP10.5")
temp = dtb_command("PID?")
```

**Client Example (Lua)**:
```lua
local socket = require("socket")

function dtb_cmd(cmd)
    local client = socket.tcp()
    client:connect("localhost", 9000)
    client:send("DTB02" .. cmd .. "\n")
    local response = client:receive("*l")
    client:close()
    return response
end

dtb_cmd("SETUP")
dtb_cmd("AT")
```

**Effort**: 1-2 days
**Dependencies**: CVI runtime still required
**Benefits**: Immediate scripting capability, minimal code changes
**Limitations**: Not truly headless, GUI must run in background

---

### Option 2: Headless Core Library ⭐⭐ *Most Flexible*

**Description**: Extract device/experiment code into standalone DLL

**Architecture**:
```
battery_exploder_core.dll (headless, no UI)
    ├── All device drivers
    ├── Device queue system
    └── Experiment engines

battery_exploder_gui.exe (CVI, thin wrapper)
battery_exploder_cli.exe (CLI frontend)
Python/Lua bindings
```

**API Example**:
```c
// battery_exploder_core.h
typedef struct ExperimentHandle ExperimentHandle;

// Device control
int DTB_Connect(int slaveAddress);
int DTB_SetPID(int slaveAddress, double P, double I, double D);
int DTB_StartRamp(int slaveAddress, double startTemp, double endTemp, double rate);

// Experiment control
ExperimentHandle* TempRamp_Create(TempRampParams* params);
int TempRamp_Start(ExperimentHandle* exp);
int TempRamp_GetStatus(ExperimentHandle* exp, TempRampStatus* status);
void TempRamp_Destroy(ExperimentHandle* exp);
```

**Effort**: 1-2 weeks
**Dependencies**: Requires significant refactoring
**Benefits**: True headless operation, language-agnostic, reusable core
**Limitations**: Major architectural change

---

### Option 3: Embedded Lua Scripting ⭐⭐⭐ *Most Powerful*

**Description**: Embed Lua interpreter for full experiment scripting

**Architecture**:
```
battery_exploder.exe
    ├── Lua 5.4 interpreter (~200KB)
    ├── Core library (headless)
    └── Lua bindings (lua_bindings.c)
```

**Execution Modes**:
```bash
battery_exploder.exe --gui              # Normal GUI mode
battery_exploder.exe --script exp.lua   # Run Lua script
battery_exploder.exe --server 9000      # TCP command server
battery_exploder.exe --cli              # Interactive console
```

**Example Script**:
```lua
-- experiment.lua: Automated temperature ramp with EIS

require("battery_exploder")

-- Initialize devices
dtb1 = DTB.new(0x02)
dtb2 = DTB.new(0x03)
bio = BioLogic.new(0)

-- Setup
dtb1:enableWriteAccess()
dtb1:configure({
    sensorType = "K-type",
    controlMethod = "PID",
    pidMode = "AUTO"
})

-- Auto-tune
print("Starting auto-tune...")
dtb1:autoTune()
dtb1:waitForAutoTune(timeout = 600)

-- Run temperature ramp
ramp = TempRampExperiment.new({
    initialTemp = 25.0,
    finalTemp = 100.0,
    rampRate = 1.0,
    eisInterval = 5.0,
    continueRampDuringEIS = true,
    outputDir = "data/ramp_" .. os.date("%Y%m%d_%H%M%S")
})

-- Add callbacks
ramp:onProgress(function(pct, temp)
    print(string.format("Progress: %.1f%%, Temp: %.1f°C", pct, temp))
end)

ramp:onEISComplete(function(eisData)
    print("EIS measurement completed")
    -- Could do real-time analysis here
end)

-- Execute
ramp:run()

print("Experiment complete!")
```

**Parametric Study Example**:
```lua
-- Test multiple ramp rates
for rate = 0.5, 5.0, 0.5 do
    print(string.format("Testing ramp rate: %.1f °C/min", rate))

    ramp = TempRampExperiment.new({
        initialTemp = 25.0,
        finalTemp = 100.0,
        rampRate = rate,
        eisInterval = 5.0,
        outputDir = string.format("data/ramp_rate_%.1f", rate)
    })

    ramp:run()

    -- Cool down between runs
    print("Cooling down...")
    sleep(300)
end
```

**Effort**: 3-5 days (after Phase 2)
**Dependencies**: Requires headless core library
**Benefits**: Maximum automation flexibility, reproducible experiments, versioned methods
**Why Lua**: Tiny (200KB), fast, easy C integration, designed for embedding

---

## Recommended Implementation Path

### Phase 1: Network Command Interface (1-2 days)
**Goal**: Enable immediate scripting without refactoring

**Tasks**:
1. Add TCP server thread to BatteryExploder.c
2. Reuse existing cmd_prompt parsing logic
3. Return responses as JSON
4. Document protocol

**Result**: Control from Python/Lua/MATLAB while GUI runs

---

### Phase 2: Extract Core Library (1-2 weeks)
**Goal**: Separate business logic from UI

**Tasks**:
1. Create battery_exploder_core project
2. Move device drivers to core
3. Remove UI dependencies (replace with callbacks)
4. Create C API for experiments
5. Build CLI frontend

**Result**: True headless operation, foundation for language bindings

---

### Phase 3: Lua Scripting Environment (3-5 days)
**Goal**: Full experiment automation

**Tasks**:
1. Integrate Lua 5.4 interpreter
2. Create lua_bindings.c
3. Expose devices as Lua objects
4. Expose experiments as Lua objects
5. Add utility functions (sleep, logging, etc.)
6. Create example scripts

**Result**: Production-ready automation system

---

### Phase 4: Enhanced Features (Ongoing)
**Goal**: Improve developer experience

**Possible additions**:
- Experiment templates library
- Real-time data analysis hooks
- Safety validation layer
- Configuration file support
- Interactive debugger
- Remote monitoring API

---

## Use Cases

### Scientific Research
```lua
-- Reproduce published experiment exactly
dofile("experiments/nature_paper_fig3.lua")
```

### Quality Control
```lua
-- Daily battery characterization
for i, battery_id in ipairs({"A01", "A02", "A03"}) do
    run_standard_test(battery_id)
end
```

### Algorithm Development
```lua
-- Test new PID tuning algorithm
for p in arange(5.0, 15.0, 0.5) do
    for i in arange(80, 140, 10) do
        test_pid_performance(p, i, 30)
    end
end
```

### Remote Operation
```lua
-- Cloud-triggered experiment
mqtt:subscribe("experiments/battery/start", function(msg)
    params = json.decode(msg)
    run_experiment(params)
    mqtt:publish("experiments/battery/complete", results)
end)
```

---

## Development Priority

**Recommended approach**: Implement phases incrementally

1. **Start with Phase 1** (TCP server)
   - Quickest implementation
   - Immediate value for automation
   - Test concept before major refactoring
   - Low risk

2. **Then Phase 3** (Lua scripting)
   - Huge productivity boost
   - Enables reproducible research
   - Scripts can be versioned with data
   - Parametric studies become trivial

3. **Phase 2 can be optional initially**
   - Still works with CVI runtime
   - Can refactor later if headless becomes critical
   - Focus on user-facing features first

---

## Technical Considerations

### Threading Model
- Lua scripts run in main thread
- Device operations use existing queue system
- Callbacks posted to script thread
- Need to handle cancellation gracefully

### Error Handling
```lua
-- Lua error handling
status, err = pcall(function()
    ramp:run()
end)

if not status then
    print("Experiment failed: " .. err)
    emergency_shutdown()
end
```

### Safety
- Scripts should respect hardware limits
- Emergency stop accessible from Lua
- Timeout on blocking operations
- Validation layer for parameters

### Compatibility
- Keep existing GUI fully functional
- Scripts and GUI can coexist
- Shared configuration files
- Same data format output

---

## Next Steps

When ready to implement:

1. Choose starting phase (recommend Phase 1)
2. Create detailed technical specification
3. Define protocol/API
4. Write test suite
5. Implement iteratively
6. Document with examples
7. Field test with real experiments

---

## References

- Lua C API: https://www.lua.org/manual/5.4/
- LuaSocket (for TCP): http://w3.impa.br/~diego/software/luasocket/
- LuaJSON: https://github.com/rxi/json.lua
- Python socket module: https://docs.python.org/3/library/socket.html

---

## Questions to Consider

1. What scripting language do you prefer? (Python, Lua, MATLAB?)
2. Is headless operation required or can CVI runtime run in background?
3. What's the typical experiment workflow to automate?
4. Need real-time data processing during experiments?
5. Remote control over network important?

---

**Status**: Documented for future consideration
**Next Action**: Discuss priorities and select implementation phase

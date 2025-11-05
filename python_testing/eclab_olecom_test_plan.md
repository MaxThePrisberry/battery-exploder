# Python EC-Lab OLE COM Test Script - Implementation Plan

## Overview

This document outlines a comprehensive testing plan to understand and resolve the EC-Lab OLE COM connection stability issue observed in the Battery Exploder system. The device disconnects after ~10-14 seconds of inactivity, and reconnection attempts fail with COM error 0x800401F0.

## Problem Statement

### Observed Behavior
- BioLogic SP-150e connects successfully via EC-Lab OLE COM
- Connection remains stable during active communication
- After ~10-14 seconds of idle time (no OLE COM calls), device auto-disconnects
- All reconnection attempts return COM error -2147221008 (0x800401F0)
- EC-Lab COM server appears to enter a broken state after disconnect

### Timeline from Log Analysis
```
15:01:20 - ConnectDevice returns 1 (success)
15:01:24 - Diagnostic complete (5 TestConnection calls)
15:01:24-15:01:34 - 10 seconds of silence (initializing DTB, ALICAT, Teensy)
15:01:34 - Connection already lost when next TestConnection is called
15:01:34+ - All reconnection attempts fail with COM error
```

### Root Cause Hypothesis
From EC-Lab OLE COM User Manual (page 6):
> "During OLECOM messages EC-Lab and BT-Lab will switch to an automatic mode... And then return to normal behavior when between OLECOM messages."

This suggests EC-Lab auto-disconnects idle devices when "between OLECOM messages".

## Objectives

1. Measure exact idle timeout threshold for auto-disconnect
2. Test keep-alive strategies to prevent disconnection
3. Test reconnection strategies after disconnection occurs
4. Identify minimal/optimal interaction pattern for stable connection
5. Determine CPU/performance impact of potential solutions
6. Provide clear recommendation for C codebase implementation

## Prerequisites

- EC-Lab software running on Windows
- Python 3.x installed
- `pywin32` package: `pip install pywin32` (provides `win32com.client`)
- EC-Lab registered as OLE COM server
- BioLogic device connected (or EC-Lab in simulation mode)
- Simple .mps template file for LoadSettings tests

## Test Script Structure

```
python_testing/
├── eclab_olecom_test_plan.md          # This document
├── eclab_com_test.py                  # Main test script
├── results/                           # Test results directory
│   ├── test_YYYYMMDD_HHMMSS.log      # Detailed log files
│   └── summary.txt                    # Summary of findings
└── templates/                         # Test .mps files
    └── simple_ocv.mps                 # Simple OCV template for testing
```

## Test Scenarios

### Test 1: Baseline Connection Behavior

**Purpose**: Establish baseline timing for auto-disconnect

**Steps**:
1. Connect to EC-Lab COM server
2. Call `ConnectDevice(deviceNumber)`
3. Log connection timestamp
4. Wait without any OLE COM calls
5. Poll `TestConnection()` every 500ms
6. Record exact time when disconnect is detected

**Expected Outcome**: Measure precise idle timeout threshold (hypothesis: ~10-14 seconds)

**Data to Collect**:
- Time from connect to auto-disconnect
- TestConnection return values over time (should be 1 until disconnect, then 0)
- Any COM errors during disconnect detection
- Consistency across multiple runs

**Success Criteria**: Identify reproducible timeout value with ±1 second variance

---

### Test 2: Keep-Alive with TestConnection

**Purpose**: Test if periodic TestConnection calls prevent disconnect

**Steps**:
1. Connect to device
2. Call `TestConnection()` at varying intervals:
   - **Test 2a**: Every 5 seconds (should disconnect based on baseline)
   - **Test 2b**: Every 3 seconds
   - **Test 2c**: Every 2 seconds
   - **Test 2d**: Every 1 second
   - **Test 2e**: Every 500ms
3. Run each interval test for 60 seconds
4. Monitor for disconnections

**Expected Outcome**: Identify minimum keep-alive frequency that maintains connection

**Data to Collect**:
- Success/failure for each interval
- Number of TestConnection calls made
- CPU usage during high-frequency calls (rough measurement)
- Any performance degradation
- Any COM errors

**Success Criteria**:
- Identify shortest interval that maintains 100% connection stability
- Verify interval is practical for production use (reasonable CPU load)

---

### Test 3: Immediate Settings Load After Connection

**Purpose**: Test if LoadSettings prevents initial disconnect

**Hypothesis**: Loading settings may "anchor" the connection by putting EC-Lab in active mode.

**Steps**:
1. Connect to device
2. **Immediately** call `LoadSettings()` with a simple .mps template
3. Wait 30 seconds without further calls
4. Monitor connection status with TestConnection

**Variations**:
- **Test 3a**: LoadSettings immediately after connect
- **Test 3b**: LoadSettings + TestConnection after 15s
- **Test 3c**: No LoadSettings (baseline comparison)

**Expected Outcome**: Determine if LoadSettings prevents disconnect or extends timeout

**Data to Collect**:
- Does LoadSettings prevent disconnect during 30s idle?
- How long does connection remain stable after LoadSettings?
- Any difference vs. no LoadSettings?
- LoadSettings execution time
- Any error messages or warnings

**Success Criteria**: Connection remains stable for at least 30 seconds after LoadSettings

---

### Test 4: Reconnection Strategies

**Purpose**: Find reliable reconnection approach after disconnect

#### Test 4a: Simple Reconnect
**Steps**:
1. Connect to device
2. Wait for auto-disconnect (use baseline timeout + 5 seconds)
3. Call `ConnectDevice()` again
4. Record return value and success/failure
5. If successful, verify with TestConnection

#### Test 4b: Explicit Disconnect Before Reconnect
**Steps**:
1. Connect to device
2. Wait for auto-disconnect
3. Call `DisconnectDevice()`
4. Wait 1 second
5. Call `ConnectDevice()`
6. Record return value and success/failure

#### Test 4c: COM Interface Recreation
**Steps**:
1. Connect to device
2. Wait for auto-disconnect
3. Release COM interface (`interface = None`)
4. Recreate interface (`win32com.client.Dispatch("ECLabCOM.ECLabInterface")`)
5. Call `ConnectDevice()`
6. Record return value and success/failure

#### Test 4d: Multiple Reconnect Attempts
**Steps**:
1. Connect to device
2. Wait for auto-disconnect
3. Attempt reconnection up to 5 times with 2-second delays
4. Track which attempt (if any) succeeds

**Expected Outcome**: Identify which reconnection approach succeeds

**Data to Collect**:
- Success rate of each approach (test 10 times each)
- Return values (1=success, 0=failure, negative=COM error)
- Time required for reconnection
- Any error messages or COM exceptions
- Consistency of approach

**Success Criteria**: Identify approach with >90% success rate

---

### Test 5: Multiple Method Keep-Alive

**Purpose**: Test if other OLE COM methods keep connection alive

**Hypothesis**: Any OLE COM call may count as "activity" to prevent auto-disconnect.

**Steps**:
1. Connect to device
2. Test different keep-alive methods (2-second intervals for 60 seconds):
   - **Test 5a**: Only `TestConnection()`
   - **Test 5b**: Only `LoadSettings()` (same simple template)
   - **Test 5c**: Alternate between TestConnection and LoadSettings
3. Monitor connection status

**Expected Outcome**: Determine if all OLE COM calls prevent disconnect equally

**Data to Collect**:
- Which methods successfully prevent disconnect
- Relative performance impact of each method
- Any side effects (e.g., LoadSettings reloading same file repeatedly)
- Any error messages

**Success Criteria**: Identify if TestConnection is sufficient or if other methods are needed

---

### Test 6: Mixed Workload Simulation

**Purpose**: Simulate real Battery Exploder behavior with gaps

**Steps**:
1. Connect to device
2. Simulate Battery Exploder initialization sequence:
   - **Phase 1**: ConnectDevice
   - **Phase 2**: Diagnostic (5 quick TestConnection calls, 100ms apart)
   - **Phase 3**: 10-second idle period (simulating DTB/ALICAT initialization)
   - **Phase 4**: Resume activity (TestConnection every 2 seconds)
3. Run test with two variants:
   - **Test 6a**: No keep-alive during Phase 3 (should fail)
   - **Test 6b**: Keep-alive every 2s during Phase 3 (should succeed)

**Expected Outcome**: Validate solution in realistic scenario

**Data to Collect**:
- Does disconnect occur during 10s idle (as in C code)?
- Does keep-alive prevent it?
- Minimum keep-alive frequency for this pattern
- Total number of OLE COM calls required

**Success Criteria**: Connection remains stable through entire initialization sequence with keep-alive

---

### Test 7: Stress Test - Long Duration

**Purpose**: Verify solution stability over extended operation

**Steps**:
1. Connect to device
2. Apply chosen keep-alive strategy from Test 2
3. Run for 10 minutes
4. Monitor connection status continuously

**Expected Outcome**: Confirm long-term stability

**Data to Collect**:
- Any disconnections during 10-minute run
- CPU usage over time
- Memory usage over time
- Any COM errors or warnings

**Success Criteria**: Zero disconnections over 10-minute period

---

## Python Code Template

### Main Test Script: `eclab_com_test.py`

```python
"""
EC-Lab OLE COM Connection Stability Tests

Purpose: Systematically test connection behavior to identify optimal
         keep-alive or reconnection strategy for Battery Exploder system.

Author: Battery Exploder Team
Date: 2025-11-05
"""

import win32com.client
import time
import logging
import os
import sys
from datetime import datetime

# Setup results directory
RESULTS_DIR = "results"
if not os.path.exists(RESULTS_DIR):
    os.makedirs(RESULTS_DIR)

# Setup logging
log_filename = os.path.join(RESULTS_DIR, f'test_{datetime.now().strftime("%Y%m%d_%H%M%S")}.log')
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s.%(msecs)03d - %(levelname)s - %(message)s',
    datefmt='%H:%M:%S',
    handlers=[
        logging.FileHandler(log_filename),
        logging.StreamHandler(sys.stdout)
    ]
)

class ECLabTester:
    """EC-Lab OLE COM test interface"""

    def __init__(self, device_number=1):
        self.interface = None
        self.device_number = device_number
        self.connected = False

    def connect_to_eclab(self):
        """Initialize COM connection to EC-Lab"""
        try:
            logging.info("Connecting to EC-Lab COM server...")
            self.interface = win32com.client.Dispatch("ECLabCOM.ECLabInterface")
            logging.info("COM connection established")
            return True
        except Exception as e:
            logging.error(f"Failed to connect to EC-Lab COM server: {e}")
            return False

    def connect_device(self):
        """Connect to BioLogic device"""
        try:
            logging.info(f"Calling ConnectDevice({self.device_number})...")
            ret = self.interface.ConnectDevice(self.device_number)
            logging.info(f"ConnectDevice returned: {ret}")

            # EC-Lab returns 1 for success, 0 for failure
            if ret == 1:
                self.connected = True
                return True
            else:
                logging.error(f"ConnectDevice failed with return value: {ret}")
                if ret == 0:
                    logging.error("Return value 0 means: Device not connected")
                else:
                    logging.error(f"Return value 0x{ret:08X} is a COM/OLE error")
                return False

        except Exception as e:
            logging.error(f"Exception during ConnectDevice: {e}")
            return False

    def disconnect_device(self):
        """Disconnect from device"""
        try:
            logging.info(f"Calling DisconnectDevice({self.device_number})...")
            ret = self.interface.DisconnectDevice(self.device_number)
            logging.info(f"DisconnectDevice returned: {ret}")

            if ret == 1:
                self.connected = False
                return True
            else:
                logging.warning(f"DisconnectDevice returned unexpected value: {ret}")
                return False

        except Exception as e:
            logging.error(f"Exception during DisconnectDevice: {e}")
            return False

    def test_connection(self):
        """Test if device is connected"""
        try:
            ret = self.interface.TestConnection(self.device_number)
            # Don't log every call - too verbose for keep-alive testing
            return ret == 1
        except Exception as e:
            logging.error(f"Exception during TestConnection: {e}")
            return False

    def load_settings(self, mps_path):
        """Load settings from .mps file"""
        try:
            logging.info(f"Calling LoadSettings('{mps_path}')...")
            ret = self.interface.LoadSettings(mps_path)
            logging.info(f"LoadSettings returned: {ret}")

            if ret == 1:
                return True
            else:
                logging.error(f"LoadSettings failed with return value: {ret}")
                return False

        except Exception as e:
            logging.error(f"Exception during LoadSettings: {e}")
            return False

    def cleanup(self):
        """Release COM resources"""
        if self.connected:
            self.disconnect_device()
        self.interface = None

# ============================================================================
# Test 1: Baseline Auto-Disconnect Timing
# ============================================================================

def test_1_baseline_disconnect():
    """Measure time until auto-disconnect with no activity"""
    logging.info("=" * 70)
    logging.info("TEST 1: Baseline Auto-Disconnect Timing")
    logging.info("=" * 70)

    tester = ECLabTester()

    if not tester.connect_to_eclab():
        logging.error("Cannot proceed - EC-Lab COM server not available")
        return None

    if not tester.connect_device():
        logging.error("Cannot proceed - device connection failed")
        return None

    logging.info("Connected successfully. Waiting for auto-disconnect...")
    logging.info("Polling TestConnection every 0.5 seconds...")

    start_time = time.time()
    last_log_time = start_time

    while True:
        time.sleep(0.5)
        elapsed = time.time() - start_time

        # Log status every 5 seconds
        if elapsed - last_log_time >= 5:
            logging.info(f"Still connected after {elapsed:.1f}s")
            last_log_time = elapsed

        if not tester.test_connection():
            disconnect_time = elapsed
            logging.warning(f"CONNECTION LOST after {disconnect_time:.2f} seconds")
            tester.cleanup()
            return disconnect_time

        if elapsed > 60:
            logging.info("Still connected after 60 seconds - stopping test")
            tester.cleanup()
            return None

# ============================================================================
# Test 2: Keep-Alive with TestConnection
# ============================================================================

def test_2_keepalive(interval_seconds):
    """Test keep-alive with TestConnection at specified interval"""
    logging.info("=" * 70)
    logging.info(f"TEST 2: Keep-Alive (interval={interval_seconds}s)")
    logging.info("=" * 70)

    tester = ECLabTester()

    if not tester.connect_to_eclab() or not tester.connect_device():
        return False

    logging.info(f"Testing keep-alive with {interval_seconds}s interval for 60 seconds...")

    start_time = time.time()
    call_count = 0
    success = True

    while time.time() - start_time < 60:
        time.sleep(interval_seconds)
        call_count += 1

        if not tester.test_connection():
            elapsed = time.time() - start_time
            logging.warning(f"CONNECTION LOST after {elapsed:.2f}s (made {call_count} calls)")
            success = False
            break

        # Log every 10th call
        if call_count % 10 == 0:
            logging.debug(f"Keep-alive call #{call_count} at {time.time() - start_time:.1f}s")

    if success:
        logging.info(f"SUCCESS: Connection maintained for 60s with {interval_seconds}s interval")
        logging.info(f"Total TestConnection calls: {call_count}")

    tester.cleanup()
    return success

# ============================================================================
# Test 3: Immediate LoadSettings After Connection
# ============================================================================

def test_3_immediate_load_settings(mps_path):
    """Test if LoadSettings prevents disconnect"""
    logging.info("=" * 70)
    logging.info("TEST 3: Immediate LoadSettings After Connection")
    logging.info("=" * 70)

    tester = ECLabTester()

    if not tester.connect_to_eclab() or not tester.connect_device():
        return False

    # Load settings immediately
    logging.info("Loading settings immediately after connection...")
    if not tester.load_settings(mps_path):
        logging.error("LoadSettings failed - cannot proceed")
        tester.cleanup()
        return False

    logging.info("Settings loaded. Waiting 30 seconds with no activity...")
    start_time = time.time()

    time.sleep(30)

    if tester.test_connection():
        logging.info("SUCCESS: Connection still active after 30s idle (with LoadSettings)")
        tester.cleanup()
        return True
    else:
        elapsed = time.time() - start_time
        logging.warning(f"CONNECTION LOST after {elapsed:.1f}s despite LoadSettings")
        tester.cleanup()
        return False

# ============================================================================
# Test 4a: Simple Reconnection
# ============================================================================

def test_4a_simple_reconnect(disconnect_time):
    """Test simple reconnection after auto-disconnect"""
    logging.info("=" * 70)
    logging.info("TEST 4a: Simple Reconnection After Auto-Disconnect")
    logging.info("=" * 70)

    tester = ECLabTester()

    if not tester.connect_to_eclab() or not tester.connect_device():
        return False

    # Wait for auto-disconnect
    wait_time = disconnect_time + 5 if disconnect_time else 15
    logging.info(f"Waiting {wait_time}s for auto-disconnect...")
    time.sleep(wait_time)

    # Verify disconnected
    if tester.test_connection():
        logging.warning("Device still connected - disconnect didn't occur as expected")
    else:
        logging.info("Device disconnected as expected")

    # Attempt simple reconnection
    logging.info("Attempting simple reconnection...")
    success = tester.connect_device()

    if success:
        logging.info("SUCCESS: Simple reconnection worked!")
        # Verify with TestConnection
        if tester.test_connection():
            logging.info("Verification: TestConnection confirms connection")
        else:
            logging.warning("Verification: TestConnection shows NOT connected")
            success = False
    else:
        logging.error("FAILED: Simple reconnection did not work")

    tester.cleanup()
    return success

# ============================================================================
# Test 4b: Explicit Disconnect Before Reconnect
# ============================================================================

def test_4b_disconnect_before_reconnect(disconnect_time):
    """Test reconnection with explicit DisconnectDevice call first"""
    logging.info("=" * 70)
    logging.info("TEST 4b: Explicit Disconnect Before Reconnect")
    logging.info("=" * 70)

    tester = ECLabTester()

    if not tester.connect_to_eclab() or not tester.connect_device():
        return False

    # Wait for auto-disconnect
    wait_time = disconnect_time + 5 if disconnect_time else 15
    logging.info(f"Waiting {wait_time}s for auto-disconnect...")
    time.sleep(wait_time)

    # Explicit disconnect
    logging.info("Calling DisconnectDevice explicitly...")
    tester.disconnect_device()
    time.sleep(1)

    # Attempt reconnection
    logging.info("Attempting reconnection...")
    success = tester.connect_device()

    if success:
        logging.info("SUCCESS: Reconnection with explicit disconnect worked!")
        if tester.test_connection():
            logging.info("Verification: TestConnection confirms connection")
        else:
            logging.warning("Verification: TestConnection shows NOT connected")
            success = False
    else:
        logging.error("FAILED: Reconnection with explicit disconnect did not work")

    tester.cleanup()
    return success

# ============================================================================
# Test 4c: COM Interface Recreation
# ============================================================================

def test_4c_interface_recreation(disconnect_time):
    """Test reconnection with COM interface recreation"""
    logging.info("=" * 70)
    logging.info("TEST 4c: COM Interface Recreation Before Reconnect")
    logging.info("=" * 70)

    tester = ECLabTester()

    if not tester.connect_to_eclab() or not tester.connect_device():
        return False

    # Wait for auto-disconnect
    wait_time = disconnect_time + 5 if disconnect_time else 15
    logging.info(f"Waiting {wait_time}s for auto-disconnect...")
    time.sleep(wait_time)

    # Release and recreate interface
    logging.info("Releasing COM interface...")
    tester.interface = None
    time.sleep(1)

    logging.info("Recreating COM interface...")
    if not tester.connect_to_eclab():
        logging.error("Failed to recreate COM interface")
        return False

    # Attempt reconnection
    logging.info("Attempting reconnection with new interface...")
    success = tester.connect_device()

    if success:
        logging.info("SUCCESS: Reconnection with interface recreation worked!")
        if tester.test_connection():
            logging.info("Verification: TestConnection confirms connection")
        else:
            logging.warning("Verification: TestConnection shows NOT connected")
            success = False
    else:
        logging.error("FAILED: Reconnection with interface recreation did not work")

    tester.cleanup()
    return success

# ============================================================================
# Test 6: Mixed Workload Simulation
# ============================================================================

def test_6_mixed_workload():
    """Simulate Battery Exploder initialization sequence"""
    logging.info("=" * 70)
    logging.info("TEST 6: Mixed Workload Simulation (Battery Exploder Init)")
    logging.info("=" * 70)

    tester = ECLabTester()

    if not tester.connect_to_eclab():
        return False

    # Phase 1: Connect
    logging.info("=== Phase 1: Initial Connection ===")
    if not tester.connect_device():
        return False

    # Phase 2: Diagnostic (5 quick calls)
    logging.info("=== Phase 2: Diagnostic (5 TestConnection calls) ===")
    for i in range(5):
        if not tester.test_connection():
            logging.error(f"Diagnostic call {i+1} failed!")
            tester.cleanup()
            return False
        time.sleep(0.1)
    logging.info("Diagnostic complete")

    # Phase 3: 10-second idle with keep-alive
    logging.info("=== Phase 3: 10-Second Init Gap (2s keep-alive) ===")
    start_time = time.time()
    keepalive_count = 0

    while time.time() - start_time < 10:
        time.sleep(2)
        keepalive_count += 1

        if not tester.test_connection():
            elapsed = time.time() - start_time
            logging.error(f"CONNECTION LOST at {elapsed:.1f}s during init gap!")
            tester.cleanup()
            return False

        logging.debug(f"Keep-alive #{keepalive_count} at {time.time() - start_time:.1f}s")

    # Phase 4: Resume activity
    logging.info("=== Phase 4: Resume Activity ===")
    for i in range(5):
        if not tester.test_connection():
            logging.error("Connection lost during resume phase!")
            tester.cleanup()
            return False
        time.sleep(1)

    logging.info("SUCCESS: Connection maintained through entire initialization sequence!")
    logging.info(f"Total keep-alive calls during gap: {keepalive_count}")

    tester.cleanup()
    return True

# ============================================================================
# Main Test Runner
# ============================================================================

def main():
    """Run all tests in sequence"""
    logging.info("=" * 70)
    logging.info("EC-Lab OLE COM Connection Stability Test Suite")
    logging.info("=" * 70)
    logging.info(f"Log file: {log_filename}")
    logging.info("")

    results = {}

    # Test 1: Baseline
    disconnect_time = test_1_baseline_disconnect()
    results['baseline_disconnect_time'] = disconnect_time

    if disconnect_time:
        logging.info(f"\nBaseline disconnect time: {disconnect_time:.2f} seconds\n")
    else:
        logging.warning("\nCould not determine baseline disconnect time\n")
        disconnect_time = 10  # Default estimate

    time.sleep(2)

    # Test 2: Keep-alive at various intervals
    results['keepalive_5s'] = test_2_keepalive(5)
    time.sleep(2)

    results['keepalive_3s'] = test_2_keepalive(3)
    time.sleep(2)

    results['keepalive_2s'] = test_2_keepalive(2)
    time.sleep(2)

    results['keepalive_1s'] = test_2_keepalive(1)
    time.sleep(2)

    # Test 3: LoadSettings (requires .mps file path)
    mps_path = "templates/simple_ocv.mps"
    if os.path.exists(mps_path):
        results['immediate_load_settings'] = test_3_immediate_load_settings(mps_path)
        time.sleep(2)
    else:
        logging.warning(f"Skipping Test 3 - .mps file not found: {mps_path}")
        results['immediate_load_settings'] = None

    # Test 4: Reconnection strategies
    results['simple_reconnect'] = test_4a_simple_reconnect(disconnect_time)
    time.sleep(2)

    results['disconnect_before_reconnect'] = test_4b_disconnect_before_reconnect(disconnect_time)
    time.sleep(2)

    results['interface_recreation'] = test_4c_interface_recreation(disconnect_time)
    time.sleep(2)

    # Test 6: Mixed workload
    results['mixed_workload'] = test_6_mixed_workload()

    # Print summary
    logging.info("\n" + "=" * 70)
    logging.info("TEST SUMMARY")
    logging.info("=" * 70)

    if results['baseline_disconnect_time']:
        logging.info(f"Baseline Disconnect Time: {results['baseline_disconnect_time']:.2f}s")
    else:
        logging.info("Baseline Disconnect Time: Not determined")

    logging.info("\nKeep-Alive Results:")
    logging.info(f"  5s interval: {'PASS' if results['keepalive_5s'] else 'FAIL'}")
    logging.info(f"  3s interval: {'PASS' if results['keepalive_3s'] else 'FAIL'}")
    logging.info(f"  2s interval: {'PASS' if results['keepalive_2s'] else 'FAIL'}")
    logging.info(f"  1s interval: {'PASS' if results['keepalive_1s'] else 'FAIL'}")

    logging.info("\nLoadSettings Test:")
    if results['immediate_load_settings'] is not None:
        logging.info(f"  Immediate LoadSettings: {'PASS' if results['immediate_load_settings'] else 'FAIL'}")
    else:
        logging.info("  Immediate LoadSettings: SKIPPED")

    logging.info("\nReconnection Strategy Results:")
    logging.info(f"  Simple reconnect: {'PASS' if results['simple_reconnect'] else 'FAIL'}")
    logging.info(f"  Disconnect first: {'PASS' if results['disconnect_before_reconnect'] else 'FAIL'}")
    logging.info(f"  Interface recreation: {'PASS' if results['interface_recreation'] else 'FAIL'}")

    logging.info("\nMixed Workload Test:")
    logging.info(f"  Battery Exploder simulation: {'PASS' if results['mixed_workload'] else 'FAIL'}")

    logging.info("\n" + "=" * 70)
    logging.info("Testing complete. Review log for detailed results.")
    logging.info("=" * 70)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        logging.info("\n\nTests interrupted by user")
    except Exception as e:
        logging.error(f"\n\nUnexpected error: {e}", exc_info=True)
```

## Usage Instructions

### 1. Install Prerequisites

```bash
# Install Python (if not already installed)
# Download from: https://www.python.org/downloads/

# Install pywin32 package
pip install pywin32
```

### 2. Prepare Test Environment

1. Ensure EC-Lab is running on the Windows machine
2. Verify BioLogic device is connected (or EC-Lab is in simulation mode)
3. Create `templates/` directory if testing LoadSettings (Test 3)
4. Copy a simple .mps file to `templates/simple_ocv.mps` (optional)

### 3. Run Tests

```bash
cd python_testing
python eclab_com_test.py
```

### 4. Review Results

- Detailed log: `results/test_YYYYMMDD_HHMMSS.log`
- Summary printed to console at end of test run
- Review timing data and success/failure patterns

## Expected Deliverables

### 1. Test Log File
Timestamped log of all OLE COM calls with:
- Return values
- Timing information
- Connection state changes
- Any errors or exceptions

### 2. Summary Report
Key findings from each test scenario:
- Baseline disconnect time
- Minimum keep-alive frequency
- Whether LoadSettings helps
- Which reconnection strategy works
- Validation of mixed workload

### 3. Recommendations
Based on test results, provide clear recommendation:
- **Option A**: Implement keep-alive mechanism at X-second interval
- **Option B**: Load settings immediately after connection
- **Option C**: Implement improved reconnection with strategy Y
- **Option D**: Combination of approaches

## Key Questions to Answer

1. **What is the exact idle timeout?** (Test 1)
   - Expected: ~10-14 seconds based on C code logs
   - Need: Precise measurement for keep-alive tuning

2. **What keep-alive frequency is required?** (Test 2)
   - Expected: Somewhere between 1-5 seconds
   - Need: Minimum frequency that's reliable but not excessive

3. **Does LoadSettings prevent disconnect?** (Test 3)
   - Expected: May extend timeout or "anchor" connection
   - Need: Determine if this is a viable alternative to keep-alive

4. **How do we successfully reconnect?** (Test 4)
   - Expected: Simple reconnect likely fails (COM error 0x800401F0)
   - Need: Working reconnection strategy for robustness

5. **Is the solution practical for real workload?** (Test 6)
   - Expected: 2s keep-alive should work for Battery Exploder init
   - Need: Validation in realistic scenario

## Next Steps After Testing

Once Python testing identifies the optimal approach:

### If Keep-Alive is Recommended:
1. Add keep-alive timer to `biologic_abstract.c` or `status.c`
2. Call `BIO_Abstract_TestConnection()` at identified interval
3. Only during initialization gaps (can reduce frequency during active operation)
4. Test with actual Battery Exploder workload

### If LoadSettings is Recommended:
1. Modify `BIO_InitializeAbstract()` in `biologic_abstract.c`
2. Add immediate `BIO_ECLAB_LoadSettings()` call after connection
3. Use default template from configuration
4. Test with actual Battery Exploder workload

### If Improved Reconnection is Recommended:
1. Implement identified strategy in `eclab_olecom.c`
2. Update `BIO_Abstract_Connect()` to use new approach
3. Add retry logic if needed
4. Test reconnection reliability

### Documentation:
1. Document findings in commit message
2. Update comments in relevant source files
3. Add note to `CLAUDE.md` about EC-Lab keep-alive requirements
4. Consider adding to user documentation if configuration is needed

## Notes and Considerations

### COM/OLE Automation Considerations
- COM errors may require specific handling
- Interface lifetime management is critical
- Python `win32com` vs `comtypes` - use what's most stable

### Performance Considerations
- Keep-alive frequency vs. CPU load trade-off
- Logging verbosity in production vs. testing
- Thread safety (Python testing is single-threaded, C code is multi-threaded)

### EC-Lab Behavior Assumptions
- Manual states "automatic mode during OLECOM messages"
- Unclear what "between messages" timeout is
- May vary by EC-Lab version or configuration

### Testing Limitations
- Python testing is single-threaded (C code is multi-threaded)
- Test environment may differ from production
- Device hardware state may affect behavior
- EC-Lab software version differences

### Safety Considerations
- No actual electrochemical measurements during testing
- Simple .mps templates to minimize hardware interaction
- Quick tests to minimize device stress

## References

- **EC-Lab OLE COM User Manual v7**: `manuals/bt-lab-and-ec-lab-ole-com-user-manual_v7.pdf`
- **Battery Exploder Source**: `biologic/eclab_olecom.c`, `biologic/biologic_abstract.c`
- **Debug Logs**: `notes/log 2025-11-05-8.txt`
- **Bug Fix Commit**: "Fix critical EC-Lab return value checking bug in all OLE COM functions"

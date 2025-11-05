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

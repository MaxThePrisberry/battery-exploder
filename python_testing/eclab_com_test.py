"""
EC-Lab OLE COM Connection Stability Tests

Purpose: Systematically test connection behavior to identify optimal
         keep-alive or reconnection strategy for Battery Exploder system.

Author: Battery Exploder Team
Date: 2025-11-05

IMPORTANT NOTES:
================
1. This script uses CUSTOM INTERFACE (IEClabExe) via comtypes library
2. ProgID: "EClabCOM.EClabExe" (NOT "ECLabCOM.ECLabInterface")
3. CLSID: {77FE5C93-42EE-4127-944B-5BA14FD33447}
4. IID: {642C68D2-85BD-494B-93EB-583CCBB11794}
5. Uses comtypes to directly access custom COM interface (NOT IDispatch)
6. LoadSettings requires 3 parameters: (device, channel, filepath)

PREREQUISITES:
==============
1. EC-Lab must be running BEFORE running this script
2. Python packages: pip install comtypes
3. EC-Lab must be registered: ECLab.exe /regserver (as Administrator)

COM INTERFACE NOTES:
====================
The C code uses direct vtable calls via IEClabExe custom interface.
EC-Lab does NOT support IDispatch, so we use comtypes to directly
access the custom interface, matching the C code's approach exactly.

Interface definition manually created from ECLabCOM_EClabExeInterface.txt
"""

import time
import logging
import os
import sys
from datetime import datetime

# comtypes for custom COM interface support
from comtypes import GUID, IUnknown, COMMETHOD, HRESULT, POINTER, BSTR
from comtypes.automation import VARIANT
from comtypes.client import CreateObject
import comtypes
from ctypes import c_int

# EC-Lab COM identifiers (must match C code and interface definition)
CLSID_EClabExe = GUID("{77FE5C93-42EE-4127-944B-5BA14FD33447}")
IID_IEClabExe = GUID("{642C68D2-85BD-494B-93EB-583CCBB11794}")

# Define IEClabExe COM Interface
# This matches the interface definition from ECLabCOM_EClabExeInterface.txt
# CRITICAL: ALL methods must be defined in exact vtable order, even if unused!
class IEClabExe(IUnknown):
    """EC-Lab IEClabExe COM Interface

    This is the custom COM interface that EC-Lab exposes.
    Methods return int (1=success, 0=failure) not HRESULT.

    IMPORTANT: vtable order MUST match interface definition exactly.
    Skipping methods will cause access violations!
    """
    _iid_ = IID_IEClabExe
    _methods_ = [
        # Method 1: ConnectDevice(DeviceNumber) -> int
        COMMETHOD([], c_int, 'ConnectDevice',
                  (['in'], c_int, 'DeviceNumber')),

        # Method 2: DisconnectDevice(DeviceNumber) -> int
        COMMETHOD([], c_int, 'DisconnectDevice',
                  (['in'], c_int, 'DeviceNumber')),

        # Method 3: MeasureDcValue(FileName, DataIndex, Data) -> int
        # Must be defined even if not used!
        COMMETHOD([], c_int, 'MeasureDcValue',
                  (['in'], BSTR, 'FileName'),
                  (['in'], c_int, 'DataIndex'),
                  (['out'], POINTER(VARIANT), 'Data')),

        # Method 4: MeasureEisValue(FileName, DataIndex, Data) -> int
        COMMETHOD([], c_int, 'MeasureEisValue',
                  (['in'], BSTR, 'FileName'),
                  (['in'], c_int, 'DataIndex'),
                  (['out'], POINTER(VARIANT), 'Data')),

        # Method 5: MeasureNumberOfPoints(FileName) -> int
        COMMETHOD([], c_int, 'MeasureNumberOfPoints',
                  (['in'], BSTR, 'FileName')),

        # Method 6: GetDeviceChannelList(Device, ChannelArray) -> int
        COMMETHOD([], c_int, 'GetDeviceChannelList',
                  (['in'], c_int, 'Device'),
                  (['out'], POINTER(VARIANT), 'ChannelArray')),

        # Method 7: LoadSettings(Device, Channel, FileName) -> int
        COMMETHOD([], c_int, 'LoadSettings',
                  (['in'], c_int, 'Device'),
                  (['in'], c_int, 'Channel'),
                  (['in'], BSTR, 'FileName')),

        # Method 8: RunChannel(Device, Channel, FileName) -> int
        COMMETHOD([], c_int, 'RunChannel',
                  (['in'], c_int, 'Device'),
                  (['in'], c_int, 'Channel'),
                  (['in'], BSTR, 'FileName')),

        # Method 9: StopChannel(Device, Channel) -> int
        COMMETHOD([], c_int, 'StopChannel',
                  (['in'], c_int, 'Device'),
                  (['in'], c_int, 'Channel')),

        # Method 10: GetDataFileName(Device, Channel, Technique, FileName) -> int
        COMMETHOD([], c_int, 'GetDataFileName',
                  (['in'], c_int, 'Device'),
                  (['in'], c_int, 'Channel'),
                  (['in'], c_int, 'Technique'),
                  (['out'], POINTER(VARIANT), 'FileName')),

        # Method 11: MeasureStatus(Device, Channel, CurrentValues) -> int
        COMMETHOD([], c_int, 'MeasureStatus',
                  (['in'], c_int, 'Device'),
                  (['in'], c_int, 'Channel'),
                  (['out'], POINTER(VARIANT), 'CurrentValues')),

        # Method 12: TestConnection(DeviceNumber) -> int
        COMMETHOD([], c_int, 'TestConnection',
                  (['in'], c_int, 'DeviceNumber')),

        # Method 13: ConnectDeviceByIP(IPaddress, DeviceNumber) -> int
        COMMETHOD([], c_int, 'ConnectDeviceByIP',
                  (['in'], BSTR, 'IPaddress'),
                  (['out'], POINTER(c_int), 'DeviceNumber')),
    ]

# Setup results directory
RESULTS_DIR = "C:\\Users\\CV166\\Documents\\LabWindowsCVI\\BatteryApplication\\battery-exploder\\python_testing\\results"
if not os.path.exists(RESULTS_DIR):
    os.makedirs(RESULTS_DIR)
    print(f"Created results directory: {os.path.abspath(RESULTS_DIR)}")

# Setup logging
log_filename = os.path.join(RESULTS_DIR, f'test_{datetime.now().strftime("%Y%m%d_%H%M%S")}.log')

# Print log location for user
print("=" * 70)
print(f"Log file: {os.path.abspath(log_filename)}")
print("=" * 70)

# Configure logging
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s.%(msecs)03d - %(levelname)s - %(message)s',
    datefmt='%H:%M:%S',
    handlers=[
        logging.FileHandler(log_filename, mode='w', encoding='utf-8'),
        logging.StreamHandler(sys.stdout)
    ]
)

# Test that logging works
logging.info("Logging initialized successfully")
logging.info(f"Log file location: {os.path.abspath(log_filename)}")

class ECLabTester:
    """EC-Lab OLE COM test interface"""

    def __init__(self, device_number=0, channel=1):
        self.interface = None
        self.device_number = device_number
        self.channel = channel
        self.connected = False

    def connect_to_eclab(self):
        """Initialize COM connection to EC-Lab using comtypes custom interface"""
        try:
            logging.info("=" * 70)
            logging.info("Initializing COM connection to EC-Lab via comtypes")
            logging.info("=" * 70)

            logging.info(f"Step 1: Creating COM instance")
            logging.info(f"  CLSID: {CLSID_EClabExe}")
            logging.info(f"  IID:   {IID_IEClabExe}")
            logging.info(f"  Using custom IEClabExe interface (NOT IDispatch)")

            # Create COM instance with custom interface
            # This is equivalent to CoCreateInstance(&CLSID_EClabExe, ..., &IID_IEClabExe, ...)
            self.interface = CreateObject(CLSID_EClabExe, interface=IEClabExe)

            logging.info("SUCCESS: COM instance created with custom interface")
            logging.info("NOTE: Direct vtable access like C code - no IDispatch")
            logging.info("=" * 70)
            return True

        except OSError as e:
            logging.error("=" * 70)
            logging.error(f"FAILED to connect to EC-Lab COM server: {e}")
            logging.error("")

            # Decode common COM error codes
            if hasattr(e, 'winerror'):
                error_code = e.winerror
                if error_code == -2147221005:  # 0x80040153
                    logging.error("Error: REGDB_E_CLASSNOTREG - COM class not registered")
                    logging.error("Solution: Run 'ECLab.exe /regserver' as Administrator")
                elif error_code == -2147221164:  # 0x800401F4
                    logging.error("Error: CO_E_APPNOTFOUND - EC-Lab application not found")
                elif error_code == -2147467262:  # 0x80004002
                    logging.error("Error: E_NOINTERFACE - Interface not supported")
                    logging.error("This should not happen with comtypes + custom interface")
                elif error_code == -2147023174:  # 0x800706BA
                    logging.error("Error: RPC_S_SERVER_UNAVAILABLE - EC-Lab not running")
                    logging.error("Solution: Start EC-Lab application first")
                else:
                    logging.error(f"Error code: 0x{error_code & 0xFFFFFFFF:08X}")

            logging.error("")
            logging.error("Common causes:")
            logging.error("  1. EC-Lab is not running - START EC-Lab first")
            logging.error("  2. EC-Lab not registered: run 'ECLab.exe /regserver' as Administrator")
            logging.error("  3. Incorrect CLSID/IID")
            logging.error("  4. EC-Lab version doesn't support OLE COM")
            logging.error("=" * 70)
            return False

        except Exception as e:
            logging.error("=" * 70)
            logging.error(f"Unexpected error: {e}")
            logging.error("=" * 70)
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

    def load_settings(self, mps_path, device=None, channel=None):
        """Load settings from .mps file

        Args:
            mps_path: Path to .mps settings file
            device: Device number (defaults to self.device_number)
            channel: Channel number (defaults to self.channel)
        """
        if device is None:
            device = self.device_number
        if channel is None:
            channel = self.channel

        try:
            logging.info(f"Calling LoadSettings({device}, {channel}, '{mps_path}')...")
            ret = self.interface.LoadSettings(device, channel, mps_path)
            logging.info(f"LoadSettings returned: {ret}")

            if ret == 1:
                return True
            else:
                logging.error(f"LoadSettings failed with return value: {ret}")
                return False

        except Exception as e:
            logging.error(f"Exception during LoadSettings: {e}")
            logging.error(f"Signature: LoadSettings(device={device}, channel={channel}, path='{mps_path}')")
            return False

    def cleanup(self):
        """Release COM resources"""
        if self.connected:
            self.disconnect_device()

        # Release interface (comtypes handles reference counting)
        if self.interface is not None:
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
    """Test if LoadSettings prevents disconnect (with initialization delay)"""
    logging.info("=" * 70)
    logging.info("TEST 3: LoadSettings After Connection (with 2s initialization delay)")
    logging.info("=" * 70)

    tester = ECLabTester()

    if not tester.connect_to_eclab() or not tester.connect_device():
        return False

    # Add delay to allow EC-Lab to fully initialize the channel
    # EC-Lab needs time after ConnectDevice() before it can accept LoadSettings()
    initialization_delay = 2.0  # seconds
    logging.info(f"Waiting {initialization_delay}s for channel initialization...")
    time.sleep(initialization_delay)

    # Load settings after initialization delay
    logging.info("Loading settings after initialization delay...")
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
    logging.info(f"Working directory: {os.getcwd()}")
    logging.info(f"Log file: {os.path.abspath(log_filename)}")
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

    # Test 3: LoadSettings (requires .mps file path)
    mps_path = os.path.join(os.path.dirname(__file__), "templates", "simple_eis.mps")
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

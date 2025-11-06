"""
EC-Lab Simple LoadSettings Test

Purpose: Minimal test to verify LoadSettings functionality with proper initialization delay.
         Connect once, load settings, then disconnect cleanly.

Author: Battery Exploder Team
Date: 2025-11-06

PREREQUISITES:
==============
1. EC-Lab must be running BEFORE running this script
2. Python packages: pip install comtypes
3. EC-Lab registered: ECLab.exe /regserver (as Administrator)
4. BioLogic device connected (or EC-Lab in simulation mode)
5. Valid .mps settings file available
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

# EC-Lab COM identifiers
CLSID_EClabExe = GUID("{77FE5C93-42EE-4127-944B-5BA14FD33447}")
IID_IEClabExe = GUID("{642C68D2-85BD-494B-93EB-583CCBB11794}")

# Define IEClabExe COM Interface
class IEClabExe(IUnknown):
    """EC-Lab IEClabExe COM Interface"""
    _iid_ = IID_IEClabExe
    _methods_ = [
        # Method 1: ConnectDevice(DeviceNumber) -> int
        COMMETHOD([], c_int, 'ConnectDevice',
                  (['in'], c_int, 'DeviceNumber')),

        # Method 2: DisconnectDevice(DeviceNumber) -> int
        COMMETHOD([], c_int, 'DisconnectDevice',
                  (['in'], c_int, 'DeviceNumber')),

        # Method 3: MeasureDcValue(FileName, DataIndex, Data) -> int
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
RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
if not os.path.exists(RESULTS_DIR):
    os.makedirs(RESULTS_DIR)
    print(f"Created results directory: {os.path.abspath(RESULTS_DIR)}")

# Setup logging
log_filename = os.path.join(RESULTS_DIR, f'simple_load_test_{datetime.now().strftime("%Y%m%d_%H%M%S")}.log')

# Print log location for user
print("=" * 70)
print(f"Log file: {os.path.abspath(log_filename)}")
print("=" * 70)

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s.%(msecs)03d - %(levelname)s - %(message)s',
    datefmt='%H:%M:%S',
    handlers=[
        logging.FileHandler(log_filename, mode='w', encoding='utf-8'),
        logging.StreamHandler(sys.stdout)
    ]
)

logging.info("Logging initialized successfully")
logging.info(f"Log file location: {os.path.abspath(log_filename)}")


def simple_load_test(device_number=0, channel=1, mps_path=None, initialization_delay=2.0):
    """
    Simple test: Connect to EC-Lab, connect device, load settings, disconnect.

    Args:
        device_number: Device number (0-based), default 0
        channel: Channel number (0-based), default 1
        mps_path: Path to .mps settings file
        initialization_delay: Delay in seconds after ConnectDevice before LoadSettings

    Returns:
        True if test passes, False otherwise
    """

    logging.info("=" * 70)
    logging.info("EC-Lab Simple LoadSettings Test")
    logging.info("=" * 70)
    logging.info(f"Device: {device_number}")
    logging.info(f"Channel: {channel}")
    logging.info(f"Settings file: {mps_path}")
    logging.info(f"Initialization delay: {initialization_delay}s")
    logging.info("")

    interface = None

    try:
        # ====================================================================
        # Step 1: Create COM connection to EC-Lab
        # ====================================================================
        logging.info("Step 1: Creating COM connection to EC-Lab")
        logging.info("-" * 70)

        try:
            interface = CreateObject(CLSID_EClabExe, interface=IEClabExe)
            logging.info("SUCCESS: COM connection established")
        except OSError as e:
            logging.error(f"FAILED: Could not connect to EC-Lab COM server")
            logging.error(f"Error: {e}")
            logging.error("")
            logging.error("Common causes:")
            logging.error("  1. EC-Lab is not running - START EC-Lab first")
            logging.error("  2. EC-Lab not registered: run 'ECLab.exe /regserver' as Administrator")
            return False

        logging.info("")

        # ====================================================================
        # Step 2: Connect to device
        # ====================================================================
        logging.info("Step 2: Connecting to device")
        logging.info("-" * 70)

        start_time = time.time()
        ret = interface.ConnectDevice(device_number)
        elapsed_ms = (time.time() - start_time) * 1000

        logging.info(f"ConnectDevice({device_number}) returned: {ret}")
        logging.info(f"Time elapsed: {elapsed_ms:.1f}ms")

        if ret != 1:
            logging.error("FAILED: ConnectDevice did not return 1 (success)")
            return False

        logging.info("SUCCESS: Device connected")
        logging.info("")

        # ====================================================================
        # Step 3: Wait for channel initialization
        # ====================================================================
        logging.info("Step 3: Waiting for channel initialization")
        logging.info("-" * 70)
        logging.info(f"Waiting {initialization_delay}s for EC-Lab to initialize channel...")

        time.sleep(initialization_delay)

        logging.info("Initialization delay complete")
        logging.info("")

        # ====================================================================
        # Step 4: Load settings
        # ====================================================================
        logging.info("Step 4: Loading settings file")
        logging.info("-" * 70)

        if not mps_path or not os.path.exists(mps_path):
            logging.error(f"FAILED: Settings file not found: {mps_path}")
            return False

        logging.info(f"Loading: {mps_path}")

        start_time = time.time()
        ret = interface.LoadSettings(device_number, channel, mps_path)
        elapsed_ms = (time.time() - start_time) * 1000

        logging.info(f"LoadSettings({device_number}, {channel}, ...) returned: {ret}")
        logging.info(f"Time elapsed: {elapsed_ms:.1f}ms")

        if ret != 1:
            logging.error("FAILED: LoadSettings did not return 1 (success)")
            logging.error("")
            logging.error("Possible causes:")
            logging.error("  1. Settings file not compatible with hardware")
            logging.error("  2. Channel not fully initialized (try increasing initialization_delay)")
            logging.error("  3. Device/channel combination invalid")
            return False

        logging.info("SUCCESS: Settings loaded successfully")
        logging.info("")

        # ====================================================================
        # Step 5: Disconnect device
        # ====================================================================
        logging.info("Step 5: Disconnecting device")
        logging.info("-" * 70)

        ret = interface.DisconnectDevice(device_number)
        logging.info(f"DisconnectDevice({device_number}) returned: {ret}")

        if ret != 1:
            logging.warning("WARNING: DisconnectDevice did not return 1")
        else:
            logging.info("SUCCESS: Device disconnected")

        logging.info("")

        # ====================================================================
        # Test Complete
        # ====================================================================
        logging.info("=" * 70)
        logging.info("TEST RESULT: PASS")
        logging.info("=" * 70)
        logging.info("All steps completed successfully:")
        logging.info("  ✓ COM connection established")
        logging.info("  ✓ Device connected")
        logging.info("  ✓ Settings loaded successfully")
        logging.info("  ✓ Device disconnected")
        logging.info("")

        return True

    except Exception as e:
        logging.error("=" * 70)
        logging.error(f"TEST RESULT: FAIL")
        logging.error("=" * 70)
        logging.error(f"Unexpected error: {e}", exc_info=True)
        return False

    finally:
        # Release COM interface
        if interface is not None:
            interface = None
            logging.debug("COM interface released")


def main():
    """Main entry point"""

    # Default settings file path
    mps_path = os.path.join(
        os.path.dirname(__file__),
        "templates",
        "simple_ocv.mps"
    )

    # Check if file exists
    if not os.path.exists(mps_path):
        logging.error("=" * 70)
        logging.error("ERROR: Settings file not found")
        logging.error("=" * 70)
        logging.error(f"Expected path: {mps_path}")
        logging.error("")
        logging.error("Please ensure the .mps template file exists")
        logging.error("You can specify a different file by editing the mps_path variable in main()")
        return 1

    # Run the test
    success = simple_load_test(
        device_number=0,
        channel=1,
        mps_path=mps_path,
        initialization_delay=2.0
    )

    # Return exit code
    return 0 if success else 1


if __name__ == "__main__":
    try:
        exit_code = main()
        sys.exit(exit_code)
    except KeyboardInterrupt:
        logging.info("\n\nTest interrupted by user")
        sys.exit(1)
    except Exception as e:
        logging.error(f"\n\nUnexpected error: {e}", exc_info=True)
        sys.exit(1)

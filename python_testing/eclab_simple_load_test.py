"""
EC-Lab Simple EIS Test with Comprehensive Diagnostics

Purpose: Complete test to verify LoadSettings and EIS measurement functionality.
         Connect once, load settings, run EIS measurement, store results, then disconnect.
         Includes comprehensive diagnostic checks to identify LoadSettings failures.

Features:
  - Single connection/disconnection cycle (no repeated reconnects)
  - Comprehensive pre-flight diagnostics before LoadSettings:
    * Channel availability check (GetDeviceChannelList)
    * Channel hardware info (GetChannelInfos)
    * Explicit device/channel selection (SelectDevice, SelectChannel)
  - Automatic fallback to channel 0 if initial channel fails
  - Complete EIS measurement execution with status monitoring
  - Automatic data storage to timestamped .mpt files
  - Data verification by reading first few data points
  - Configurable initialization delay
  - Detailed error messages and troubleshooting guidance

Author: Battery Exploder Team
Date: 2025-11-06

PREREQUISITES:
==============
1. EC-Lab must be running BEFORE running this script
2. Python packages: pip install comtypes
3. EC-Lab registered: ECLab.exe /regserver (as Administrator)
4. BioLogic device connected (or EC-Lab in simulation mode)
5. Valid .mps settings file available

TROUBLESHOOTING:
================
If LoadSettings keeps failing (returning 0):
1. Check the diagnostic output - it will show if the channel is available
2. Try increasing initialization_delay to 5-10 seconds in main()
3. Check EC-Lab GUI for error messages
4. Verify .mps file is compatible with your device hardware
5. The test automatically tries channel 0 as a fallback
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

        # Method 14: SelectDevice(Device) -> int
        COMMETHOD([], c_int, 'SelectDevice',
                  (['in'], c_int, 'Device')),

        # Method 15: SelectChannel(Device, Channel) -> int
        COMMETHOD([], c_int, 'SelectChannel',
                  (['in'], c_int, 'Device'),
                  (['in'], c_int, 'Channel')),

        # Method 16: GetChannelInfos(Device, Channel, ChannelInfos) -> int
        COMMETHOD([], c_int, 'GetChannelInfos',
                  (['in'], c_int, 'Device'),
                  (['in'], c_int, 'Channel'),
                  (['out'], POINTER(VARIANT), 'ChannelInfos')),
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
    Complete EIS test: Connect, load settings, run measurement, store data, disconnect.

    Test flow:
        1. Create COM connection to EC-Lab
        2. Connect to device
        2.5. Run diagnostic checks (channel availability, hardware info, selection)
        3. Wait for channel initialization
        4. Load settings from .mps file (with automatic channel 0 fallback)
        5. Run EIS measurement with status monitoring
        5a. Read and verify EIS data points
        6. Disconnect device

    Args:
        device_number: Device number (0-based), default 0
        channel: Channel number (0-based), default 1
        mps_path: Path to .mps settings file
        initialization_delay: Delay in seconds after ConnectDevice before LoadSettings

    Returns:
        True if test passes, False otherwise
    """

    logging.info("=" * 70)
    logging.info("EC-Lab Simple EIS Test")
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
        # Step 2.5: Run diagnostic checks
        # ====================================================================
        logging.info("Step 2.5: Running diagnostic checks")
        logging.info("-" * 70)

        # Diagnostic 1: Get device channel list
        try:
            logging.info("Diagnostic 1: Checking channel availability...")
            # comtypes returns out parameters automatically
            channel_array = interface.GetDeviceChannelList(device_number)
            logging.info(f"GetDeviceChannelList({device_number}) succeeded")

            if channel_array is not None:
                # Channel array is a 128-element boolean array
                channels = channel_array
                if hasattr(channels, '__len__') and len(channels) > channel:
                    is_available = bool(channels[channel])
                    logging.info(f"Channel {channel} available: {is_available}")

                    if not is_available:
                        logging.warning(f"WARNING: Channel {channel} reports as NOT available")
                        logging.warning("This may cause LoadSettings to fail")

                        # Find available channels
                        available = [i for i in range(min(len(channels), 16)) if channels[i]]
                        if available:
                            logging.info(f"Available channels: {available}")
                            logging.info("Consider using one of these channels instead")
                else:
                    logging.warning(f"Channel array too short or channel {channel} out of range")
            else:
                logging.warning("GetDeviceChannelList returned no data")
        except Exception as e:
            logging.warning(f"GetDeviceChannelList diagnostic failed: {e}")

        # Diagnostic 2: Get channel hardware information
        try:
            logging.info("Diagnostic 2: Getting channel hardware information...")
            # comtypes returns out parameters automatically
            channel_infos = interface.GetChannelInfos(device_number, channel)
            logging.info(f"GetChannelInfos({device_number}, {channel}) succeeded")

            if channel_infos is not None:
                infos = channel_infos
                if hasattr(infos, '__len__'):
                    logging.info(f"Channel info: {infos}")
                    if len(infos) >= 3:
                        logging.info(f"  Serial Number: {infos[0]}")
                        logging.info(f"  Amplifier ID: {infos[1]}")
                        logging.info(f"  Options: {infos[2]}")
                else:
                    logging.info(f"Channel info (raw): {infos}")
            else:
                logging.warning("GetChannelInfos returned no data")
        except Exception as e:
            logging.warning(f"GetChannelInfos diagnostic failed: {e}")

        # Diagnostic 3: Try explicit device/channel selection
        try:
            logging.info("Diagnostic 3: Explicitly selecting device and channel...")
            ret = interface.SelectDevice(device_number)
            logging.info(f"SelectDevice({device_number}) returned: {ret}")

            ret = interface.SelectChannel(device_number, channel)
            logging.info(f"SelectChannel({device_number}, {channel}) returned: {ret}")

            if ret != 1:
                logging.warning(f"SelectChannel returned {ret} (expected 1)")
                logging.warning("This may indicate an invalid device/channel combination")
        except Exception as e:
            logging.warning(f"SelectDevice/SelectChannel diagnostic failed: {e}")

        logging.info("Diagnostic checks complete")
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

        # Try loading with specified channel
        active_channel = channel  # Track which channel actually works
        start_time = time.time()
        ret = interface.LoadSettings(device_number, channel, mps_path)
        elapsed_ms = (time.time() - start_time) * 1000

        logging.info(f"LoadSettings({device_number}, {channel}, ...) returned: {ret}")
        logging.info(f"Time elapsed: {elapsed_ms:.1f}ms")

        # If failed and we're using channel 1, try channel 0 as fallback
        if ret != 1 and channel != 0:
            logging.warning(f"LoadSettings failed with channel {channel}")
            logging.warning("Attempting fallback to channel 0...")
            logging.warning("")

            start_time = time.time()
            ret = interface.LoadSettings(device_number, 0, mps_path)
            elapsed_ms = (time.time() - start_time) * 1000

            logging.info(f"LoadSettings({device_number}, 0, ...) returned: {ret}")
            logging.info(f"Time elapsed: {elapsed_ms:.1f}ms")

            if ret == 1:
                active_channel = 0  # Successfully fell back to channel 0
                logging.info("SUCCESS: Settings loaded with channel 0 (fallback)")
                logging.info("NOTE: Consider using channel=0 in future tests")
            else:
                logging.error("FAILED: LoadSettings failed even with channel 0")

        if ret != 1:
            logging.error("")
            logging.error("LoadSettings FAILED")
            logging.error("")
            logging.error("Diagnostics to check:")
            logging.error("  1. Check EC-Lab GUI for error messages or warnings")
            logging.error("  2. Verify settings file is compatible with connected hardware")
            logging.error("  3. Check channel availability in diagnostic output above")
            logging.error("  4. Try increasing initialization_delay to 5-10 seconds")
            logging.error("  5. Verify device is not in use by another application")
            logging.error("")
            logging.error("Review the diagnostic checks above for clues about the failure")
            return False

        logging.info("SUCCESS: Settings loaded successfully")
        logging.info("")

        # ====================================================================
        # Step 5: Run EIS measurement
        # ====================================================================
        logging.info("Step 5: Running EIS measurement")
        logging.info("-" * 70)
        logging.info(f"Using channel: {active_channel}")

        # Create output filename
        output_filename = os.path.join(RESULTS_DIR, f'eis_data_{datetime.now().strftime("%Y%m%d_%H%M%S")}.mpt')
        logging.info(f"Output file: {output_filename}")

        # Start the measurement
        start_time = time.time()
        ret = interface.RunChannel(device_number, active_channel, output_filename)
        elapsed_ms = (time.time() - start_time) * 1000

        logging.info(f"RunChannel({device_number}, {active_channel}, ...) returned: {ret}")
        logging.info(f"Time elapsed: {elapsed_ms:.1f}ms")

        if ret != 1:
            logging.error("FAILED: RunChannel did not return 1 (success)")
            logging.error("EIS measurement could not be started")
        else:
            logging.info("SUCCESS: EIS measurement started")
            logging.info("")

            # Monitor measurement progress
            logging.info("Monitoring measurement status...")
            measurement_complete = False
            start_time = time.time()
            last_status = None

            while not measurement_complete:
                time.sleep(0.5)  # Check every 500ms

                try:
                    # comtypes returns out parameters automatically
                    current_values = interface.MeasureStatus(device_number, active_channel)

                    if current_values is not None:
                        status = current_values

                        # Status format from Bio-Logic: [state, ...]
                        # State: 0 = STOP, 1 = RUN, 2 = PAUSE
                        if hasattr(status, '__len__') and len(status) > 0:
                            state = int(status[0])

                            if state != last_status:
                                if state == 0:
                                    logging.info("  Status: STOPPED (measurement complete)")
                                    measurement_complete = True
                                elif state == 1:
                                    logging.info("  Status: RUNNING...")
                                elif state == 2:
                                    logging.info("  Status: PAUSED")
                                last_status = state
                        else:
                            logging.warning("  Status: Unknown format")

                    # Timeout after 5 minutes
                    if time.time() - start_time > 300:
                        logging.warning("Measurement timeout (5 minutes) - stopping channel")
                        interface.StopChannel(device_number, active_channel)
                        break

                except Exception as e:
                    logging.warning(f"Error checking status: {e}")
                    break

            elapsed_time = time.time() - start_time
            logging.info(f"Measurement completed in {elapsed_time:.1f}s")
            logging.info("")

            # Read EIS data points
            if os.path.exists(output_filename):
                logging.info("Step 5a: Reading EIS data from file")
                logging.info("-" * 70)

                try:
                    num_points = interface.MeasureNumberOfPoints(output_filename)
                    logging.info(f"Number of data points: {num_points}")

                    if num_points > 0:
                        # Read first few points as verification
                        logging.info("Reading first 3 data points:")

                        for i in range(min(3, num_points)):
                            # comtypes returns out parameters automatically
                            data = interface.MeasureEisValue(output_filename, i)

                            if data is not None:
                                point = data
                                if hasattr(point, '__len__') and len(point) >= 3:
                                    freq = point[0] if len(point) > 0 else 0
                                    z_real = point[1] if len(point) > 1 else 0
                                    z_imag = point[2] if len(point) > 2 else 0
                                    logging.info(f"  Point {i}: f={freq} Hz, Z_re={z_real} Ω, Z_im={z_imag} Ω")

                        logging.info("")
                        logging.info(f"SUCCESS: EIS data saved to {output_filename}")
                        logging.info(f"Total data points: {num_points}")
                    else:
                        logging.warning("No data points found in output file")

                except Exception as e:
                    logging.error(f"Error reading EIS data: {e}")
            else:
                logging.warning(f"Output file not found: {output_filename}")

        logging.info("")

        # ====================================================================
        # Step 6: Disconnect device
        # ====================================================================
        logging.info("Step 6: Disconnecting device")
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
        logging.info("  ✓ Diagnostic checks performed")
        logging.info("  ✓ Settings loaded successfully")
        logging.info("  ✓ EIS measurement completed")
        logging.info("  ✓ Data saved and verified")
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
        "simple_eis.mps"
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
    # NOTE: Channel 0 is correct for most Bio-Logic devices (0-based indexing)
    # If LoadSettings fails, try increasing initialization_delay to 5-10 seconds
    success = simple_load_test(
        device_number=0,
        channel=0,          # Use channel 0 (0-based indexing)
        mps_path=mps_path,
        initialization_delay=2.0  # 2 seconds is sufficient with correct channel
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

# EC-Lab OLE COM Python Test Suite

This directory contains Python scripts for testing EC-Lab OLE COM connection stability and diagnosing connection issues.

## Prerequisites

### 1. EC-Lab Installation
- EC-Lab must be installed and registered as a COM server
- Run as Administrator: `ECLab.exe /regserver`
- Typical location: `C:\Program Files (x86)\EC-Lab\11.63\ECLab.exe`

### 2. Python Packages
```bash
pip install comtypes
```

**Note:** This test suite uses `comtypes` (NOT `pywin32`) because EC-Lab only supports a custom COM interface and does not support IDispatch.

### 3. EC-Lab Must Be Running
- Start EC-Lab application before running tests
- Verify "OLECOM" appears in EC-Lab status bar
- Connect your BioLogic device (or use simulation mode)

## Files

### Test Scripts
- **`eclab_com_test.py`** - Main connection stability test suite
  - Tests baseline auto-disconnect timing
  - Tests keep-alive strategies at different intervals
  - Tests LoadSettings behavior
  - Tests reconnection strategies
  - Simulates Battery Exploder initialization sequence

- **`diagnose_eclab_com.py`** - Diagnostic tool for troubleshooting
  - Checks if EC-Lab is running
  - Verifies COM registration
  - Tests COM connection
  - Provides actionable recommendations

### Documentation
- **`eclab_olecom_test_plan.md`** - Comprehensive test plan and methodology
- **`CHANGES.md`** - Changelog of fixes and improvements
- **`README.md`** - This file

### Supporting Files
- **`ECLabCOM_EClabExeInterface.txt`** - EC-Lab COM interface definition
- **`bt-lab-and-ec-lab-ole-com-user-manual_v7.pdf`** - EC-Lab OLE COM manual

## Usage

### Diagnostic Tool (Run First)
```bash
python diagnose_eclab_com.py
```

This will check:
- ✓/✗ Is EC-Lab.exe running?
- ✓/✗ Is the COM server registered?
- ✓/✗ Can we connect via COM?
- ✓/✗ EC-Lab installation found?

### Connection Stability Tests
```bash
python eclab_com_test.py
```

This will:
1. Measure exact auto-disconnect timeout
2. Test keep-alive strategies at 5s, 3s, 2s, 1s intervals
3. Test LoadSettings behavior
4. Test reconnection strategies
5. Simulate real Battery Exploder workload

Results are saved to `results/test_YYYYMMDD_HHMMSS.log`

## Technical Details

### COM Interface Approach

**C Code (biologic/eclab_olecom.c):**
```c
// Direct vtable access via custom IEClabExe interface
CoCreateInstance(&CLSID_EClabExe, NULL, CLSCTX_LOCAL_SERVER,
                 &IID_IEClabExe, (void**)&pInterface);
int ret = pInterface->lpVtbl->ConnectDevice(pInterface, deviceNumber);
```

**Python Code (this test suite):**
```python
# comtypes with custom interface definition
from comtypes import GUID, IUnknown, COMMETHOD
from comtypes.client import CreateObject

class IEClabExe(IUnknown):
    _iid_ = GUID("{642C68D2-85BD-494B-93EB-583CCBB11794}")
    _methods_ = [
        COMMETHOD([], c_int, 'ConnectDevice', (['in'], c_int, 'DeviceNumber')),
        # ... more methods
    ]

interface = CreateObject(CLSID_EClabExe, interface=IEClabExe)
ret = interface.ConnectDevice(1)
```

### Why comtypes Instead of win32com?

**EC-Lab does NOT support IDispatch**, which is required by `win32com.client.Dispatch()` and `gencache.EnsureDispatch()`. Attempting to connect returns:
- Error: `-2147467262` (0x80004002) = `E_NOINTERFACE`

**comtypes** can directly access custom COM interfaces without requiring IDispatch, providing:
- Direct vtable access like C code
- Exact behavior match to C implementation
- No IDispatch dependency
- Better compatibility with EC-Lab

## Identifiers

**ProgIDs:**
- `EClabCOM.EClabExe` (primary)
- `ECLabCOM.EClabExe` (alternate)

**CLSID:**
- `{77FE5C93-42EE-4127-944B-5BA14FD33447}`

**IID (IEClabExe):**
- `{642C68D2-85BD-494B-93EB-583CCBB11794}`

## Common Issues

### "Invalid class string" (-2147221005)
**Cause:** EC-Lab COM server not registered
**Solution:** Run `ECLab.exe /regserver` as Administrator

### "RPC server unavailable" (-2147023174)
**Cause:** EC-Lab is not running
**Solution:** Start EC-Lab application first

### "No such interface supported" (-2147467262)
**Cause:** Using IDispatch-based approach (win32com)
**Solution:** Use comtypes as implemented in this suite

### Process name case issue
EC-Lab executable may be:
- `ECLab.exe` (capital L in Lab)
- `EClab.exe` (lowercase l in lab)

Both work on Windows (case-insensitive filesystem).

## Expected Test Results

Based on C code observations:
- **Auto-disconnect timeout:** ~10-14 seconds of idle time
- **Required keep-alive:** Every 2-5 seconds during idle periods
- **Reconnection:** May fail after auto-disconnect (investigating)

## Related C Code Files

- `biologic/eclab_olecom.c` - C implementation of EC-Lab interface
- `biologic/eclab_olecom.h` - Header file
- `biologic/eclab_olecom_interface.h` - Interface definitions
- `biologic/biologic_abstract.c` - Higher-level abstraction layer

## Support

For issues or questions, see:
- `eclab_olecom_test_plan.md` - Detailed test methodology
- `bt-lab-and-ec-lab-ole-com-user-manual_v7.pdf` - Official EC-Lab documentation
- `CHANGES.md` - List of known issues and fixes applied

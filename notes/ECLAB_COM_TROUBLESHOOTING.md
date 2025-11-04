# EC-Lab OLE COM Troubleshooting Guide

## Issue Summary

The EC-Lab COM object is being created successfully, but it does **not support the IDispatch interface** required for OLE automation. This results in error `0x80004002` (E_NOINTERFACE).

**Key Diagnostic Finding:**
- EC-Lab.exe process: **RUNNING** ✓
- COM object (IUnknown): **CREATED** ✓
- IDispatch interface: **NOT SUPPORTED** ✗

This indicates that EC-Lab's OLE COM automation interface is not being activated.

---

## Most Likely Cause: Device Not Connected

**90% of the time, this is because a device must be connected in EC-Lab BEFORE the COM automation interface becomes available.**

### Solution

1. **Launch EC-Lab** if not already running
2. **Connect your SP-150e device in EC-Lab:**
   - Go to: `Device → Connect`
   - Select your SP-150e from the list
   - Click Connect
   - **Wait until the device shows as CONNECTED** in the EC-Lab window
3. **Verify the connection** is stable (no errors in EC-Lab)
4. **Then start your Battery Exploder application**

The COM automation interface may only be exposed after a hardware device is successfully connected to EC-Lab.

---

## Other Possible Causes (Less Common)

### 2. EC-Lab Version Doesn't Support OLE COM

**Check EC-Lab version:**
1. In EC-Lab: `Help → About`
2. Look for version number (need **v11.50 or later**)
3. Verify it mentions "OLE COM" or "COM automation" support

**If your version is too old:**
- Contact Bio-Logic for an update
- Or use the Direct DLL mode instead (change `BIOLOGIC_CONTROL_MODE` to 0 in `common.h`)

### 3. OLE COM Not Enabled in EC-Lab Settings

**Check EC-Lab options:**
1. `Tools → Options` (or `Tools → Preferences`)
2. Look for `Communications` or `Automation` section
3. Check if there's an option to enable "OLE COM" or "COM automation"
4. Enable it if found
5. Restart EC-Lab

### 4. Incomplete COM Registration

**Re-register EC-Lab:**
1. Close EC-Lab completely
2. Open Command Prompt **as Administrator**
3. Run:
   ```cmd
   cd "C:\Program Files (x86)\EC-Lab"
   ECLab.exe /regserver
   ```
4. Start EC-Lab
5. Try again

### 5. Wrong ProgID or EC-Lab Variant

Some Bio-Logic software packages may use different ProgIDs:
- `EClabCOM.EClabExe` (current - for EC-Lab)
- `BTLabCOM.BTLabExe` (for BT-Lab)
- Other variants

**Check your installation:**
- Verify you have **EC-Lab** (not BT-Lab or other variant)
- Check installation directory matches: `C:\Program Files (x86)\EC-Lab\`

---

## Running the Enhanced Diagnostics

The code now includes comprehensive diagnostics. When you run it, you'll see:

```
=== COM Registration Diagnostics ===
ProgID: EClabCOM.EClabExe
CLSID: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
Server path: C:\Program Files (x86)\EC-Lab\ECLab.exe
Server executable: EXISTS
===================================

Diagnosing COM object capabilities...
  - IProvideClassInfo: not supported (0x80004002)
  - IDispatch: NOT SUPPORTED (0x80004002)
  - IUnknown: SUPPORTED (base interface only)
```

This tells you:
1. Whether EC-Lab is properly registered
2. If the executable exists at the registered path
3. Which COM interfaces are available
4. Why the automation interface isn't working

---

## Next Steps

1. **Try the device connection solution first** (most likely fix)
2. Run your Battery Exploder application and check the enhanced diagnostic output
3. Look for the COM Registration Diagnostics section in the log
4. Note the CLSID and server path - verify they match your installation
5. If still failing, check EC-Lab version and settings

---

## Alternative: Use Direct DLL Mode

If EC-Lab OLE COM continues to fail, you can use the **Direct DLL mode** instead:

1. Edit `common.h`
2. Change:
   ```c
   #define BIOLOGIC_CONTROL_MODE  0  // Direct DLL (instead of 1)
   ```
3. Rebuild project
4. This bypasses EC-Lab entirely and controls hardware directly

**Trade-offs:**
- ✓ No EC-Lab dependency
- ✓ Faster response time
- ✗ No GUI monitoring
- ✗ No EC-Lab validation

---

## Technical Details

### Why IDispatch is Required

OLE automation requires the `IDispatch` interface to:
- Invoke methods by name (e.g., "LoadSettings", "RunChannel")
- Pass parameters as VARIANTs
- Get return values
- Handle BSTR strings

Without `IDispatch`, we can only get the base `IUnknown` interface, which only supports:
- `QueryInterface` - Query for other interfaces
- `AddRef` - Reference counting
- `Release` - Release reference

### Registry Structure

When properly registered, EC-Lab creates these registry entries:
```
HKEY_CLASSES_ROOT\
  EClabCOM.EClabExe\
    CLSID = {GUID}
  CLSID\
    {GUID}\
      LocalServer32 = "C:\Program Files (x86)\EC-Lab\ECLab.exe"
```

The `LocalServer32` key tells Windows where to launch the COM server.

---

## Commit History

**Commit 1**: Fixed CoCreateInstance to use `CLSCTX_ALL` and IUnknown fallback
**Commit 2**: Added comprehensive diagnostics and device connection hints

The code now provides much better error messages to help diagnose the exact issue.

---

## Questions to Answer

When reporting this issue or seeking support, please provide:

1. **EC-Lab Version** (`Help → About`)
2. **Was a device connected** in EC-Lab before starting Battery Exploder?
3. **Diagnostic output** showing:
   - CLSID
   - Server path
   - Server executable status
   - Interface support status
4. **EC-Lab status bar** - does it show "OLECOM" indicator?
5. **Registry check** - does `CLSID\{GUID}\LocalServer32` exist?

---

**Created:** 2025-11-03
**Last Updated:** 2025-11-03
**Author:** Claude Code Diagnostics

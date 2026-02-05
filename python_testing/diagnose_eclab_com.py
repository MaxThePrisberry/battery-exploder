"""
EC-Lab COM Server Diagnostic Tool

Purpose: Diagnose EC-Lab OLE COM registration and connectivity issues
Usage: python diagnose_eclab_com.py

Author: Battery Exploder Team
Date: 2025-11-05
"""

import winreg
import win32com.client
import pythoncom
import os
import subprocess
import sys

print("=" * 70)
print("EC-Lab COM Server Diagnostic Tool")
print("=" * 70)

# 1. Check if EC-Lab process is running
print("\n1. Checking if EC-Lab is running...")
try:
    result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq EClab.exe'],
                          capture_output=True, text=True)
    if 'ECLab.exe' in result.stdout:
        print("   ✓ EC-Lab.exe is running")
    else:
        print("   ✗ EC-Lab.exe is NOT running")
        print("   → Start EC-Lab before testing COM connection")
except Exception as e:
    print(f"   ? Could not check process status: {e}")

# 2. Check ProgID registration
print("\n2. Checking ProgID registration...")
progids = ['EClabCOM.EClabExe', 'ECLabCOM.ECLabInterface', 'ECLabCOM.EClabExe']
found_progid = None

for progid in progids:
    try:
        key = winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, progid)
        clsid_key = winreg.OpenKey(key, 'CLSID')
        clsid = winreg.QueryValue(clsid_key, '')
        print(f"   ✓ {progid} is registered")
        print(f"     CLSID: {clsid}")
        found_progid = progid
        winreg.CloseKey(clsid_key)
        winreg.CloseKey(key)
    except FileNotFoundError:
        print(f"   ✗ {progid} NOT registered")

# 3. Check CLSID directly
print("\n3. Checking expected CLSID...")
clsid = '{77FE5C93-42EE-4127-944B-5BA14FD33447}'
try:
    key = winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, f'WOW6432Node\\CLSID\\{clsid}')
    print(f"   ✓ CLSID {clsid} is registered")
    try:
        server_key = winreg.OpenKey(key, 'LocalServer32')
        server_path = winreg.QueryValue(server_key, '')
        print(f"     LocalServer32: {server_path}")

        # Clean up path (remove quotes and parameters)
        clean_path = server_path.strip('"').split()[0] if server_path else ""

        if clean_path and os.path.exists(clean_path):
            print(f"     ✓ EC-Lab executable exists")
        else:
            print(f"     ✗ EC-Lab executable NOT found at path")

        winreg.CloseKey(server_key)
    except Exception as e:
        print(f"     ✗ LocalServer32 entry not found: {e}")

    # Check ProgID
    try:
        progid_key = winreg.OpenKey(key, 'ProgID')
        registered_progid = winreg.QueryValue(progid_key, '')
        print(f"     ProgID: {registered_progid}")
        winreg.CloseKey(progid_key)
    except:
        print("     ProgID: Not specified in CLSID")

    winreg.CloseKey(key)
except FileNotFoundError:
    print(f"   ✗ CLSID {clsid} NOT registered")
    print("   → Run: ECLab.exe /regserver (as Administrator)")
    print("   → Example: cd \"C:\\Program Files (x86)\\11.63\\EC-Lab\" && ECLab.exe /regserver")

# 4. Search for all EC-Lab related COM registrations
print("\n4. Searching for all EC-Lab related COM entries...")
found_entries = []
root = winreg.HKEY_CLASSES_ROOT
i = 0

try:
    while i < 10000:  # Limit search to prevent infinite loop
        try:
            key_name = winreg.EnumKey(root, i)
            if any(x in key_name.lower() for x in ['eclab', 'elab', 'biologic']):
                found_entries.append(key_name)
            i += 1
        except OSError:
            break
except Exception as e:
    print(f"   ? Search error: {e}")

if found_entries:
    print(f"   Found {len(found_entries)} EC-Lab related entries:")
    for entry in found_entries[:10]:  # Show first 10
        print(f"     - {entry}")
    if len(found_entries) > 10:
        print(f"     ... and {len(found_entries) - 10} more")
else:
    print("   ✗ No EC-Lab related entries found in registry")

# 5. Test COM connection
print("\n5. Testing COM connection...")
if found_progid:
    try:
        pythoncom.CoInitialize()
        print(f"   Attempting connection with: {found_progid}")
        obj = win32com.client.Dispatch(found_progid)
        print(f"   ✓ Successfully connected to {found_progid}")
        print(f"   ✓ COM object created successfully")
        del obj
        pythoncom.CoUninitialize()
    except Exception as e:
        print(f"   ✗ Failed to connect: {e}")
        print(f"   Error code: {e.args[0] if e.args else 'Unknown'}")

        # Provide specific guidance based on error
        if hasattr(e, 'args') and len(e.args) > 0:
            error_code = e.args[0]
            if error_code == -2147221005:  # 0x80040153
                print("   → This is REGDB_E_CLASSNOTREG - class not registered")
                print("   → Solution: Register EC-Lab with /regserver")
            elif error_code == -2147221164:  # 0x800401F4
                print("   → This is CO_E_APPNOTFOUND - application not found")
                print("   → Solution: Check EC-Lab installation path")
else:
    print("   ✗ Skipped - no registered ProgID found")
    print("   → EC-Lab COM server is not registered on this system")

# 6. Check for EC-Lab installation
print("\n6. Searching for EC-Lab installation...")
common_paths = [
    r"C:\Program Files (x86)\EC-Lab\11.63\ECLab.exe",
]

found_installation = False
for path in common_paths:
    if os.path.exists(path):
        print(f"   ✓ Found EC-Lab at: {path}")
        found_installation = True

        # Get version info if possible
        try:
            result = subprocess.run([path, '/?'], capture_output=True, text=True, timeout=2)
        except:
            pass

if not found_installation:
    print("   ✗ EC-Lab not found in common installation locations")
    print("   → EC-Lab may be installed in a different location")
    print("   → Or EC-Lab may not be installed on this computer")

print("\n" + "=" * 70)
print("Diagnostic Summary")
print("=" * 70)

# Summary
issues = []
if 'EC-Lab.exe is NOT running' in str(sys.stdout):
    issues.append("EC-Lab is not running - start EC-Lab application")
if not found_progid:
    issues.append("EC-Lab COM server is not registered - run ECLab.exe /regserver as Administrator")
if not found_installation:
    issues.append("EC-Lab may not be installed on this system")

if issues:
    print("\nIssues found:")
    for i, issue in enumerate(issues, 1):
        print(f"  {i}. {issue}")
    print("\nNext steps:")
    print("  1. Install EC-Lab if not present")
    print("  2. Register COM server: ECLab.exe /regserver (as Administrator)")
    print("  3. Start EC-Lab application and verify 'OLECOM' in status bar")
    print("  4. Re-run this diagnostic tool to verify")
else:
    print("\n✓ No obvious issues detected")
    print("  If connection still fails, check:")
    print("  - EC-Lab version supports OLE COM")
    print("  - Windows firewall/antivirus settings")
    print("  - User permissions")

print("=" * 70)

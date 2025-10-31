/******************************************************************************
 * eclab_olecom.c
 *
 * Implementation of EC-Lab OLE COM wrapper
 *
 * This file contains Windows COM automation code for interfacing with EC-Lab.
 * It handles IDispatch method invocation, BSTR string conversions, and
 * VARIANT type management.
 ******************************************************************************/

#include "eclab_olecom.h"
#include "logging.h"
#include <oleauto.h>
#include <tlhelp32.h>
#include <string.h>

/******************************************************************************
 * VARIANT Access Macros for LabWindows/CVI
 ******************************************************************************/

// LabWindows/CVI uses named unions in VARIANT structure (NONAMELESSUNION mode)
// Access pattern: variant.n1.n2.vt and variant.n1.n2.n3.lVal
#ifndef V_VT
#define V_VT(X)         ((X)->n1.n2.vt)
#endif
#ifndef V_I4
#define V_I4(X)         ((X)->n1.n2.n3.lVal)
#endif
#ifndef V_R8
#define V_R8(X)         ((X)->n1.n2.n3.dblVal)
#endif
#ifndef V_BOOL
#define V_BOOL(X)       ((X)->n1.n2.n3.boolVal)
#endif
#ifndef V_BSTR
#define V_BSTR(X)       ((X)->n1.n2.n3.bstrVal)
#endif
#ifndef V_ARRAY
#define V_ARRAY(X)      ((X)->n1.n2.n3.parray)
#endif

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

/**
 * Convert ASCII string to BSTR (wide string)
 */
static BSTR StringToBSTR(const char *str) {
    if (!str) return NULL;

    int len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    if (len == 0) return NULL;

    wchar_t *wstr = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!wstr) return NULL;

    MultiByteToWideChar(CP_ACP, 0, str, -1, wstr, len);
    BSTR bstr = SysAllocString(wstr);
    free(wstr);

    return bstr;
}

/**
 * Convert BSTR to ASCII string
 */
static int BSTRToString(BSTR bstr, char *str, int maxLen) {
    if (!bstr || !str || maxLen <= 0) return ERR_INVALID_PARAMETER;

    int len = WideCharToMultiByte(CP_ACP, 0, bstr, -1, NULL, 0, NULL, NULL);
    if (len == 0 || len > maxLen) return ECLAB_ERR_BSTR_CONVERSION;

    WideCharToMultiByte(CP_ACP, 0, bstr, -1, str, maxLen, NULL, NULL);
    return SUCCESS;
}

/**
 * Invoke IDispatch method by name
 */
static HRESULT InvokeMethod(IDispatch *pDisp, LPOLESTR methodName,
                           VARIANT *pResult, int numArgs, ...) {
    if (!pDisp) return E_POINTER;

    DISPID dispid;
    HRESULT hr = pDisp->lpVtbl->GetIDsOfNames(pDisp, &IID_NULL, &methodName,
                                               1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) return hr;

    // Build parameter array (COM uses reverse order)
    VARIANT *pArgs = NULL;
    if (numArgs > 0) {
        pArgs = (VARIANT*)malloc(sizeof(VARIANT) * numArgs);
        if (!pArgs) return E_OUTOFMEMORY;

        va_list args;
        va_start(args, numArgs);
        for (int i = numArgs - 1; i >= 0; i--) {
            VariantInit(&pArgs[i]);
            VariantCopy(&pArgs[i], va_arg(args, VARIANT*));
        }
        va_end(args);
    }

    // Set up dispatch parameters
    DISPPARAMS params;
    params.cArgs = numArgs;
    params.rgvarg = pArgs;
    params.cNamedArgs = 0;
    params.rgdispidNamedArgs = NULL;

    // Invoke method
    EXCEPINFO excepInfo;
    UINT argErr;
    VariantInit(pResult);

    hr = pDisp->lpVtbl->Invoke(pDisp, dispid, &IID_NULL, LOCALE_USER_DEFAULT,
                               DISPATCH_METHOD, &params, pResult,
                               &excepInfo, &argErr);

    // Cleanup
    if (pArgs) {
        for (int i = 0; i < numArgs; i++) {
            VariantClear(&pArgs[i]);
        }
        free(pArgs);
    }

    return hr;
}

/**
 * Get property value from IDispatch
 */
static HRESULT GetProperty(IDispatch *pDisp, LPOLESTR propName, VARIANT *pResult) {
    if (!pDisp) return E_POINTER;

    DISPID dispid;
    HRESULT hr = pDisp->lpVtbl->GetIDsOfNames(pDisp, &IID_NULL, &propName,
                                               1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) return hr;

    DISPPARAMS params = {NULL, NULL, 0, 0};
    VariantInit(pResult);

    hr = pDisp->lpVtbl->Invoke(pDisp, dispid, &IID_NULL, LOCALE_USER_DEFAULT,
                               DISPATCH_PROPERTYGET, &params, pResult, NULL, NULL);

    return hr;
}

/******************************************************************************
 * Initialization and Cleanup
 ******************************************************************************/

int ECLAB_Initialize(ECLabConnection **conn, const char *workingDir) {
    if (!conn) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "Initializing EC-Lab OLE COM connection");
    LogMessageEx(LOG_DEVICE_BIO, "========================================");

    // Allocate connection structure
    ECLabConnection *c = (ECLabConnection*)calloc(1, sizeof(ECLabConnection));
    if (!c) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to allocate memory for connection");
        return ERR_OUT_OF_MEMORY;
    }

    // Set working directory
    if (workingDir) {
        strncpy(c->workingDir, workingDir, MAX_PATH - 1);
        LogMessageEx(LOG_DEVICE_BIO, "Working directory: %s", c->workingDir);
    } else {
        GetCurrentDirectoryA(MAX_PATH, c->workingDir);
        LogMessageEx(LOG_DEVICE_BIO, "Using current directory: %s", c->workingDir);
    }

    // Initialize COM
    LogMessageEx(LOG_DEVICE_BIO, "Step 1: Initializing COM...");
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: CoInitialize failed with HRESULT: 0x%08X", hr);
        LogErrorEx(LOG_DEVICE_BIO, "This indicates a COM system error.");
        free(c);
        return ECLAB_ERR_COM_INIT_FAILED;
    }
    if (hr == RPC_E_CHANGED_MODE) {
        LogMessageEx(LOG_DEVICE_BIO, "COM already initialized in different mode (this is OK)");
    } else {
        LogMessageEx(LOG_DEVICE_BIO, "COM initialized successfully");
    }

    // Get CLSID for EC-Lab
    LogMessageEx(LOG_DEVICE_BIO, "Step 2: Resolving ProgID 'ECLab.Application'...");
    wchar_t progId[] = L"ECLab.Application";
    hr = CLSIDFromProgID(progId, &c->clsid);
    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: CLSIDFromProgID failed with HRESULT: 0x%08X", hr);
        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "This means EC-Lab is NOT registered as an OLE COM server.");
        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "To fix this, run the following command as Administrator:");
        LogErrorEx(LOG_DEVICE_BIO, "  cd \"C:\\Program Files (x86)\\EC-Lab\"");
        LogErrorEx(LOG_DEVICE_BIO, "  ECLab.exe /regserver");
        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "After registration, restart this application.");
        CoUninitialize();
        free(c);
        return ECLAB_ERR_COM_CREATE_FAILED;
    }
    LogMessageEx(LOG_DEVICE_BIO, "ProgID resolved successfully. EC-Lab is registered.");

    // Create EC-Lab COM object
    LogMessageEx(LOG_DEVICE_BIO, "Step 3: Creating EC-Lab COM instance...");
    LogMessageEx(LOG_DEVICE_BIO, "NOTE: EC-Lab must be running for this to succeed.");
    hr = CoCreateInstance(&c->clsid, NULL, CLSCTX_LOCAL_SERVER,
                         &IID_IDispatch, (void**)&c->pECLab);
    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: CoCreateInstance failed with HRESULT: 0x%08X", hr);
        LogErrorEx(LOG_DEVICE_BIO, "");

        if (hr == 0x800401F3) {  // CLSID_E_CLASSSTRING
            LogErrorEx(LOG_DEVICE_BIO, "Class string error - EC-Lab may not be properly registered.");
        } else if (hr == 0x80080005) {  // CO_E_SERVER_EXEC_FAILURE
            LogErrorEx(LOG_DEVICE_BIO, "EC-Lab server execution failed.");
            LogErrorEx(LOG_DEVICE_BIO, "Possible causes:");
            LogErrorEx(LOG_DEVICE_BIO, "  1. EC-Lab is not running - START EC-Lab first");
            LogErrorEx(LOG_DEVICE_BIO, "  2. EC-Lab crashed during startup");
            LogErrorEx(LOG_DEVICE_BIO, "  3. Insufficient permissions");
        } else if (hr == 0x80070005) {  // E_ACCESSDENIED
            LogErrorEx(LOG_DEVICE_BIO, "Access denied - run as Administrator");
        } else {
            LogErrorEx(LOG_DEVICE_BIO, "Unknown COM error occurred.");
        }

        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "SOLUTION:");
        LogErrorEx(LOG_DEVICE_BIO, "  1. Start EC-Lab application");
        LogErrorEx(LOG_DEVICE_BIO, "  2. Wait for it to fully load");
        LogErrorEx(LOG_DEVICE_BIO, "  3. Check for 'OLECOM' indicator in EC-Lab status bar");
        LogErrorEx(LOG_DEVICE_BIO, "  4. Then start this application");

        CoUninitialize();
        free(c);
        return ECLAB_ERR_COM_CREATE_FAILED;
    }
    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab COM instance created successfully!");

    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab OLE COM connection initialized");
    LogMessageEx(LOG_DEVICE_BIO, "========================================");

    *conn = c;
    return SUCCESS;
}

int ECLAB_Shutdown(ECLabConnection *conn) {
    if (!conn) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Shutting down EC-Lab OLE COM connection");

    // Disconnect if connected
    if (conn->isConnected) {
        ECLAB_DisconnectDevice(conn);
    }

    // Release COM interface
    if (conn->pECLab) {
        conn->pECLab->lpVtbl->Release(conn->pECLab);
        conn->pECLab = NULL;
    }

    CoUninitialize();
    free(conn);

    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab OLE COM connection shutdown complete");
    return SUCCESS;
}

int ECLAB_RegisterServer(const char *eclabPath) {
    if (!eclabPath) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Registering EC-Lab as OLE COM server: %s", eclabPath);

    // Build command: "ECLab.exe" /regserver
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "\"%s\" /regserver", eclabPath);

    // Execute registration command
    STARTUPINFO si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to execute: %s (Error: %d)", cmd, GetLastError());
        return ECLAB_ERR_REGISTRATION_FAILED;
    }

    // Wait for registration to complete
    WaitForSingleObject(pi.hProcess, 10000);  // 10 second timeout

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0 && exitCode != STILL_ACTIVE) {
        LogErrorEx(LOG_DEVICE_BIO, "Registration failed with exit code: %d", exitCode);
        return ECLAB_ERR_REGISTRATION_FAILED;
    }

    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab OLE COM registration complete");
    return SUCCESS;
}

/******************************************************************************
 * Device Management
 ******************************************************************************/

int ECLAB_ConnectDevice(ECLabConnection *conn, int deviceNumber) {
    if (!conn || !conn->pECLab) return ECLAB_ERR_INVALID_CONNECTION;
    if (conn->isConnected) return ECLAB_ERR_ALREADY_CONNECTED;

    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "Connecting to EC-Lab device %d", deviceNumber);
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "IMPORTANT: Device must be connected in EC-Lab first!");
    LogMessageEx(LOG_DEVICE_BIO, "  1. In EC-Lab, go to Device menu");
    LogMessageEx(LOG_DEVICE_BIO, "  2. Select 'Connect Device'");
    LogMessageEx(LOG_DEVICE_BIO, "  3. Verify device %d is connected and active", deviceNumber);
    LogMessageEx(LOG_DEVICE_BIO, "");

    // Build parameter
    VARIANT vDevice;
    VariantInit(&vDevice);
    V_VT(&vDevice) = VT_I4;
    V_I4(&vDevice) = deviceNumber;

    // Call ConnectDevice method
    LogMessageEx(LOG_DEVICE_BIO, "Calling EC-Lab ConnectDevice method...");
    VARIANT result;
    HRESULT hr = InvokeMethod(conn->pECLab, L"ConnectDevice", &result, 1, &vDevice);

    VariantClear(&vDevice);

    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: ConnectDevice COM call failed with HRESULT: 0x%08X", hr);
        LogErrorEx(LOG_DEVICE_BIO, "This could mean:");
        LogErrorEx(LOG_DEVICE_BIO, "  - The method name is incorrect");
        LogErrorEx(LOG_DEVICE_BIO, "  - EC-Lab interface has changed");
        LogErrorEx(LOG_DEVICE_BIO, "  - Communication with EC-Lab was interrupted");
        VariantClear(&result);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    // Check return value (should be 0 for success)
    int retVal = (V_VT(&result) == VT_I4) ? V_I4(&result) : -1;
    VariantClear(&result);

    if (retVal != 0) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: ConnectDevice returned error code: %d", retVal);
        LogErrorEx(LOG_DEVICE_BIO, "Possible causes:");
        LogErrorEx(LOG_DEVICE_BIO, "  - Device %d is not physically connected", deviceNumber);
        LogErrorEx(LOG_DEVICE_BIO, "  - Device is not powered on");
        LogErrorEx(LOG_DEVICE_BIO, "  - Device is already in use by another application");
        LogErrorEx(LOG_DEVICE_BIO, "  - Wrong device number specified");
        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "Check EC-Lab's device list to verify available devices.");
        return ECLAB_ERR_DEVICE_NOT_FOUND;
    }

    conn->deviceNumber = deviceNumber;
    conn->isConnected = true;

    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "Successfully connected to device %d!", deviceNumber);
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    return SUCCESS;
}

int ECLAB_DisconnectDevice(ECLabConnection *conn) {
    if (!conn || !conn->pECLab) return ECLAB_ERR_INVALID_CONNECTION;
    if (!conn->isConnected) return SUCCESS;  // Already disconnected

    LogMessageEx(LOG_DEVICE_BIO, "Disconnecting from EC-Lab device %d", conn->deviceNumber);

    VARIANT vDevice;
    VariantInit(&vDevice);
    V_VT(&vDevice) = VT_I4;
    V_I4(&vDevice) = conn->deviceNumber;

    VARIANT result;
    HRESULT hr = InvokeMethod(conn->pECLab, L"DisconnectDevice", &result, 1, &vDevice);

    VariantClear(&vDevice);
    VariantClear(&result);

    conn->isConnected = false;

    if (FAILED(hr)) {
        LogWarningEx(LOG_DEVICE_BIO, "DisconnectDevice COM call failed: 0x%08X", hr);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    LogMessageEx(LOG_DEVICE_BIO, "Disconnected from EC-Lab device");
    return SUCCESS;
}

int ECLAB_TestConnection(ECLabConnection *conn) {
    if (!conn || !conn->pECLab) return ECLAB_ERR_INVALID_CONNECTION;
    if (!conn->isConnected) return ECLAB_ERR_NOT_CONNECTED;

    VARIANT vDevice;
    VariantInit(&vDevice);
    V_VT(&vDevice) = VT_I4;
    V_I4(&vDevice) = conn->deviceNumber;

    VARIANT result;
    HRESULT hr = InvokeMethod(conn->pECLab, L"TestConnection", &result, 1, &vDevice);

    VariantClear(&vDevice);

    if (FAILED(hr)) {
        VariantClear(&result);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    int retVal = (V_VT(&result) == VT_I4) ? V_I4(&result) : -1;
    VariantClear(&result);

    return (retVal == 0) ? SUCCESS : ECLAB_ERR_NOT_CONNECTED;
}

/******************************************************************************
 * Experiment Control
 ******************************************************************************/

int ECLAB_LoadSettings(ECLabConnection *conn, int device, int channel,
                      const char *mpsFilePath) {
    if (!conn || !conn->pECLab) return ECLAB_ERR_INVALID_CONNECTION;
    if (!mpsFilePath) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Loading settings from: %s", mpsFilePath);

    // Check file exists
    if (GetFileAttributesA(mpsFilePath) == INVALID_FILE_ATTRIBUTES) {
        LogErrorEx(LOG_DEVICE_BIO, "Settings file not found: %s", mpsFilePath);
        return ECLAB_ERR_FILE_NOT_FOUND;
    }

    // Build parameters
    VARIANT vDevice, vChannel, vFilePath;
    VariantInit(&vDevice);
    VariantInit(&vChannel);
    VariantInit(&vFilePath);

    V_VT(&vDevice) = VT_I4;
    V_I4(&vDevice) = device;

    V_VT(&vChannel) = VT_I4;
    V_I4(&vChannel) = channel;

    V_VT(&vFilePath) = VT_BSTR;
    V_BSTR(&vFilePath) = StringToBSTR(mpsFilePath);

    // Call LoadSettings method
    VARIANT result;
    HRESULT hr = InvokeMethod(conn->pECLab, L"LoadSettings", &result, 3,
                              &vDevice, &vChannel, &vFilePath);

    VariantClear(&vDevice);
    VariantClear(&vChannel);
    VariantClear(&vFilePath);

    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO, "LoadSettings COM call failed: 0x%08X", hr);
        VariantClear(&result);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    int retVal = (V_VT(&result) == VT_I4) ? V_I4(&result) : -1;
    VariantClear(&result);

    if (retVal != 0) {
        LogErrorEx(LOG_DEVICE_BIO, "LoadSettings returned error: %d", retVal);
        return ECLAB_ERR_INVALID_MPS_FILE;
    }

    LogMessageEx(LOG_DEVICE_BIO, "Settings loaded successfully");
    return SUCCESS;
}

int ECLAB_RunChannel(ECLabConnection *conn, int device, int channel,
                    const char *outputMprPath) {
    if (!conn || !conn->pECLab) return ECLAB_ERR_INVALID_CONNECTION;
    if (!outputMprPath) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Starting measurement, output: %s", outputMprPath);

    // Build parameters
    VARIANT vDevice, vChannel, vOutputPath;
    VariantInit(&vDevice);
    VariantInit(&vChannel);
    VariantInit(&vOutputPath);

    V_VT(&vDevice) = VT_I4;
    V_I4(&vDevice) = device;

    V_VT(&vChannel) = VT_I4;
    V_I4(&vChannel) = channel;

    V_VT(&vOutputPath) = VT_BSTR;
    V_BSTR(&vOutputPath) = StringToBSTR(outputMprPath);

    // Call RunChannel method
    VARIANT result;
    HRESULT hr = InvokeMethod(conn->pECLab, L"RunChannel", &result, 3,
                              &vDevice, &vChannel, &vOutputPath);

    VariantClear(&vDevice);
    VariantClear(&vChannel);
    VariantClear(&vOutputPath);

    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO, "RunChannel COM call failed: 0x%08X", hr);
        VariantClear(&result);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    int retVal = (V_VT(&result) == VT_I4) ? V_I4(&result) : -1;
    VariantClear(&result);

    if (retVal != 0) {
        LogErrorEx(LOG_DEVICE_BIO, "RunChannel returned error: %d", retVal);
        return ECLAB_ERR_RUN_FAILED;
    }

    LogMessageEx(LOG_DEVICE_BIO, "Measurement started");
    return SUCCESS;
}

int ECLAB_StopChannel(ECLabConnection *conn, int device, int channel) {
    if (!conn || !conn->pECLab) return ECLAB_ERR_INVALID_CONNECTION;

    LogMessageEx(LOG_DEVICE_BIO, "Stopping measurement on device %d, channel %d",
                device, channel);

    VARIANT vDevice, vChannel;
    VariantInit(&vDevice);
    VariantInit(&vChannel);

    V_VT(&vDevice) = VT_I4;
    V_I4(&vDevice) = device;

    V_VT(&vChannel) = VT_I4;
    V_I4(&vChannel) = channel;

    VARIANT result;
    HRESULT hr = InvokeMethod(conn->pECLab, L"StopChannel", &result, 2,
                              &vDevice, &vChannel);

    VariantClear(&vDevice);
    VariantClear(&vChannel);
    VariantClear(&result);

    if (FAILED(hr)) {
        LogWarningEx(LOG_DEVICE_BIO, "StopChannel COM call failed: 0x%08X", hr);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    LogMessageEx(LOG_DEVICE_BIO, "Measurement stopped");
    return SUCCESS;
}

/******************************************************************************
 * Status Monitoring
 ******************************************************************************/

int ECLAB_MeasureStatus(ECLabConnection *conn, int device, int channel,
                       ECLAB_Status *status) {
    if (!conn || !conn->pECLab) return ECLAB_ERR_INVALID_CONNECTION;
    if (!status) return ERR_NULL_POINTER;

    memset(status, 0, sizeof(ECLAB_Status));

    // Build parameters
    VARIANT vDevice, vChannel, vStatusArray;
    VariantInit(&vDevice);
    VariantInit(&vChannel);
    VariantInit(&vStatusArray);

    V_VT(&vDevice) = VT_I4;
    V_I4(&vDevice) = device;

    V_VT(&vChannel) = VT_I4;
    V_I4(&vChannel) = channel;

    // vStatusArray is an output parameter (BYREF)
    VARIANT statusResult;
    VariantInit(&statusResult);
    V_VT(&vStatusArray) = VT_VARIANT | VT_BYREF;
    vStatusArray.n1.n2.n3.pvarVal = &statusResult;

    // Call MeasureStatus method
    VARIANT result;
    HRESULT hr = InvokeMethod(conn->pECLab, L"MeasureStatus", &result, 3,
                              &vDevice, &vChannel, &vStatusArray);

    VariantClear(&vDevice);
    VariantClear(&vChannel);

    if (FAILED(hr)) {
        VariantClear(&vStatusArray);
        VariantClear(&result);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    // Parse status array (should be SAFEARRAY of 32 variants)
    if (V_VT(&statusResult) == (VT_ARRAY | VT_VARIANT)) {
        SAFEARRAY *psa = V_ARRAY(&statusResult);
        LONG lBound, uBound;
        SafeArrayGetLBound(psa, 1, &lBound);
        SafeArrayGetUBound(psa, 1, &uBound);

        int numElements = uBound - lBound + 1;
        if (numElements != 32) {
            LogWarningEx(LOG_DEVICE_BIO, "Status array size mismatch: %d (expected 32)",
                        numElements);
        }

        // Extract values (manual section 3.2.9)
        VARIANT *pData;
        SafeArrayAccessData(psa, (void**)&pData);

        if (numElements >= 32) {
            status->status = (V_VT(&pData[0]) == VT_I4) ? V_I4(&pData[0]) : 0;
            status->oxRed = (V_VT(&pData[1]) == VT_I4) ? V_I4(&pData[1]) : 0;
            status->ocv = (V_VT(&pData[2]) == VT_I4) ? V_I4(&pData[2]) : 0;
            status->eis = (V_VT(&pData[3]) == VT_I4) ? V_I4(&pData[3]) : 0;
            status->techniqueNumber = (V_VT(&pData[4]) == VT_I4) ? V_I4(&pData[4]) : 0;
            status->techniqueCode = (V_VT(&pData[5]) == VT_I4) ? V_I4(&pData[5]) : 0;
            status->sequenceNumber = (V_VT(&pData[6]) == VT_I4) ? V_I4(&pData[6]) : 0;
            status->currentLoopIteration = (V_VT(&pData[7]) == VT_I4) ? V_I4(&pData[7]) : 0;
            status->currentSequenceInLoop = (V_VT(&pData[8]) == VT_I4) ? V_I4(&pData[8]) : 0;
            status->loopExperimentIteration = (V_VT(&pData[9]) == VT_I4) ? V_I4(&pData[9]) : 0;
            status->cycleNumber = (V_VT(&pData[10]) == VT_I4) ? V_I4(&pData[10]) : 0;
            status->counter1 = (V_VT(&pData[11]) == VT_I4) ? V_I4(&pData[11]) : 0;
            status->counter2 = (V_VT(&pData[12]) == VT_I4) ? V_I4(&pData[12]) : 0;
            status->counter3 = (V_VT(&pData[13]) == VT_I4) ? V_I4(&pData[13]) : 0;
            status->bufferSize = (V_VT(&pData[14]) == VT_I4) ? V_I4(&pData[14]) : 0;
            status->time = (V_VT(&pData[15]) == VT_R8) ? V_R8(&pData[15]) : 0.0;
            status->ewe = (V_VT(&pData[16]) == VT_R8) ? V_R8(&pData[16]) : 0.0;
            status->ece = (V_VT(&pData[17]) == VT_R8) ? V_R8(&pData[17]) : 0.0;
            status->eoc = (V_VT(&pData[18]) == VT_R8) ? V_R8(&pData[18]) : 0.0;
            status->current = (V_VT(&pData[19]) == VT_R8) ? V_R8(&pData[19]) : 0.0;
            status->charge = (V_VT(&pData[20]) == VT_R8) ? V_R8(&pData[20]) : 0.0;
            status->aux1 = (V_VT(&pData[21]) == VT_R8) ? V_R8(&pData[21]) : 0.0;
            status->aux2 = (V_VT(&pData[22]) == VT_R8) ? V_R8(&pData[22]) : 0.0;
            status->iRange = (V_VT(&pData[23]) == VT_R8) ? V_R8(&pData[23]) : 0.0;
            status->rCompensation = (V_VT(&pData[24]) == VT_R8) ? V_R8(&pData[24]) : 0.0;
            status->frequency = (V_VT(&pData[25]) == VT_R8) ? V_R8(&pData[25]) : 0.0;
            status->zMagnitude = (V_VT(&pData[26]) == VT_R8) ? V_R8(&pData[26]) : 0.0;
            status->currentPointIndex = (V_VT(&pData[27]) == VT_I4) ? V_I4(&pData[27]) : 0;
            status->totalPointIndex = (V_VT(&pData[28]) == VT_I4) ? V_I4(&pData[28]) : 0;
            status->temperature = (V_VT(&pData[29]) == VT_R8) ? V_R8(&pData[29]) : 0.0;
            status->safetyLimit = (V_VT(&pData[30]) == VT_I4) ? V_I4(&pData[30]) : 0;
            status->connection = (V_VT(&pData[31]) == VT_I4) ? V_I4(&pData[31]) : 0;
        }

        SafeArrayUnaccessData(psa);
    }

    VariantClear(&vStatusArray);
    VariantClear(&statusResult);
    VariantClear(&result);

    return SUCCESS;
}

/******************************************************************************
 * Data Retrieval
 ******************************************************************************/

int ECLAB_MeasureNumberOfPoints(const char *mprPath, int *numPoints) {
    if (!mprPath || !numPoints) return ERR_NULL_POINTER;

    // This function is typically called statically, so we need to create
    // a temporary COM instance or require an existing connection
    // For simplicity, we'll document that the connection must be initialized

    LogWarningEx(LOG_DEVICE_BIO, "ECLAB_MeasureNumberOfPoints not yet implemented");
    LogWarningEx(LOG_DEVICE_BIO, "This requires static COM access or connection parameter");

    *numPoints = 0;
    return ERR_NOT_IMPLEMENTED_YET;
}

int ECLAB_MeasureDcValue(const char *mprPath, int dataIndex,
                        double *time, double *voltage, double *current) {
    if (!mprPath || !time || !voltage || !current) return ERR_NULL_POINTER;

    // Similar to above - needs implementation with COM access
    LogWarningEx(LOG_DEVICE_BIO, "ECLAB_MeasureDcValue not yet implemented");

    *time = *voltage = *current = 0.0;
    return ERR_NOT_IMPLEMENTED_YET;
}

int ECLAB_MeasureEisValue(const char *mprPath, int dataIndex,
                         double *time, double *freq, double *zReal, double *zImag) {
    if (!mprPath || !time || !freq || !zReal || !zImag) return ERR_NULL_POINTER;

    LogWarningEx(LOG_DEVICE_BIO, "ECLAB_MeasureEisValue not yet implemented");

    *time = *freq = *zReal = *zImag = 0.0;
    return ERR_NOT_IMPLEMENTED_YET;
}

int ECLAB_MeasureValueByCode(const char *mprPath, int varCode, int dataIndex,
                            double *value) {
    if (!mprPath || !value) return ERR_NULL_POINTER;

    LogWarningEx(LOG_DEVICE_BIO, "ECLAB_MeasureValueByCode not yet implemented");

    *value = 0.0;
    return ERR_NOT_IMPLEMENTED_YET;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* ECLAB_GetErrorString(int errorCode) {
    switch (errorCode) {
        case SUCCESS: return "Success";
        case ECLAB_ERR_COM_INIT_FAILED: return "COM initialization failed";
        case ECLAB_ERR_COM_CREATE_FAILED: return "Failed to create EC-Lab COM object";
        case ECLAB_ERR_COM_INVOKE_FAILED: return "COM method invocation failed";
        case ECLAB_ERR_COM_RELEASE_FAILED: return "COM object release failed";
        case ECLAB_ERR_BSTR_CONVERSION: return "BSTR string conversion failed";
        case ECLAB_ERR_VARIANT_TYPE: return "Invalid VARIANT type";
        case ECLAB_ERR_INVALID_CONNECTION: return "Invalid connection handle";
        case ECLAB_ERR_NOT_CONNECTED: return "Not connected to device";
        case ECLAB_ERR_ALREADY_CONNECTED: return "Already connected to device";
        case ECLAB_ERR_DEVICE_NOT_FOUND: return "Device not found";
        case ECLAB_ERR_CHANNEL_NOT_FOUND: return "Channel not found";
        case ECLAB_ERR_FILE_NOT_FOUND: return "File not found";
        case ECLAB_ERR_INVALID_MPS_FILE: return "Invalid or corrupt .mps file";
        case ECLAB_ERR_INVALID_MPR_FILE: return "Invalid or corrupt .mpr file";
        case ECLAB_ERR_RUN_FAILED: return "Failed to start measurement";
        case ECLAB_ERR_STATUS_ARRAY_SIZE: return "Status array size mismatch";
        case ECLAB_ERR_DATA_NOT_AVAILABLE: return "Data not available";
        case ECLAB_ERR_REGISTRATION_FAILED: return "EC-Lab registration failed";
        default: return "Unknown EC-Lab error";
    }
}

bool ECLAB_IsRunning(void) {
    // Check if EC-Lab process is running
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    bool found = false;
    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (stricmp(pe32.szExeFile, "ECLab.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return found;
}

int ECLAB_EnableMessagesWindows(ECLabConnection *conn, bool enable) {
    if (!conn || !conn->pECLab) return ECLAB_ERR_INVALID_CONNECTION;

    VARIANT vEnable;
    VariantInit(&vEnable);
    V_VT(&vEnable) = VT_BOOL;
    V_BOOL(&vEnable) = enable ? VARIANT_TRUE : VARIANT_FALSE;

    VARIANT result;
    HRESULT hr = InvokeMethod(conn->pECLab, L"EnableMessagesWindows", &result, 1, &vEnable);

    VariantClear(&vEnable);
    VariantClear(&result);

    if (FAILED(hr)) {
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    return SUCCESS;
}

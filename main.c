#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <stdio.h>
#include <dbt.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "user32.lib")

#ifndef CM_DEVCAP_EJECTIONSUPPORTED
#define CM_DEVCAP_EJECTIONSUPPORTED 0x00000020
#endif

static HWND g_hwnd = NULL;
static HHOOK g_mouseHook = NULL;
static HHOOK g_keyboardHook = NULL;
static wchar_t logBuffer[4096] = L""; 
static BOOL g_lockdownActive = FALSE;
extern LONG g_armed ;
static HDEVNOTIFY g_hDevNotify = NULL;

// Whitelist structure
typedef struct {
    WORD vendorId;
    WORD productId;
    WCHAR description[128];
} WhitelistEntry;

// Add your trusted devices here for now 
WhitelistEntry g_whitelist[] = {
    {0x046D, 0xC52B, L"Logitech Mouse"},
    {0x13FE, 0x4300, L"Moster USB"}, //ALL EXAMPLES 
    {0x0781, 0x5583, L"SanDisk USB Drive"},
};

WhitelistEntry g_keyDevice[] = {
    {0x13FE, 0x4300, L"Monster USB"},
};

#define WHITELIST_SIZE (sizeof(g_whitelist) / sizeof(WhitelistEntry))

void AddLog(const wchar_t *fmt, ...) {
    wchar_t buffer[512];
    va_list args;
    va_start(args, fmt);
    vswprintf(buffer, 512, fmt, args);
    va_end(args);

    wcscat(logBuffer, buffer);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        return 1;
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* kbStruct = (KBDLLHOOKSTRUCT*)lParam;
        if (kbStruct->vkCode == VK_ESCAPE) {
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }
        return 1;
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

void CursorToCenter() {
    int x = GetSystemMetrics(SM_CXSCREEN) / 2;
    int y = GetSystemMetrics(SM_CYSCREEN) / 2;
    SetCursorPos(x,y);
}

// Extract VID and PID from device path
BOOL GetDeviceVIDPID(LPCWSTR  devicePath, WORD* vid, WORD* pid) {
    if (!devicePath || !vid || !pid) return FALSE;
    
    LPCWSTR vidPos = wcsstr(devicePath, L"VID_");
    LPCWSTR pidPos = wcsstr(devicePath, L"PID_");
    
    if (vidPos && pidPos) {
        *vid = (WORD)wcstol(vidPos + 4, NULL, 16);
        *pid = (WORD)wcstol(pidPos + 4, NULL, 16);
        return TRUE;
    }
    return FALSE;
}

// Check if device is whitelisted
BOOL IsDeviceWhitelisted(WORD vid, WORD pid) {
    for (int i = 0; i < WHITELIST_SIZE; i++) {
        if (g_whitelist[i].vendorId == vid && g_whitelist[i].productId == pid) {
            AddLog(L"[OK] Authorized device: %ws (VID:0x%04X PID:0x%04X)\n", 
                   g_whitelist[i].description, vid, pid);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL IsKeyDevice(WORD vid, WORD pid) {
    for (int i = 0; i < sizeof(g_keyDevice) / sizeof(WhitelistEntry); i++) {
        if (g_keyDevice[i].vendorId == vid && g_keyDevice[i].productId == pid) {
            AddLog(L"[KEY] Key device detected: %ws (VID:0x%04X PID:0x%04X)\n", 
                   g_keyDevice[i].description, vid, pid);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL EjectDevice(DEVINST devInst) {
    DEVINST parentDevInst = devInst;
    DEVINST currentDevInst = devInst;
    DWORD capabilities = 0;
    ULONG size = sizeof(capabilities);
    BOOL foundEjectable = FALSE;
    
    for (int level = 0; level < 10; level++) {
        capabilities = 0;
        size = sizeof(capabilities);
        
        if (CM_Get_DevNode_Registry_Property(currentDevInst, CM_DRP_CAPABILITIES,
            NULL, &capabilities, &size, 0) == CR_SUCCESS) {
            
            if (capabilities & CM_DEVCAP_EJECTIONSUPPORTED) {
                AddLog(L"[INFO] Found ejectable device at level %d\n", level);
                parentDevInst = currentDevInst;
                foundEjectable = TRUE;
                break;
            }
        }
        
        DWORD removable = 0;
        size = sizeof(removable);
        if (CM_Get_DevNode_Registry_Property(currentDevInst, CM_DRP_REMOVAL_POLICY,
            NULL, &removable, &size, 0) == CR_SUCCESS) {
            if (removable == 2 || removable == 3) {
                AddLog(L"[INFO] Found removable device at level %d (policy: %d)\n", level, removable);
                parentDevInst = currentDevInst;
                foundEjectable = TRUE;
                break;
            }
        }

        DEVINST tempDevInst;
        if (CM_Get_Parent(&tempDevInst, currentDevInst, 0) != CR_SUCCESS) {
            break;
        }
        currentDevInst = tempDevInst;
    }
    
    if (!foundEjectable) {
        AddLog(L"[INFO] No ejectable/removable device found in hierarchy\n");
        return FALSE;
    }
    
    PNP_VETO_TYPE vetoType;
    WCHAR vetoName[MAX_PATH];
    
    AddLog(L"[INFO] Attempting to eject device...\n");
    CONFIGRET result = CM_Request_Device_Eject(parentDevInst, &vetoType, 
        vetoName, MAX_PATH, 0);
    
    if (result == CR_SUCCESS) {
        AddLog(L"[SUCCESS] Device ejected successfully!\n");
        return TRUE;
    } else {
        AddLog(L"[FAILED] Ejection failed with error code: 0x%X\n", result);
        if (vetoType != PNP_VetoTypeUnknown) {
            AddLog(L"[VETO] Veto Type: %d, Veto Name: %ws\n", vetoType, vetoName);
        }
        return FALSE;
    }
}

DWORD WINAPI EnumerateUSBDevices(LPVOID lp) {
    HDEVINFO deviceInfoSet;
    SP_DEVINFO_DATA deviceInfoData;
    DWORD i;
    
    // Get all USB devices
    deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE,
        NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        AddLog(L"Failed to get device information set\n");
        return FALSE;
    }
    
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) { // information about that device
        WCHAR deviceDesc[256];
        DWORD dataType;
        WCHAR devicePath[256];

        if (SetupDiGetDeviceRegistryProperty(deviceInfoSet, &deviceInfoData,
            SPDRP_DEVICEDESC, &dataType, (BYTE*)deviceDesc, 
            sizeof(deviceDesc), NULL)) {

            AddLog(L"[Device %d] %ws\n", i + 1, deviceDesc);

            EjectDevice(deviceInfoData.DevInst);
        }
    }
    
    SetupDiDestroyDeviceInfoList(deviceInfoSet); //Long ass function name
}

void ActivateLockdown() {
    if (g_lockdownActive) return;
    
    g_lockdownActive = TRUE;
    
    // Show fullscreen window
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    ShowCursor(FALSE);
    
    // Activate hooks
    if (!g_mouseHook) {
        g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(NULL), 0);
    }
    if (!g_keyboardHook) {
        g_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    }
    
    SetTimer(g_hwnd, 1, 10, NULL);
    
}

void HandleUnauthorizedDevice(LPCWSTR devicePath, WORD vid, WORD pid) {
    AddLog(L"\n[ALERT] UNAUTHORIZED DEVICE!\n");
    AddLog(L"Device Path: %ws\n", devicePath);
    AddLog(L"VID: 0x%04X, PID: 0x%04X\n", vid, pid);

    Sleep(200);
    EnumerateUSBDevices(NULL);
    
    ActivateLockdown();
}

void HandleDeviceArrival(LPCWSTR devicePath) {
    WORD vid = 0, pid = 0;
    
    AddLog(L"\n[INFO] New USB device connected\n");
    
    if (!GetDeviceVIDPID(devicePath, &vid, &pid)) {
        AddLog(L"[WARN] Could not extract VID/PID\n");
        return;
    }
    
    AddLog(L"VID: 0x%04X, PID: 0x%04X\n", vid, pid);
    
    if (!IsDeviceWhitelisted(vid, pid)) {
        HandleUnauthorizedDevice(devicePath, vid, pid);
    }

    if (IsKeyDevice(vid, pid) && InterlockedCompareExchange(&g_armed, 1, 0) == 0) {

        MessageBox(NULL, L"Key device is connected!", L"Armed", MB_OK );
    }
}

void HandleDeviceRemoval(LPCWSTR devicePath)
{
    WORD vid = 0, pid = 0;
    if (!GetDeviceVIDPID(devicePath, &vid, &pid)) {
        AddLog(L"[WARN] Could not extract VID/PID on removal\n");
        return;
    }

    AddLog(L"[INFO] Device removed VID:0x%04X PID:0x%04X\n", vid, pid);

    if (IsKeyDevice(vid, pid)) {
        // perform lockdown regardless of current g_armed state
        ActivateLockdown();
        // reset g_armed
        //InterlockedExchange(&g_armed, 0);
    }
}

BOOL Shutdown(BOOL reboot) {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;

    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;

    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if(!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES) NULL, 0))
        return FALSE;

    if(!InitiateSystemShutdown(NULL, NULL, 0, TRUE, reboot))
        return FALSE;

    tkp.Privileges[0].Attributes = 0;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES) NULL, 0);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DEVICECHANGE:
        {
            // protect against NULL lParam
            if (lParam == 0)
                break;

            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
                break;

            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
            LPCWSTR devicePath = pDevInf->dbcc_name;

            switch (wParam)
            {
            case DBT_DEVICEARRIVAL:
                HandleDeviceArrival(devicePath);
                break;

            case DBT_DEVICEREMOVECOMPLETE:
                // optionally check a guard (g_armed) but still parse VID/PID inside helper
                if (g_armed)
                {
                    HandleDeviceRemoval(devicePath);
                }
                break;

            default:
                break;
            }
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            SetBkColor(hdc, RGB(0, 0, 0));
            SetTextColor(hdc, RGB(225, 255, 255));

            RECT rect;
            GetClientRect(hwnd, &rect);

            DrawText(hdc, logBuffer, -1, &rect, DT_TOP | DT_LEFT | DT_NOPREFIX);
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
                if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);
                if (g_hDevNotify) UnregisterDeviceNotification(g_hDevNotify);
                PostQuitMessage(0);
            }
            break;

        case WM_TIMER:
            if (g_lockdownActive) {
                CursorToCenter();
            }
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nCmdShow) {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"TTYWindow";
    RegisterClassW(&wc);

    g_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"TTYWindow", L"TTY Emulation",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL
    );

    // Register for USB device notifications
    DEV_BROADCAST_DEVICEINTERFACE notificationFilter = {0};
    notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;
    
    g_hDevNotify = RegisterDeviceNotification(g_hwnd, &notificationFilter,
                                             DEVICE_NOTIFY_WINDOW_HANDLE);
    
    if (!g_hDevNotify) {
        MessageBox(NULL, L"Failed to register device notification!", L"Error", MB_OK);
        return 1;
    }

    AddLog(L"Monitoring %d whitelisted devices...\n\n", WHITELIST_SIZE);

    // ShowWindow(g_hwnd, SW_HIDE);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}
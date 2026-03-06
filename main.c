#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <stdio.h>
#include <dbt.h>
#include <fcntl.h>
#include <io.h>
#include <processenv.h>

#include "toast.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(linker, "/entry:wmainCRTStartup")
#pragma comment(linker, "/subsystem:console") 

#ifndef CM_DEVCAP_EJECTIONSUPPORTED
#define CM_DEVCAP_EJECTIONSUPPORTED 0x00000020
#endif

#define WHITELIST_FILE L"whitelist.cfg"
#define MAX_ENTRIES    256

static HWND g_hwnd = NULL;
static HHOOK g_mouseHook = NULL;
static HHOOK g_keyboardHook = NULL;
static wchar_t logBuffer[4096] = L""; 
static BOOL g_lockdownActive = FALSE;
LONG g_armed ;
static HDEVNOTIFY g_hDevNotify = NULL;

typedef enum { SECTION_NONE, SECTION_WHITELIST, SECTION_KEYDEVICE } Section;

// Whitelist structure
typedef struct {
    WORD vendorId;
    WORD productId;
    WCHAR description[128];
} WhitelistEntry;

WhitelistEntry *g_whitelist = NULL;
DWORD g_whitelistCount     = 0;
WhitelistEntry *g_keyDevice  = NULL;
DWORD g_keyDeviceCount       = 0;


#define WHITELIST_SIZE (g_whitelistCount)

void AddLog(const wchar_t *fmt, ...) {
    wchar_t buffer[512];
    va_list args;
    va_start(args, fmt);
    vswprintf(buffer, 512, fmt, args);
    va_end(args);

    if (g_hwnd) {
        wcscat(logBuffer, buffer);
        InvalidateRect(g_hwnd, NULL, TRUE);
    } else {
        // --list mode: no window yet, print directly
        _setmode(_fileno(stdout), _O_U16TEXT); // ensure UTF-16 console output
        wprintf(L"%ws", buffer);
    }
}

BOOL LaunchedFromConsole() {
    HWND consoleWnd = GetConsoleWindow();
    if (!consoleWnd) return FALSE;

    DWORD consoleProcessId;
    GetWindowThreadProcessId(consoleWnd, &consoleProcessId);

    // If the console's owner PID != our PID, 
    // we SHARE a console with a parent (terminal that launched us)
    // If they match, Windows created a NEW console just for us
    return GetCurrentProcessId() != consoleProcessId;
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

    WCHAR lowerPath[512];
    wcsncpy_s(lowerPath, 512, devicePath, _TRUNCATE);
    _wcslwr_s(lowerPath, 512);

    LPCWSTR vidPos = wcsstr(lowerPath, L"vid_");
    LPCWSTR pidPos = wcsstr(lowerPath, L"pid_");
    
    if (vidPos && pidPos) {
        *vid = (WORD)wcstol(vidPos + 4, NULL, 16);
        *pid = (WORD)wcstol(pidPos + 4, NULL, 16);
        return TRUE;
    }
    return FALSE;
}

// Check if device is whitelisted
BOOL IsDeviceWhitelisted(WORD vid, WORD pid) {
    for (DWORD i = 0; i < g_whitelistCount; i++) {
        if (g_whitelist[i].vendorId == vid && g_whitelist[i].productId == pid) {
            AddLog(L"[OK] Authorized device: %ws (VID:0x%04X PID:0x%04X)\n", 
                   g_whitelist[i].description, vid, pid);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL IsKeyDevice(WORD vid, WORD pid) {
    for (DWORD i = 0; i < g_keyDeviceCount; i++) {
        if (g_keyDevice[i].vendorId == vid && g_keyDevice[i].productId == pid) {
            AddLog(L"[KEY] Key device detected: %ws (VID:0x%04X PID:0x%04X)\n", 
                   g_keyDevice[i].description, vid, pid);
            return TRUE;
        }
    }
    return FALSE;
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
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs( //Gett all USB devices 
        &GUID_DEVINTERFACE_USB_DEVICE,
        NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        AddLog(L"Failed to get device information set\n");
        return FALSE;
    }

    SP_DEVICE_INTERFACE_DATA ifaceData = { .cbSize = sizeof(SP_DEVICE_INTERFACE_DATA) };

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL,
             &GUID_DEVINTERFACE_USB_DEVICE, i, &ifaceData); i++)
    {
        // First call: get required buffer size
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &ifaceData, NULL, 0, &requiredSize, NULL);

        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *) malloc(requiredSize);
        if (!detail) continue;

        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData = { .cbSize = sizeof(SP_DEVINFO_DATA) };

        // Second call: get path AND devInfoData in one shot
        if (!SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &ifaceData, detail, requiredSize, NULL, &devInfoData))
        {
            free(detail);
            continue;
        }

        WORD vid = 0, pid = 0;
        GetDeviceVIDPID(detail->DevicePath, &vid, &pid);

        WCHAR desc[256] = L"(unknown)";
        DWORD dataType;
        SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &devInfoData,
            SPDRP_DEVICEDESC, &dataType,
            (BYTE *)desc, sizeof(desc), NULL);

        AddLog(L"[Device %d] %ws (VID:0x%04X PID:0x%04X)\n", i + 1, desc, vid, pid);

        if (g_lockdownActive)
            EjectDevice(devInfoData.DevInst);

        free(detail);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return TRUE;
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
    Shutdown(FALSE); // Force shutdown, no reboot
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

        sendToastAsync(L"C Panic", L"Key device connected!");
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
        ActivateLockdown();
    }
}

BOOL LoadWhitelist(
    WhitelistEntry **outWhitelist, DWORD *outWhitelistCount,
    WhitelistEntry **outKeyDevice, DWORD *outKeyDeviceCount)
{
    FILE *f = _wfopen(WHITELIST_FILE, L"r, ccs=UTF-8");
    if (!f) return FALSE;

    WhitelistEntry *wl = malloc(MAX_ENTRIES * sizeof(WhitelistEntry));
    WhitelistEntry *kd = malloc(MAX_ENTRIES * sizeof(WhitelistEntry));
    if (!wl || !kd) { free(wl); free(kd); fclose(f); return FALSE; }

    DWORD wlCount = 0, kdCount = 0;
    Section currentSection = SECTION_NONE;
    WCHAR line[256];

    while (fgetws(line, 256, f) &&
           wlCount < MAX_ENTRIES && kdCount < MAX_ENTRIES) {

        // Skip comments and blank lines
        if (line[0] == L'#' || line[0] == L'\n' || line[0] == L'\r')
            continue;

        
        if (wcsncmp(line, L"[whitelist]", 11) == 0) {
            currentSection = SECTION_WHITELIST;
            continue;
        }
        if (wcsncmp(line, L"[keydevice]", 11) == 0) {
            currentSection = SECTION_KEYDEVICE;
            continue;
        }

        // Skip lines before any section header
        if (currentSection == SECTION_NONE) continue;

        WCHAR desc[128] = {0};
        UINT vid = 0, pid = 0;

        // Parse "XXXX , XXXX , Description text"
        if (swscanf_s(line, L" %X , %X , %127[^\n]",
                      &vid, &pid, desc, (unsigned)_countof(desc)) == 3)
        {
            WhitelistEntry entry;
            entry.vendorId  = (WORD)vid;
            entry.productId = (WORD)pid;
            wcsncpy_s(entry.description, 128, desc, _TRUNCATE);

            if (currentSection == SECTION_WHITELIST)
                wl[wlCount++] = entry;
            else if (currentSection == SECTION_KEYDEVICE)
                kd[kdCount++] = entry;
        }
    }

    fclose(f);

    *outWhitelist      = wl;  *outWhitelistCount  = wlCount;
    *outKeyDevice      = kd;  *outKeyDeviceCount  = kdCount;
    return TRUE;
}

BOOL EnsureWhitelistFile(void) {
    FILE *f = _wfopen(WHITELIST_FILE, L"rb");
    if (f) { fclose(f); return TRUE; }

    // Create it with a comment header so the format is self-documenti
    f = _wfopen(WHITELIST_FILE, L"w, ccs=UTF-8");
    if (f) {
        fwprintf(f, L"# USB Whitelist Config File — VendorID,ProductID,Description\n");
        fwprintf(f, L"# Sections: [whitelist] for allowed devices, [keydevice] for lockdown triggers\n");
        fwprintf(f, L"# Key devices must also be in the whitelist section!\n");
        fwprintf(f, L"# Example: 046D,C52B,Logitech Mouse\n\n");
        fwprintf(f, L"[whitelist]\n\n");
        fwprintf(f, L"\n[keydevice]\n\n");
        fclose(f);
    }
    return FALSE;
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
                // optionally check a guard (g_armed)
                HandleDeviceRemoval(devicePath);
                break;

            default:
                break;
            }
        }
        break;
        

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
                free(g_whitelist);
                free(g_keyDevice);
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

int wmain(int argc, wchar_t* argv[]) {
    HINSTANCE hInstance = GetModuleHandleW(NULL);
    BOOL fromConsole = LaunchedFromConsole();

    if (argc == 1 && fromConsole) {
        wprintf(L"[INFO] C Panic running in tray. Use tray icon to exit.\n");
    } else if (argc == 1 && !fromConsole) {
        FreeConsole();
    }

    if (argc > 1) {
        if (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0) {
            wprintf(L"C Panic - USBGuard 'Clone' for windows\n\n");
            wprintf(L"--help, -h      Show this help message\n");
            wprintf(L"--edit, -e      Open the whitelist file for editing\n");
            wprintf(L"--list, -l      List currently plugged USB devices\n");
            return 0;
        }
        else if (wcscmp(argv[1], L"--edit") == 0 || wcscmp(argv[1], L"-e") == 0) {
            EnsureWhitelistFile();
            ShellExecuteW(NULL, L"open", WHITELIST_FILE, NULL, NULL, SW_SHOW);
            return 0;
        }
        else if (wcscmp(argv[1], L"--list") == 0 || wcscmp(argv[1], L"-l") == 0) {
            EnumerateUSBDevices(NULL);
            return 0;
        }
    }
    
    BOOL existed = EnsureWhitelistFile();
    if (!existed) {
        AddLog(L"[INFO] No whitelist found — created empty %ws\n", WHITELIST_FILE); // Will be a toast notification in the future
        sendToastAsync(L"C Panic", L"No whitelist found — created empty whitelist.cfg");
    }
    
    if (!LoadWhitelist(&g_whitelist, &g_whitelistCount, &g_keyDevice, &g_keyDeviceCount)) {
        g_whitelistCount = 0;  g_whitelist = NULL;
        g_keyDeviceCount = 0;  g_keyDevice = NULL;
        AddLog(L"[WARN] Whitelist is empty\n");
        sendToastAsync(L"C Panic", L"Failed to load/empty whitelist");
    }

    AddLog(L"[INFO] Loaded %d whitelist + %d key device entries\n", g_whitelistCount, g_keyDeviceCount);

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
    
    g_hDevNotify = RegisterDeviceNotification(g_hwnd, &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    
    if (!g_hDevNotify) {
        MessageBox(NULL, L"Failed to register device notification!", L"Error", MB_OK);
        return 1;
    }

    ShowWindow(g_hwnd, SW_HIDE);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}
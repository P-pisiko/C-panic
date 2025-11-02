#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

#ifndef CM_DEVCAP_EJECTIONSUPPORTED
#define CM_DEVCAP_EJECTIONSUPPORTED 0x00000020
#endif


static wchar_t logBuffer[4096] = L""; 
static HWND g_hwnd = NULL;

void AddLog(const wchar_t *fmt, ...) {
    wchar_t buffer[512];
    va_list args;
    va_start(args, fmt); //Formatting stuff here
    vswprintf(buffer, 512, fmt, args);
    va_end(args);

    wcscat(logBuffer, buffer);
    wcscat(logBuffer, L"\n");

    InvalidateRect(g_hwnd, NULL, TRUE); //redraw call
}

BOOL Shutdown(BOOL bReboot) {
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;

	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		return FALSE;

	LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);

	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	// need to adjust privileges to allow user to shutdown
	if(!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES) NULL, 0))
		return FALSE;

	if(!InitiateSystemShutdown(NULL, NULL, 0, TRUE, bReboot))
		return FALSE;

	tkp.Privileges[0].Attributes = 0;

	AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES) NULL, 0);

	return TRUE;
}

void CursorToCenter() {
    int x = GetSystemMetrics(SM_CXSCREEN) / 2;
    int y = GetSystemMetrics(SM_CYSCREEN) / 2;
    SetCursorPos(x,y);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam){
    return 1;
}

DWORD WINAPI LoggerThread(LPVOID lp) {
    for(int i = 0; i < 100; i++) {
        AddLog(L"Test line -> %d",i);
        Sleep(101);
    }
}

BOOL EjectDevice(DEVINST devInst) {
    DEVINST parentDevInst = devInst;
    DEVINST currentDevInst = devInst;
    DWORD capabilities = 0;
    ULONG size = sizeof(capabilities);
    BOOL foundEjectable = FALSE;
    
    // Walking up device tree to find an ejectable parent
    for (int level = 0; level < 10; level++) {
        capabilities = 0;
        size = sizeof(capabilities);
        
        if (CM_Get_DevNode_Registry_Property(currentDevInst, CM_DRP_CAPABILITIES,
            NULL, &capabilities, &size, 0) == CR_SUCCESS) {
            
            if (capabilities & CM_DEVCAP_EJECTIONSUPPORTED) {
                AddLog("[INFO] Found ejectable device at level %d\n", level);
                parentDevInst = currentDevInst;
                foundEjectable = TRUE;
                break;
            }
        }
        
        DWORD removable = 0;
        size = sizeof(removable);
        if (CM_Get_DevNode_Registry_Property(currentDevInst, CM_DRP_REMOVAL_POLICY,
            NULL, &removable, &size, 0) == CR_SUCCESS) { //CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL 2 / 3 is removable
            if (removable == 2 || removable == 3) {
                AddLog("[INFO] Found removable device at level %d (policy: %d)\n", level, removable);
                parentDevInst = currentDevInst;
                foundEjectable = TRUE;
                break;
            }
        }

        DEVINST tempDevInst;
        if (CM_Get_Parent(&tempDevInst, currentDevInst, 0) != CR_SUCCESS) { // Move to parent
            break;
        }
        currentDevInst = tempDevInst;
    }
    
    if (!foundEjectable) {
        AddLog("[INFO] No ejectable/removable device found in hierarchy\n");
        return FALSE;
    }
    
    PNP_VETO_TYPE vetoType;
    WCHAR vetoName[MAX_PATH];
    
    AddLog("[INFO] Attempting to eject device...\n");
    CONFIGRET result = CM_Request_Device_Eject(parentDevInst, &vetoType, 
        vetoName, MAX_PATH, 0);
    
    if (result == CR_SUCCESS) {
        AddLog("[SUCCESS] Device ejected successfully!\n");
        return TRUE;
    } else {
        AddLog("[FAILED] Ejection failed with error code: 0x%X\n", result);
        if (vetoType != PNP_VetoTypeUnknown) {
            Addlog("[VETO] Veto Type: %d, Veto Name: %ws\n", vetoType, vetoName);
        }
        return FALSE;
    }
}

void EnumerateUSBDevices() {
    HDEVINFO deviceInfoSet;
    SP_DEVINFO_DATA deviceInfoData;
    DWORD i;
    
    // Get all USB devices
    deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE,
        NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        AddLog("Failed to get device information set\n");
        return;
    }
    
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) { // information about that device
        WCHAR deviceDesc[256];
        DWORD dataType;
        WCHAR devicePath[256];
        if (SetupDiGetDeviceInstanceId(deviceInfoSet, &deviceInfoData, devicePath, sizeof(devicePath) / sizeof(WCHAR), NULL))
        {
            AddLog("Instance ID: %ws\n", devicePath);
        }

        if (SetupDiGetDeviceRegistryProperty(deviceInfoSet, &deviceInfoData,
            SPDRP_DEVICEDESC, &dataType, (BYTE*)deviceDesc, 
            sizeof(deviceDesc), NULL)) {

            AddLog("[Device %d] %ws\n", i + 1, deviceDesc);

            EjectDevice(deviceInfoData.DevInst);
        }
    }
    
    SetupDiDestroyDeviceInfoList(deviceInfoSet); //Long ass function name
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg){
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
            if (wParam == VK_ESCAPE)
                PostQuitMessage(0);
            break;

        case WM_TIMER:
            CursorToCenter();
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

    g_hwnd  = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, // always on top, no taskbar icon
        L"TTYWindow", L"TTY Emulation",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL
    );

    SetTimer(g_hwnd, 1, 10, NULL); // Timer for the mouse center lock

    ShowWindow(g_hwnd, SW_SHOW);
    ShowCursor(FALSE);

    /*AddLog(L"[+] Panic screen initialized");
    AddLog(L"[+] Checking USB ports...");
    AddLog(L"[!] System locked");*/
    CreateThread(NULL, 0, EnumerateUSBDevices, NULL, 0, NULL);
    

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}

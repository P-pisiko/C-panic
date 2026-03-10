#include <windows.h>
#include "tray.h"

void EnsureWhitelistFile(void);
DWORD WINAPI EnumerateUSBDevices(LPVOID lp);

typedef struct {
    WORD  vendorId;
    WORD  productId;
    WCHAR description[128];
} WhitelistEntry;

extern HHOOK g_mouseHook;
extern HHOOK g_keyboardHook;
extern HDEVNOTIFY g_hDevNotify;
extern WhitelistEntry *g_whitelist;
extern WhitelistEntry *g_keyDevice;

#define WHITELIST_FILE L"whitelist.cfg"

static NOTIFYICONDATA g_nid = {0};


void AddTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAY_EDIT;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); //Custom icon later
    wcscpy_s(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"C Panic - USBGuard");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

void RemoveTrayIcon(void) {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}


BOOL HandleTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(wParam)) { // for WM_COMMAND
        default: break;
    }
    return FALSE;
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING,   ID_TRAY_LIST, L"List USB Devices");
    AppendMenuW(hMenu, MF_STRING,   ID_TRAY_EDIT, L"Edit Whitelist");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING,   ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void HandleTrayCommand(HWND hwnd, WORD cmd) {
    switch (cmd) {
        case ID_TRAY_EXIT:
            if (g_mouseHook)    UnhookWindowsHookEx(g_mouseHook);
            if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);
            if (g_hDevNotify)   UnregisterDeviceNotification(g_hDevNotify);
            RemoveTrayIcon();
            free(g_whitelist);
            free(g_keyDevice);
            PostQuitMessage(0);
            break;

        case ID_TRAY_EDIT:
            EnsureWhitelistFile();
            HINSTANCE result = ShellExecuteW(NULL, L"open", WHITELIST_FILE, NULL, NULL, SW_SHOW);
            if ((INT_PTR)result <= 32) {
                ShellExecuteW(NULL, L"open", L"notepad.exe", WHITELIST_FILE, NULL, SW_SHOW);
            }
            break;

        case ID_TRAY_LIST:
            EnumerateUSBDevices(NULL);
            break;
    }
}
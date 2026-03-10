#pragma once
#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_EDIT 1002
#define ID_TRAY_LIST 1003

void AddTrayIcon(HWND hwnd);
void RemoveTrayIcon(void);
BOOL HandleTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam);
void ShowTrayMenu(HWND hwnd);
void HandleTrayCommand(HWND hwnd, WORD cmd);
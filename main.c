#include <windows.h>
#include <stdio.h>


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
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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
    SetTimer(g_hwnd, 1, 10, NULL);
        

    ShowWindow(g_hwnd, SW_SHOW);
    ShowCursor(TRUE);
    
    AddLog(L"[+] Panic screen initialized");
    AddLog(L"[+] Checking USB ports...");
    AddLog(L"[!] System locked");
    CreateThread(NULL, 0, LoggerThread, NULL, 0, NULL);
    

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

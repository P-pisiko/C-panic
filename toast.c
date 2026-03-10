#include <windows.h>
#include <stdio.h>
#include <process.h>
#include "toast.h"

typedef struct {
    wchar_t title[256];
    wchar_t message[512];
} ToastParams;

static unsigned int WINAPI ToastThread(void* param) {
    ToastParams* p = (ToastParams*)param;

    wchar_t command[2048];
    swprintf(command, 2048,
        L"powershell.exe -WindowStyle Hidden -Command \""
        L"[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null; "
        L"[Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime] | Out-Null; "
        L"$xml = [Windows.Data.Xml.Dom.XmlDocument]::new(); "
        L"$xml.LoadXml('<toast><visual><binding template=\\\"ToastText02\\\"><text id=\\\"1\\\">%ls</text><text id=\\\"2\\\">%ls</text></binding></visual></toast>'); "
        L"$toast = [Windows.UI.Notifications.ToastNotification]::new($xml); "
        L"[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier('C Panic').Show($toast)\"",
        p->title, p->message);

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};

    if (CreateProcessW(NULL, command, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    free(p);
    return 0;
}

void sendToastAsync(const wchar_t* title, const wchar_t* message) {
    ToastParams* p = (ToastParams*)malloc(sizeof(ToastParams));
    if (!p) return;

    wcsncpy(p->title,   title,   255); p->title[255]   = L'\0';
    wcsncpy(p->message, message, 511); p->message[511] = L'\0';

    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ToastThread, p, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        free(p);
    }
}
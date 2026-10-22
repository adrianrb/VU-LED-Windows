#include "ui/tray.h"

#include <shellapi.h>

#include "ui/menu.h"

static const UINT WM_TRAYICON = WM_APP + 1;   // callback do ícone da bandeja

static NOTIFYICONDATAW gNid = {};
static UINT  gTaskbarCreated = 0;            // "TaskbarCreated" (Explorer reiniciou)
static HICON gTrayIcon = nullptr;

void TrayInit(HICON icon) {
    gTrayIcon = icon;
    gTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
}

void TrayAdd(HWND hwnd) {
    ZeroMemory(&gNid, sizeof(gNid));
    gNid.cbSize           = sizeof(gNid);
    gNid.hWnd             = hwnd;
    gNid.uID              = 1;
    gNid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gNid.uCallbackMessage = WM_TRAYICON;
    gNid.hIcon            = gTrayIcon;
    lstrcpynW(gNid.szTip, L"VU — Saída Padrão",
              (int)(sizeof(gNid.szTip) / sizeof(gNid.szTip[0])));
    Shell_NotifyIconW(NIM_ADD, &gNid);
}

void TrayRemove() {
    if (gNid.hWnd) Shell_NotifyIconW(NIM_DELETE, &gNid);
    ZeroMemory(&gNid, sizeof(gNid));
}

bool TrayHandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Explorer reiniciou: readiciona o ícone da bandeja.
    if (gTaskbarCreated && msg == gTaskbarCreated) { TrayAdd(hwnd); return true; }
    if (msg == WM_TRAYICON) {
        // Clique esquerdo ou direito no ícone: abre o mesmo menu de contexto.
        if (LOWORD(lParam) == WM_LBUTTONUP ||
            LOWORD(lParam) == WM_RBUTTONUP ||
            LOWORD(lParam) == WM_CONTEXTMENU) {
            ShowContextMenu(hwnd, true);
        }
        return true;
    }
    return false;
}

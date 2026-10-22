// Ícone na bandeja (Shell_NotifyIcon): clique abre o mesmo menu de contexto.
#pragma once
#include <windows.h>

void TrayInit(HICON icon);  // guarda o ícone e registra a mensagem "TaskbarCreated"
void TrayAdd(HWND hwnd);    // NIM_ADD (no WM_CREATE e quando o Explorer reinicia)
void TrayRemove();          // NIM_DELETE (no WM_DESTROY)

// Trata as mensagens da bandeja no WndProc; true = mensagem consumida.
bool TrayHandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

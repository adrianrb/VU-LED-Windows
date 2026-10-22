#include "ui/menu.h"

#include <cwchar>

#include "app/ids.h"
#include "ui/main_window.h"

void ShowContextMenu(HWND owner, bool fromTray) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_OPTIONS, L"Opções...");

    // "Tamanho:" → radio com a escala atual (ou a mais próxima).
    HMENU sz = CreatePopupMenu();
    int cur = CurrentScalePct();
    int nearIdx = 0;
    for (int i = 0; i < SCALE_COUNT; ++i) {
        int d = SCALES[i] - cur;
        if (d < 0) d = -d;
        int dbest = SCALES[nearIdx] - cur;
        if (dbest < 0) dbest = -dbest;
        if (d < dbest) nearIdx = i;
    }
    for (int i = 0; i < SCALE_COUNT; ++i) {
        wchar_t lbl[8];
        swprintf(lbl, 8, L"%d%%", SCALES[i]);
        AppendMenuW(sz, MF_STRING, IDM_SIZE + i, lbl);
    }
    CheckMenuRadioItem(sz, 0, SCALE_COUNT - 1, nearIdx, MF_BYPOSITION);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sz, L"Tamanho:");

    AppendMenuW(m, MF_STRING, IDM_ABOUT,   L"Sobre...");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_EXIT,    L"Sair\tAlt+F4");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(owner);            // necessário p/ o menu fechar sozinho
    // Na bandeja o menu abre acima do cursor (com 1-2 px de folga): se abrisse
    // sobre ele, o segundo clique de um duplo clique cairia no último item
    // ("Sair") e fecharia o app sem querer.
    UINT flags = TPM_RIGHTBUTTON | TPM_LEFTALIGN |
                 (fromTray ? TPM_BOTTOMALIGN : TPM_TOPALIGN);
    int x = fromTray ? pt.x - 8 : pt.x;
    int y = fromTray ? pt.y - 8 : pt.y;
    TrackPopupMenu(m, flags, x, y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(m);
}

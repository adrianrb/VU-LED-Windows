// Janela de configurações. Os controles são criados em código (sem .rc) para
// manter o app em um único .exe sem dependências.
#include "ui/options_window.h"

#include <commctrl.h>
#include <cwchar>

#include "app/config.h"
#include "app/ids.h"
#include "app/state.h"
#include "gfx/render.h"
#include "ui/main_window.h"

static LRESULT CALLBACK OptionsProc(HWND, UINT, WPARAM, LPARAM);

static HINSTANCE gInst = nullptr;      // hInstance do módulo (registrado no start)
static HBRUSH   gSwatchBrush = nullptr;
static COLORREF gCustom[16] = {};

// ---------------------------------------------------------------------------
// Helpers do slider: o trackbar vertical tem o mínimo no topo; a transparência
// é o inverso dele (0% embaixo, 100% no topo).
// ---------------------------------------------------------------------------
static int TransToSlider(int t) { return 100 - t; }
static int SliderToTrans(int p) { return 100 - p; }

// ---------------------------------------------------------------------------
// Cor do LED (ChooseColor) + amostra ao lado do botão
// ---------------------------------------------------------------------------
static void PickColor(HWND owner) {
    static COLORREF init = 0;
    init = gLedOn;
    CHOOSECOLORW cc = {};
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner    = owner;
    cc.rgbResult    = init;
    cc.lpCustColors = gCustom;
    cc.Flags        = CC_RGBINIT | CC_FULLOPEN | CC_ANYCOLOR;
    if (ChooseColorW(&cc)) {
        gLedOn = cc.rgbResult;
        if (gSwatchBrush) DeleteObject(gSwatchBrush);
        gSwatchBrush = CreateSolidBrush(gLedOn);
        InvalidateRect(GetDlgItem(gOpts, IDC_SWATCH), nullptr, TRUE);
        RequestRepaint();
    }
}

// ---------------------------------------------------------------------------
// Estado dos controles
// ---------------------------------------------------------------------------
void SyncOptions() {
    if (!gOpts) return;
    CheckDlgButton(gOpts, IDC_CHK_TITLE,
                   FrameEffective() ? BST_CHECKED : BST_UNCHECKED);
    // Com transparência ativa a janela é em camadas (sem moldura do sistema).
    EnableWindow(GetDlgItem(gOpts, IDC_CHK_TITLE), gBgTrans == 0);
    CheckDlgButton(gOpts, IDC_CHK_VERT,  gLayout == 1 ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(gOpts, IDC_CHK_PEAK,  gShowPeak ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(gOpts, IDC_CHK_TOPMOST, gTopmost ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(gOpts, IDC_CHK_STARTUP, gStartup ? BST_CHECKED : BST_UNCHECKED);
    CheckRadioButton(gOpts, IDC_RAD_SIMPLE, IDC_RAD_SCHEME,
                     (gScheme == 0) ? IDC_RAD_SIMPLE : IDC_RAD_SCHEME);
    SetWindowTextW(GetDlgItem(gOpts, IDC_CHK_VERT),
                   (gLayout == 1) ? L"Alternar para Horizontal"
                                  : L"Alternar para Vertical");
    EnableWindow(GetDlgItem(gOpts, IDC_BTN_COLOR), gScheme == 0);
    SendMessageW(GetDlgItem(gOpts, IDC_SLIDER_TRANS), TBM_SETPOS, TRUE,
                 TransToSlider(gBgTrans));
    wchar_t b[32];
    swprintf(b, 32, L"Atual: %d%%", gBgTrans);
    SetWindowTextW(GetDlgItem(gOpts, IDC_LBL_TRANS), b);
}

static void CreateOptionsControls(HWND h) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style,
                  int x, int y, int w, int hh, UINT id) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                                 x, y, w, hh, h, (HMENU)(INT_PTR)id,
                                 hInst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };
    mk(L"BUTTON", L"Exibir barra de título", BS_AUTOCHECKBOX,
       12, 14, 320, 18, IDC_CHK_TITLE);
    mk(L"BUTTON", L"Alternar para Vertical", BS_AUTOCHECKBOX,
       12, 40, 320, 18, IDC_CHK_VERT);
    mk(L"BUTTON", L"Exibir pico", BS_AUTOCHECKBOX,
       12, 66, 320, 18, IDC_CHK_PEAK);
    mk(L"BUTTON", L"Sempre visível", BS_AUTOCHECKBOX,
       12, 92, 320, 18, IDC_CHK_TOPMOST);
    mk(L"BUTTON", L"Iniciar com o Windows", BS_AUTOCHECKBOX,
       12, 118, 320, 18, IDC_CHK_STARTUP);
    mk(L"STATIC", L"Esquema de cores:", SS_LEFT,
       12, 148, 180, 16, 0);
    mk(L"BUTTON", L"Simples (atual)", BS_AUTORADIOBUTTON | WS_GROUP,
       24, 166, 300, 18, IDC_RAD_SIMPLE);
    mk(L"BUTTON", L"Verde (0) | Amarelo (70%) | Vermelho (85%)",
       BS_AUTORADIOBUTTON,
       24, 186, 310, 18, IDC_RAD_SCHEME);
    mk(L"BUTTON", L"Escolher cor do LED...", BS_PUSHBUTTON,
       12, 216, 170, 26, IDC_BTN_COLOR);
    mk(L"STATIC", L"", WS_BORDER | SS_NOTIFY,
       192, 216, 56, 26, IDC_SWATCH);
    mk(L"BUTTON", L"Salvar", BS_PUSHBUTTON,
       12, 254, 90, 26, IDC_BTN_SAVE);
    mk(L"BUTTON", L"Fechar", BS_PUSHBUTTON,
       112, 254, 90, 26, IDC_BTN_CLOSE);

    // Transparência do fundo: slider vertical 0..100 (incremento 1).
    // O valor é invertido (0% embaixo), então as dicas acompanham o eixo.
    mk(L"STATIC", L"Transparência\ndo fundo (%)", SS_LEFT,
       344, 12, 118, 34, 0);
    mk(L"STATIC", L"100%", SS_LEFT, 398, 50, 56, 16, 0);
    HWND sl = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                              WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_AUTOTICKS,
                              352, 68, 40, 164, h,
                              (HMENU)(INT_PTR)IDC_SLIDER_TRANS, hInst, nullptr);
    SendMessageW(sl, WM_SETFONT, (WPARAM)f, TRUE);
    SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(sl, TBM_SETTICFREQ, 10, 0);
    SendMessageW(sl, TBM_SETPOS, TRUE, TransToSlider(gBgTrans));
    mk(L"STATIC", L"0%", SS_LEFT, 398, 216, 56, 16, 0);
    mk(L"STATIC", L"Atual: 0%", SS_CENTER, 344, 242, 118, 18, IDC_LBL_TRANS);
    SyncOptions();
}

// ---------------------------------------------------------------------------
// Janela
// ---------------------------------------------------------------------------
void OptionsInit() {
    if (!gSwatchBrush) gSwatchBrush = CreateSolidBrush(gLedOn);
}

void OptionsCleanup() {
    if (gSwatchBrush) { DeleteObject(gSwatchBrush); gSwatchBrush = nullptr; }
}

bool RegisterOptionsClass(HINSTANCE hInst, HICON big, HICON small) {
    gInst = hInst;
    WNDCLASSEX oc = { sizeof(oc) };
    oc.lpfnWndProc   = OptionsProc;
    oc.hInstance     = hInst;
    oc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    oc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    oc.lpszClassName = OPT_CLASS;
    oc.hIcon         = big;
    oc.hIconSm       = small;
    return RegisterClassEx(&oc) != 0;
}

void ShowOptions() {
    if (!gOpts) {
        RECT rc = { 0, 0, OPT_CW, OPT_CH };
        AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        gOpts = CreateWindowExW(0, OPT_CLASS, L"Opções...",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, gInst, nullptr);
        if (!gOpts) return;
    }
    SyncOptions();

    // Posiciona junto à janela do VU, centralizada, dentro da área de trabalho.
    RECT vr, or_;
    GetWindowRect(gMain, &vr);
    GetWindowRect(gOpts, &or_);
    int ow = or_.right - or_.left, oh = or_.bottom - or_.top;
    int x = vr.left + ((vr.right - vr.left) - ow) / 2;
    int y = vr.bottom + 4;
    HMONITOR mon = MonitorFromRect(&vr, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    if (x + ow > mi.rcWork.right)  x = mi.rcWork.right - ow - 4;
    if (y + oh > mi.rcWork.bottom) y = vr.top - oh - 4;   // sem espaço abaixo → acima
    if (x < mi.rcWork.left)   x = mi.rcWork.left + 4;
    if (y < mi.rcWork.top)    y = mi.rcWork.top + 4;

    SetWindowPos(gOpts, HWND_TOP, x, y, 0, 0,
                 SWP_NOSIZE | SWP_SHOWWINDOW);
    ShowWindow(gOpts, SW_SHOW);
    SetForegroundWindow(gOpts);
}

static LRESULT CALLBACK OptionsProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateOptionsControls(h);
        return 0;

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case IDC_CHK_TITLE: ToggleTitle(gMain); break;
            case IDC_CHK_VERT:  ToggleLayout();     break;
            case IDC_CHK_PEAK:
                gShowPeak = (IsDlgButtonChecked(h, IDC_CHK_PEAK) == BST_CHECKED);
                if (!gShowPeak) gPeakL = gPeakR = 0.0f;  // limpa a marca
                RequestRepaint();
                break;
            case IDC_CHK_TOPMOST:
                gTopmost = (IsDlgButtonChecked(h, IDC_CHK_TOPMOST) == BST_CHECKED);
                ApplyTopmost();
                break;
            case IDC_CHK_STARTUP:
                gStartup = (IsDlgButtonChecked(h, IDC_CHK_STARTUP) == BST_CHECKED);
                SetStartup(gStartup);
                SaveIni();      // efeito externo (registro): grava o estado já
                break;
            case IDC_RAD_SIMPLE:
            case IDC_RAD_SCHEME:
                gScheme = (LOWORD(wParam) == IDC_RAD_SIMPLE) ? 0 : 1;
                SyncOptions();
                RequestRepaint();
                break;
            case IDC_BTN_COLOR: PickColor(h);       break;
            case IDC_BTN_SAVE:
                SaveIni();
                SetWindowTextW(GetDlgItem(h, IDC_BTN_SAVE), L"Salvo!");
                SetTimer(h, IDC_SAVE_FEEDBACK, 1200, nullptr);
                break;
            case IDC_BTN_CLOSE: ShowWindow(h, SW_HIDE); break;
            }
        }
        return 0;

    // Trackbar vertical manda WM_VSCROLL (TBS_VERT), não WM_HSCROLL; aceitamos
    // os dois para não depender da orientação do controle.
    case WM_HSCROLL:
    case WM_VSCROLL: {
        HWND sl = GetDlgItem(h, IDC_SLIDER_TRANS);
        if (sl && (HWND)lParam == sl) {
            int v = SliderToTrans((int)SendMessageW(sl, TBM_GETPOS, 0, 0));
            SetBgTrans(v);   // aplica na hora: o VU acompanha o arrasto
            wchar_t b[32];
            swprintf(b, 32, L"Atual: %d%%", gBgTrans);
            SetWindowTextW(GetDlgItem(h, IDC_LBL_TRANS), b);
            if (LOWORD(wParam) == TB_ENDTRACK) SaveIni();   // persistir ao soltar
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDC_SAVE_FEEDBACK) {
            SetWindowTextW(GetDlgItem(h, IDC_BTN_SAVE), L"Salvar");
            KillTimer(h, IDC_SAVE_FEEDBACK);
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == GetDlgItem(h, IDC_SWATCH) && gSwatchBrush) {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, gLedOn);
            SetTextColor(hdc, gLedOn);
            return (LRESULT)gSwatchBrush;
        }
        break;

    case WM_CLOSE:           // o X apenas esconde; destruir fica para a saída
        ShowWindow(h, SW_HIDE);
        return 0;
    }
    return DefWindowProc(h, msg, wParam, lParam);
}

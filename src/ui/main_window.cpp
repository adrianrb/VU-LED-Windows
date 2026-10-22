// Janela principal: timers de captura, pintura (GDI ou camada), arrasto e o
// menu de contexto. As toggles de preferência ficam aqui porque mexem no
// estilo/tamanho da janela.
#include "ui/main_window.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "app/config.h"
#include "app/ids.h"
#include "app/state.h"
#include "app/version.h"
#include "audio/capture.h"
#include "gfx/render.h"
#include "ui/menu.h"
#include "ui/options_window.h"
#include "ui/tray.h"

static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

static bool gDragging = false;
static POINT gDragStart = {};
static RECT  gDragRect = {};
static int   gLastLitL = -1, gLastLitR = -1;  // suprime repaint quando ocioso
static bool  gPeakWasVisible = false;         // p/ apagar o LED do pico no fim

// ---------------------------------------------------------------------------
// Ações
// ---------------------------------------------------------------------------

// Aplica moldura/barra de título preservando o tamanho da área cliente.
static void ApplyFrameStyle(bool frame) {
    // Lê o cliente ANTES de mexer no estilo: depois do SetWindowLongPtr o
    // GetClientRect já pode refletir o estilo novo (popup ⇒ cliente = janela),
    // o que fazia o tamanho "derivar" a cada alternância.
    RECT crc;
    GetClientRect(gMain, &crc);

    LONG_PTR st = GetWindowLongPtr(gMain, GWL_STYLE);
    if (frame) {
        st = (st & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
    } else {
        st = (st & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
                     WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
    }
    st |= WS_VISIBLE;
    SetWindowLongPtr(gMain, GWL_STYLE, st);

    RECT adj = crc;
    AdjustWindowRect(&adj, (DWORD)st, FALSE);
    SetWindowPos(gMain, nullptr, 0, 0, adj.right - adj.left, adj.bottom - adj.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

bool FrameEffective() { return gTitleBar && gBgTrans == 0; }

// "Sempre visível": mantém a janela acima das demais (ou devolve ao normal).
void ApplyTopmost() {
    if (!gMain) return;
    SetWindowPos(gMain, gTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Define a transparência do fundo (0..100) e ajusta o modo camada.
void SetBgTrans(int t) {
    t = std::clamp(t, 0, 100);
    bool wasLayered = (gBgTrans > 0);
    gBgTrans = t;
    bool layered = (t > 0);
    if (layered != wasLayered) {
        LONG_PTR ex = GetWindowLongPtr(gMain, GWL_EXSTYLE);
        ex = layered ? (ex | WS_EX_LAYERED) : (ex & ~WS_EX_LAYERED);
        SetWindowLongPtr(gMain, GWL_EXSTYLE, ex);
        ApplyFrameStyle(FrameEffective());
        SyncOptions();
    }
    RequestRepaint();
}

// Mostra/oculta a barra de título preservando o tamanho da área cliente.
void ToggleTitle(HWND) {
    if (gBgTrans > 0) return;        // transparente ⇒ sempre sem moldura
    gTitleBar = !(GetWindowLongPtr(gMain, GWL_STYLE) & WS_CAPTION);
    ApplyFrameStyle(FrameEffective());
    SyncOptions();
    // Alternar a barra de título é uma ação rápida (duplo clique), fora do
    // botão "Salvar": grava na hora para o próximo start respeitar o estado.
    SaveIni();
}

// Alterna horizontal/vertical; inverte também a altura/largura da janela para
// preservar a "área" do medidor (300x75 vira 75x300 e vice-versa).
void ToggleLayout() {
    gLayout ^= 1;
    RECT rc;
    GetWindowRect(gMain, &rc);
    SetWindowPos(gMain, nullptr, 0, 0, rc.bottom - rc.top, rc.right - rc.left,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SyncOptions();
    RequestRepaint();
}

// % atual = largura do cliente ÷ largura base (300 em H, 75 em V) — vale para
// qualquer layout, pois a base acompanha a orientação.
int CurrentScalePct() {
    if (!gMain) return 100;
    RECT rc;
    GetClientRect(gMain, &rc);
    int baseW = (gLayout == 0) ? DEFAULT_W : DEFAULT_H;
    if (rc.right <= 0) return 100;
    return (int)std::lround(rc.right * 100.0 / baseW);
}

// Redimensiona a janela mantendo o canto superior esquerdo.
void ApplyScale(int pct) {
    int cw  = ((gLayout == 0) ? DEFAULT_W  : DEFAULT_H) * pct / 100;
    int chh = ((gLayout == 0) ? DEFAULT_H : DEFAULT_W)  * pct / 100;
    cw  = std::max(16, cw);
    chh = std::max(12, chh);
    DWORD style = (DWORD)GetWindowLongPtr(gMain, GWL_STYLE);
    RECT rc = { 0, 0, cw, chh };
    AdjustWindowRect(&rc, style, FALSE);
    SetWindowPos(gMain, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RequestRepaint();
}

// ---------------------------------------------------------------------------
// Registro da classe
// ---------------------------------------------------------------------------
bool RegisterMainWindowClass(HINSTANCE hInst, HICON big, HICON small) {
    WNDCLASSEX wc = { sizeof(wc) };
    wc.style         = CS_DBLCLKS;           // habilita WM_LBUTTONDBLCLK
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon         = big;
    wc.hIconSm       = small;
    return RegisterClassEx(&wc) != 0;
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Bandeja: "TaskbarCreated" (Explorer reiniciou) e clique no ícone.
    if (TrayHandleMessage(hwnd, msg, wParam, lParam)) return 0;

    switch (msg) {
    case WM_CREATE:
        TrayAdd(hwnd);                           // ícone na bandeja
        SetTimer(hwnd, IDT_AUDIO, 10, nullptr);  // tick de 10 ms
        return 0;

    case WM_SIZE:
        // Redimensionou: refaz o buffer do modo camada no novo tamanho.
        if (gBgTrans > 0) RenderLayered();
        break;          // segue para o DefWindowProc

    case WM_TIMER:
        if (wParam == IDT_AUDIO) {
            float pl, pr;
            gAudio.Poll(pl, pr);
            // Ataque instantâneo, decaimento suave.
            gHoldL = (pl > gHoldL) ? pl : (gHoldL * RELEASE);
            gHoldR = (pr > gHoldR) ? pr : (gHoldR * RELEASE);
            if (gHoldL < 1e-5f) gHoldL = 0.0f;
            if (gHoldR < 1e-5f) gHoldR = 0.0f;

            // Pico (hold): sobe com o nível exibido, segura ~0,7 s e cai.
            // Usar o nível suavizado (gHold) em vez do pico cru evita que o
            // LED do pico fique grudado no topo: com música comprimida o pico
            // instantâneo bate no teto a cada buffer (~45 ms) e re-arma o hold
            // para sempre. Depois do hold o pico cai e, se alcançar o nível,
            // passa a acompanhá-lo (nunca fica abaixo da barra).
            auto updPeak = [](float lvl, float& peak, int& hold) {
                if (lvl > peak) { peak = lvl; hold = PEAK_HOLD_TICKS; }
                else if (hold > 0) { --hold; }
                else { peak *= PEAK_FALL; if (peak < lvl) peak = lvl; }
                if (peak < 1e-5f) peak = 0.0f;
            };
            updPeak(gHoldL, gPeakL, gPeakHoldL);
            updPeak(gHoldR, gPeakR, gPeakHoldR);

            int litL = LevelToLitCount(gHoldL);
            int litR = LevelToLitCount(gHoldR);
            // Sem som (holds zerados) e LEDs já apagados → não repinta.
            // Evita repaint contínuo a 100 Hz quando ocioso (flicker).
            bool peakVisible = gShowPeak && (gPeakL > 0.0f || gPeakR > 0.0f);
            if (litL != gLastLitL || litR != gLastLitR ||
                gHoldL > 0.0f || gHoldR > 0.0f ||
                peakVisible || gPeakWasVisible) {
                gLastLitL = litL;
                gLastLitR = litR;
                RequestRepaint();
            }
            gPeakWasVisible = peakVisible;   // 1 repaint a mais p/ apagar o pico
        }
        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH b = CreateSolidBrush(COL_BG);
        FillRect(hdc, &rc, b);
        DeleteObject(b);
        return 1;
    }

    case WM_PAINT: {
        if (gBgTrans > 0) {
            // Janela em camadas: o conteúdo vem de UpdateLayeredWindow.
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            RenderLayered();
            return 0;
        }
        // Double buffering: desenha tudo em uma DC memória e copia uma única
        // vez com BitBlt — sem flicker do GDI pintando direto na tela.
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int W = rc.right, H = rc.bottom;
        if (W > 0 && H > 0) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            DrawAll(mem, W, H);
            BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDBLCLK:                    // duplo clique → barra de título
        ToggleTitle(hwnd);
        return 0;

    case WM_RBUTTONUP:
        ShowContextMenu(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_OPTIONS: ShowOptions(); return 0;
        default:
            if (LOWORD(wParam) >= IDM_SIZE &&
                LOWORD(wParam) < IDM_SIZE + SCALE_COUNT) {
                ApplyScale(SCALES[LOWORD(wParam) - IDM_SIZE]);
                return 0;
            }
            break;
        case IDM_ABOUT: {
            std::wstring msg = L"VU — Saída Padrão\nVersão ";
            msg += APP_VERSION;
            msg += L"\n\n(2026) Copyleft by adrian.barbosa@gmail.com";
            MessageBoxW(hwnd, msg.c_str(), L"Sobre...", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_GETMINMAXINFO: {
        // Mínimo = o tamanho da menor escala (25%) + moldura; impede fechar a
        // janela ou espremer demais, mas não bloqueia os presets pequenos.
        int cw  = DEFAULT_W * SCALES[0] / 100;   // 25% de 300
        int chh = DEFAULT_H * SCALES[0] / 100;   // 25% de 75
        RECT rc = { 0, 0, std::max(48, cw), std::max(24, chh) };
        AdjustWindowRect(&rc, (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE), FALSE);
        ((MINMAXINFO*)lParam)->ptMinTrackSize = { rc.right, rc.bottom };
        return 0;
    }

    // Sem barra de título: arrastar pela área cliente move a janela.
    case WM_LBUTTONDOWN:
        if (!gTitleBar) {
            gDragging = true;
            GetCursorPos(&gDragStart);
            GetWindowRect(hwnd, &gDragRect);
            SetCapture(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (gDragging && (wParam & MK_LBUTTON)) {
            POINT p;
            GetCursorPos(&p);
            SetWindowPos(hwnd, nullptr,
                         gDragRect.left + (p.x - gDragStart.x),
                         gDragRect.top  + (p.y - gDragStart.y),
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (gDragging) {
            gDragging = false;
            if (msg == WM_LBUTTONUP && GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;

    case WM_DESTROY:
        SaveIni();          // persiste tudo (inclusive toggles fora do "Salvar")
        TrayRemove();
        FreeDib();
        KillTimer(hwnd, IDT_AUDIO);
        gAudio.Shutdown();
        if (gOpts) DestroyWindow(gOpts);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

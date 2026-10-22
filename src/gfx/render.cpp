// Desenho do VU: LEDs (incl. o LED do pico), rótulos L/R — em GDI puro, para uma
// DC de memória (double buffering) ou para o DIB 32bpp do modo camada.
#include "gfx/render.h"

#include <algorithm>
#include <cmath>

#include "app/ids.h"
#include "app/state.h"

// Buffer 32bpp do modo camada (transparência do fundo).
static HBITMAP gDib = nullptr;
static HDC     gDibDC = nullptr;
static void*   gDibBits = nullptr;
static int     gDibW = 0, gDibH = 0;

// ---------------------------------------------------------------------------
// Níveis -> LEDs / posição
// ---------------------------------------------------------------------------
static COLORREF LedOffColor(COLORREF c) {
    return RGB(GetRValue(c) * 3 / 10, GetGValue(c) * 3 / 10, GetBValue(c) * 3 / 10);
}

int LevelToLitCount(float level) {
    if (level <= 1e-5f) return 0;
    float db = 20.0f * std::log10(level);
    float t  = std::clamp((db - DB_MIN) / (0.0f - DB_MIN), 0.0f, 1.0f);
    return (int)std::lround(t * LED_COUNT);
}

// Posição normalizada (0..1) do nível na escala — usada p/ o LED do pico.
static float LevelToPos(float level) {
    if (level <= 1e-5f) return 0.0f;
    float db = 20.0f * std::log10(level);
    return std::clamp((db - DB_MIN) / (0.0f - DB_MIN), 0.0f, 1.0f);
}

// Quantidade de LEDs que o pico acende (0..LED_COUNT); 0 = sem pico.
// O pico é o LED da posição do pico, aceso na cor daquela posição (hold do
// Winamp) — **não** um traço branco. Ele fica aceso mesmo com a barra abaixo
// e desce junto com o decaimento (`PEAK_HOLD_TICKS`/`PEAK_FALL`).
static int PeakLitCount(float level) {
    if (level <= 1e-5f) return 0;
    return std::clamp((int)std::lround(LevelToPos(level) * LED_COUNT), 1, LED_COUNT);
}

// Cor acesa do LED i conforme o esquema:
//  0 (simples): cor única (gLedOn).
//  1 (faixas): Verde da posição 0, Amarelo a partir de 70%, Vermelho a 85%.
static COLORREF LedColor(int i) {
    if (gScheme == 0) return gLedOn;
    float pos = (i + 1) / (float)LED_COUNT;
    if (pos < 0.70f) return RGB(0, 220, 60);
    if (pos < 0.85f) return RGB(240, 210, 0);
    return RGB(240, 40, 40);
}

static void DrawLed(HDC hdc, int x, int y, int w, int h, COLORREF color) {
    if (w < 1 || h < 1) return;
    HBRUSH b = CreateSolidBrush(color);
    HGDIOBJ old = SelectObject(hdc, b);
    RoundRect(hdc, x, y, x + w, y + h, 3, 3);
    SelectObject(hdc, old);
    DeleteObject(b);
}

// ---------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------
void DrawAll(HDC hdc, int W, int H) {
    RECT rc = { 0, 0, W, H };
    HBRUSH bg = CreateSolidBrush(COL_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    COLORREF offAll = LedOffColor(gLedOn);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(170, 170, 170));

    // Rótulos L/R: a fonte padrão do DC (System) é alta demais para a faixa de
    // 14px e as letras saíam cortadas embaixo. Usa a fonte de UI e mede o texto
    // de verdade (L e R têm larguras diferentes) para dimensionar/centrar.
    HGDIOBJ oldFont = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    SIZE gl[2] = { { 6, 12 }, { 6, 12 } };   // extents de "L" e "R"
    const wchar_t* glTxt[2] = { L"L", L"R" };
    SIZE m;
    for (int ch = 0; ch < 2; ++ch) {
        if (GetTextExtentPoint32W(hdc, glTxt[ch], 1, &m) && m.cx > 0 && m.cy > 0)
            gl[ch] = m;
    }
    int lblW = std::max((int)gl[0].cx, (int)gl[1].cx);
    int lblH = std::max((int)gl[0].cy, (int)gl[1].cy);

    int lit[2] = { LevelToLitCount(gHoldL), LevelToLitCount(gHoldR) };
    int peakLit[2] = { PeakLitCount(gPeakL), PeakLitCount(gPeakR) };

    if (gLayout == 0) {
        // Horizontal: barras empilhadas (L em cima, R embaixo), crescem p/ direita.
        const int mt = 6, mb = 6, rowGap = 8;
        int labelW = lblW + 5;
        int rowH = std::max(4, (H - mt - mb - rowGap) / 2);
        int areaX = labelW;
        int areaW = std::max(LED_COUNT, W - labelW - 6);
        int segW  = (areaW - (LED_COUNT - 1) * LED_GAP) / LED_COUNT;
        for (int ch = 0; ch < 2; ++ch) {
            int y = mt + ch * (rowH + rowGap);
            for (int i = 0; i < LED_COUNT; ++i) {
                int x = areaX + i * (segW + LED_GAP);
                COLORREF on = LedColor(i);
                // Pico: acende o LED da posição do pico mesmo com a barra abaixo.
                bool peakLed = gShowPeak && (i == peakLit[ch] - 1);
                DrawLed(hdc, x, y, segW, rowH, (i < lit[ch] || peakLed) ? on
                        : (gScheme == 0 ? offAll : LedOffColor(on)));
            }
            TextOut(hdc, (labelW - (int)gl[ch].cx) / 2, y + (rowH - (int)gl[ch].cy) / 2,
                    glTxt[ch], 1);
        }
    } else {
        // Vertical: colunas lado a lado (L esq., R dir.), crescem de baixo p/ cima.
        const int mt = 6, mx = 6, colGap = 10;
        int labelH  = lblH + 4;
        int areaH   = std::max(LED_COUNT, H - mt - labelH);
        int segH    = (areaH - (LED_COUNT - 1) * LED_GAP) / LED_COUNT;
        int colW    = std::max(4, (W - 2 * mx - colGap) / 2);
        int yBottom = H - labelH;
        for (int ch = 0; ch < 2; ++ch) {
            int x = mx + ch * (colW + colGap);
            for (int i = 0; i < LED_COUNT; ++i) {
                int y = yBottom - (i + 1) * segH - i * LED_GAP;
                COLORREF on = LedColor(i);
                // Pico: acende o LED da posição do pico mesmo com a barra abaixo.
                bool peakLed = gShowPeak && (i == peakLit[ch] - 1);
                DrawLed(hdc, x, y, colW, segH, (i < lit[ch] || peakLed) ? on
                        : (gScheme == 0 ? offAll : LedOffColor(on)));
            }
            // Centraliza no eixo X da coluna e mantém a letra dentro da janela.
            int lx = std::clamp(x + (colW - (int)gl[ch].cx) / 2, 0,
                                std::max(0, W - (int)gl[ch].cx));
            TextOut(hdc, lx, H - labelH + 2, glTxt[ch], 1);
        }
    }
    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldPen);
}

// ---------------------------------------------------------------------------
// Modo camada: transparência só do fundo (LEDs/texto continuam opacos)
// ---------------------------------------------------------------------------
void FreeDib() {
    if (gDibDC) { DeleteDC(gDibDC); gDibDC = nullptr; }
    if (gDib)   { DeleteObject(gDib); gDib = nullptr; }
    gDibBits = nullptr;
    gDibW = gDibH = 0;
}

static bool EnsureDib(int W, int H) {
    if (gDib && gDibW == W && gDibH == H) return true;
    FreeDib();
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = W;
    bi.bmiHeader.biHeight      = -H;          // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    gDib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &gDibBits, nullptr, 0);
    gDibDC = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!gDib || !gDibDC || !gDibBits) { FreeDib(); return false; }
    SelectObject(gDibDC, gDib);
    gDibW = W;
    gDibH = H;
    return true;
}

// Desenha no buffer 32bpp, converte o fundo (COL_BG) para a transparência
// pedida e mantém LEDs/texto opacos; entrega via UpdateLayeredWindow.
void RenderLayered() {
    if (!gMain || gBgTrans <= 0) return;
    RECT rc;
    GetClientRect(gMain, &rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;
    if (!EnsureDib(W, H)) return;
    DrawAll(gDibDC, W, H);
    // GDI pode estar com as operações em buffer: sem o flush, o loop abaixo
    // leria lixo e o flush posterior sobrescreveria os alphas.
    GdiFlush();

    // Alpha mínimo 1: pixel com alpha 0 vira "click-through" e a janela não
    // poderia mais ser arrastada pelo fundo.
    BYTE aBg = (BYTE)std::clamp(255 * (100 - gBgTrans) / 100, 1, 255);
    DWORD* p = (DWORD*)gDibBits;
    int n = W * H;
    for (int i = 0; i < n; ++i) {
        if ((p[i] & 0x00FFFFFFu) == 0) p[i] = (DWORD)aBg << 24;  // fundo (preto)
        else                           p[i] |= 0xFF000000u;       // conteúdo
    }

    HDC screen = GetDC(nullptr);
    POINT src = { 0, 0 };
    RECT wr;
    GetWindowRect(gMain, &wr);
    POINT dst = { wr.left, wr.top };
    SIZE  sz  = { W, H };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(gMain, screen, &dst, &sz, gDibDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

// Repinta pelo caminho certo: camada (imediato) ou GDI normal (via WM_PAINT).
void RequestRepaint() {
    if (!gMain) return;
    if (gBgTrans > 0) RenderLayered();
    else              InvalidateRect(gMain, nullptr, FALSE);
}

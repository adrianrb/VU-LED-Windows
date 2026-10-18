// VU de LEDs mono (L+R) — lê a saída padrão do Windows via WASAPI loopback.
// Depende apenas de Win32/GDI/WASAPI/ComDlg. Compilável com MSVC ou MinGW-w64.
//
// Interação:
//  - Clique direito → menu: "Opções...", "Tamanho:" (escala 25%..400%) e
//    "Sobre...".
//  - Duplo clique em qualquer lugar da janela → mostra/oculta barra de título.
//  - Janela redimensionável; sem barra de título, arrastar pelo cliente move.
//  - Opções: título on/off, Horizontal/Vertical (troca W↔H), linha de pico,
//    esquema de cores (simples ou verde/amarelo/vermelho), cor do LED
//    (ChooseColor) e "Salvar" → grava X,Y,W,H,cor,estilo em VULed.ini (ao lado
//    do .exe). Linha de pico e esquema NÃO são persistidos (escopo do Save).
//
// Áudio: a cada 10 ms drena o buffer de loopback do dispositivo de render
// padrão e calcula o pico por canal. Loopback só entrega dados quando há
// playback ativo; sem som, as barras ficam zeradas (comportamento esperado).

#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <commdlg.h>
#include <commctrl.h>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>

// ksmedia.h do MinGW-w64 não fornece a definição deste GUID; definimos manualmente.
static const GUID GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
    0x00000003, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// ---------------------------------------------------------------------------
// Constantes / layout
// ---------------------------------------------------------------------------
static const int DEFAULT_W  = 300;
static const int DEFAULT_H  = 75;
static const int LED_COUNT  = 26;
static const int LED_GAP    = 2;
static const float DB_MIN   = -60.0f;
static const float RELEASE  = 0.85f;  // decaimento por tick de 10 ms

static const COLORREF COL_BG = RGB(0, 0, 0);

// IDs de comando/controles
enum {
    IDM_OPTIONS = 100,
    IDM_ABOUT,
    IDM_SIZE = 110,            // ..IDM_SIZE+8 (9 escalas)
    IDC_CHK_TITLE = 200,
    IDC_CHK_VERT,
    IDC_CHK_PEAK,
    IDC_RAD_SIMPLE,
    IDC_RAD_SCHEME,
    IDC_BTN_COLOR,
    IDC_BTN_SAVE,
    IDC_BTN_CLOSE,
    IDC_SWATCH,
    IDC_SLIDER_TRANS,
    IDC_LBL_TRANS,
    IDC_SAVE_FEEDBACK = 300,
    IDT_AUDIO = 1,
};

static const int SCALES[] = { 25, 50, 75, 100, 125, 150, 200, 300, 400 };
static const int SCALE_COUNT = (int)(sizeof(SCALES) / sizeof(SCALES[0]));

static const int PEAK_HOLD_TICKS = 60;   // hold do pico: 600 ms (60 × 10 ms)
static const float PEAK_FALL = 0.97f;    // queda do pico após o hold

static const wchar_t* MAIN_CLASS = L"VULedMeter";
static const wchar_t* OPT_CLASS  = L"VUOptions";
static const int OPT_CW = 470, OPT_CH = 240;

// ---------------------------------------------------------------------------
// Captura WASAPI loopback do dispositivo de render padrão
// ---------------------------------------------------------------------------
struct AudioCapture {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice*           device     = nullptr;
    IAudioClient*        client     = nullptr;
    IAudioCaptureClient* capture    = nullptr;
    WAVEFORMATEX*        mix        = nullptr;
    bool  isFloat = true;
    int   channels = 0;
    bool  ready = false;
    double retryAccum = 0.0;

    void Shutdown() {
        if (capture) { capture->Release(); capture = nullptr; }
        if (client)  { client->Release();  client  = nullptr; }
        if (mix)     { CoTaskMemFree(mix); mix      = nullptr; }
        if (device)  { device->Release();  device   = nullptr; }
        ready = false;
    }

    bool Init() {
        Shutdown();
        HRESULT hr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                              CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              (void**)&enumerator);
        if (FAILED(hr)) return false;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) { Shutdown(); return false; }
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              (void**)&client);
        if (FAILED(hr)) { Shutdown(); return false; }
        hr = client->GetMixFormat(&mix);
        if (FAILED(hr)) { Shutdown(); return false; }

        // Determina o formato: float (padrão) ou PCM 16-bit.
        if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            WAVEFORMATEXTENSIBLE* ext = (WAVEFORMATEXTENSIBLE*)mix;
            isFloat = IsEqualGUID(ext->SubFormat, GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        } else {
            isFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        }
        channels = mix->nChannels;

        // Loopback: captura o que o renderizador de destino está tocando.
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK,
                                0, 0, mix, nullptr);
        if (FAILED(hr)) { Shutdown(); return false; }
        hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture);
        if (FAILED(hr)) { Shutdown(); return false; }
        hr = client->Start();
        if (FAILED(hr)) { Shutdown(); return false; }

        ready = true;
        return true;
    }

    // Drena todos os pacotes disponíveis e devolve o pico por canal (0..1).
    void Poll(float& peakL, float& peakR) {
        peakL = peakR = 0.0f;
        if (!ready) {
            // Sem áudio (dispositivo removido, etc.): tenta reinit a cada ~1 s.
            retryAccum += 0.010;
            if (retryAccum >= 1.0) { retryAccum = 0.0; Init(); }
            return;
        }

        for (;;) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            HRESULT hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
            if (FAILED(hr)) { Shutdown(); return; }

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                if (isFloat) {
                    const float* p = (const float*)data;
                    for (UINT32 i = 0; i < frames; ++i) {
                        float l = std::fabs(p[i * channels]);
                        float r = (channels > 1) ? std::fabs(p[i * channels + 1]) : l;
                        if (l > peakL) peakL = l;
                        if (r > peakR) peakR = r;
                    }
                } else {
                    const int16_t* p = (const int16_t*)data;
                    for (UINT32 i = 0; i < frames; ++i) {
                        float l = std::fabs(p[i * channels] / 32768.0f);
                        float r = (channels > 1)
                                ? std::fabs(p[i * channels + 1] / 32768.0f) : l;
                        if (l > peakL) peakL = l;
                        if (r > peakR) peakR = r;
                    }
                }
            }
            capture->ReleaseBuffer(frames);
        }
    }
};

// ---------------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------------
static AudioCapture gAudio;
static float gHoldL = 0.0f, gHoldR = 0.0f;   // níveis com decaimento
static int    gLayout  = 0;                   // 0 = horizontal, 1 = vertical
static bool   gTitleBar = true;
static COLORREF gLedOn = RGB(0, 230, 255);
static bool   gShowPeak = false;         // linha de pico visível
static int    gScheme = 0;               // 0 = simples, 1 = verde/amarelo/vermelho
static int    gBgTrans = 0;              // 0..100 = % de transparência do fundo
static float  gPeakL = 0.0f, gPeakR = 0.0f;    // pico com hold
static int    gPeakHoldL = 0, gPeakHoldR = 0;
static HBRUSH  gSwatchBrush = nullptr;
static COLORREF gCustom[16] = {};
static std::wstring gIniPath;
static bool gHasXY = false, gHasWH = false, gHasScale = false;
static int gIniX = 0, gIniY = 0, gIniW = DEFAULT_W, gIniH = DEFAULT_H;
static int gIniScale = 100;
static HWND gMain = nullptr, gOpts = nullptr;
static HINSTANCE gHInst = nullptr;
static HANDLE gOnce = nullptr;           // mutex de instância única
static bool gDragging = false;
static POINT gDragStart = {};
static RECT  gDragRect = {};
static int   gLastLitL = -1, gLastLitR = -1;  // suprime repaint quando ocioso

// Buffer 32bpp do modo camada (transparência do fundo).
static HBITMAP gDib = nullptr;
static HDC     gDibDC = nullptr;
static void*   gDibBits = nullptr;
static int     gDibW = 0, gDibH = 0;

static void SyncOptions();
static int CurrentScalePct();
static void RequestRepaint();

// ---------------------------------------------------------------------------
// Preferências (.ini ao lado do .exe)
// ---------------------------------------------------------------------------
static void MakeIniPath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L'\\');
    gIniPath = (pos == std::wstring::npos ? L"." : p.substr(0, pos)) + L"\\VULed.ini";
}

static void LoadIni() {
    if (gIniPath.empty()) return;
    wchar_t b[64] = {};
    GetPrivateProfileStringW(L"VU", L"X", L"", b, 64, gIniPath.c_str());
    if (*b) {
        gHasXY = true;
        gIniX  = (int)wcstol(b, nullptr, 10);
        GetPrivateProfileStringW(L"VU", L"Y", L"0", b, 64, gIniPath.c_str());
        gIniY  = (int)wcstol(b, nullptr, 10);
    }
    GetPrivateProfileStringW(L"VU", L"W", L"", b, 64, gIniPath.c_str());
    if (*b) {
        gHasWH = true;
        // Mínimos baixos de propósito: presets de 25% geram janela ~91px larga.
        gIniW  = std::max(64, (int)wcstol(b, nullptr, 10));
        GetPrivateProfileStringW(L"VU", L"H", L"60", b, 64, gIniPath.c_str());
        gIniH  = std::max(40, (int)wcstol(b, nullptr, 10));
    }
    gLedOn = (COLORREF)GetPrivateProfileIntW(L"VU", L"Color", (int)gLedOn,
                                             gIniPath.c_str());
    GetPrivateProfileStringW(L"VU", L"Style", L"H", b, 64, gIniPath.c_str());
    gLayout = (b[0] == L'V' || b[0] == L'v') ? 1 : 0;

    // Tudo que a janela Opções expõe + escala do menu Tamanho.
    gTitleBar = (GetPrivateProfileIntW(L"VU", L"Title", 1,
                                       gIniPath.c_str()) != 0);
    gShowPeak = (GetPrivateProfileIntW(L"VU", L"Peak", 0,
                                       gIniPath.c_str()) != 0);
    gScheme   = (GetPrivateProfileIntW(L"VU", L"Scheme", 0,
                                       gIniPath.c_str()) != 0) ? 1 : 0;
    int tr = GetPrivateProfileIntW(L"VU", L"Trans", 0, gIniPath.c_str());
    gBgTrans = std::clamp(tr, 0, 100);
    int sc = GetPrivateProfileIntW(L"VU", L"Scale", 100, gIniPath.c_str());
    if (sc >= 25 && sc <= 400) { gHasScale = true; gIniScale = sc; }
}

static void SaveIni() {
    if (gIniPath.empty()) return;
    RECT rc;
    GetWindowRect(gMain, &rc);
    wchar_t b[64];
    auto wr = [&](const wchar_t* key, int v) {
        swprintf(b, 64, L"%d", v);
        WritePrivateProfileStringW(L"VU", key, b, gIniPath.c_str());
    };
    wr(L"X", rc.left);
    wr(L"Y", rc.top);
    wr(L"W", rc.right - rc.left);
    wr(L"H", rc.bottom - rc.top);
    wr(L"Color", (int)gLedOn);
    WritePrivateProfileStringW(L"VU", L"Style", (gLayout == 1) ? L"V" : L"H",
                               gIniPath.c_str());
    // Resto da janela Opções + escala do menu Tamanho.
    wr(L"Title", gTitleBar ? 1 : 0);
    wr(L"Peak", gShowPeak ? 1 : 0);
    wr(L"Scheme", gScheme);
    wr(L"Scale", CurrentScalePct());
    wr(L"Trans", gBgTrans);
}

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

// Estado efetivo da moldura: a transparência (janela em camadas) não desenha
// a moldura do sistema, então nesse modo a janela fica sempre sem barra.
static bool FrameEffective() { return gTitleBar && gBgTrans == 0; }

// Define a transparência do fundo (0..100) e ajusta o modo camada.
static void SetBgTrans(int t) {
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
static void ToggleTitle(HWND) {
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
static void ToggleLayout() {
    gLayout ^= 1;
    RECT rc;
    GetWindowRect(gMain, &rc);
    SetWindowPos(gMain, nullptr, 0, 0, rc.bottom - rc.top, rc.right - rc.left,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SyncOptions();
    RequestRepaint();
}

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
// Desenho
// ---------------------------------------------------------------------------
static COLORREF LedOffColor(COLORREF c) {
    return RGB(GetRValue(c) * 3 / 10, GetGValue(c) * 3 / 10, GetBValue(c) * 3 / 10);
}

static int LevelToLitCount(float level) {
    if (level <= 1e-5f) return 0;
    float db = 20.0f * std::log10(level);
    float t  = std::clamp((db - DB_MIN) / (0.0f - DB_MIN), 0.0f, 1.0f);
    return (int)std::lround(t * LED_COUNT);
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

// Posição normalizada (0..1) do nível na escala — usada p/ a linha de pico.
static float LevelToPos(float level) {
    if (level <= 1e-5f) return 0.0f;
    float db = 20.0f * std::log10(level);
    return std::clamp((db - DB_MIN) / (0.0f - DB_MIN), 0.0f, 1.0f);
}

static void DrawLed(HDC hdc, int x, int y, int w, int h, COLORREF color) {
    if (w < 1 || h < 1) return;
    HBRUSH b = CreateSolidBrush(color);
    HGDIOBJ old = SelectObject(hdc, b);
    RoundRect(hdc, x, y, x + w, y + h, 3, 3);
    SelectObject(hdc, old);
    DeleteObject(b);
}

static void DrawAll(HDC hdc, int W, int H) {
    RECT rc = { 0, 0, W, H };
    HBRUSH bg = CreateSolidBrush(COL_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    COLORREF offAll = LedOffColor(gLedOn);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(170, 170, 170));

    int lit[2] = { LevelToLitCount(gHoldL), LevelToLitCount(gHoldR) };
    float ppos[2] = { LevelToPos(gPeakL), LevelToPos(gPeakR) };

    if (gLayout == 0) {
        // Horizontal: barras empilhadas (L em cima, R embaixo), crescem p/ direita.
        const int labelW = 14, mt = 6, mb = 6, rowGap = 8;
        int rowH = std::max(4, (H - mt - mb - rowGap) / 2);
        int areaX = labelW;
        int areaW = std::max(LED_COUNT, W - labelW - 6);
        int segW  = (areaW - (LED_COUNT - 1) * LED_GAP) / LED_COUNT;
        for (int ch = 0; ch < 2; ++ch) {
            int y = mt + ch * (rowH + rowGap);
            for (int i = 0; i < LED_COUNT; ++i) {
                int x = areaX + i * (segW + LED_GAP);
                COLORREF on = LedColor(i);
                DrawLed(hdc, x, y, segW, rowH, (i < lit[ch]) ? on
                        : (gScheme == 0 ? offAll : LedOffColor(on)));
            }
            TextOut(hdc, 3, y + rowH / 2 - 6, (ch == 0) ? L"L" : L"R", 1);
            // Linha de pico: marca vertical branca na posição do pico.
            if (gShowPeak && ppos[ch] > 0.0f) {
                int x = areaX + (int)std::lround(ppos[ch] * areaW) - 1;
                x = std::clamp(x, areaX, areaX + areaW - 2);
                RECT pk = { x, y - 1, x + 2, y + rowH + 1 };
                HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(hdc, &pk, b);
                DeleteObject(b);
            }
        }
    } else {
        // Vertical: colunas lado a lado (L esq., R dir.), crescem de baixo p/ cima.
        const int labelH = 14, mt = 6, mx = 6, colGap = 10;
        int areaH   = std::max(LED_COUNT, H - mt - labelH);
        int segH    = (areaH - (LED_COUNT - 1) * LED_GAP) / LED_COUNT;
        int colW    = std::max(4, (W - 2 * mx - colGap) / 2);
        int yBottom = H - labelH;
        for (int ch = 0; ch < 2; ++ch) {
            int x = mx + ch * (colW + colGap);
            for (int i = 0; i < LED_COUNT; ++i) {
                int y = yBottom - (i + 1) * segH - i * LED_GAP;
                COLORREF on = LedColor(i);
                DrawLed(hdc, x, y, colW, segH, (i < lit[ch]) ? on
                        : (gScheme == 0 ? offAll : LedOffColor(on)));
            }
            TextOut(hdc, x + colW / 2 - 4, H - labelH + 2,
                    (ch == 0) ? L"L" : L"R", 1);
            // Linha de pico: marca horizontal branca na posição do pico.
            if (gShowPeak && ppos[ch] > 0.0f) {
                int y = yBottom - (int)std::lround(ppos[ch] * areaH) - 1;
                y = std::clamp(y, mt, yBottom - 2);
                RECT pk = { x - 1, y, x + colW + 1, y + 2 };
                HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(hdc, &pk, b);
                DeleteObject(b);
            }
        }
    }
    SelectObject(hdc, oldPen);
}

// ---------------------------------------------------------------------------
// Modo camada: transparência só do fundo (LEDs/texto continuam opacos)
// ---------------------------------------------------------------------------
static void FreeDib() {
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
static void RenderLayered() {
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
static void RequestRepaint() {
    if (!gMain) return;
    if (gBgTrans > 0) RenderLayered();
    else              InvalidateRect(gMain, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Janela "Opções..."
// ---------------------------------------------------------------------------
static void SyncOptions() {
    if (!gOpts) return;
    CheckDlgButton(gOpts, IDC_CHK_TITLE,
                   FrameEffective() ? BST_CHECKED : BST_UNCHECKED);
    // Com transparência ativa a janela é em camadas (sem moldura do sistema).
    EnableWindow(GetDlgItem(gOpts, IDC_CHK_TITLE), gBgTrans == 0);
    CheckDlgButton(gOpts, IDC_CHK_VERT,  gLayout == 1 ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(gOpts, IDC_CHK_PEAK,  gShowPeak ? BST_CHECKED : BST_UNCHECKED);
    CheckRadioButton(gOpts, IDC_RAD_SIMPLE, IDC_RAD_SCHEME,
                     (gScheme == 0) ? IDC_RAD_SIMPLE : IDC_RAD_SCHEME);
    SetWindowTextW(GetDlgItem(gOpts, IDC_CHK_VERT),
                   (gLayout == 1) ? L"Alternar para Horizontal"
                                  : L"Alternar para Vertical");
    EnableWindow(GetDlgItem(gOpts, IDC_BTN_COLOR), gScheme == 0);
    SendMessageW(GetDlgItem(gOpts, IDC_SLIDER_TRANS), TBM_SETPOS, TRUE, gBgTrans);
    wchar_t b[16];
    swprintf(b, 16, L"%d%%", gBgTrans);
    SetWindowTextW(GetDlgItem(gOpts, IDC_LBL_TRANS), b);
}

static void CreateOptionsControls(HWND h) {
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style,
                  int x, int y, int w, int hh, UINT id) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                                 x, y, w, hh, h, (HMENU)(INT_PTR)id,
                                 gHInst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };
    mk(L"BUTTON", L"Exibir barra de título", BS_AUTOCHECKBOX,
       12, 14, 320, 18, IDC_CHK_TITLE);
    mk(L"BUTTON", L"Alternar para Vertical", BS_AUTOCHECKBOX,
       12, 40, 320, 18, IDC_CHK_VERT);
    mk(L"BUTTON", L"Exibir linha de pico", BS_AUTOCHECKBOX,
       12, 66, 320, 18, IDC_CHK_PEAK);
    mk(L"STATIC", L"Esquema de cores:", SS_LEFT,
       12, 94, 180, 16, 0);
    mk(L"BUTTON", L"Simples (atual)", BS_AUTORADIOBUTTON | WS_GROUP,
       24, 112, 300, 18, IDC_RAD_SIMPLE);
    mk(L"BUTTON", L"Verde (0) | Amarelo (70%) | Vermelho (85%)",
       BS_AUTORADIOBUTTON,
       24, 132, 310, 18, IDC_RAD_SCHEME);
    mk(L"BUTTON", L"Escolher cor do LED...", BS_PUSHBUTTON,
       12, 162, 170, 26, IDC_BTN_COLOR);
    mk(L"STATIC", L"", WS_BORDER | SS_NOTIFY,
       192, 162, 56, 26, IDC_SWATCH);
    mk(L"BUTTON", L"Salvar", BS_PUSHBUTTON,
       12, 200, 90, 26, IDC_BTN_SAVE);
    mk(L"BUTTON", L"Fechar", BS_PUSHBUTTON,
       112, 200, 90, 26, IDC_BTN_CLOSE);

    // Transparência do fundo: slider vertical 0..100 (incremento 1).
    mk(L"STATIC", L"Transparência\ndo fundo (%)", SS_LEFT,
       344, 12, 118, 34, 0);
    HWND sl = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                              WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_AUTOTICKS,
                              352, 50, 40, 140, h,
                              (HMENU)(INT_PTR)IDC_SLIDER_TRANS, gHInst, nullptr);
    SendMessageW(sl, WM_SETFONT, (WPARAM)f, TRUE);
    SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(sl, TBM_SETTICFREQ, 10, 0);
    SendMessageW(sl, TBM_SETPOS, TRUE, gBgTrans);
    mk(L"STATIC", L"0%", SS_LEFT, 400, 54, 62, 18, IDC_LBL_TRANS);
    SyncOptions();
}

static void ShowOptions() {
    if (!gOpts) {
        RECT rc = { 0, 0, OPT_CW, OPT_CH };
        AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        gOpts = CreateWindowExW(0, OPT_CLASS, L"Opções...",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, gHInst, nullptr);
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

    // Trackbar sempre manda WM_HSCROLL, mesmo vertical.
    case WM_HSCROLL: {
        HWND sl = GetDlgItem(h, IDC_SLIDER_TRANS);
        if (sl && (HWND)lParam == sl) {
            int v = (int)SendMessageW(sl, TBM_GETPOS, 0, 0);
            SetBgTrans(v);
            wchar_t b[16];
            swprintf(b, 16, L"%d%%", gBgTrans);
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

// ---------------------------------------------------------------------------
// Janela principal
// ---------------------------------------------------------------------------
// % atual = largura do cliente ÷ largura base (300 em H, 75 em V) — vale para
// qualquer layout, pois a base acompanha a orientação.
static int CurrentScalePct() {
    if (!gMain) return 100;
    RECT rc;
    GetClientRect(gMain, &rc);
    int baseW = (gLayout == 0) ? DEFAULT_W : DEFAULT_H;
    if (rc.right <= 0) return 100;
    return (int)std::lround(rc.right * 100.0 / baseW);
}

// Redimensiona a janela mantendo o canto superior esquerdo.
static void ApplyScale(int pct) {
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

static void ShowContextMenu(HWND owner) {
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
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(owner);            // necessário p/ o menu fechar sozinho
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                   pt.x, pt.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
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

            // Linha de pico: segue o pico, segura 600 ms, depois cai.
            auto updPeak = [](float lvl, float& peak, int& hold) {
                if (lvl >= peak) { peak = lvl; hold = PEAK_HOLD_TICKS; }
                else if (hold > 0) { --hold; }
                else { peak *= PEAK_FALL; if (peak < 1e-5f) peak = 0.0f; }
            };
            updPeak(pl, gPeakL, gPeakHoldL);
            updPeak(pr, gPeakR, gPeakHoldR);

            int litL = LevelToLitCount(gHoldL);
            int litR = LevelToLitCount(gHoldR);
            // Sem som (holds zerados) e LEDs já apagados → não repinta.
            // Evita repaint contínuo a 100 Hz quando ocioso (flicker).
            bool peakBusy = gShowPeak && (gPeakL > 0.0f || gPeakR > 0.0f);
            if (litL != gLastLitL || litR != gLastLitR ||
                gHoldL > 0.0f || gHoldR > 0.0f || peakBusy) {
                gLastLitL = litL;
                gLastLitR = litR;
                RequestRepaint();
            }
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
        case IDM_ABOUT:
            MessageBoxW(hwnd,
                        L"(2026) Copyleft by adrian.barbosa@gmail.com",
                        L"Sobre...", MB_OK | MB_ICONINFORMATION);
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
        FreeDib();
        KillTimer(hwnd, IDT_AUDIO);
        gAudio.Shutdown();
        if (gOpts) DestroyWindow(gOpts);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    // Instância única: se já houver uma rodando, traz a janela dela à frente e sai.
    gOnce = CreateMutexW(nullptr, FALSE, L"Local\\VULedMeter_SingleInstance");
    if (gOnce && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(MAIN_CLASS, nullptr);
        if (prev) {
            if (IsIconic(prev)) ShowWindow(prev, SW_RESTORE);
            SetForegroundWindow(prev);
        }
        CloseHandle(gOnce);
        return 0;
    }

    gHInst = hInst;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);        // classe do trackbar (slider)

    MakeIniPath();
    LoadIni();
    gSwatchBrush = CreateSolidBrush(gLedOn);

    WNDCLASSEX wc = { sizeof(wc) };
    wc.style         = CS_DBLCLKS;           // habilita WM_LBUTTONDBLCLK
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassEx(&wc);

    WNDCLASSEX oc = { sizeof(oc) };
    oc.lpfnWndProc   = OptionsProc;
    oc.hInstance     = hInst;
    oc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    oc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    oc.lpszClassName = OPT_CLASS;
    RegisterClassEx(&oc);

    // Transparência > 0 ⇒ janela em camadas (sem moldura do sistema).
    bool layered = (gBgTrans > 0);
    DWORD style   = (layered || !gTitleBar) ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    DWORD exStyle = layered ? WS_EX_LAYERED : 0;
    RECT rc = { 0, 0, DEFAULT_W, DEFAULT_H };
    AdjustWindowRect(&rc, style, FALSE);
    int w = gHasWH ? gIniW : rc.right - rc.left;
    int h = gHasWH ? gIniH : rc.bottom - rc.top;
    int x = gHasXY ? gIniX : CW_USEDEFAULT;
    int y = gHasXY ? gIniY : CW_USEDEFAULT;

    gMain = CreateWindowExW(exStyle, MAIN_CLASS, L"VU — Saída Padrão", style,
                            x, y, w, h, nullptr, nullptr, hInst, nullptr);
    if (!gMain) { CoUninitialize(); return 1; }
    ShowWindow(gMain, nCmdShow);
    if (layered) RenderLayered();
    UpdateWindow(gMain);

    // W/H no ini já reflete a escala escolhida (ApplyScale redimensiona e o
    // Save grava o resultado). A escala sozinha só é aplicada se W/H faltar.
    if (gHasScale && !gHasWH) ApplyScale(gIniScale);

    // Tenta iniciar a captura; se falhar (ex.: sem dispositivo), o timer
    // re-tenta sozinho a cada ~1 s.
    gAudio.Init();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (gSwatchBrush) DeleteObject(gSwatchBrush);
    CoUninitialize();
    return (int)msg.wParam;
}

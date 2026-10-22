// Constantes, IDs e nomes de classe compartilhados pelos módulos.
#pragma once
#include <windows.h>

// ---------------------------------------------------------------------------
// Layout / escalas / áudio
// ---------------------------------------------------------------------------
inline constexpr int DEFAULT_W = 300;
inline constexpr int DEFAULT_H = 75;
inline constexpr int LED_COUNT = 26;
inline constexpr int LED_GAP   = 2;
inline constexpr float DB_MIN  = -60.0f;
inline constexpr float RELEASE = 0.97f;  // queda das barras por tick (~-17 dB/s, VU clássico)

inline const COLORREF COL_BG = RGB(0, 0, 0);

inline constexpr int PEAK_HOLD_TICKS = 45;   // hold do pico: ~0,7 s
inline constexpr float PEAK_FALL = 0.95f;    // queda do pico após o hold (~-28 dB/s)

inline constexpr int SCALES[] = { 25, 50, 75, 100, 125, 150, 200, 300, 400 };
inline constexpr int SCALE_COUNT = (int)(sizeof(SCALES) / sizeof(SCALES[0]));

// ---------------------------------------------------------------------------
// IDs de comando/controles
// ---------------------------------------------------------------------------
enum {
    IDM_OPTIONS = 100,
    IDM_ABOUT,
    IDM_EXIT,
    IDM_SIZE = 110,            // ..IDM_SIZE+8 (9 escalas)
    IDC_CHK_TITLE = 200,
    IDC_CHK_VERT,
    IDC_CHK_PEAK,
    IDC_CHK_TOPMOST,
    IDC_CHK_STARTUP,
    IDC_RAD_SIMPLE,
    IDC_RAD_SCHEME,
    IDC_BTN_COLOR,
    IDC_BTN_SAVE,
    IDC_BTN_CLOSE,
    IDC_SWATCH,
    IDC_SLIDER_TRANS,
    IDC_LBL_TRANS,
    IDC_SAVE_FEEDBACK = 300,
    IDT_AUDIO = 1,             // timer de captura (10 ms)
};

// ---------------------------------------------------------------------------
// Janelas
// ---------------------------------------------------------------------------
inline const wchar_t* const MAIN_CLASS = L"VULedMeter";
inline const wchar_t* const OPT_CLASS  = L"VUOptions";
inline constexpr int OPT_CW = 470, OPT_CH = 292;

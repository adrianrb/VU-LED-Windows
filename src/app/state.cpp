#include "app/state.h"

float gHoldL = 0.0f, gHoldR = 0.0f;          // níveis com decaimento
float gPeakL = 0.0f, gPeakR = 0.0f;          // pico com hold
int   gPeakHoldL = 0, gPeakHoldR = 0;

int      gLayout   = 0;                       // 0 = horizontal, 1 = vertical
bool     gTitleBar = true;
COLORREF gLedOn    = RGB(0, 230, 255);
bool     gShowPeak = false;
int      gScheme   = 0;
int      gBgTrans  = 0;
bool     gTopmost  = true;
bool     gStartup  = false;

HWND gMain = nullptr, gOpts = nullptr;

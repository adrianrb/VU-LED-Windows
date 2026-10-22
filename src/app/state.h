// Estado compartilhado: preferências em memória e handles das janelas.
// (Quem define é state.cpp — os demais módulos só leem/escrevem via extern.)
#pragma once
#include <windows.h>

// Níveis exibidos (com decaimento) e pico (hold).
extern float gHoldL, gHoldR;
extern float gPeakL, gPeakR;
extern int   gPeakHoldL, gPeakHoldR;

// Preferências (ver AGENTS.md: chaves do VULed.ini).
extern int      gLayout;          // 0 = horizontal, 1 = vertical
extern bool     gTitleBar;
extern COLORREF gLedOn;
extern bool     gShowPeak;        // LED do pico (hold) visível
extern int      gScheme;          // 0 = simples, 1 = verde/amarelo/vermelho
extern int      gBgTrans;         // 0..100 = % de transparência do fundo
extern bool     gTopmost;         // janela sempre visível
extern bool     gStartup;         // iniciar com o Windows

// Janelas.
extern HWND gMain, gOpts;

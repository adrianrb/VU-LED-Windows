// Janela principal do VU: registro da classe, ações de janela e WndProc.
#pragma once
#include <windows.h>

bool RegisterMainWindowClass(HINSTANCE hInst, HICON big, HICON small);

// Estado efetivo da moldura: a transparência (janela em camadas) não desenha
// a moldura do sistema, então nesse modo a janela fica sempre sem barra.
bool FrameEffective();

void ApplyTopmost();             // "Sempre visível"
void SetBgTrans(int t);          // 0..100 (ajusta o modo camada)
void ToggleTitle(HWND h);        // mostra/oculta a barra de título
void ToggleLayout();             // horizontal <-> vertical
int  CurrentScalePct();          // % derivada da largura do cliente
void ApplyScale(int pct);        // redimensiona mantendo o canto superior esq.

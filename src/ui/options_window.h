// Janela "Opções...": título, layout, pico, cor/esquema, transparência,
// "Sempre visível", "Iniciar com o Windows" e Salvar.
#pragma once
#include <windows.h>

bool RegisterOptionsClass(HINSTANCE hInst, HICON big, HICON small);
void OptionsInit();      // cria o brush da amostra de cor (depois do LoadIni)
void OptionsCleanup();   // libera o brush
void ShowOptions();      // cria (uma vez) e traz a janela para frente
void SyncOptions();      // reflete o estado atual nos controles

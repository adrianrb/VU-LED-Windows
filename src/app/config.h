// Preferências persistidas em VULed.ini (ao lado do .exe) e
// "Iniciar com o Windows" (HKCU\Software\Microsoft\Windows\CurrentVersion\Run).
#pragma once
#include "app/ids.h"

// Janela/posição/tamanho lidos do .ini — usados na criação da janela.
struct IniWindow {
    bool hasXY    = false;
    bool hasWH    = false;
    bool hasScale = false;
    int  x = 0, y = 0;
    int  w = DEFAULT_W, h = DEFAULT_H;
    int  scale = 100;
};
extern IniWindow gIniWindow;

void ConfigInit();          // descobre o caminho do VULed.ini
void LoadIni();             // aplica o .ini no estado global
void SaveIni();             // grava o estado global no .ini
void SetStartup(bool on);   // cria/remove a chave do autostart (efeito externo)

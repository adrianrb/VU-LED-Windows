// Menu de contexto (clique direito na janela ou clique no ícone da bandeja):
// "Opções...", "Tamanho:" (radio 25%..400%), "Sobre..." e "Sair\tAlt+F4".
#pragma once
#include <windows.h>

void ShowContextMenu(HWND owner, bool fromTray = false);

// Desenho dos LEDs (GDI) e modo camada (transparência do fundo).
#pragma once
#include <windows.h>

int  LevelToLitCount(float level);   // nível 0..1 -> LEDs acesos
void DrawAll(HDC hdc, int W, int H); // fundo + barras + rótulos + LED do pico
void RequestRepaint();               // camada: redesenha já; normal: invalida
void RenderLayered();                // desenha no DIB e entrega via UpdateLayeredWindow
void FreeDib();                      // libera o DIB do modo camada

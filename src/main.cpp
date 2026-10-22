// VU de LEDs mono (L+R) — lê a saída padrão do Windows via WASAPI loopback.
// Depende apenas de Win32/GDI/WASAPI/ComDlg. Compilável com MSVC ou MinGW-w64.
//
// Interação:
//  - Clique direito → menu: "Opções...", "Tamanho:" (escala 25%..400%),
//    "Sobre..." e "Sair\tAlt+F4" (o mesmo menu abre pelo ícone da bandeja).
//  - Duplo clique em qualquer lugar da janela → mostra/oculta barra de título.
//  - Janela redimensionável; sem barra de título, arrastar pelo cliente move.
//  - Opções: título on/off, Horizontal/Vertical (troca W↔H), linha de pico,
//    "Sempre visível" (topmost), "Iniciar com o Windows" (HKCU\...\Run),
//    esquema de cores (simples ou verde/amarelo/vermelho), cor do LED
//    (ChooseColor), transparência do fundo e "Salvar" → grava tudo em
//    VULed.ini (ao lado do .exe).
//
// Áudio: a cada 10 ms drena o buffer de loopback do dispositivo de render
// padrão e calcula o pico por canal. Loopback só entrega dados quando há
// playback ativo; sem som, as barras ficam zeradas (comportamento esperado).
//
// Módulos (src/):
//   main.cpp              entrada, instância única, classes e loop de mensagens
//   app/ids.h             constantes, IDs de comando/controle, nomes de classe
//   app/state.cpp         estado compartilhado (preferências em memória)
//   app/config.cpp        VULed.ini (load/save) e "Iniciar com o Windows"
//   audio/capture.cpp     captura WASAPI loopback do dispositivo padrão
//   gfx/render.cpp        desenho dos LEDs e modo camada (transparência)
//   ui/main_window.cpp    janela principal (timer, pintura, ações, WndProc)
//   ui/options_window.cpp janela "Opções..."
//   ui/menu.cpp           menu de contexto (clique direito / bandeja)
//   ui/tray.cpp           ícone da bandeja
//   res/                  app.rc, app.ico, resource.h (ícone embutido)

#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>

#include "app/config.h"
#include "app/ids.h"
#include "app/state.h"
#include "audio/capture.h"
#include "gfx/render.h"
#include "res/resource.h"   // IDI_APP (ícone embutido via res/app.rc)
#include "ui/main_window.h"
#include "ui/options_window.h"
#include "ui/tray.h"

// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    // Instância única: se já houver uma rodando, traz a janela dela à frente e sai.
    HANDLE once = CreateMutexW(nullptr, FALSE, L"Local\\VULedMeter_SingleInstance");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(MAIN_CLASS, nullptr);
        if (prev) {
            if (IsIconic(prev)) ShowWindow(prev, SW_RESTORE);
            SetForegroundWindow(prev);
        }
        CloseHandle(once);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);        // classe do trackbar (slider)

    ConfigInit();
    LoadIni();
    // Se o .exe foi movido/renomeado, reescreve o caminho do autostart.
    if (gStartup) SetStartup(true);
    OptionsInit();

    // Ícone embutido (res/app.rc): usado na janela, na barra de tarefas e no Alt+Tab.
    HICON hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXICON),
                                       GetSystemMetrics(SM_CYICON), 0);
    HICON hIconSm  = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON), 0);
    TrayInit(hIconSm);                 // mesmo ícone na bandeja

    RegisterMainWindowClass(hInst, hIconBig, hIconSm);
    RegisterOptionsClass(hInst, hIconBig, hIconSm);

    // Transparência > 0 ⇒ janela em camadas (sem moldura do sistema).
    bool layered = (gBgTrans > 0);
    DWORD style   = (layered || !gTitleBar) ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    DWORD exStyle = layered ? WS_EX_LAYERED : 0;
    RECT rc = { 0, 0, DEFAULT_W, DEFAULT_H };
    AdjustWindowRect(&rc, style, FALSE);
    int w = gIniWindow.hasWH ? gIniWindow.w : rc.right - rc.left;
    int h = gIniWindow.hasWH ? gIniWindow.h : rc.bottom - rc.top;
    int x = gIniWindow.hasXY ? gIniWindow.x : CW_USEDEFAULT;
    int y = gIniWindow.hasXY ? gIniWindow.y : CW_USEDEFAULT;

    gMain = CreateWindowExW(exStyle, MAIN_CLASS, L"VU — Saída Padrão", style,
                            x, y, w, h, nullptr, nullptr, hInst, nullptr);
    if (!gMain) { CoUninitialize(); return 1; }
    ApplyTopmost();                      // "Sempre visível" (padrão: ligado)
    ShowWindow(gMain, nCmdShow);
    if (layered) RenderLayered();
    UpdateWindow(gMain);

    // W/H no ini já reflete a escala escolhida (ApplyScale redimensiona e o
    // Save grava o resultado). A escala sozinha só é aplicada se W/H faltar.
    if (gIniWindow.hasScale && !gIniWindow.hasWH) ApplyScale(gIniWindow.scale);

    // Tenta iniciar a captura; se falhar (ex.: sem dispositivo), o timer
    // re-tenta sozinho a cada ~1 s.
    gAudio.Init();

    // Atalho do item "Sair": Alt+F4 precisa de tabela de aceleradores porque a
    // janela sem moldura é WS_POPUP puro (sem sysmenu) e não fecha sozinha.
    ACCEL acc[1] = { { (BYTE)(FVIRTKEY | FALT), VK_F4, IDM_EXIT } };
    HACCEL hAccel = CreateAcceleratorTableW(acc, 1);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (hAccel && TranslateAcceleratorW(gMain, hAccel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (hAccel) DestroyAcceleratorTable(hAccel);
    OptionsCleanup();
    if (hIconSm)  DestroyIcon(hIconSm);
    if (hIconBig) DestroyIcon(hIconBig);
    CoUninitialize();
    return (int)msg.wParam;
}

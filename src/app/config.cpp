// Leitura/gravação das preferências (VULed.ini) e do autostart no registro.
#include "app/config.h"

#include <algorithm>
#include <cwchar>
#include <string>

#include "app/state.h"
#include "ui/main_window.h"

IniWindow gIniWindow;

static std::wstring gIniPath;

// ---------------------------------------------------------------------------
// .ini ao lado do .exe
// ---------------------------------------------------------------------------
static void MakeIniPath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L'\\');
    gIniPath = (pos == std::wstring::npos ? L"." : p.substr(0, pos)) + L"\\VULed.ini";
}

void ConfigInit() { MakeIniPath(); }

void LoadIni() {
    if (gIniPath.empty()) return;
    wchar_t b[64] = {};
    GetPrivateProfileStringW(L"VU", L"X", L"", b, 64, gIniPath.c_str());
    if (*b) {
        gIniWindow.hasXY = true;
        gIniWindow.x     = (int)wcstol(b, nullptr, 10);
        GetPrivateProfileStringW(L"VU", L"Y", L"0", b, 64, gIniPath.c_str());
        gIniWindow.y     = (int)wcstol(b, nullptr, 10);
    }
    GetPrivateProfileStringW(L"VU", L"W", L"", b, 64, gIniPath.c_str());
    if (*b) {
        gIniWindow.hasWH = true;
        // Mínimos baixos de propósito: presets de 25% geram janela ~91px larga.
        gIniWindow.w = std::max(64, (int)wcstol(b, nullptr, 10));
        GetPrivateProfileStringW(L"VU", L"H", L"60", b, 64, gIniPath.c_str());
        gIniWindow.h = std::max(40, (int)wcstol(b, nullptr, 10));
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
    // "Sempre visível" ligado por padrão; "Iniciar com o Windows" desligado.
    gTopmost = (GetPrivateProfileIntW(L"VU", L"Topmost", 1,
                                      gIniPath.c_str()) != 0);
    gStartup = (GetPrivateProfileIntW(L"VU", L"Startup", 0,
                                      gIniPath.c_str()) != 0);
    int sc = GetPrivateProfileIntW(L"VU", L"Scale", 100, gIniPath.c_str());
    if (sc >= 25 && sc <= 400) { gIniWindow.hasScale = true; gIniWindow.scale = sc; }
}

void SaveIni() {
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
    wr(L"Topmost", gTopmost ? 1 : 0);
    wr(L"Startup", gStartup ? 1 : 0);
}

// ---------------------------------------------------------------------------
// "Iniciar com o Windows": valor em HKCU\...\Run apontando para este .exe.
// O ini é a fonte da verdade do checkbox; a chave é só o efeito colateral.
// ---------------------------------------------------------------------------
static const wchar_t* RUN_KEY   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* RUN_VALUE = L"VULedMeter";

void SetStartup(bool on) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    if (on) {
        // Aspas: o caminho pode ter espaços (o Run executa a linha de comando).
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, RUN_VALUE, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                       (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, RUN_VALUE);
    }
    RegCloseKey(k);
}

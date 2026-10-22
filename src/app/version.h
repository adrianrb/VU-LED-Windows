// Versão exibida em "Sobre..." (e no título do diálogo).
// Manter igual a version.txt — o Makefile aborta o build se divergirem.
#pragma once
#include <windows.h>

inline constexpr const wchar_t* APP_VERSION = L"1.6.2";

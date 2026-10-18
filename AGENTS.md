# AGENTS.md

Projeto Windows: VU de LEDs mono (L+R) que lê a **saída padrão** via WASAPI
loopback. C++ puro (Win32 + GDI), zero dependências externas.

## Código

- `main.cpp` — app inteiro (janela + captura WASAPI + desenho dos LEDs)
- `VULed.exe` — binário compilado (cross-compile a partir do WSL)

## Build (MinGW-w64 a partir do WSL)

```sh
x86_64-w64-mingw32-g++ -O2 -std=c++17 -municode -mwindows -static \
  -o VULed.exe main.cpp -lole32 -luuid -lgdi32 -luser32 -lcomdlg32
```

`-static` é **obrigatório**: sem ele o `.exe` depende de `libstdc++-6.dll` /
`libgcc_s_seh-1.dll` / `libwinpthread-1.dll`, que não existem no Windows e
derrubam a execução com "libstdc++-6.dll não foi encontrado". Validar com:

```sh
x86_64-w64-mingw32-objdump -p VULed.exe | grep "DLL Name"
# deve listar só DLLs de sistema (GDI32, USER32, ole32, comdlg32, KERNEL32, msvcrt)
```

Requisitos: `sudo apt install g++-mingw-w64-x86-64`. Não há testes/CI — a
verificação é compilar e rodar o `.exe` no Windows.

## Versão

- `version.txt` contém a versão em `X.Y.Z` (uma linha, sem `v` prefixo).
- Padrão semântico simples: **incrementar Z** (patch) em cada alteração que
  mudar o binário/código; reservar Y para mudanças de comportamento novo
  (ex.: novo item de menu) e X para quebras grandes. Primeira release: 1.0.0.
- Após mudar a versão, commitar junto (`version.txt` foi criado no commit
  raiz junto com o código).

## Armadilhas (não "conserte" sem verificar)

- **GUID `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`**: o `ksmedia.h` do MinGW-w64 não
  fornece a definição (dá `undefined reference`). O código define o GUID
  manualmente no topo de `main.cpp`. Não inclua `ksmedia.h` nem `initguid.h`
  para "resolver" isso.
- **WASAPI loopback só entrega dados quando há playback ativo.** Sem som
  tocando, as barras ficam zeradas — é comportamento esperado, não bug.
- **`IAudioCaptureClient::GetBuffer` retorna `E_POINTER`** com ponteiro nulo;
  não há como só consultar o tamanho do pacote. Sempre drenar com buffers reais.
- Se o dispositivo padrão for removido/trocado, `GetBuffer` retorna
  `AUDCLNT_E_DEVICE_INVALIDATED`: o código faz `Shutdown()` e re-tenta `Init()`
  a cada ~1 s (via acumulador no `Poll`). Preservar esse caminho de recuperação.
- O tick é um `SetTimer` de 10 ms (limitação do scheduler do Windows: real
  ≈15.6 ms com granularidade padrão). Se precisar de timing mais fino, seria
  necessário `timeBeginPeriod` ou thread dedicada — decisão consciente atual é
  não usar.
- **Flicker**: `WM_PAINT` usa double buffering (DC memória + um único `BitBlt`)
  e o timer só chama `InvalidateRect` se houve mudança de LEDs ou o hold está
  acima de zero (`gLastLitL/R`). Repintar a tela direto a 100 Hz, ou continuar
  repaintando com tudo em zero (caso "sem som"), causa flicker visível —
  preservar essas duas proteções ao mexer no desenho.

## Interação / preferências

- **Menu**: clique direito na janela → `Opções...` (janela de configurações) e
  `Sobre...` (mostra "(2026) Copyleft by adrian.barbosa@gmail.com").
- **Barra de título**: duplo clique em qualquer lugar alterna; a janela perde
  também thickframe/sysmenu (vira `WS_POPUP` puro). Sem título, arrastar pela
  área cliente move a janela (drag manual com `SetCapture`, não usar
  `WM_NCLBUTTONDOWN/HTCAPTION` — conflitaria com o duplo clique).
- **Layout H/V**: `ToggleLayout()` inverte também altura↔largura da janela
  (300x75 ↔ 75x300) para preservar a área do medidor.
- **Persistência**: `Salvar` grava `VULed.ini` ao lado do `.exe` (chaves em
  `[VU]`: `X,Y,W,H,Color,Style`). `Color` é `COLORREF` decimal; `Style` é
  `H`/`V`. **Estado do título e cores customizadas do picker não são salvos** —
  por decisão de escopo, só o que a janela Opções lista.
- Preferências são lidas no `wWinMain` *antes* do `CreateWindow` (posição/tam.
  entram direto na criação); não mover essa leitura para depois.

## Convenções

- Unicode obrigatório (`wWinMain`, `UNICODE` definido no código; flag
  `-municode` no build). Janela padrão 300x75 (redimensionável, mín. 160x80),
  fundo preto, LEDs ciano padrão `RGB(0,230,255)` (configurável; apagado é a
  cor acesa × 30%), 26 LEDs por canal, escala −60..0 dB, ataque instantâneo +
  decaimento por tick (`RELEASE`).

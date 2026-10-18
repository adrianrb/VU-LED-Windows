# AGENTS.md

Projeto Windows: VU de LEDs mono (L+R) que lê a **saída padrão** via WASAPI
loopback. C++ puro (Win32 + GDI), zero dependências externas.

## Código

- `src/main.cpp` — app inteiro (janela + captura WASAPI + desenho dos LEDs)
- `output/VULed.exe` — binário compilado (cross-compile a partir do WSL)
- `output/VULed.ini` — preferências persistidas; fica ao lado do `.exe` porque
  é ali que o app lê/grava (`MakeIniPath` usa `GetModuleFileNameW`)
- `build/` — temporários de build (objetos); **não** versionado (`.gitignore`)

## Build (MinGW-w64 a partir do WSL)

```sh
make          # compila src/main.cpp -> output/VULed.exe (objeto em build/)
make clean    # apaga só os temporários (build/), preserva output/
```

Sem `make` (duas etapas, para os temporários irem para `build/`):

```sh
x86_64-w64-mingw32-g++ -O2 -std=c++17 -Wall -municode -mwindows \
  -c src/main.cpp -o build/main.o
x86_64-w64-mingw32-g++ -static -municode -mwindows -o output/VULed.exe \
  build/main.o -lole32 -luuid -lgdi32 -luser32 -lcomdlg32 -lcomctl32
```

`-static` é **obrigatório**: sem ele o `.exe` depende de `libstdc++-6.dll` /
`libgcc_s_seh-1.dll` / `libwinpthread-1.dll`, que não existem no Windows e
derrubam a execução com "libstdc++-6.dll não foi encontrado". Validar com:

```sh
x86_64-w64-mingw32-objdump -p output/VULed.exe | grep "DLL Name"
# deve listar só DLLs de sistema (GDI32, USER32, ole32, comdlg32, comctl32,
# KERNEL32, msvcrt)
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
  manualmente no topo de `src/main.cpp`. Não inclua `ksmedia.h` nem `initguid.h`
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
  e o timer só chama `InvalidateRect` se houve mudança de LEDs, o hold está
  acima de zero, ou (`gShowPeak` e pico ativo — `peakBusy`). Repintar a tela
  direto a 100 Hz, ou continuar repaintando com tudo em zero (caso "sem som"),
  causa flicker visível — preservar essas proteções ao mexer no desenho. No
  modo camada o repaint sai de `RequestRepaint()`, que chama `RenderLayered()`
  na hora (janela em camadas não repinta por `InvalidateRect`).
- **Transparência ⇒ janela em camadas.** Com `Trans > 0` o app usa
  `WS_EX_LAYERED` + `UpdateLayeredWindow` (alpha por pixel), e isso
  **substitui a janela inteira**: a moldura/barra de título do sistema deixa de
  ser desenhada. Por isso, com transparência ativa a janela é sempre `WS_POPUP`
  e o checkbox "Exibir barra de título" fica desabilitado (a preferência
  `Title` é preservada e volta a valer quando `Trans` volta a 0, e
  `ToggleTitle` ignora o duplo clique nesse estado). Não tentar "consertar"
  mantendo `WS_OVERLAPPEDWINDOW`: o `UpdateLayeredWindow` cobre a moldura e
  sobra lixo visual.
- **`GdiFlush()` antes de ler/escrever os pixels do DIB** em `RenderLayered`:
  o GDI mantém as operações em buffer; sem o flush o loop de alpha lê lixo e o
  flush posterior sobrescreve os alphas — a janela fica 100% invisível (sem
  erro nenhum: `UpdateLayeredWindow` retorna sucesso).
- O fundo usa **alpha mínimo 1** (`clamp(...,1,255)`): pixel com alpha 0 vira
  "click-through" e a janela não poderia mais ser arrastada pelo fundo (em
  100% o fundo fica invisível na prática, mas continua clicável).
- **Trackbar vertical manda `WM_VSCROLL`, não `WM_HSCROLL`** (`TBS_VERT`
  inverte o eixo da notificação). O handler trata os dois
  (`case WM_HSCROLL: case WM_VSCROLL:`); escrever só `WM_HSCROLL` compila e
  passa em teste sintético (`SendMessage` do `WM_HSCROLL`), mas com o mouse
  real o polegar mexe e o VU não acompanha — nenhuma mensagem chega ao parent.

## Interação / preferências

- **Menu**: clique direito na janela → `Opções...` (janela de configurações),
  `Tamanho:` (submenu de radio: 25%..400%, padrão 100%), `Sobre...` (mostra
  "(2026) Copyleft by adrian.barbosa@gmail.com"), separador e `Sair\tAlt+F4`.
  `Sair` é `IDM_EXIT` → `DestroyWindow`; o Alt+F4 funciona também sem moldura
  (`WS_POPUP` sem sysmenu) por uma tabela de aceleradores (`gAccel` +
  `TranslateAcceleratorW` no loop de mensagens) — sem ela o `DefWindowProc`
  não fecharia a janela sem sysmenu.
- **Escala (`ApplyScale`)**: redimensiona mantendo o canto superior esquerdo;
  a % atual é derivada da largura do cliente (base 300 em H / 75 em V), não de
  uma variável — o menu marca a entrada mais próxima. O mínimo de
  `WM_GETMINMAXINFO` é dinâmico (25% + moldura) para não bloquear presets
  pequenos; clamps do `LoadIni` também são baixos (64x40) por isso.
- **Linha de pico**: marca branca de 2px na posição do pico; hold de 600 ms
  (`PEAK_HOLD_TICKS`) e queda `PEAK_FALL` por tick. Desligar zera `gPeakL/R`
  (senão a marca fica congelada).
- **Esquema de cores** (`gScheme`): 0 = simples (cor única `gLedOn`,
  picker habilitado); 1 = faixas Verde/Amarelo/Vermelho em 70%/85% da posição
  do LED — o picker fica desabilitado e cada LED apagado usa a própria cor ×30%.
- **Barra de título**: duplo clique em qualquer lugar alterna; a janela perde
  também thickframe/sysmenu (vira `WS_POPUP` puro). Sem título, arrastar pela
  área cliente move a janela (drag manual com `SetCapture`, não usar
  `WM_NCLBUTTONDOWN/HTCAPTION` — conflitaria com o duplo clique).
- **Transparência do fundo** (`gBgTrans`, 0..100, chave `Trans`, padrão 0):
  slider vertical na janela Opções (`TBS_VERT`, incremento 1, **0% embaixo e
  100% no topo**). O trackbar é linear com o mínimo no topo, então o valor é
  invertido no código (`TransToSlider`/`SliderToTrans` = 100 − posição).
  Cada notificação do slider (`WM_VSCROLL`, ver armadilhas) aplica na hora:
  o VU acompanha o arrasto em tempo real, e ao soltar (`TB_ENDTRACK`) grava a
  preferência.
  Aplica-se **só ao fundo** (`COL_BG` = preto): LEDs, texto e linha de pico
  ficam opacos. Implementado com `UpdateLayeredWindow` sobre um DIB 32bpp
  (premultiplied) — quando `Trans > 0` a janela fica sem moldura (ver
  armadilhas). Com `Trans = 0` o caminho é o GDI normal com double buffering.
- **Instância única**: mutex nomeado `Local\VULedMeter_SingleInstance` criado
  no início do `wWinMain`; se já existir, a segunda instância traz a janela da
  primeira para frente (`ShowWindow(SW_RESTORE)` + `SetForegroundWindow`) e
  sai sem criar nada.
- **Layout H/V**: `ToggleLayout()` inverte também altura↔largura da janela
  (300x75 ↔ 75x300) para preservar a área do medidor.
- **Persistência**: grava `VULed.ini` ao lado do `.exe` (hoje
  `output/VULed.ini`, chaves em `[VU]`):
  tudo que a janela Opções expõe + o menu Tamanho — `X,Y,W,H`, `Color`
  (COLORREF decimal), `Style` (`H`/`V`), `Title` (0/1), `Peak` (0/1),
  `Scheme` (0/1), `Trans` (0..100) e `Scale` (25..400). Além do botão `Salvar`,
  o estado é gravado **automaticamente** em `WM_DESTROY` (sair), em
  `ToggleTitle` e ao soltar o slider de transparência (`TB_ENDTRACK`) — o
  duplo clique alterna a barra de título fora do `Salvar`, então sem esse
  save imediato o app reiniciava sempre com título. Único estado **não** salvo:
  cores customizadas da paleta do picker (16 slots do `CHOOSECOLOR`).
- `W/H` e `Scale` são redundantes de propósito: `ApplyScale` redimensiona e o
  Save grava o resultado em `W/H`; no load, `W/H` tem prioridade e `Scale` só
  é aplicado se `W/H` faltar (aplicar os dois dobraria o tamanho).
- Preferências são lidas no `wWinMain` *antes* do `CreateWindow` (posição/tam.
  e `Title`, que define o estilo inicial, entram direto na criação); não mover
  essa leitura para depois.

## Convenções

- Unicode obrigatório (`wWinMain`, `UNICODE` definido no código; flag
  `-municode` no build). Janela padrão 300x75 (redimensionável; mínimo dinâmico
  = escala 25% + moldura), fundo preto, LEDs ciano padrão `RGB(0,230,255)`
  (configurável; apagado é a cor acesa × 30%), 26 LEDs por canal,
  escala −60..0 dB, ataque instantâneo + decaimento por tick (`RELEASE`).

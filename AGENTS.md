# AGENTS.md

Projeto Windows: VU de LEDs mono (L+R) que lê a **saída padrão** via WASAPI
loopback. C++ puro (Win32 + GDI), zero dependências externas.

## Código

Fontes organizados por responsabilidade (cada tela/menu em seu arquivo). Os
includes usam sempre o caminho a partir de `src/` (o build passa `-I src`):

- `src/main.cpp` — entrada (`wWinMain`): instância única, ícones, registro das
  classes, criação da janela e loop de mensagens (acelerador Alt+F4)
- `src/app/ids.h` — constantes (layout, `DB_MIN`, `RELEASE`, pico, escalas),
  IDs de comando/controle e nomes de classe
- `src/app/state.{h,cpp}` — estado compartilhado: preferências em memória,
  níveis/picos (`gHold*`, `gPeak*`) e handles `gMain`/`gOpts`
- `src/app/version.h` — versão exibida no `Sobre...` (`APP_VERSION`)
- `src/app/config.{h,cpp}` — `VULed.ini` (load/save) e "Iniciar com o Windows"
  (`HKCU\...\Run`)
- `src/audio/capture.{h,cpp}` — captura WASAPI loopback (`struct AudioCapture`)
- `src/gfx/render.{h,cpp}` — desenho dos LEDs/rótulos/LED do pico e o modo
  camada (DIB 32bpp + `UpdateLayeredWindow`)
- `src/ui/main_window.{h,cpp}` — `WndProc` principal: timer de captura,
  pintura, arrasto e as ações de janela (moldura, topmost, transparência,
  layout, escala)
- `src/ui/options_window.{h,cpp}` — janela "Opções..." (controles criados em
  código, sem `.rc`)
- `src/ui/menu.{h,cpp}` — menu de contexto (clique direito ou bandeja)
- `src/ui/tray.{h,cpp}` — ícone da bandeja (`Shell_NotifyIconW`)
- `src/res/app.rc` + `app.ico` + `resource.h` — ícone embutido no `.exe`
  (`IDI_APP`, 16/24/32/48/64/128 px; gerado dos PNGs `icon32x32.png` /
  `icon128x128.png` na raiz). O Explorer/taskbar leem o **recurso** ICON, não o
  `.ico`, por isso o `app.rc` é compilado com `windres` e linkado junto.
- `output/VULed.exe` — binário compilado (cross-compile a partir do WSL)
- `output/VULed.ini` — preferências persistidas; fica ao lado do `.exe` porque
  é ali que o app lê/grava (`ConfigInit` usa `GetModuleFileNameW`)
- `build/` — temporários de build (objetos espelhando as pastas de `src/`);
  **não** versionado (`.gitignore`)

## Build (MinGW-w64 a partir do WSL)

```sh
make          # compila src/**/*.cpp + src/res/app.rc -> output/VULed.exe
make clean    # apaga só os temporários (build/), preserva output/
```

Sem `make` (três etapas, para os temporários irem para `build/`; repetir a
compilação para cada `.cpp` de `src/`):

```sh
x86_64-w64-mingw32-g++ -O2 -std=c++17 -Wall -municode -mwindows -I src \
  -c src/main.cpp -o build/main.o
x86_64-w64-mingw32-windres -I src/res -O coff -o build/app.res src/res/app.rc
x86_64-w64-mingw32-g++ -static -municode -mwindows -o output/VULed.exe \
  build/main.o build/app/*.o build/audio/*.o build/gfx/*.o build/ui/*.o \
  build/app.res \
  -lole32 -luuid -lgdi32 -luser32 -lcomdlg32 -lcomctl32 -ladvapi32 -lshell32
```

`-static` é **obrigatório**: sem ele o `.exe` depende de `libstdc++-6.dll` /
`libgcc_s_seh-1.dll` / `libwinpthread-1.dll`, que não existem no Windows e
derrubam a execução com "libstdc++-6.dll não foi encontrado". Validar com:

```sh
x86_64-w64-mingw32-objdump -p output/VULed.exe | grep "DLL Name"
# deve listar só DLLs de sistema (GDI32, USER32, ole32, comdlg32, comctl32,
# ADVAPI32, KERNEL32, msvcrt)
```

Requisitos: `sudo apt install g++-mingw-w64-x86-64`. Não há testes/CI — a
verificação é compilar e rodar o `.exe` no Windows.

## Versão

- `version.txt` contém a versão em `X.Y.Z` (uma linha, sem `v` prefixo) e é a
  fonte da verdade; `src/app/version.h` (`APP_VERSION`) é o que o app compila
  (texto do `Sobre...`). O `Makefile` (`check-version`) **aborta o build** se
  os dois divergirem — ao subir a versão, mudar os dois arquivos.
- Padrão semântico simples: **incrementar Z** (patch) em cada alteração que
  mudar o binário/código; reservar Y para mudanças de comportamento novo
  (ex.: novo item de menu) e X para quebras grandes. Primeira release: 1.0.0.
- Após mudar a versão, commitar junto (`version.txt` foi criado no commit
  raiz junto com o código).

## Armadilhas (não "conserte" sem verificar)

- **GUID `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`**: o `ksmedia.h` do MinGW-w64 não
  fornece a definição (dá `undefined reference`). O código define o GUID
  manualmente no topo de `src/audio/capture.cpp`. Não inclua `ksmedia.h` nem
  `initguid.h` para "resolver" isso.
- **WASAPI loopback só entrega dados quando há playback ativo.** Sem som
  tocando, as barras ficam zeradas — é comportamento esperado, não bug.
- **Mutar o endpoint padrão NÃO silencia o loopback** (o tap é antes do
  volume/mute do dispositivo). Para testar o decaimento com silêncio é preciso
  zerar o volume da *sessão* do player (`ISimpleAudioVolume`) ou parar o áudio;
  `keybd_event(VK_VOLUME_MUTE)` não serve (e ainda deixa o sistema mudo se o
  teste falhar no meio).
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
  acima de zero, ou (`gShowPeak` e pico ativo — `peakVisible`/`gPeakWasVisible`,
  este último só para o repaint que apaga o LED do pico). Repintar a tela
  direto a 100 Hz, ou continuar repaintando com tudo em zero (caso "sem som"),
  causa flicker visível — preservar essas proteções ao mexer no desenho. No
  modo camada o repaint sai de `RequestRepaint()`, que chama `RenderLayered()`
  na hora (janela em camadas não repinta por `InvalidateRect`).
- **Rótulos L/R**: `DrawAll` seleciona `DEFAULT_GUI_FONT` e mede cada letra com
  `GetTextExtentPoint32W` (L e R têm larguras diferentes); a fonte padrão do DC
  (System) é mais alta que a faixa de 14 px e as letras saíam **cortadas
  embaixo**, e posições fixas (`x + colW/2 - 4`, `x = 3`) deixavam o "R" embaixo
  do primeiro LED. As faixas `labelH`/`labelW` derivam do texto medido — não
  voltar a números mágicos.
- **Transparência ⇒ janela em camadas.** Com `Trans > 0` o app usa
  `WS_EX_LAYERED` + `UpdateLayeredWindow` (alpha por pixel), e isso
  **substitui a janela inteira**: a moldura/barra de título do sistema deixa de
  ser desenhada. Por isso, com transparência ativa a janela é sempre `WS_POPUP`
  e o checkbox "Exibir barra de título" fica desabilitado (a preferência
  `Title` é preservada e volta a valer quando `Trans` volta a 0).
  O duplo clique **não** é ignorado nesse estado: como moldura e camada não
  coexistem, `ToggleTitle` desliga a transparência (`SetBgTrans(0)`) para poder
  mostrar a barra de título — sem isso a janela ficava travada sem moldura e
  sem jeito de reverter pelo cliente. Não tentar "consertar" mantendo
  `WS_OVERLAPPEDWINDOW` com a camada: o `UpdateLayeredWindow` cobre a moldura e
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

- **Menu**: clique direito na janela (ou clique no ícone da bandeja) →
  `Opções...` (janela de configurações),
  `Tamanho:` (submenu de radio: 25%..400%, padrão 100%), `Sobre...` (mostra
  "VU — Saída Padrão", `Versão X.Y.Z` e
  "(2026) Copyleft by adrian.barbosa@gmail.com"), separador e `Sair\tAlt+F4`.
  `Sair` é `IDM_EXIT` → `DestroyWindow`; o Alt+F4 funciona também sem moldura
  (`WS_POPUP` sem sysmenu) por uma tabela de aceleradores (`hAccel` em
  `src/main.cpp` + `TranslateAcceleratorW` no loop de mensagens) — sem ela o
  `DefWindowProc` não fecharia a janela sem sysmenu.
- **Escala (`ApplyScale`)**: redimensiona mantendo o canto superior esquerdo;
  a % atual é derivada da largura do cliente (base 300 em H / 75 em V), não de
  uma variável — o menu marca a entrada mais próxima. O mínimo de
  `WM_GETMINMAXINFO` é dinâmico (25% + moldura) para não bloquear presets
  pequenos; clamps do `LoadIni` também são baixos (64x40) por isso.
- **Pico (hold)**: é um **LED da própria barra** — o LED da posição do pico
  (`PeakLitCount`) fica aceso na cor daquela posição, acima da barra, e desce
  conforme o decaimento. **Não** é traço/marca branca (essa era a versão
  errada: a "linha" parecia um LED avulso e ainda cobria um segmento). Hold de
  ~0,7 s (`PEAK_HOLD_TICKS`) e queda `PEAK_FALL` por tick. É alimentado pelo
  **nível exibido** (`gHold`), não pelo pico cru do buffer: com música
  comprimida o pico instantâneo bate no teto a cada buffer (~45 ms) e re-arma o
  hold para sempre — o LED fica congelado no topo enquanto as barras dançam.
  Depois do hold o pico cai e, ao alcançar o nível, passa a acompanhá-lo (nunca
  fica abaixo da barra). Desligar zera `gPeakL/R` (senão o LED fica aceso) e o
  timer guarda `gPeakWasVisible` para garantir o repaint que apaga o LED
  quando o pico chega a zero com a barra já parada.
- **Bandeja (tray)**: ícone adicionado em `WM_CREATE` (`Shell_NotifyIconW`
  `NIM_ADD`, callback `WM_TRAYICON = WM_APP+1`) e removido em `WM_DESTROY`.
  Clique esquerdo/direito abre o **mesmo** menu de contexto. O menu da bandeja
  usa `TPM_BOTTOMALIGN` e abre 8 px acima do cursor: se abrisse sobre ele, o
  segundo clique de um duplo clique cairia no último item (`Sair`) e fecharia o
  app. O ícone é recriado quando o Explorer reinicia (mensagem registrada
  `TaskbarCreated`). No Windows 11 ícones novos caem no "mostrar ícones
  ocultos" — é o usuário que fixa na barra; não há como forçar por código.
- **Esquema de cores** (`gScheme`): 0 = simples (cor única `gLedOn`,
  picker habilitado); 1 = faixas Verde/Amarelo/Vermelho em 70%/85% da posição
  do LED — o picker fica desabilitado e cada LED apagado usa a própria cor ×30%.
- **Barra de título**: duplo clique em qualquer lugar alterna; a janela perde
  também thickframe/sysmenu (vira `WS_POPUP` puro). Sem título, arrastar pela
  área cliente move a janela (drag manual com `SetCapture`, não usar
  `WM_NCLBUTTONDOWN/HTCAPTION` — conflitaria com o duplo clique).
  O arrasto é habilitado pelo **estilo real** (`!(GetWindowLongPtr(GWL_STYLE) &
  WS_CAPTION)`), não por `gTitleBar`: com transparência a janela fica sem
  moldura mesmo com `Title=1`, e checar a preferência deixava a janela
  impossível de mover nesse estado.
  Com moldura o Windows impõe uma largura mínima (`SM_CXMIN`, 136 px em 100%):
  ligar a barra de título numa janela vertical de 75 px alarga o cliente para
  ~120 px — é limitação do sistema, não do app.
- **Transparência do fundo** (`gBgTrans`, 0..100, chave `Trans`, padrão 0):
  slider vertical na janela Opções (`TBS_VERT`, incremento 1, **0% embaixo e
  100% no topo**). O trackbar é linear com o mínimo no topo, então o valor é
  invertido no código (`TransToSlider`/`SliderToTrans` = 100 − posição).
  Cada notificação do slider (`WM_VSCROLL`, ver armadilhas) aplica na hora:
  o VU acompanha o arrasto em tempo real, e ao soltar (`TB_ENDTRACK`) grava a
  preferência.
  Aplica-se **só ao fundo** (`COL_BG` = preto): LEDs, texto e LED do pico
  ficam opacos. Implementado com `UpdateLayeredWindow` sobre um DIB 32bpp
  (premultiplied) — quando `Trans > 0` a janela fica sem moldura (ver
  armadilhas). Com `Trans = 0` o caminho é o GDI normal com double buffering.
- **Sempre visível** (`gTopmost`, chave `Topmost`, padrão **1**): checkbox na
  janela Opções; `ApplyTopmost()` faz
  `SetWindowPos(HWND_TOPMOST/HWND_NOTOPMOST)` e é reaplicado no start (depois do
  `CreateWindow`). A janela Opções **não** é topmost — ela abre abaixo do VU,
  então não fica escondida atrás dele.
- **Iniciar com o Windows** (`gStartup`, chave `Startup`, padrão **0**): grava
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, valor `VULedMeter`, com
  o caminho do `.exe` entre aspas (pode ter espaços). O ini é a fonte da verdade
  do checkbox; marcar/desmarcar cria/remove a chave na hora e chama `SaveIni()`
  junto (o efeito é fora do processo). No start, se `Startup=1`, o caminho é
  reescrito — conserta o autostart quando o `.exe` é movido.
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
  `Scheme` (0/1), `Trans` (0..100), `Topmost` (0/1), `Startup` (0/1) e
  `Scale` (25..400). Além do botão `Salvar`,
  o estado é gravado **automaticamente** em `WM_DESTROY` (sair), em
  `ToggleTitle`, ao soltar o slider de transparência (`TB_ENDTRACK`) e ao
  marcar/desmarcar "Iniciar com o Windows" — o
  duplo clique alterna a barra de título fora do `Salvar`, então sem esse
  save imediato o app reiniciava sempre com título. Único estado **não** salvo:
  cores customizadas da paleta do picker (16 slots do `CHOOSECOLOR`).
- `W/H` e `Scale` são redundantes de propósito: `ApplyScale` redimensiona e o
  Save grava o resultado em `W/H`; no load, `W/H` tem prioridade e `Scale` só
  é aplicado se `W/H` faltar (aplicar os dois dobraria o tamanho).
- Preferências são lidas no `wWinMain` *antes* do `CreateWindow` (posição/tam.
  e `Title`, que define o estilo inicial, entram direto na criação); não mover
  essa leitura para depois.
- **Módulos**: cada janela/menu/serviço em seu próprio arquivo (ver "Código");
  includes sempre no formato `"pasta/arquivo.h"` (o build passa `-I src`) e
  nenhum `.cpp` inclui outro `.cpp`. Estado compartilhado só em
  `app/state.h`; o que é de uso interno fica `static` no módulo.

## Convenções

- Unicode obrigatório (`wWinMain`, `UNICODE` definido no código; flag
  `-municode` no build). Janela padrão 300x75 (redimensionável; mínimo dinâmico
  = escala 25% + moldura), fundo preto, LEDs ciano padrão `RGB(0,230,255)`
  (configurável; apagado é a cor acesa × 30%), 26 LEDs por canal,
  escala −60..0 dB, ataque instantâneo + decaimento por tick (`RELEASE` = 0.97,
  ~-17 dB/s reais — antes era 0.85, que piscava os LEDs e fazia o pico parecer
  congelado).

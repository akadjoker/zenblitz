# Plano do engine — zenblitz3d

Continuação do `PLANO_BLITZ3D.md`, atualizado com o que ficou decidido e
verificado hoje (dependências reais confirmadas, GPU/Batch inspecionados,
CI a provar que `zen_vm` cross-compila). Este documento é o que se segue de
facto; o antigo fica como referência de arquitetura geral.

## Decisões tomadas

- **Mesmo repositório** (`zenblitz`), não um projeto separado. Um Blitz sem
  engine não é uma linguagem prática — os 300 samples só valem a pena se
  correrem aqui.
- **Nome do produto**: `zenblitz3d`.
- **GPU e math**: submodules (`extern/GPU`, `extern/math`), não copiados.
- **Batch 2D/3D**: copiado de
  `/media/projectos/projects/cpp/Kinetix3d/kinetix/src/render/Batch.cpp`
  (1588 linhas + 259 de header), não submodule — é só uma peça, já
  compilada para web no Kinetix, dependências limpas (`GPU`, `mathc`,
  `ct::Vector`, SDL2).
- **Canvas 2D híbrido**: `DrawImage`/`Rect`/`Line`/`Cls` por GPU (batch de
  quads, um draw call para centenas de sprites — mais rápido que o
  DirectDraw original); `LockBuffer`/`WritePixel`/`ReadPixel` por CPU com
  upload preguiçoso; `ImagesCollide` por bitmask em CPU (como o original —
  não há como fazer pixel-perfect só em GPU sem readback).
- **Fiéis primeiro, melhores depois**: a semântica dos comandos existentes
  não muda (samples têm de continuar a correr); comandos novos
  (`DrawImageRotated`, blend modes, `SetAlpha`) entram a par, sem tocar nos
  antigos. O `drawTexture` do Batch já aceita `pivot`/`rotationDeg` de
  graça — coisa que o Blitz3D nunca teve.
- **`libzen` fica limpo**: nenhuma dependência de SDL/GPU passa a compilar
  `zenblitz`/`zenblitz-rt`. O CI de hoje (Android NDK, Emscripten, MSVC)
  confirmou que `zen_vm` cross-compila sozinho — é a prova em que assenta
  todo este plano. `zenblitz3d` é um alvo novo, `ZEN_BUILD_ENGINE=ON`.

## Estado verificado hoje (evita redescobrir)

- `GPU_BUILD_VULKAN` já é `OFF` por omissão; ligamos só `GPU_BUILD_OPENGL`.
  Custo real: core 24 592 linhas + backend GL 4233, não as 41 000 totais.
- `GPU` tem `SDLWindow`/`GPUSDLWindow.h`: janela + contexto + `createDevice`
  numa API só — resolve metade do `Platform` sem código nosso.
- `GPU::Device` tem `updateTexture`/`readTexture`/`mapBuffer` — o necessário
  para o Canvas híbrido (upload da parte CPU, readback para `ReadPixel`).
- O 2D do Blitz3D original é DirectDraw puro (`surf->Blt`, colorkey,
  `LockBuffer` com ponteiro direto) — **não** um pipeline de quads. É por
  isso que o Canvas tem de ser híbrido, não só GPU.
- `ImagesCollide` no original usa uma bitmask de 1 bit/pixel
  (`gxCanvas::updateBitMask`, `gxruntime/gxcanvas.cpp:180`) — reaproveitável
  quase tal e qual.
- Engine original: 8413 linhas em 39 ficheiros úteis (`blitz3d/blitz3d/`).
  Runtime de comandos original: 6942 linhas (`blitz3d/bbruntime/`).

## Estrutura de diretórios

```
zenblitz/                     (como está: libzen/, cli/, tests/)
├── extern/
│   ├── GPU/                  submodule
│   └── math/                 submodule
├── engine/
│   ├── include/engine/
│   └── src/
│       ├── render/           Batch.{h,cpp} copiado + adaptações
│       ├── platform/         Platform (janela, input, timers via GPUSDLWindow)
│       ├── canvas/           Canvas híbrido (GPU + CPU + bitmask)
│       └── world/            entity, camera, light, model — fase 2+
├── runtime3d/
│   └── src/                  bb_cmds_graphics.cpp, bb_cmds_input.cpp, ...
│       (mesma BBCommand/registerCommands que bb_cmds.cpp já usa)
└── PLANO_ENGINE.md           (este ficheiro)
```

`CMakeLists.txt` raiz ganha `option(ZEN_BUILD_ENGINE "Build zenblitz3d" OFF)`.
OFF é o que o CI de hoje já testa (Android/web/MSVC continuam a não ver
SDL/GPU). ON acrescenta `extern/GPU`, `extern/math`, `engine/`, `runtime3d/`
e o executável `zenblitz3d`.

## Marcos

Cada marco é "corre e dá para ver/jogar", não uma pilha de código sem prova.

### Marco 0 — esqueleto (meio dia)
- Submodules `GPU` e `math` em `extern/`.
- `CMakeLists.txt`: `ZEN_BUILD_ENGINE`, liga `GPU_BUILD_OPENGL`, desliga o
  resto.
- Copiar `Batch.{h,cpp}` + `ShaderDialect.h`, `Log.h`, `FontData.h` para
  `engine/src/render/`; compila sozinho como biblioteca antes de qualquer
  ligação ao zenblitz.
- **Prova**: `zenblitz3d` existe, abre uma janela preta (SDL) e fecha com
  Escape. Sem comandos Blitz ainda.

### Marco 1 — janela, loop, input (dias)
- `Platform`: `GPUSDLWindow` + bomba de eventos SDL + tabela de scancodes
  do Blitz (copiada do original, `gxruntime/gxinput.cpp`) + `MilliSecs`.
- Comandos: `Graphics`, `Graphics3D`, `Flip`, `Cls`, `ClsColor`, `KeyDown`,
  `KeyHit`, `MouseX/Y/Down`, `GraphicsWidth/Height`, `EndGraphics`,
  `WaitKey`, `AppTitle`.
- `Flip` usa `VM::request_suspend`/`wake_at` como o `Delay` de hoje: um
  frame de suspensão, não um sleep — é o que permite o loop web depois.
- **Prova**: `While Not KeyHit(1): Cls: Flip: Wend` corre, a janela responde
  a teclado e rato. Primeiro sample real do disco (`Samples/Blitz 2D
  Samples/`) que só precise disto compila e corre.

### Marco 2 — Canvas 2D (1 semana)
- `Canvas` híbrido: `Color`, `Plot`, `Line`, `Rect`, `Oval` via Batch;
  `LoadImage`/`DrawImage`/`MaskImage` (textura GPU + colorkey);
  `LockBuffer`/`ReadPixel`/`WritePixel` (buffer CPU, upload preguiçoso);
  `ImagesCollide`/`ImagesOverlap` (bitmask CPU, do original).
- Texto: `stb_truetype` desde o início — `LoadFont`, `SetFont`, `Text` já
  com fontes carregadas do disco; a fonte embutida do Batch fica só de
  fallback para `Text` antes de qualquer `LoadFont`.
- **Prova**: correr um sample 2D real (`Samples/Blitz 2D Samples/`) sem o
  editar. Comparar visualmente com o Blitz3D original em Wine.

### Marco 3 — primeiro cubo (1 a 2 semanas)
- `Renderer` sobre `gpu::Device`: pipeline "fixed-function DX7" (luzes por
  vértice, fog, 2 estágios de textura, blend modes, `EntityFX`).
- `Transform` = `Math::Mat3` + `Math::Vec3` substituindo o `geom.h`
  original (~60 usos em `entity.cpp`, `world.cpp`, `collision.cpp`,
  `camera.cpp` — único ponto de adaptação manual do plano).
- Engine: `entity`, `world`, `camera`, `light`, `model`, `meshmodel`,
  `surface`, `brush`, `texture` portados quase sem alteração
  (`std::vector/map` → `ct::Vector/HashMap`).
- Comandos: `CreateCamera`, `CreateLight`, `CreateCube/Sphere/Cylinder/
  Cone/Pivot/Plane`, `Position/Rotate/Move/Turn/Translate/Scale/
  PointEntity`, `EntityParent`, `EntityColor/Alpha/FX/Blend/Order/
  Shininess`, `LoadTexture`, `EntityTexture`, `AmbientLight`,
  `CameraRange/Zoom/ClsColor/Fog*`, `RenderWorld`, `UpdateWorld`,
  `FreeEntity/Hide/Show/Copy`.
- **Prova**: cubo texturado com luz a rodar, num `.bb` de 10 linhas.

### Marco 4 — meshes e animação (1 a 2 semanas)
`CreateMesh/Surface/AddVertex/AddTriangle`, `UpdateNormals`, `LoadMesh`,
`LoadAnimMesh` (B3D, 3DS, X, MD2), `Animate`, `AnimSeq`, `skinmodel`.
**Prova**: carregar e animar um modelo dos `Samples/`.

### Marco 5 — colisões, pick, terreno, sprites, som 3D (2 a 3 semanas)
`collision.cpp` do original quase tal e qual; `EntityType/Collisions/
Collision*`, `LinePick/CameraPick/EntityPick`, `terrainrep`, `sprite`,
`md2rep`, `q3bsprep`, `CreateMirror`, som 3D via `Audio` (miniaudio).
**Prova**: um dos `Games/` (TunnelRun, wing_ring) corre de ponta a ponta.

### Marco 6 — som 2D, ficheiros (1 semana)
`Audio` (miniaudio) para `PlaySound/PlayMusic`; os comandos de
ficheiros/banks já existem no zenblitz — só confirmar que o backend SDL os
serve tal como o `backend_stdio` de hoje serve os de consola.

### Marco 7 — verificação
Correr os `Games/`/`Samples/` reais (364 ficheiros, já mapeados: 26
compilam hoje sem engine, 273 falham só por comandos gráficos — essa lista
é o roteiro dos marcos 1-6) e comparar com o Blitz3D em Wine. Script que
confere as ~480 assinaturas `rtSym` do original contra o que ficou
registado.

## Decidido

1. **Compilador dentro do `zenblitz3d`** — linka `zen_static`, corre `.bb`
   direto como o Blitz3D original (editar → play). Sem executável separado
   "só runtime" para já.
2. **Teclas**: `KeyDown(n)`/`KeyHit(n)` do original passam `n` direto ao
   DirectInput — os números que os samples usam **são** os códigos `DIK_*`,
   não uma tabela própria do Blitz. O `Platform` mapeia `SDL_Scancode` →
   DIK ao ler o evento; a VM só vê o valor DIK. Tabela completa (SDL →
   DIK, ~90 teclas úteis, os aliases do original) em
   `PLANO_ENGINE_KEYS.md` — usar direto no Marco 1, já não é preciso
   redescobrir os valores.
3. **Fontes carregadas desde o início**: `stb_truetype` entra no Marco 2
   junto com `LoadFont`/`SetFont`, não fica para depois. A fonte embutida
   do Batch serve de *fallback* quando o programa não carregou nenhuma
   (`Text` antes de qualquer `LoadFont` deve continuar a desenhar algo).

## Bug MSVC em aberto (não bloqueia o engine)

`zenblitz.exe` no CI do MSVC dá `[runtime error] APPEND expected array, got
nil` só em `Print "x"` — o bytecode disassemblado é idêntico ao do Linux, o
que aponta para o dispatch (`switch`, modo não-computed-goto) a executar o
opcode errado, não para o compilador. Ainda não reproduzido localmente
(sem Windows à mão). Ver a conversa de hoje para o que já foi descartado
(union não inicializada, ABI de userlib, ordem de opcodes) antes de
recomeçar a investigação.

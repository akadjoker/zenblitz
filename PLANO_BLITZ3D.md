# Plano: Blitz3D independente sobre GPU + mathc + containers, ligado ao zenblitz

Objectivo: recriar o Blitz3D como engine C++ portável, com **um único backend
de render** (a tua lib `GPU`, como submodule), a tua `mathc` e os teus
`containers`, e expor os comandos Blitz3D ao zenblitz. O original serve de
referência de comportamento; as 13 classes `gx*` de DirectX desaparecem.

Fontes:
- Blitz3D original: `/media/projectos/projects/basic/blitz3d/` (zlib)
- GPU: `github.com/akadjoker/GPU` (`gpu::Device`, backends GL/GLES/Vulkan/Null, `gpu_sdl`)
- mathc: `github.com/akadjoker/math` (`Math::Vec3/Mat4/Quaternion/Plane/Box/Frustum`, column-major como GLM)
- containers: `github.com/akadjoker/containers` (`ct::Vector/Array/HashMap/SlotMap/String/Pool/...`)

## 1. Arquitectura em três camadas (em vez de 13 classes gx*)

```
zenblitz  ──BBCommand──▶  runtime/   (comandos: 234 3D + 111 2D + 41 input + 17 som)
                             │
                          engine/    (Entity, Camera, Light, Model, Mesh, Surface, Brush,
                             │        Texture, Animator, Collision, Terrain, Sprite, BSP, World)
                          backend/   (3 classes: Renderer, Canvas, Platform)
                             │
                   GPU (submodule)  +  SDL2 (janela/input via gpu_sdl)  +  miniaudio
```

### backend/ — o que substitui `gxruntime/` (6 500 linhas → ~1 500)

| Classe nova | Substitui | Conteúdo |
|---|---|---|
| `Platform` | `gxRuntime`, `gxInput`, `gxTimer` | janela (`gpu_sdl`), eventos SDL, teclado/rato/joystick com a tabela de scancodes do Blitz, `MilliSecs`, `Delay`, modos de vídeo, log |
| `Renderer` | `gxGraphics`, `gxScene`, `gxMesh`, `gxLight` | `gpu::Device`; um pipeline "fixed-function DX7" (shader com luzes por vértice, fog, 2 stages de textura, `EntityFX`, blend modes); `MeshBuffer` = VBO+IBO de `gpu::BufferHandle`; `RenderState` igual ao do original; `render(mesh, first_vert, vert_cnt, first_tri, tri_cnt)` |
| `Canvas` | `gxCanvas`, `gxFont` | superfície 2D = `gpu::TextureHandle` + framebuffer; `Plot/Line/Rect/Oval/Text/DrawImage` desenhados por um batch de quads (um pipeline 2D); `Lock/ReadPixel/WritePixel` via `readTexture/updateTexture`; fontes com stb_truetype |

Som fica fora do render: `Audio` (miniaudio) substitui `gxAudio/gxSound/gxChannel` numa classe.
`gxMovie` não é portado.

### engine/ — port de `blitz3d/` (10 500 linhas, mantém-se)

- `geom.h` (Vector/Matrix/Quat/Box/Line/Plane, 557 linhas) → **apagado**;
  passa a `using Vector = Math::Vec3; using Quat = Math::Quaternion;` e uma
  `Matrix` fina (o Blitz usa matriz 3x3 + posição, "Transform{ Matrix m; Vector v; }")
  implementada com `Math::Mat3` + `Math::Vec3`. É a única adaptação com
  trabalho manual: ~60 usos de `Transform` em `entity.cpp`, `world.cpp`,
  `collision.cpp`, `meshcollider.cpp`, `camera.cpp`.
- `std::vector/map/set/list` → `ct::Vector`, `ct::HashMap`, `ct::HashSet`
  (o original usa cerca de 90 sítios; substituição mecânica).
- Ficheiros que compilam praticamente sem alteração: `entity`, `animation`,
  `animator`, `frustum`, `boxvis`, `meshutil`, `meshloader`, `loader`,
  `loader_3ds`, `loader_x`, `loader_b3d`, `md2model`, `md2norms`,
  `q3bspmodel`, `terrain`, `collision`, `meshcollider`.
- Ficheiros que chamam o backend (trocar `gxScene/gxMesh/gxCanvas` por
  `Renderer/MeshBuffer/Canvas`, mesma forma): `surface`, `model`,
  `meshmodel`, `planemodel`, `skinmodel`, `sprite`, `md2rep`, `terrainrep`,
  `q3bsprep`, `world`, `camera`, `light`, `brush`, `texture`, `cachedtexture`.
- Som 3D (`object`, `emitter`, `listener`) → `Audio`.

### runtime/ — port de `bbruntime/`

As tabelas `rtSym` do original são copiadas tal e qual e registadas no zenblitz
através de `BBCommand` (o mecanismo que já serve os comandos de consola):
nomes, ordem de parâmetros e defaults iguais ao Blitz3D. Handles são
inteiros (tabela handle→ponteiro com `ct::SlotMap`), `BBStr` desaparece.

## 2. Estrutura do projecto (novo, separado; nome a confirmar: `blitz3d`)

```
blitz3d/
  CMakeLists.txt
  extern/GPU          submodule
  extern/math         submodule (mathc)
  extern/containers   submodule
  extern/miniaudio, stb_image, stb_truetype
  backend/  Platform.*  Renderer.*  Canvas.*  Audio.*  shaders/ff.vert ff.frag 2d.vert 2d.frag
  engine/   port de blitz3d/
  runtime/  bbgraphics.cpp bbinput.cpp bbaudio.cpp bbblitz3d.cpp bbfilesystem.cpp bbstream.cpp bbbank.cpp
  module/   blitz3d_module.cpp  (regista tudo no zenblitz: BBCommand[])
  tests/    programas .bb + screenshots de referência do Blitz3D original
```

zenblitz: opção `ZENBLITZ_ENABLE_3D=ON` faz `add_subdirectory(../blitz3d)` e
liga `blitz3d_module`; sem ela continua só consola. Nada mais muda no zenblitz.

## 3. Ordem de trabalho (por marcos, não por meses)

**Marco 1 — janela e loop.** `Platform` + `Renderer::clear/present` + input.
Comandos: `Graphics`, `Graphics3D`, `Flip`, `Cls`, `ClsColor`, `KeyDown`,
`KeyHit`, `MouseX/Y`, `GraphicsWidth/Height`, `EndGraphics`.
`While Not KeyHit(1): Cls: Flip: Wend` corre. (dias)

**Marco 2 — primeiro cubo.** `Renderer` com o pipeline fixed-function,
`MeshBuffer`; engine: `world`, `entity`, `camera`, `light`, `model`,
`meshmodel`, `surface`, `brush`, `texture`. Comandos: `CreateCamera`,
`CreateLight`, `CreateCube/Sphere/Cylinder/Cone/Pivot/Plane`, `Position/
Rotate/Move/Turn/Translate/Scale/PointEntity`, `EntityParent`, `EntityColor/
Alpha/FX/Blend/Order/Shininess`, `LoadTexture`, `EntityTexture`,
`AmbientLight`, `CameraRange/Zoom/ClsColor/Fog*`, `RenderWorld`,
`UpdateWorld`, `FreeEntity/Hide/Show/Copy`. (1 a 2 semanas)

**Marco 3 — 2D.** `Canvas`: `Color`, `Plot`, `Line`, `Rect`, `Oval`, `Text`,
`LoadImage/DrawImage/DrawBlock/MaskImage/ImagesCollide/ImagesOverlap`,
buffers e `LockBuffer/ReadPixel/WritePixel`, fontes. (1 semana)

**Marco 4 — meshes e animação.** `CreateMesh/Surface/AddVertex/AddTriangle/
Vertex*/Triangle*`, `UpdateNormals`, `LoadMesh`, `LoadAnimMesh` (B3D, 3DS,
X, MD2), `Animate`, `AnimSeq`, `ExtractAnimSeq`, `skinmodel`. (1 a 2 semanas)

**Marco 5 — colisões, pick, terreno, sprites, BSP, mirror, som 3D.**
`collision.cpp` tal como está; `EntityType/Collisions/Collision*`,
`LinePick/CameraPick/EntityPick/Picked*`, `terrainrep`, `sprite`, `md2rep`,
`q3bsprep`, `CreateMirror`, `CreateListener/EmitSound`. (2 a 3 semanas)

**Marco 6 — som 2D, ficheiros, banks, streams.** miniaudio + `std::fstream`. (1 semana)

**Marco 7 — verificação.** Correr os `samples/` do Blitz3D original e
comparar com o Blitz3D em Wine; script que confere as 480 assinaturas
`rtSym` contra o módulo.

Com os marcos 1 e 2 já se fazem jogos simples; a paridade completa é a soma
dos marcos, à volta de 2 meses de trabalho contínuo (não amanhã, mas o
primeiro cubo a rodar com luz e textura é questão de dias, porque o engine
já existe e só o backend é novo).

## 4. Decisões a confirmar

1. Nome/local: `/media/projectos/projects/cpp/blitz3d` (ao lado do zenblitz).
2. Backend GPU inicial: OpenGL (GLES/Vulkan depois, sem tocar no engine).
3. Submodules: `GPU`, `math`, `containers` em `extern/`.
4. `gxMovie` fora; `CallDLL` e userlibs `.decls` fora.
5. Handles inteiros como no Blitz3D.

## 5. Riscos

- Fidelidade do fixed-function DX7 no shader (multitexture blend, `EntityFX`,
  W-buffer, fog): é onde vai a afinação; comparar com screenshots do original.
- `Transform`/`Matrix` do Blitz (3x3 + posição, row-major, `M * v` com
  convenção própria) contra `mathc` (column-major, GLM): fazer a adaptação
  uma vez no wrapper `Transform` e cobrir com testes contra o `geom.h`
  original antes de o apagar.
- Colisões: usar o código original sem "melhorias" para manter o
  comportamento dos jogos antigos.

## 6. Matemática: DirectX (Blitz) → OpenGL/mathc, sem partir os jogos antigos

O Blitz3D é **mão esquerda**: X direita, Y cima, **Z para a frente**;
`MoveEntity e,0,0,1` anda para +Z, `TurnEntity` roda com os sinais do DX,
`EntityPitch/Yaw/Roll` e `TFormPoint` devolvem valores nessa convenção. As
matrizes do `geom.h` são 3x3 + posição, row-major, aplicadas como
`vector * matrix`. A `mathc` é column-major e mão direita (GLM). Se a
conversão for feita "por dentro" do engine, os programas Blitz antigos
passam a rodar ao contrário ou a espelhar. Regra:

1. **O mundo do engine continua em mão esquerda, com a semântica exacta do
   Blitz.** O wrapper `Transform` (3x3 + posição) mantém as operações do
   `geom.h` (`operator*`, `~` inversa, `Quat` para Euler na ordem do Blitz)
   por cima de `Math::Mat3/Vec3/Quaternion`; os testes comparam cada função
   com o `geom.h` original antes de este ser apagado.
2. **A conversão acontece num único sítio: a matriz de projecção.** O
   `Renderer` recebe as matrizes view/world do engine (LH) e usa uma
   projecção LH (`Mat4::PerspectiveLH`, ou a `Perspective` da mathc com o Z
   negado na última coluna). Assim o clip space fica correcto sem tocar em
   nada acima.
3. **Winding:** ao passar de LH para RH o sentido dos triângulos inverte.
   O DX7 do Blitz faz cull dos CCW; no pipeline GL fixamos `cullFace=Front`
   (ou invertemos o winding no upload dos índices, uma vez). `FlipMesh` e
   `EntityFX 16` (disable backface culling) continuam a funcionar.
4. **Texturas:** o DX tem `v=0` no topo, o GL em baixo. Inverter as imagens
   no upload (`stb_image` com flip vertical) em vez de mexer nos UVs dos
   meshes, para `VertexV`/`VertexTexCoords` devolverem os valores do Blitz.
5. **Profundidade:** DX7 usa Z em `[0,1]`, GL em `[-1,1]`; só afecta a
   projecção (ponto 2) e `CameraRange`. W-buffer do Blitz (`WBuffer`) é
   ignorado: usa-se sempre Z-buffer com `near` razoável.
6. **Fog, luzes e cores:** por vértice como no DX7, no shader; `AmbientLight`,
   `LightRange` (atenuação linear do DX7) e `EntityShininess` reproduzidos
   com as fórmulas do fixed-function DX7, não com as do GL clássico.

## 7. Web (Emscripten / WebGL2) desde o início

- Backend: o `GPU` já tem GLES; WebGL2 = GLES 3.0. Shaders escritos em
  GLSL ES 3.00 (`#version 300 es`) e usados em desktop via o mesmo texto
  (GL 3.3 core aceita com pequenas diferenças; manter um ficheiro só).
- Janela/input: SDL2 compila em Emscripten (`gpu_sdl` idem); som: miniaudio
  tem backend Web Audio.
- **Loop principal:** o browser não deixa bloquear em `While ... Flip ... Wend`.
  Como é uma VM, o estado de execução (`ip`, registos, frames) já vive fora
  da pilha C: a nativa `Flip` apresenta o frame e marca "suspender"; o loop
  de dispatch sai do `execute()` logo a seguir a essa nativa; no frame
  seguinte o host chama `execute()` outra vez e a execução continua no `ip`
  guardado. Sem Asyncify, sem workers, sem mudanças no compilador. Em desktop
  o host chama de imediato; na web chama a partir do `requestAnimationFrame`.
  `WaitTimer`, `Delay` e `Input` usam o mesmo mecanismo. A única regra é que
  uma nativa não pode chamar código Blitz que faça `Flip` a meio.
- Evitar readbacks síncronos: `LockBuffer/ReadPixel` sobre o backbuffer é
  lento em WebGL; manter as `ImageBuffer` como texturas com cópia em RAM
  (o Blitz já trata imagens como superfícies próprias).
- Sem ficheiros locais: `LoadMesh/LoadImage/LoadSound` lêem de um pacote
  (`--preload-file`) ou por `fetch` para um VFS em memória; `OpenFile`/
  `WriteFile` gravam em IndexedDB via o FS do Emscripten.
- Sem `ExecFile`, `CallDLL`, threads; `MilliSecs` via `performance.now()`.

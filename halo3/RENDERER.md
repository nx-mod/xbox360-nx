# Halo 3 renderer: the plan

Why there is no picture yet, and the route to one. Findings are from our own diagnostics (`[gpu]`/`[present]` lines in
`logs/rex/rex_stderr.log`) and from reading the code; nothing here relies on the XenonRecomp archive's logs.

## Where it stands (2026-10-19)

- `halo3_gpu.cpp` is Halo's own graphics system (ring reader, register mirror, interrupts, swaps). It routes only
  copy-mode draws (resolves and clears) to the deko3d renderer (`RenderDk::Copiar`). **Geometry draws are counted
  and dropped**: there is no shader tracking and no `PeticionDibujo`.
- About one resolve per frame arrives, and every one is rejected with cause `c110`: Halo renders into color format 10,
  `k_2_10_10_10_AS_10_10_10_10`, and both gates (`sdk/src/x360/deko/destinos.cpp` `Copiar`, `dibujos.cpp`) accept
  only `k_8_8_8_8` / `_GAMMA`, NFS's formats.
- So `Presentar` finds no resolved texture at the swap base (`04E20000`) and the screen stays white or black. The
  presenter itself works (the ImGui overlay shows).

## How NFS gets a picture (the model)

`nfsmw_pal/src/nfsmw_nativo_sistema.cpp` (`SistemaGraficoNativo`, also an `IGraphicsSystem`):

1. `PM4_IM_LOAD` / `IM_LOAD_IMMEDIATE`: keeps the microcode and identifies it **by fingerprint of the masked
   microcode** (`shaders_.Identificar(vertex, microcode)`). No game hooks are needed for this.
2. Register writes bump generation counters (VS/PS constants, fetch constants, viewport/scissor).
3. `PM4_DRAW_INDX`: builds a `PeticionDibujo` (register mirror, VS/PS `EntradaShader`, VS microcode, generations)
   and calls `RenderDk::Dibujar`.

The shaders are **pre-translated offline** into a library (`nfsmw_shaders_dk.nfsp`): containers found in the game
data → XenosRecomp → HLSL → DXC → SPIR-V → packed. On the console the deko3d renderer turns each SPIR-V into GLSL
(SPIRV-Cross) and compiles it with UAM into a DKSH cache.

## Halo's shaders fit the same pipeline

A scan of `maps/chill.map` (97 MB) found about 2,285 shader containers, all with the signature `10 2A 11 00/01`.
Version `0x11` is the layout upstream XenosRecomp was written for (Sonic Unleashed, `0x102A1100`). NFS's 2005 layout
(`0E`) is the odd one that needed `shaders/nfsmw_contenedor.h`. Many containers repeat across maps, so dedupe by hash.
`maps/mainmenu.map` and `maps/shared.map` are the first to cover.

## Shader library status (2026-10-22)

Built locally from `mainmenu.map` and `010_jungle.map` (`shared.map` has no containers): 1,127 distinct containers,
1,105 to valid SPIR-V, packed into `halo3_shaders_dk.nfsp` (3.1 MB, not shipped). The chain, all on the phone:

1. `shaders/x360_buscar_contenedores 11 <out> <maps>`: Halo's containers have **no constant table** (offset 0).
2. `halo3/tools/halo3_hlsl` (XenosRecomp with `NFSMW_RECOMP` + `HALO3_RECOMP`), one container per process under
   `ulimit -v 1 GB` and a timeout: a synthetic table (`g_c` 256 registers, `g_b0..15`).
3. `shaders/wasm/dxc_node.mjs --sin-invertir-y --seguir -D NFSMW_DEKO3D -D HALO3_RECOMP`, then `spirv-val`.
4. `halo3/tools/halo3_empaquetar`.

What `HALO3_RECOMP` does that the runtime must match:

- **Samplers:** only the slots the fetches use, at texture/sampler binding = slot (0-31, no shadow pair), with each
  fetch's dimension (1D reads a 2D texture of height 1). The runtime gets them from SPIR-V reflection, not a table.
- **Vertex fetches with no element** (most pixel shaders, some vertex streams): `ByteAddressBuffer g_vf<slot>` in
  set 5, bindings 0.. in slot order. The renderer copies the fetch constant's range (byte-swapped as vertex data)
  and binds it as a storage buffer.
- **Booleans and loop constants:** UBO binding 3 (`b[2]` = 256 booleans, then `bucles[8]` = 32 loop constants).
- **Vertex inputs:** location = order of first appearance in the element list (`ubicaciones_por_orden`).
- **Constants:** 256 for pixel shaders too (the pixel UBO must be 256 registers).
- Undefined opcodes (scalar 41, 51-63; vector 30, 31) are no-ops, marked in the HLSL.

Still refused (22): leftover braces, vertex-shader samples outside the helpers, a few broken lines.

## The library route hits a wall (2026-10-22, first runs)

The first console runs drew one draw per frame on the boot screen and identified 2 of 6 shaders. The ones that miss
are not in any container we can scan: D3D's built-in shaders (raw microcode in `default.xex`, 9-27 words) and the
boot/loading shaders (in data that does not appear raw: `shared.map` looks compressed). Comparing them with the 1,141
containers (any length, any order) gives at most 18 % of words in common. Patched vertex shaders also need fuzzy
matching.

The working PC port (george0815/halo-3-recomp, the H3Recomp code base; no license, facts only) never builds a
library: it translates the microcode each IM_LOAD carries to GLSL at run time and caches the compiled result. That
covers everything above at once, and it is the on-console shader generation the TODO wants anyway.

**Direction:** a GLSL output mode for our XenosRecomp (MIT), compiled into the game and run when an IM_LOAD arrives
(cached by microcode hash): microcode → GLSL → uam → DKSH, with the existing DKSH cache. The offline library work
stays useful as the test corpus: every container is checked with the host `uam` (devkitPro) before the console.

## Run-time translation (2026-10-22)

Both routes, as decided: the library is the fast path, the translator covers the rest (and everything when there is
no library, so a player needs no PC step).

- `XENOS_GLSL` in `shaders/XenosRecomp`: same translator, GLSL out. Prelude `shaders/XenosRecomp/shader_common.glsl`
  (HLSL names given GLSL meaning; UBOs 0-3, `g_vf<slot>` storage buffers, combined samplers at binding = slot).
  Host check: 1,130 of the 1,141 map containers compile with `uam`; the other 11 are scan junk (8 all-zero vertex
  shaders, 3 whose "microcode" is a debug-symbol path).
- Raw microcode is wrapped like Xenia reads it (`halo3/src/halo3_contenedor.h`): no vertex elements, so every
  vertex fetch is a buffer load by `r0.x`; 16 interpolators by index (VS export k = TEXCOORDk = PS register k).
- `r0.x` of a vertex shader is the guest vertex index: `gl_VertexID` (SV_VertexID in the HLSL library) plus the
  draw's first vertex, which the renderer writes at shared-constant byte 488 (the draw rebases indices by -vmin).
- Pixel shaders are translated twice: the second pass declares only the outputs the first one wrote.
- A pair with one library shader and one translated would read interpolators differently (by usage vs by index):
  `halo3_gpu.cpp` then translates both.
- Renderer: `EntradaShader::glsl`; `ShaderDe`/`InfoDe` take `rex::deko3d::GlslDeTraductor` instead of SPIR-V,
  DKSH cache keyed by the text's hash. PC test of the console path: `halo3/tools/halo3_glsl_crudo.cpp`.

## Steps

1. **Halo shader library.** Scan the maps for `10 2A 11` containers (the scanner, `shaders/nfsmw_buscar_contenedores.cpp`
   and the installer's `lib/containers.js`, accept only `0E` today), translate them with XenosRecomp's own
   container reader (not `nfsmw_contenedor.h`), keep the HLSL fix-ups that apply, compile with the WASM DXC
   (`shaders/wasm/dxc_node.mjs`, works on the phone), pack into a Halo library. The packer and the runtime must
   agree on the fingerprint: masked microcode.
2. **Shared ring→draw path in the SDK.** Move the generic part of NFS's system (microcode capture + identification,
   generation counters, building `PeticionDibujo`, calling `Dibujar`) into `sdk/` so halo3 and NFS use one copy.
   NFS-specific parts stay in NFS: its D3D markers and draw records, sky guard, occlusion scale, glow.
3. **Feed Halo's draws** through it from `halo3_gpu.cpp`.
4. **Render-target formats.** Accept `k_2_10_10_10` / `_AS_10_10_10_10` in both gates (host format
   `DkImageFormat_RGB10A2_Unorm`, or RGBA8 first for a picture), then whatever else Halo uses.
5. Iterate on what draws wrong, in that order: shaders missing from the library, formats, then effects.

## Reference

The XenonRecomp archive (`~/switch/halo3-nx-xenonrecomp`) rendered Halo on deko3d with a different design: a runtime
microcode → GLSL translator (`H3Recomp/gpu/shader/translator.cpp`, 2,111 lines) laid out for SDL_GPU's binding
model. Useful for semantics; its binding layout does not match the rex::x360 renderer, so it is not a drop-in.

## Picture state (2026-10-22, end of day)

Geometry drawing is behind `halo3_dibujar_geometria` (off on the console: with it on the game is too slow and the
audio crackles). With it on:

- **Draws reach the screen.** Fixed today: one host image per EDRAM base whatever the format (Halo draws a base as
  8_8_8_8_GAMMA / 7e3 and resolves it as 8_8_8_8 / 2_10_10_10), OpenGL clip-space z remapped in the translated
  vertex shaders (byte 492), resolves of two sizes at one address kept apart, CTX1 decoded, rectangle lists through a
  geometry shader. The intro video (Bink, PS n329) and the menu background show.
- **The picture washes white and never clears.** Measured: 27 copies a frame and **0 clears**: Halo never sets the
  copy's clear bits, so its clears are D3D's own full-screen draws (translated n2147483649 / n2147483650). Whatever
  it draws additively (blend 01010101) builds up. Next: log each clear draw's target and colour and check the
  translated clear shaders. The 16-bit exposure chain (formats 5/7, resolves 26/32) as RGBA16F is in
  `deko_destinos_flotantes` (off; it did not fix the white). Resolves to 16_16 and 16_16_FLOAT (25, 31) stay refused:
  Halo copies an 800x800 target as 8_8_8_8 and as 16_16_FLOAT into the same memory every frame.
- **Slow.** 60-130 ms of GPU a frame in the menu (~2,000 draws). Most is translated vertex shaders in the general
  control-flow form (`switch (pc)` loop): 44 of the 66 such vertex shaders use loops, 20 backward jumps. Next:
  structured loops in the translator.
- **First runs compile.** Translated shaders compile in the background (`deko_compilar_solo_en_fondo`) and their
  draws are skipped until ready; the DKSH cache on the SD makes later runs instant. Changing the translator's output
  invalidates it.

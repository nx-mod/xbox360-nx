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

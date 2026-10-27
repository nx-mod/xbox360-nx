# Halo 3: ODST: notes

Not started beyond reading the disc. Halo 3's work (`../halo3/`) comes first; ODST shares its engine era and needs the
same renderer fixes.

## The disc

- `Halo 3 - ODST (Europe, Australia) (En,Ja,Fr,De,Es,It,Pt,Zh,Ko) (Disc 1) (Campaign).iso`, 7.8 GB, XGD2 (game
  partition at 0x0FD90000). 75 files, 3.7 GB: `default.xex`, `maps/` (campaign maps, `mainmenu`, `shared`,
  `campaign`), `bink/` (`intro_60`, `story_60`, `credits_60`), `nxeart`, and the Waves audio DLLs.
- `default.xex`: title `4D530877`, 9,486,336 bytes. XDK 2.0.7978 for every library (`RTDMP` 2.0.4902); Halo 3 links
  2.0.6132.
- Modules, laid out as Halo 3's: `WaveShell-Xbox.dll`, `WavesLibDLL.dll`, `waves/L360.dll`, `waves/Q10.dll`. **All four
  differ from Halo 3's** (SHA-1), so nothing of Halo 3's per-module analysis carries over.
- The code files are extracted to `assets/game_root/` (git-ignored, as every `assets/`), with
  `tools/fase1_extraer.py` (`--solo-xex`, then the DLLs and `nxeart` by path).

## Related projects (facts only, neither has a licence)

- **OpenH3M** (GTTeancum/OpenH3M): ReXGlue recomp of ODST's **Disc 2**, Halo 3 Mythic multiplayer, Windows/D3D12, beta
  0.8 (custom games, split screen, Forge). Its xex: title `4D5307E6` (Halo 3's), base 0x82000000, entry 0x82598C38;
  no region or title update named. Its manifest declares only ~11 functions and a few switch tables, so this engine
  build mostly analyses on its own. It draws with ReXGlue's stock Xenia GPU (D3D12) plus speed patches (memexport,
  index buffer cache, vertex residency); its log still reports memexport shaders it cannot translate.
- **george0815/halo-3-recomp**: Halo 3 on its own renderer; its status says in-game frames are still washed out, as
  ours were.

## To do

1. Scaffold as Halo 3 was (`../halo3/PORTING.md`, section 1): `rexglue init --project-name odst --xex-path
   assets/game_root/default.xex --game-root assets/game_root --project-root odst --scan-dll`, then fix the DLL
   `file_path` case in `odst_manifest.toml`.
2. Codegen (`REXGLUE=$PWD/sdk/out/linux-arm64/rexglue tools/codegen.sh odst`, absolute path), then the missing
   functions and jump tables (PORTING.md sections 4 and 7); compare with OpenH3M's short list as facts.
3. `CMakeLists.txt`/`src/` from Halo 3: the graphics system (`halo3_gpu.cpp`), the shader translator, fibers, the
   game paths (`sdmc:/odst-nx/`), the icon from the disc. The first build is hours at -j2: use phone2.
4. Find Halo 3's busy waits again in ODST's code (Direct3D's fence spin, XAudio's barrier, the worker poll): the
   addresses all differ. The stack profiler (`logs/rex/perfil_pilas.flag`) finds them.
5. The renderer work lands in the SDK and is shared with Halo 3.
6. Disc 2 (Mythic) only if the user has it; check its xex entry point against 0x82598C38.

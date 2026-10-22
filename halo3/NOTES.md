# halo3 — quick notes

**State (2026-10-19):** builds + runs on Switch via deko3d, and is **menu-navigable** —
boot, main menu (heard, not seen — click sounds on input), into a game's intro. The `sub_8231D8D0`
crashes (boots 8–13) were a **read-fault storm**, not a translation bug: read-only guest
pages are unmapped on the Switch, so the native code reading Halo's read-only data faulted
~113k/s, starving audio and corrupting a pointer. Fixed by keeping guest memory read-write
(`RexGmProtect`, see PORTING.md §13). Confirmed: `lectura emulada` = 0, steady 24–30 fps.
Open: no picture — geometry draws are never sent to the deko renderer and Halo's 10-bit
(2_10_10_10) render target is rejected; early audio static; a deep crash on starting a match; long load.

## Build (Switch NRO)
```
cmake -S halo3 -B halo3/out/sw2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/tools/switch/cmake/switch-devkitA64.cmake \
  -DREXSDK_DIR=$PWD/sdk -DREXGLUE_PLATFORM_SWITCH=ON -DREXGLUE_SWITCH_DEKO3D=ON
cmake --build halo3/out/sw2 -j2        # -> halo3/out/sw2/halo3.nro
```
SDK is a submodule at `sdk/` → rexglue-sdk-nx@switch. First time: `python sdk/tools/switch_setup.py`.

## Regenerate recompiled code (to pick up SDK codegen fixes)
```
# build host CLI once:
cmake -S sdk -B sdk/out/linux-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF
cmake --build sdk/out/linux-arm64 --target rexglue -j2
# then regen:
REXGLUE=sdk/out/linux-arm64/rexglue tools/codegen.sh halo3
```

## Deploy + test
```
curl -T halo3/out/sw2/halo3.nro ftp://anonymous:anonymous@<switch-ip>:5000/halo3-nx/halo3-nx.nro
```
Crash logs: console `sdmc:/halo3-nx/halo3-err.log` + `sdmc:/atmosphere/crash_reports/`.
Read a crash: `aarch64-none-elf-addr2line -f -e halo3/out/sw2/halo3 <offset>`.

## Next
1. **Picture** — plan in [RENDERER.md](RENDERER.md). halo3_gpu only routes resolves to the renderer; geometry draws are dropped. Port
   NFS's ring→`PeticionDibujo` path (`nfsmw_pal/src/nfsmw_nativo_sistema.cpp` `DibujarNativo`) into
   the SDK and feed halo3's draws through it; accept 2_10_10_10 targets (reject cause `c110`).
2. **Static** — early-load audio; dump PCM + per-voice sample rates. Suspect voice resampling.
3. **Deep crash** on starting a match — pull its `atmosphere/crash_reports/` log.
4. **Load time** — offload shader compile / game-thread work to the background (sibling-port lead).

**Debugging note:** the buffered `halo3.log` / `halo3-err.log` are **lost on an `svcBreak` abort**.
What survives, flushed, is `logs/rex/`: `rex_crash.log` (fault + backtrace), `rex_stderr.log`
(anything via `fprintf(stderr)`; `REXLOG_WARN` does *not* reach it), `rex_perfil.log` (per-thread
CPU + the `lectura emulada` / `vigilancia` storm counters every 10 s).

## Run on PC (host, for fast crash debugging)
Recompiled code runs natively on this linux-arm64 box; faster to debug than the Switch cycle.
Vulkan via lavapipe (software) or freedreno/panfrost. The recomp's fault handler prints the
guest crash address to stderr even without gdb.
```
cmake -S halo3 -B halo3/out/host -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DREXSDK_DIR=$PWD/sdk            # no switch toolchain, no DEKO3D -> Vulkan backend
cmake --build halo3/out/host -j2
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./halo3/out/host/halo3  # lavapipe
```

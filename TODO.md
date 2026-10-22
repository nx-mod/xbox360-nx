# TODO

## Need for Speed: Most Wanted

1. **Bloom downsample in two passes** (in progress). The first 320x180 pass reads 16 spread taps per pixel and takes
   2.4 ms of a ~25 ms GPU frame, 95 % of it texture reads. A 640x360 pre-pass of 2x2 averages lets the game's own
   shader run with taps on texel centers: the same result with cache-friendly reads.
2. **Cracked glass**: near the end of the career intro the screen goes dark instead of showing cracked glass. Ruled
   out so far are in the [devlog](docs/deko3d-devlog.md). Next: a diagnostic that writes only around that moment.
3. **`nfsmw_intervalo_presentacion = "inmediato"`**: test on the console (menus above 30 FPS; check that movies keep
   their speed), then decide the default.
4. **Port from the other NFS MW recompilations** (all GPL-3.0):
   - [madelrandel-blip/NFSMW-Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled): the game's `memset`
     (0x826BE610) as native NEON; occlusion culling and a shadow map cache; main thread priority.
   - [frankyfife/NFSMW-Recompiled_ff](https://github.com/frankyfife/NFSMW-Recompiled_ff): frame pacing in `VdSwap`
     and a low-latency mode, ZPD occlusion queries as a running counter (sun glare through buildings), cars at full
     detail at any distance.
   - [codepdbh/nfsmw-android](https://github.com/codepdbh/nfsmw-android): the shader library generated on the device
     at first start (then the release needs no PC step at all); cutscene audio fixes on skip.
5. **Online** over a LAN server: [victorgbd/nfsmw-server-lan](https://github.com/victorgbd/nfsmw-server-lan) stands in
   for EA's 2005 servers (Quick Match works between phones). Port the game-side changes of
   [NFSMW-Recompiled-Mobile](https://github.com/victorgbd/NFSMW-Recompiled-Mobile) (`docs/diario/red-y-privilegios.md`)
   and add a server IP setting.
6. Spain runs some menus at 30 FPS where USA reaches 60; a crash when closing the game.
7. Remove the temporary stderr diagnostics once the hunts are over.

## Halo 3

- A picture: the plan is in [halo3/RENDERER.md](halo3/RENDERER.md) (shader library from the maps, the draw path
  moved into the SDK, 2_10_10_10 render targets).
- Static in the audio.

## SDK

- Shader library generation on the console as an SDK feature, for every game.
